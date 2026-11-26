#include "server/WebSocketServer.h"
#include "game/InventoryExtractor.h"
#include "game/PerkExtractor.h"
#include "game/StatExtractor.h"
#include "game/AlchemyDataExtractor.h"
#include "game/AlchemyController.h"
#include "sync/ProtoPacketBuilder.h"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <thread>
#include <vector>

namespace SkyPotion::Server {

    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace net = boost::asio;
    using tcp = boost::asio::ip::tcp;

    namespace {
        struct OutgoingMessage {
            std::string data;
            bool binary{false};
        };

        template <typename ResultT>
        void DispatchTaskWithTimeout(
            const std::shared_ptr<Session>& session,
            bool useUITask,
            std::chrono::milliseconds timeout,
            std::function<ResultT()> mainThreadWork,
            std::function<void(Session&, ResultT)> onResult,
            std::function<void(Session&)> onTimeout);
    }

    class Session : public std::enable_shared_from_this<Session> {
    public:
        Session(tcp::socket&& socket, WebSocketServer& owner, uint64_t id)
            : _ws(std::move(socket))
            , _owner(owner)
            , _id(id) {}

        uint64_t Id() const { return _id; }

        auto GetExecutor() { return _ws.get_executor(); }

        void Run() {
            net::dispatch(_ws.get_executor(),
                beast::bind_front_handler(&Session::OnRun, shared_from_this()));
        }

        void SendText(std::string text) {
            Enqueue(std::move(text), false);
        }

        void SendBinary(std::string binary) {
            Enqueue(std::move(binary), true);
        }

        void ForceClose() {
            net::post(_ws.get_executor(), [self = shared_from_this()]() {
                if (self->_closed) return;
                self->_closed = true;
                beast::error_code ec;
                beast::get_lowest_layer(self->_ws).cancel();
                beast::get_lowest_layer(self->_ws).socket().shutdown(tcp::socket::shutdown_both, ec);
                beast::get_lowest_layer(self->_ws).socket().close(ec);
            });
        }

    private:
        void Enqueue(std::string data, bool binary) {
            net::post(_ws.get_executor(),
                [self = shared_from_this(), data = std::move(data), binary]() mutable {
                    if (self->_closed) return;
                    self->_writeQueue.push_back(OutgoingMessage{std::move(data), binary});
                    if (self->_writeQueue.size() == 1) {
                        self->DoWrite();
                    }
                });
        }

