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
        _clientCount.store(0, std::memory_order_relaxed);

        if (_impl) {
            std::vector<std::shared_ptr<Session>> activeSessions;
            {
                std::lock_guard<std::mutex> lock(_impl->sessionsMutex);
                activeSessions = std::move(_impl->sessions);
                _impl->sessions.clear();
            }

            if (_impl->listener) {
                net::post(_impl->ioc, [listener = _impl->listener]() {
                    listener->Stop();
                });
            }

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

    void WebSocketServer::Broadcast(const nlohmann::json& message) {
        if (!_running.load() || !_impl) return;

        std::string jsonStr = message.dump();
        std::vector<std::shared_ptr<Session>> snapshot;
        {
            std::lock_guard<std::mutex> lock(_impl->sessionsMutex);
            snapshot = _impl->sessions;
        }

        for (auto& client : snapshot) {
            if (client) {
                client->SendText(jsonStr);
            }
        }
    }

    void WebSocketServer::HandleSessionMessage(const std::shared_ptr<Session>& session, bool isBinary, std::string payload) {
        // 바이너리 프레임 -> Protobuf AppCommandPacket 처리
        if (isBinary) {
            HandleProtoCommand(session, payload);
            return;
        }

        try {
            auto root = nlohmann::json::parse(payload);
            std::string type = root.value("type", "");

            if (type == "ping") {
                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                nlohmann::json pong = {
                    { "type", "pong" },
                    { "ts", nowMs }
                };
                session->SendText(pong.dump());
                return;
            }

            if (type == "query") {
                std::string id = root.value("id", "");
                nlohmann::json fields = root.contains("fields") ? root["fields"] : nlohmann::json::object();

                // Fast-path: If querying static alchemy gamedata and cache is already populated,
                // serve it safely from the WebSocket worker thread without scheduling on Skyrim's main thread!
                if (id == "gamedata.alchemy" || id == "gamedata.all" || id == "alchemy.database") {
                    auto cachedPtr = Game::AlchemyDataExtractor::GetCachedGameData();
                    if (cachedPtr) {
                        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        nlohmann::json response = {
                            { "type", "data" },
                            { "id", id },
                            { "fields", *cachedPtr },
                            { "ts", nowMs }
                        };
                        session->SendText(response.dump());
                        return;
                    }
                }

                // In-flight guard for polling queries: Drop duplicate requests if previous one is still queued
                static std::atomic<bool> s_queryInFlight{ false };
                if (s_queryInFlight.exchange(true)) {
                    SKSE::log::warn("Query '{}' dropped: previous query still in flight", id);
                    return;
                }

                if (SKSE::GetTaskInterface()) {
                    DispatchTaskWithTimeout<nlohmann::json>(
                        session,
                        /*useUITask=*/false,
                        std::chrono::milliseconds(500),
                        [id, fields]() -> nlohmann::json {
                            try {
                                if (SkyPotion::g_isShuttingDown.load()) {
                                    s_queryInFlight.store(false);
                                    return nlohmann::json::object();
                                }

                                if (!Util::IsGameSessionReady()) {
                                    s_queryInFlight.store(false);
                                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                    return nlohmann::json{
                                        { "type", "data" },
                                        { "id", id },
                                        { "fields", nlohmann::json::object() },
                                        { "status", "not_ready" },
                                        { "warning", "Game session not ready (in main menu or loading)" },
                                        { "ts", nowMs }
                                    };
                                }

                                auto response = ProcessQuery(id, fields);
                                s_queryInFlight.store(false);
                                return response;
                            } catch (const std::exception& e) {
                                s_queryInFlight.store(false);
                                SKSE::log::error("Error processing query in main thread: {}", e.what());
                                return nlohmann::json::object();
                            } catch (...) {
                                s_queryInFlight.store(false);
                                return nlohmann::json::object();
                            }
                        },
                        [](Session& s, nlohmann::json response) {
                            if (!response.empty()) {
                                s.SendText(response.dump());
                            }
                        },
                        [id](Session&) {
                            SKSE::log::warn("Query '{}' timed out waiting for main thread", id);
                        });
                } else {
                    s_queryInFlight.store(false);
                    auto response = ProcessQuery(id, fields);
                    session->SendText(response.dump());
                }
                return;
            }

            if (type == "command") {
                std::string action = root.value("action", "");
                nlohmann::json cmdPayload = root;

                if (SKSE::GetTaskInterface()) {
                    DispatchTaskWithTimeout<nlohmann::json>(
                        session,
                        /*useUITask=*/false,
                        std::chrono::milliseconds(2000),
                        [action, cmdPayload]() -> nlohmann::json {
                            try {
                                if (!Util::IsGameSessionReady()) {
                                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                    return nlohmann::json{
                                        { "type", "command_result" },
                                        { "action", action },
                                        { "success", false },
                                        { "message", "Game session not ready (in main menu or loading)" },
                                        { "ts", nowMs }
                                    };
                                }

                                return ProcessCommand(action, cmdPayload);
                            } catch (const std::exception& e) {
                                SKSE::log::error("Error processing command in main thread: {}", e.what());
                                return nlohmann::json{
                                    { "type", "command_result" },
                                    { "action", action },
                                    { "success", false },
                                    { "message", std::string("Exception: ") + e.what() }
                                };
                            } catch (...) {
                                return nlohmann::json{
                                    { "type", "command_result" },
                                    { "action", action },
                                    { "success", false },
                                    { "message", "Unknown exception in main thread" }
                                };
                            }
                        },
                        [](Session& s, nlohmann::json response) {
                            s.SendText(response.dump());
                        },
                        [action](Session& s) {
                            SKSE::log::warn("Command '{}' timed out waiting for main thread", action);
                            nlohmann::json timeoutResp = {
                                { "type", "command_result" },
                                { "action", action },
                                { "success", false },
                                { "message", "Timeout waiting for main thread" }
                            };
                            s.SendText(timeoutResp.dump());
                        });
                } else {
                    auto response = ProcessCommand(action, cmdPayload);
                    session->SendText(response.dump());
                }
                return;
            }
        } catch (const std::exception& e) {
            SKSE::log::warn("Failed to parse incoming WebSocket message: {}", e.what());
            nlohmann::json err = {
                { "type", "error" },
                { "message", std::string("Malformed JSON: ") + e.what() }
            };
            session->SendText(err.dump());
        }
    }

    nlohmann::json WebSocketServer::ProcessQuery(const std::string& id, const nlohmann::json& fields) {
        nlohmann::json respFields = nlohmann::json::object();

        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // 1. Ingredients Query
        if (id == "inventory.ingredients" || 
            (fields.is_object() && fields.value("items", "") == "Inventory::Items::Ingredients") ||
            (fields.is_object() && fields.value("ingredients", "") == "Actor::Inventory::Ingredients")) {
            respFields["items"] = Game::InventoryExtractor::GetIngredientsJson();
        }
        // 3. Alchemy Stats Query
        else if (id == "skypotion.alchemy" || 
                 (fields.is_object() && (fields.contains("alchemy") || fields.contains("alchemyBase")))) {
            respFields = Game::StatExtractor::GetAlchemyStatsJson();
        }
        // 4. Perks Query (Player perks + Alchemist perk rank & summary)
        else if (id == "player.perks" || id == "skypotion.perks" || 
                 (fields.is_object() && (fields.contains("perks") || fields.value("perks", "") == "Player::Perks"))) {
            respFields["perks"] = Game::PerkExtractor::GetAllPerksJson();
            respFields["alchemyPerks"] = Game::PerkExtractor::GetAlchemyPerksSummary();
        }
        // 5. Complete Snapshot Query
        else if (id == "skypotion.all") {
            respFields["ingredients"] = Game::InventoryExtractor::GetIngredientsJson();
            respFields["alchemy"] = Game::StatExtractor::GetAlchemyStatsJson();
            respFields["alchemyPerks"] = Game::PerkExtractor::GetAlchemyPerksSummary();
            respFields["stats"] = Game::StatExtractor::GetPlayerStatsJson();
        }
        // 6. Generic/Actor Stats Query
        else if (id == "character.stats") {
            respFields = Game::StatExtractor::GetPlayerStatsJson();
        }
        // 7. Master Game Data Query (Alchemy BaseEffects + Playable Ingredients)
        else if (id == "gamedata.alchemy" || id == "gamedata.all" || id == "alchemy.database") {
            respFields = Game::AlchemyDataExtractor::GetAlchemyGameDataJson();
        }
        // 8. Alchemy Workbench Status Query
        else if (id == "alchemy.status" || id == "alchemy.is_open") {
            respFields["isOpen"] = Game::AlchemyController::IsAlchemyItemListReady();
        }
        else {
            respFields["warning"] = "Unrecognized query id";
            respFields["requestedId"] = id;
        }

        return {
            { "type", "data" },
            { "id", id },
            { "fields", respFields },
            { "ts", nowMs }
        };
    }

    nlohmann::json WebSocketServer::ProcessCommand(const std::string& action, const nlohmann::json& payload) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (action == "alchemy.select") {
            std::vector<std::string> ingredients;
            if (payload.contains("ingredients") && payload["ingredients"].is_array()) {
                for (const auto& item : payload["ingredients"]) {
                    if (item.is_string()) {
                        ingredients.push_back(item.get<std::string>());
                    } else if (item.is_number_integer()) {
                        ingredients.push_back(std::format("0x{:08X}", item.get<uint32_t>()));
                    }
                }
            }
            bool craft = payload.value("craft", false);
            int count = payload.value("count", craft ? 1 : 0);
            auto result = Game::AlchemyController::SelectIngredients(ingredients, craft, count);

            nlohmann::json resp = result.ToJson();
            resp["type"] = "command_result";
            resp["action"] = action;
            resp["ts"] = nowMs;
            return resp;
        }
        else if (action == "alchemy.craft") {
            int count = payload.value("count", 1);
            auto result = Game::AlchemyController::CraftPotions(count);
            nlohmann::json resp = result.ToJson();
            resp["type"] = "command_result";
            resp["action"] = action;
            resp["ts"] = nowMs;
            return resp;
        }
        else if (action == "alchemy.stop_craft") {
            bool ok = Game::AlchemyController::StopCrafting();
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", ok },
                { "message", ok ? "Stopped crafting" : "No active craft to stop" },
                { "ts", nowMs }
            };
        }
        else if (action == "alchemy.dismiss_popups") {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                taskInterface->AddTask([]() {
                    Game::AlchemyController::DismissOpenMessageBoxes();
                });
            }
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", true },
                { "message", "Dismissed open popups" },
                { "ts", nowMs }
            };
        }
        else if (action == "alchemy.clear") {
            bool ok = Game::AlchemyController::ClearSelectedIngredients();
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", ok },
                { "message", ok ? "Cleared ingredient selections" : "Failed to clear (alchemy workbench not open?)" },
                { "ts", nowMs }
            };
        }
        else if (action == "alchemy.status") {
            bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", true },
                { "isOpen", isOpen },
                { "ts", nowMs }
            };
        }

        return {
            { "type", "command_result" },
            { "action", action },
            { "success", false },
            { "message", std::format("Unrecognized command action '{}'", action) },
            { "ts", nowMs }
        };
    }

    void WebSocketServer::BroadcastFullSnapshot() {
        if (!HasConnectedClients()) return;

        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_impl || !HasConnectedClients()) return;

                    if (!Util::IsGameSessionReady()) {
                        SKSE::log::info("BroadcastFullSnapshot: Game session not ready, deferring");
                        return;
                    }

                    auto charStats = Game::StatExtractor::GetPlayerStatsJson();
                    auto alchemyStats = Game::StatExtractor::GetAlchemyStatsJson();
                    auto allPerks = Game::PerkExtractor::GetAllPerksJson();
                    auto alchemyPerks = Game::PerkExtractor::GetAlchemyPerksSummary();
                    auto ingredients = Game::InventoryExtractor::GetIngredientsJson();
                    bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();

                    if (isOpen) {
                        Game::MenuOpenCloseHandler::StartMonitoring();
                    }

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 1. character.stats (includes name and language)
                    Broadcast({
                        { "type", "data" },
                        { "id", "character.stats" },
                        { "fields", std::move(charStats) },
                        { "ts", nowMs }
                    });

                    // 2. skypotion.alchemy
                    Broadcast({
                        { "type", "data" },
                        { "id", "skypotion.alchemy" },
                        { "fields", std::move(alchemyStats) },
                        { "ts", nowMs }
                    });

                    // 3. player.perks
                    Broadcast({
                        { "type", "data" },
                        { "id", "player.perks" },
                        { "fields", {
                            { "perks", std::move(allPerks) },
                            { "alchemyPerks", std::move(alchemyPerks) }
                        }},
                        { "ts", nowMs }
                    });

                    // 4. inventory.ingredients
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.ingredients" },
                        { "fields", {
                            { "items", std::move(ingredients) }
                        }},
                        { "ts", nowMs }
                    });

                    // 6. alchemy.menu event
                    Broadcast({
                        { "type", "event" },
                        { "event", "alchemy.menu" },
                        { "isOpen", isOpen },
                        { "ts", nowMs }
                    });

                    SKSE::log::info("BroadcastFullSnapshot: broadcasted full game snapshot to clients (alchemy isOpen={})", isOpen);
                } catch (const std::exception& e) {
                    SKSE::log::error("Error in BroadcastFullSnapshot: {}", e.what());
                } catch (...) {
                    SKSE::log::error("Unknown exception in BroadcastFullSnapshot");
                }
            });
        }
    }

    void WebSocketServer::BroadcastGameData() {
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_impl) return;

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    auto gameData = Game::AlchemyDataExtractor::GetAlchemyGameDataJson();
                    Broadcast({
                        { "type", "data" },
                        { "id", "gamedata.alchemy" },
                        { "fields", std::move(gameData) },
                        { "ts", nowMs }
                    });

                    SKSE::log::info("BroadcastGameData: broadcasted gamedata.alchemy to clients");
                } catch (const std::exception& e) {
                    SKSE::log::error("Error in BroadcastGameData: {}", e.what());
                } catch (...) {
                    SKSE::log::error("Unknown exception in BroadcastGameData");
                }
            });
        }
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

        // 1) 재료 선택 동기화 (Fire-and-Forget)
        if (cmd.has_select_ingredients()) {
            SKSE::log::info("HandleProtoCommand: select_ingredients (reqId={}, count={})",
                reqId, cmd.select_ingredients().form_ids_size());
            std::vector<RE::FormID> formIds;
            formIds.reserve(cmd.select_ingredients().form_ids_size());
            for (uint32_t id : cmd.select_ingredients().form_ids()) {
                formIds.push_back(static_cast<RE::FormID>(id));
            }

            taskInterface->AddUITask([formIds = std::move(formIds)]() {
                try {
                    if (!Util::IsGameSessionReady() || !Game::AlchemyController::IsAlchemyMenuOpen()) {
                        return;
                    }
                    if (formIds.empty()) {
                        Game::AlchemyController::ClearSelectedIngredients();
                    } else {
                        Game::AlchemyController::SelectIngredients(formIds, /*autoCraft=*/false, /*craftCount=*/0);
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("HandleProtoCommand select_ingredients exception: {}", e.what());
                } catch (...) {
                    SKSE::log::error("HandleProtoCommand select_ingredients unknown exception");
                }
            });
            return;
        }

        // 2) 포션 제조 요청 (비동기 응답 -> CommandResult 반환)
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

                    return Sync::ProtoPacketBuilder::BuildCommandResult(reqId, result.success, result.craftedCount, errorCode);
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

    void WebSocketServer::BroadcastGameSessionLoaded() {
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_impl) return;

                    if (!Util::IsGameSessionReady()) {
                        SKSE::log::info("BroadcastGameSessionLoaded: Game session not ready, deferring until it closes");
                        return;
                    }

                    // Pre-warm static gamedata cache on main thread if not already done
                    if (!Game::AlchemyDataExtractor::HasCache()) {
                        Game::AlchemyDataExtractor::GetAlchemyGameDataJson();
                    }

                    // Fast read of dynamic player pointers on main thread (< 1ms)
                    SKSE::log::info("[TRACE] Snapshot 1: GetPlayerStatsJson");
                    auto charStats = Game::StatExtractor::GetPlayerStatsJson();
                    SKSE::log::info("[TRACE] Snapshot 2: GetAlchemyStatsJson");
                    auto alchemyStats = Game::StatExtractor::GetAlchemyStatsJson();
                    SKSE::log::info("[TRACE] Snapshot 3: GetAllPerksJson & Summary");
                    auto allPerks = Game::PerkExtractor::GetAllPerksJson();
                    auto alchemyPerks = Game::PerkExtractor::GetAlchemyPerksSummary();
                    SKSE::log::info("[TRACE] Snapshot 4: GetIngredientsJson");
                    auto ingredients = Game::InventoryExtractor::GetIngredientsJson();
                    SKSE::log::info("[TRACE] Snapshot 5: IsAlchemyItemListReady");
                    bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 1. game.loaded event
                    Broadcast({
                        { "type", "event" },
                        { "event", "game.loaded" },
                        { "ts", nowMs }
                    });

                    // 2. gamedata.alchemy (served from memory cache)
                    Broadcast({
                        { "type", "data" },
                        { "id", "gamedata.alchemy" },
                        { "fields", Game::AlchemyDataExtractor::GetAlchemyGameDataJson() },
                        { "ts", nowMs }
                    });

                    // 3. character.stats (includes name, level, language)
                    Broadcast({
                        { "type", "data" },
                        { "id", "character.stats" },
                        { "fields", std::move(charStats) },
                        { "ts", nowMs }
                    });

                    // 4. skypotion.alchemy (skill level)
                    Broadcast({
                        { "type", "data" },
                        { "id", "skypotion.alchemy" },
                        { "fields", std::move(alchemyStats) },
                        { "ts", nowMs }
                    });

                    // 5. player.perks
                    Broadcast({
                        { "type", "data" },
                        { "id", "player.perks" },
                        { "fields", {
                            { "perks", std::move(allPerks) },
                            { "alchemyPerks", std::move(alchemyPerks) }
                        }},
                        { "ts", nowMs }
                    });

                    // 6. inventory.ingredients
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.ingredients" },
                        { "fields", {
                            { "items", std::move(ingredients) }
                        }},
                        { "ts", nowMs }
                    });

                    // 8. alchemy.menu event
                    Broadcast({
                        { "type", "event" },
                        { "event", "alchemy.menu" },
                        { "isOpen", isOpen },
                        { "ts", nowMs }
                    });

                    SKSE::log::info("BroadcastGameSessionLoaded: broadcasted master game data and player snapshot to clients (alchemy isOpen={})", isOpen);
                } catch (const std::exception& e) {
                    SKSE::log::error("Error in BroadcastGameSessionLoaded: {}", e.what());
                } catch (...) {
                    SKSE::log::error("Unknown exception in BroadcastGameSessionLoaded");
                }
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

                // 1) 마스터 카탈로그 (CatalogSnapshot: 캐릭터명, 재료 마스터, MGEF 마스터, 초기 스탯 등)
                std::string catalogBin = Sync::ProtoPacketBuilder::BuildCatalogSnapshot();
                if (!catalogBin.empty()) {
                    BroadcastBinary(catalogBin);
                }

                // 2) 실시간 플레이어 스탯 & 퍽 (PlayerCraftingStats, Tag 12)
                // 안드로이드 앱의 스킬/스탯 관찰 리스너(Flow)가 즉시 갱신되도록 명시적 전송
                std::string statsBin = Sync::ProtoPacketBuilder::BuildPlayerAlchemyStats();
                if (!statsBin.empty()) {
                    BroadcastBinary(statsBin);
                }

                // 3) 실시간 인벤토리 재료 수량 (InventoryUpdate, Tag 11)
                std::string invBin = Sync::ProtoPacketBuilder::BuildInventoryFullSnapshot();
                if (!invBin.empty()) {
                    BroadcastBinary(invBin);
                }

                // 4) 현재 연금술대 열림 상태 및 선택 재료 전송 (BenchState, Tag 15)
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

    void WebSocketServer::ScheduleSessionSnapshot(int attempt) {
        if (!HasConnectedClients()) return;

        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) return;

        taskInterface->AddTask([this, attempt]() {
            if (!_running.load() || !_impl || !HasConnectedClients()) return;

            if (!Util::IsGameSessionReady()) {
                if (attempt < 20) {
                    // 게임 세션 준비(LoadingMenu 닫힘, 3D 로드 등) 대기 후 재시도 (100ms 간격, 최대 2초)
                    std::thread([this, attempt]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        ScheduleSessionSnapshot(attempt + 1);
                    }).detach();
                } else {
                    SKSE::log::warn("ScheduleSessionSnapshot: Timed out waiting for game session (attempt={})", attempt);
                }
                return;
            }

            SKSE::log::info("ScheduleSessionSnapshot: Game session ready (attempt={}), broadcasting snapshots...", attempt);
            BroadcastProtoSnapshot();
            BroadcastGameSessionLoaded(); // JSON 클라이언트 동시 지원
        });
    }

}