        void OnRun() {
            // Set suggested timeout settings for the websocket (includes idle keep-alive pings)
            _ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

            // Identify server in handshake response (permessage-deflate remains disabled by default for Android OkHttp)
            _ws.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& res) {
                    res.set(beast::http::field::server, "SkyPotionSKSE-Beast");
                }));

            // Log WebSocket control frames (Beast automatically replies to Ping with Pong)
            _ws.control_callback(
                [weak = weak_from_this()](websocket::frame_type kind, beast::string_view payload) {
                    auto self = weak.lock();
                    if (!self) return;
                    if (kind == websocket::frame_type::ping) {
                        SKSE::log::debug("WebSocket Ping received from session {} ({} bytes)", self->_id, payload.size());
                    } else if (kind == websocket::frame_type::pong) {
                        SKSE::log::debug("WebSocket Pong received from session {}", self->_id);
                    }
                });

            _ws.async_accept(
                beast::bind_front_handler(&Session::OnAccept, shared_from_this()));
        }

        void OnAccept(beast::error_code ec) {
            if (ec) {
                if (ec != net::error::operation_aborted) {
                    SKSE::log::warn("WebSocket accept failed (session {}): {}", _id, ec.message());
                }
                return;
            }

            _owner.RegisterSession(shared_from_this());

            beast::error_code epEc;
            auto remoteEp = beast::get_lowest_layer(_ws).socket().remote_endpoint(epEc);
            if (!epEc) {
                SKSE::log::info("Client connected: session {} ({}:{})",
                    _id, remoteEp.address().to_string(), remoteEp.port());
            } else {
                SKSE::log::info("Client connected: session {}", _id);
            }

            _owner.ScheduleSessionSnapshot();
            DoRead();
        }

        void DoRead() {
            if (_closed) return;
            _ws.async_read(
                _buffer,
                beast::bind_front_handler(&Session::OnRead, shared_from_this()));
        }

        void OnRead(beast::error_code ec, std::size_t bytesTransferred) {
            if (ec) {
                _closed = true;
                if (ec == websocket::error::closed) {
                    SKSE::log::info("Client disconnected: session {}", _id);
                } else if (ec != net::error::operation_aborted) {
                    SKSE::log::warn("WebSocket client error (session {}): {}", _id, ec.message());
                }
                _owner.UnregisterSession(this);
                return;
            }

            bool isBinary = _ws.got_binary();
            std::string payload = beast::buffers_to_string(_buffer.data());
            _buffer.consume(bytesTransferred);

            _owner.HandleSessionMessage(shared_from_this(), isBinary, std::move(payload));
            DoRead();
        }

        void DoWrite() {
            if (_closed || _writeQueue.empty()) return;
            const auto& front = _writeQueue.front();
            _ws.binary(front.binary);
            _ws.async_write(
                net::buffer(front.data),
                beast::bind_front_handler(&Session::OnWrite, shared_from_this()));
        }

        void OnWrite(beast::error_code ec, std::size_t /*bytesTransferred*/) {
            if (ec) {
                _closed = true;
                if (ec != net::error::operation_aborted) {
                    SKSE::log::warn("WebSocket write error (session {}): {}", _id, ec.message());
                }
                _owner.UnregisterSession(this);
                return;
            }

            _writeQueue.pop_front();
            if (!_writeQueue.empty()) {
                DoWrite();
            }
        }

        websocket::stream<beast::tcp_stream> _ws;
        beast::flat_buffer _buffer;
        std::deque<OutgoingMessage> _writeQueue;
        WebSocketServer& _owner;
        uint64_t _id;
        bool _closed{false};
    };

    namespace {
        template <typename ResultT>
        void DispatchTaskWithTimeout(
            const std::shared_ptr<Session>& session,
            bool useUITask,
            std::chrono::milliseconds timeout,
            std::function<ResultT()> mainThreadWork,
            std::function<void(Session&, ResultT)> onResult,
            std::function<void(Session&)> onTimeout) {

            auto* taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                if (onTimeout) {
                    onTimeout(*session);
                }
                return;
            }

            auto completed = std::make_shared<std::atomic<bool>>(false);
            auto timer = std::make_shared<net::steady_timer>(session->GetExecutor(), timeout);
            std::weak_ptr<Session> weakSession = session;
            auto executor = session->GetExecutor();

            timer->async_wait([weakSession, completed, onTimeout](beast::error_code ec) {
                if (ec) return; // Cancelled or aborted
                if (!completed->exchange(true)) {
                    if (auto s = weakSession.lock()) {
                        if (onTimeout) {
                            onTimeout(*s);
                        }
                    }
                }
            });

            auto taskLambda = [weakSession, executor, completed, timer,
                               mainThreadWork = std::move(mainThreadWork),
                               onResult = std::move(onResult)]() mutable {
                if (completed->load()) {
                    return;
                }
                ResultT res = mainThreadWork();
                if (!completed->exchange(true)) {
                    net::post(executor, [weakSession, timer, res = std::move(res), onResult = std::move(onResult)]() mutable {
                        timer->cancel();
                        if (auto s = weakSession.lock()) {
                            onResult(*s, std::move(res));
                        }
                    });
                }
            };

            if (useUITask) {
                taskInterface->AddUITask(std::move(taskLambda));
            } else {
                taskInterface->AddTask(std::move(taskLambda));
            }
        }
    }

    class Listener : public std::enable_shared_from_this<Listener> {
    public:
        Listener(net::io_context& ioc, WebSocketServer& owner)
            : _ioc(ioc)
            , _acceptor(net::make_strand(ioc))
            , _owner(owner) {}

        bool Init(const tcp::endpoint& endpoint, std::string& outError) {
            beast::error_code ec;

            _acceptor.open(endpoint.protocol(), ec);
            if (ec) {
                outError = "open: " + ec.message();
                return false;
            }

            _acceptor.set_option(net::socket_base::reuse_address(true), ec);
            if (ec) {
                outError = "set_option(reuse_address): " + ec.message();
                return false;
            }

            _acceptor.bind(endpoint, ec);
            if (ec) {
                outError = "bind: " + ec.message();
                return false;
            }

            _acceptor.listen(net::socket_base::max_listen_connections, ec);
            if (ec) {
                outError = "listen: " + ec.message();
                return false;
            }

            return true;
        }

        void Run() {
            net::dispatch(_acceptor.get_executor(),
                beast::bind_front_handler(&Listener::DoAccept, shared_from_this()));
        }

        void Stop() {
            beast::error_code ec;
            _acceptor.close(ec);
        }

    private:
        void DoAccept() {
            _acceptor.async_accept(
                net::make_strand(_ioc),
                beast::bind_front_handler(&Listener::OnAccept, shared_from_this()));
        }

        void OnAccept(beast::error_code ec, tcp::socket socket) {
            if (ec) {
                if (ec == net::error::operation_aborted) {
                    return;
                }
                SKSE::log::warn("WebSocket accept error: {}", ec.message());
            } else {
                uint64_t id = _nextSessionId.fetch_add(1, std::memory_order_relaxed);
                std::make_shared<Session>(std::move(socket), _owner, id)->Run();
            }

            if (_acceptor.is_open()) {
                DoAccept();
            }
        }

        net::io_context& _ioc;
        tcp::acceptor _acceptor;
        WebSocketServer& _owner;
        std::atomic<uint64_t> _nextSessionId{1};
    };

    struct WebSocketServer::Impl {
        net::io_context ioc{1};
        net::steady_timer snapshotTimer{ioc};
        std::shared_ptr<Listener> listener;
        std::thread ioThread;
        std::mutex sessionsMutex;
        std::vector<std::shared_ptr<Session>> sessions;
    };

    WebSocketServer::WebSocketServer() = default;

    WebSocketServer::~WebSocketServer() {
        Stop();
    }

    bool WebSocketServer::Start(uint16_t port, const std::string& host) {
        if (_running.load()) {
            SKSE::log::warn("WebSocketServer already running on {}:{}", _host, _port);
            return true;
        }

        _port = port;
        _host = host;

        try {
            auto impl = std::make_unique<Impl>();

            beast::error_code addrEc;
            auto address = net::ip::make_address(_host, addrEc);
            if (addrEc) {
                SKSE::log::error("WebSocketServer invalid host '{}': {}", _host, addrEc.message());
                return false;
            }

            tcp::endpoint endpoint{address, _port};
            impl->listener = std::make_shared<Listener>(impl->ioc, *this);

            std::string initErr;
            if (!impl->listener->Init(endpoint, initErr)) {
                SKSE::log::error("WebSocketServer failed to listen on {}:{}: {}", _host, _port, initErr);
                return false;
            }

            impl->listener->Run();

            Impl* rawImpl = impl.get();
            impl->ioThread = std::thread([rawImpl]() {
                try {
                    rawImpl->ioc.run();
                } catch (const std::exception& e) {
                    SKSE::log::error("Exception in Boost.Beast io_context thread: {}", e.what());
                }
            });

            _impl = std::move(impl);
            _running.store(true);

            SKSE::log::info("SkyPotion WebSocketServer (Boost.Beast) successfully started on {}:{}", _host, _port);
            return true;
        } catch (const std::exception& e) {
            SKSE::log::error("Exception in WebSocketServer::Start: {}", e.what());
            return false;
        }
    }

    void WebSocketServer::Stop() {
        if (!_running.exchange(false)) return;

        SKSE::log::info("Stopping SkyPotion WebSocketServer (Boost.Beast)...");
        _snapshotGeneration.fetch_add(1, std::memory_order_relaxed);
        _clientCount.store(0, std::memory_order_relaxed);

        if (_impl) {
            std::vector<std::shared_ptr<Session>> activeSessions;
            {
                std::lock_guard<std::mutex> lock(_impl->sessionsMutex);
                activeSessions = std::move(_impl->sessions);
                _impl->sessions.clear();
            }

            net::post(_impl->ioc, [impl = _impl.get()]() {
                impl->snapshotTimer.cancel();
                if (impl->listener) {
                    impl->listener->Stop();
                }
            });

            for (auto& s : activeSessions) {
                if (s) {
                    s->ForceClose();
                }
            }

            _impl->ioc.stop();
            if (_impl->ioThread.joinable()) {
                _impl->ioThread.join();
            }
            _impl.reset();
        }
    }

    bool WebSocketServer::IsRunning() const {
        return _running.load();
    }

    bool WebSocketServer::HasConnectedClients() const {
        return _running.load(std::memory_order_relaxed) &&
            _clientCount.load(std::memory_order_relaxed) > 0;
    }

    void WebSocketServer::RegisterSession(const std::shared_ptr<Session>& session) {
        if (!_impl || !session) return;
        {
            std::lock_guard<std::mutex> lock(_impl->sessionsMutex);
            _impl->sessions.push_back(session);
            _clientCount.store(static_cast<int>(_impl->sessions.size()), std::memory_order_relaxed);
        }
    }

    void WebSocketServer::UnregisterSession(const Session* session) {
        if (!_impl || !session) return;
        bool allDisconnected = false;
        {
            std::lock_guard<std::mutex> lock(_impl->sessionsMutex);
            auto& vec = _impl->sessions;
            vec.erase(
                std::remove_if(vec.begin(), vec.end(),
                    [session](const std::shared_ptr<Session>& s) { return !s || s.get() == session; }),
                vec.end());
            int count = static_cast<int>(vec.size());
            _clientCount.store(count, std::memory_order_relaxed);
            allDisconnected = (count == 0);
        }

        if (allDisconnected && _running.load(std::memory_order_relaxed)) {
            SKSE::log::info("All clients disconnected. Halting crafting and monitoring.");
            Game::AlchemyController::StopCrafting();
            Game::MenuOpenCloseHandler::StopMonitoring();
        }
    }

    void WebSocketServer::HandleSessionMessage(const std::shared_ptr<Session>& session, bool isBinary, std::string payload) {
        if (isBinary) {
            HandleProtoCommand(session, payload);
            return;
        }
        SKSE::log::debug("Ignoring non-binary WebSocket message from session {}", session ? session->Id() : 0);
    }

    void WebSocketServer::HandleProtoCommand(const std::shared_ptr<Session>& session, const std::string& rawBytes) {
        Sync::pb::AppCommandPacket cmd;
        if (!cmd.ParseFromString(rawBytes)) {
            SKSE::log::warn("HandleProtoCommand: failed to parse AppCommandPacket ({} bytes)", rawBytes.size());
            return;
        }

        const uint64_t reqId = cmd.request_id();
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            session->SendBinary(Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "NO_TASK_INTERFACE"));
            return;
        }

        // 1) 재료 선택 동기화 (CommandResult 반환)
        if (cmd.has_select_ingredients()) {
            SKSE::log::info("HandleProtoCommand: select_ingredients (reqId={}, count={})",
                reqId, cmd.select_ingredients().form_ids_size());
            std::vector<RE::FormID> formIds;
            formIds.reserve(cmd.select_ingredients().form_ids_size());
            for (uint32_t id : cmd.select_ingredients().form_ids()) {
                formIds.push_back(static_cast<RE::FormID>(id));
            }

            DispatchTaskWithTimeout<std::string>(
                session,
                /*useUITask=*/true,
                std::chrono::milliseconds(2500),
                [reqId, formIds = std::move(formIds)]() -> std::string {
                    if (!Util::IsGameSessionReady() || !Game::AlchemyController::IsAlchemyMenuOpen()) {
                        return Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "BENCH_CLOSED");
                    }

                    if (formIds.empty()) {
                        bool ok = Game::AlchemyController::ClearSelectedIngredients();
                        return Sync::ProtoPacketBuilder::BuildCommandResult(
                            reqId, ok, 0, ok ? "" : "SELECT_FAILED");
                    }

                    auto result = Game::AlchemyController::SelectIngredients(formIds, /*autoCraft=*/false, /*craftCount=*/0);
                    std::string errorCode = result.success
                        ? ""
                        : (result.message.empty() ? "SELECT_FAILED" : result.message);

                    return Sync::ProtoPacketBuilder::BuildCommandResult(
                        reqId, result.success, 0, errorCode, result.missingFormIds);
                },
                [](Session& s, std::string responseBytes) {
                    s.SendBinary(std::move(responseBytes));
                },
                [reqId](Session& s) {
                    SKSE::log::warn("HandleProtoCommand: select_ingredients (reqId={}) timed out", reqId);
                    s.SendBinary(Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "TIMEOUT"));
                });
            return;
        }

        // 2) ?ъ뀡 ?쒖“ ?붿껌 (鍮꾨룞湲??묐떟 -> CommandResult 諛섑솚)
        if (cmd.has_craft_potion()) {
            SKSE::log::info("HandleProtoCommand: craft_potion (reqId={}, ingredients={}, count={})",
                reqId, cmd.craft_potion().form_ids_size(), cmd.craft_potion().count());
            std::vector<RE::FormID> formIds;
            formIds.reserve(cmd.craft_potion().form_ids_size());
            for (uint32_t id : cmd.craft_potion().form_ids()) {
                formIds.push_back(static_cast<RE::FormID>(id));
            }
            const int32_t count = cmd.craft_potion().count();

            DispatchTaskWithTimeout<std::string>(
                session,
                /*useUITask=*/true,
                std::chrono::milliseconds(2500),
                [this, reqId, formIds = std::move(formIds), count]() -> std::string {
                    if (!Util::IsGameSessionReady() || !Game::AlchemyController::IsAlchemyMenuOpen()) {
                        return Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "BENCH_CLOSED");
                    }

                    auto result = Game::AlchemyController::SelectIngredients(formIds, /*autoCraft=*/true, count);
                    std::string errorCode = result.success ? "" : (result.message.empty() ? "CRAFT_FAILED" : result.message);

                    // 제조 성공 시 스킬 레벨업 / 신규 효과 해금 동기화
                    if (result.success) {
                        BroadcastBinary(Sync::ProtoPacketBuilder::BuildDiscoveredEffects(formIds));
                        Game::InventoryEventHandler::GetSingleton()->RequestAlchemyStatsSync();
                    }

                    return Sync::ProtoPacketBuilder::BuildCommandResult(
                        reqId, result.success, result.craftedCount, errorCode, result.missingFormIds);
                },
                [](Session& s, std::string responseBytes) {
                    s.SendBinary(std::move(responseBytes));
                },
                [reqId](Session& s) {
                    SKSE::log::warn("HandleProtoCommand: craft_potion (reqId={}) timed out", reqId);
                    s.SendBinary(Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "TIMEOUT"));
                });
        }
    }

    void WebSocketServer::BroadcastBinary(const std::string& binaryPayload) {
        if (!_running.load() || !_impl || binaryPayload.empty()) return;

        std::vector<std::shared_ptr<Session>> snapshot;
        {
            std::lock_guard<std::mutex> lock(_impl->sessionsMutex);
            snapshot = _impl->sessions;
        }

        for (auto& client : snapshot) {
            if (client) {
                client->SendBinary(binaryPayload);
            }
        }
    }

    void WebSocketServer::BroadcastProtoSnapshot() {
        if (!HasConnectedClients()) return;

        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) return;

        taskInterface->AddTask([this]() {
            try {
                if (!_running.load() || !_impl || !HasConnectedClients()) return;
                if (!Util::IsGameSessionReady()) {
                    SKSE::log::info("BroadcastProtoSnapshot: Game session not ready");
                    return;
                }

                // 1) 留덉뒪??移댄깉濡쒓렇 (CatalogSnapshot: 罹먮┃?곕챸, ?щ즺 留덉뒪?? MGEF 留덉뒪?? 珥덇린 ?ㅽ꺈 ??
                std::string catalogBin = Sync::ProtoPacketBuilder::BuildCatalogSnapshot();
                if (!catalogBin.empty()) {
                    BroadcastBinary(catalogBin);
                }

                // 2) ?ㅼ떆媛??뚮젅?댁뼱 ?ㅽ꺈 & ??(PlayerCraftingStats, Tag 12)
                // ?덈뱶濡쒖씠???깆쓽 ?ㅽ궗/?ㅽ꺈 愿李?由ъ뒪??Flow)媛 利됱떆 媛깆떊?섎룄濡?紐낆떆???꾩넚
                std::string statsBin = Sync::ProtoPacketBuilder::BuildPlayerAlchemyStats();
                if (!statsBin.empty()) {
                    BroadcastBinary(statsBin);
                }

                // 3) ?ㅼ떆媛??몃깽?좊━ ?щ즺 ?섎웾 (InventoryUpdate, Tag 11)
                std::string invBin = Sync::ProtoPacketBuilder::BuildInventoryFullSnapshot();
                if (!invBin.empty()) {
                    BroadcastBinary(invBin);
                }

                // 4) ?꾩옱 ?곌툑?좊? ?대┝ ?곹깭 諛??좏깮 ?щ즺 ?꾩넚 (BenchState, Tag 15)
                bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();
                std::vector<RE::FormID> selected = isOpen
                    ? Game::AlchemyController::GetSelectedIngredientFormIDs()
                    : std::vector<RE::FormID>{};

                if (isOpen) {
                    Game::MenuOpenCloseHandler::StartMonitoring();
                }

                BroadcastBinary(Sync::ProtoPacketBuilder::BuildAlchemyBenchState(isOpen, selected));
                SKSE::log::info("BroadcastProtoSnapshot: sent CatalogSnapshot, PlayerStats, InventorySnapshot, BenchState (isOpen={})", isOpen);
            } catch (const std::exception& e) {
                SKSE::log::error("Error in BroadcastProtoSnapshot: {}", e.what());
            }
        });
    }

    void WebSocketServer::CancelScheduledSnapshot() {
        _snapshotGeneration.fetch_add(1, std::memory_order_relaxed);
        if (_running.load() && _impl) {
            net::post(_impl->ioc, [this]() {
                if (_running.load() && _impl) {
                    _impl->snapshotTimer.cancel();
                }
            });
        }
    }

    void WebSocketServer::ScheduleSessionSnapshot(int attempt) {
        if (SkyPotion::g_isShuttingDown.load() || !_running.load() || !_impl || !HasConnectedClients()) {
            return;
        }

        const uint64_t gen = _snapshotGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        net::post(_impl->ioc, [this]() {
            if (_running.load() && _impl) {
                _impl->snapshotTimer.cancel();
            }
        });

        ScheduleSessionSnapshotWithGen(attempt, gen);
    }

    void WebSocketServer::ScheduleSessionSnapshotWithGen(int attempt, uint64_t gen) {
        if (SkyPotion::g_isShuttingDown.load() || !_running.load() || !_impl ||
            !HasConnectedClients() || gen != _snapshotGeneration.load(std::memory_order_relaxed)) {
            return;
        }

        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) return;

        taskInterface->AddTask([this, attempt, gen]() {
            if (SkyPotion::g_isShuttingDown.load() || !_running.load() || !_impl ||
                !HasConnectedClients() || gen != _snapshotGeneration.load(std::memory_order_relaxed)) {
                return;
            }

            if (!Util::IsGameSessionReady()) {
                if (attempt < 20) {
                    // 寃뚯엫 ?몄뀡 以鍮?LoadingMenu ?ロ옒, 3D 濡쒕뱶 ?? ?湲????ъ떆??(100ms 媛꾧꺽, 理쒕? 2珥?
                    net::post(_impl->ioc, [this, attempt, gen]() {
                        if (SkyPotion::g_isShuttingDown.load() || !_running.load() || !_impl ||
                            !HasConnectedClients() || gen != _snapshotGeneration.load(std::memory_order_relaxed)) {
                            return;
                        }

                        _impl->snapshotTimer.expires_after(std::chrono::milliseconds(100));
                        _impl->snapshotTimer.async_wait([this, attempt, gen](const beast::error_code& ec) {
                            if (ec == net::error::operation_aborted || ec) {
                                return;
                            }
                            ScheduleSessionSnapshotWithGen(attempt + 1, gen);
                        });
                    });
                } else {
                    SKSE::log::warn("ScheduleSessionSnapshot: Timed out waiting for game session (attempt={})", attempt);
                }
                return;
            }

            SKSE::log::info("ScheduleSessionSnapshot: Game session ready (attempt={}), broadcasting snapshots...", attempt);
            BroadcastProtoSnapshot();
        });
    }

}
