#include "server/WebSocketServer.h"
#include "game/InventoryExtractor.h"
#include "game/PerkExtractor.h"
#include "game/StatExtractor.h"
#include "game/AlchemyDataExtractor.h"
#include "game/AlchemyController.h"
#include "sync/ProtoPacketBuilder.h"

#include <chrono>
#include <future>
#include <thread>

namespace SkyPotion::Server {

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
            _server = std::make_unique<ix::WebSocketServer>(_port, _host);

            // Disable per-message deflate compression to prevent handshake failure with Android OkHttp
            // ("unexpected Sec-WebSocket-Extensions in response header")
            _server->disablePerMessageDeflate();

            _server->setOnClientMessageCallback(
                [this](std::shared_ptr<ix::ConnectionState> connectionState,
                       ix::WebSocket& webSocket,
                       const ix::WebSocketMessagePtr& msg) {
                    OnMessage(connectionState, webSocket, msg);
                }
            );

            auto res = _server->listen();
            if (!res.first) {
                SKSE::log::error("WebSocketServer failed to listen on {}:{}: {}", _host, _port, res.second);
                return false;
            }

            _server->start();
            _running.store(true);

            SKSE::log::info("SkyPotion WebSocketServer successfully started on {}:{}", _host, _port);
            return true;
        } catch (const std::exception& e) {
            SKSE::log::error("Exception in WebSocketServer::Start: {}", e.what());
            return false;
        }
    }

    void WebSocketServer::Stop() {
        if (!_running.load()) return;

        SKSE::log::info("Stopping SkyPotion WebSocketServer...");
        _clientCount.store(0, std::memory_order_relaxed);
        if (_server) {
            _server->stop();
            _server.reset();
        }
        _running.store(false);
    }

    bool WebSocketServer::IsRunning() const {
        return _running.load();
    }

    bool WebSocketServer::HasConnectedClients() const {
        return _running.load(std::memory_order_relaxed) &&
            _clientCount.load(std::memory_order_relaxed) > 0;
    }

    void WebSocketServer::Broadcast(const nlohmann::json& message) {
        if (!_running.load() || !_server) return;

        std::string jsonStr = message.dump();
        for (auto& client : _server->getClients()) {
            if (client) {
                client->sendText(jsonStr);
            }
        }
    }

    void WebSocketServer::OnMessage(std::shared_ptr<ix::ConnectionState> connectionState,
                                    ix::WebSocket& webSocket,
                                    const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            _clientCount.fetch_add(1, std::memory_order_relaxed);
            SKSE::log::info("Client connected: {} (URI: {})",
                connectionState ? connectionState->getId() : "unknown",
                msg->openInfo.uri);

            // 안드로이드 앱 접속 시 세션 스냅샷 스케줄링 (준비 대기 및 재시도 포함)
            ScheduleSessionSnapshot();
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Close) {
            SKSE::log::info("Client disconnected: {}", 
                connectionState ? connectionState->getId() : "unknown");

            int prev = _clientCount.fetch_sub(1, std::memory_order_relaxed);
            if (prev <= 1) {
                _clientCount.store(0, std::memory_order_relaxed);
                Game::AlchemyController::StopCrafting();
                Game::MenuOpenCloseHandler::StopMonitoring();
            }
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Error) {
            SKSE::log::warn("WebSocket client error: {}", msg->errorInfo.reason);

            if (!HasConnectedClients()) {
                SKSE::log::info("All clients disconnected. Halting crafting and monitoring.");
                Game::AlchemyController::StopCrafting();
                Game::MenuOpenCloseHandler::StopMonitoring();
            }
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Message) {
            // 바이너리 프레임 -> Protobuf AppCommandPacket 처리
            if (msg->binary) {
                HandleProtoCommand(webSocket, msg->str);
                return;
            }

            try {
                auto root = nlohmann::json::parse(msg->str);
                std::string type = root.value("type", "");

                if (type == "ping") {
                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    nlohmann::json pong = {
                        { "type", "pong" },
                        { "ts", nowMs }
                    };
                    webSocket.sendText(pong.dump());
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
                            webSocket.sendText(response.dump());
                            return;
                        }
                    }

                    // In-flight guard for polling queries: Drop duplicate requests if previous one is still queued
                    static std::atomic<bool> s_queryInFlight{ false };
                    if (s_queryInFlight.exchange(true)) {
                        SKSE::log::warn("Query '{}' dropped: previous query still in flight", id);
                        return;
                    }

                    // Safely query game data on the Skyrim main thread
                    auto taskInterface = SKSE::GetTaskInterface();
                    if (taskInterface) {
                        auto promise = std::make_shared<std::promise<nlohmann::json>>();
                        auto future = promise->get_future();

                        taskInterface->AddTask([promise, id, fields]() {
                            try {
                                if (SkyPotion::g_isShuttingDown.load()) {
                                    s_queryInFlight.store(false);
                                    promise->set_value(nlohmann::json::object());
                                    return;
                                }

                                if (!Util::IsGameSessionReady()) {
                                    s_queryInFlight.store(false);
                                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                    promise->set_value(nlohmann::json{
                                        { "type", "data" },
                                        { "id", id },
                                        { "fields", nlohmann::json::object() },
                                        { "status", "not_ready" },
                                        { "warning", "Game session not ready (in main menu or loading)" },
                                        { "ts", nowMs }
                                    });
                                    return;
                                }

                                auto response = ProcessQuery(id, fields);
                                s_queryInFlight.store(false);
                                promise->set_value(std::move(response));
                            } catch (const std::exception& e) {
                                s_queryInFlight.store(false);
                                SKSE::log::error("Error processing query in main thread: {}", e.what());
                                promise->set_value(nlohmann::json::object());
                            } catch (...) {
                                s_queryInFlight.store(false);
                                promise->set_value(nlohmann::json::object());
                            }
                        });

                        // Wait up to 500ms for main thread completion on network worker thread
                        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready) {
                            nlohmann::json response = future.get();
                            if (!response.empty()) {
                                webSocket.sendText(response.dump());
                            }
                        } else {
                            SKSE::log::warn("Query '{}' timed out waiting for main thread", id);
                        }
                    } else {
                        s_queryInFlight.store(false);
                        // Fallback direct execution if task interface is not ready
                        auto response = ProcessQuery(id, fields);
                        webSocket.sendText(response.dump());
                    }
                    return;
                }

                if (type == "command") {
                    std::string action = root.value("action", "");
                    nlohmann::json payload = root;

                    auto taskInterface = SKSE::GetTaskInterface();
                    if (taskInterface) {
                        auto promise = std::make_shared<std::promise<nlohmann::json>>();
                        auto future = promise->get_future();

                        taskInterface->AddTask([promise, action, payload]() {
                            try {
                                if (!Util::IsGameSessionReady()) {
                                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                    promise->set_value(nlohmann::json{
                                        { "type", "command_result" },
                                        { "action", action },
                                        { "success", false },
                                        { "message", "Game session not ready (in main menu or loading)" },
                                        { "ts", nowMs }
                                    });
                                    return;
                                }

                                auto response = ProcessCommand(action, payload);
                                promise->set_value(std::move(response));
                            } catch (const std::exception& e) {
                                SKSE::log::error("Error processing command in main thread: {}", e.what());
                                promise->set_value(nlohmann::json{
                                    { "type", "command_result" },
                                    { "action", action },
                                    { "success", false },
                                    { "message", std::string("Exception: ") + e.what() }
                                });
                            } catch (...) {
                                promise->set_value(nlohmann::json{
                                    { "type", "command_result" },
                                    { "action", action },
                                    { "success", false },
                                    { "message", "Unknown exception in main thread" }
                                });
                            }
                        });

                        // Wait up to 2000ms for main thread completion
                        if (future.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
                            nlohmann::json response = future.get();
                            webSocket.sendText(response.dump());
                        } else {
                            SKSE::log::warn("Command '{}' timed out waiting for main thread", action);
                            nlohmann::json timeoutResp = {
                                { "type", "command_result" },
                                { "action", action },
                                { "success", false },
                                { "message", "Timeout waiting for main thread" }
                            };
                            webSocket.sendText(timeoutResp.dump());
                        }
                    } else {
                        auto response = ProcessCommand(action, payload);
                        webSocket.sendText(response.dump());
                    }
                    return;
                }
            } catch (const std::exception& e) {
                SKSE::log::warn("Failed to parse incoming WebSocket message: {}", e.what());
                nlohmann::json err = {
                    { "type", "error" },
                    { "message", std::string("Malformed JSON: ") + e.what() }
                };
                webSocket.sendText(err.dump());
            }
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
                    if (!_running.load() || !_server || !HasConnectedClients()) return;

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
                    if (!_running.load() || !_server) return;

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

    void WebSocketServer::HandleProtoCommand(ix::WebSocket& webSocket, const std::string& rawBytes) {
        Sync::pb::AppCommandPacket cmd;
        if (!cmd.ParseFromString(rawBytes)) {
            SKSE::log::warn("HandleProtoCommand: failed to parse AppCommandPacket ({} bytes)", rawBytes.size());
            return;
        }

        const uint64_t reqId = cmd.request_id();
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            webSocket.sendBinary(Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "NO_TASK_INTERFACE"));
            return;
        }

        // 1) 재료 선택 동기화 (Fire-and-Forget)
        if (cmd.has_select_ingredients()) {
            std::vector<RE::FormID> formIds;
            formIds.reserve(cmd.select_ingredients().form_ids_size());
            for (uint32_t id : cmd.select_ingredients().form_ids()) {
                formIds.push_back(static_cast<RE::FormID>(id));
            }

            taskInterface->AddTask([formIds = std::move(formIds)]() {
                if (!Util::IsGameSessionReady() || !Game::AlchemyController::IsAlchemyMenuOpen()) {
                    return;
                }
                if (formIds.empty()) {
                    Game::AlchemyController::ClearSelectedIngredients();
                } else {
                    Game::AlchemyController::SelectIngredients(formIds, /*autoCraft=*/false, /*craftCount=*/0);
                }
            });
            return;
        }

        // 2) 포션 제조 요청 (응답 대기 -> CommandResult 반환)
        if (cmd.has_craft_potion()) {
            std::vector<RE::FormID> formIds;
            formIds.reserve(cmd.craft_potion().form_ids_size());
            for (uint32_t id : cmd.craft_potion().form_ids()) {
                formIds.push_back(static_cast<RE::FormID>(id));
            }
            const int32_t count = cmd.craft_potion().count();

            auto promise = std::make_shared<std::promise<std::string>>();
            auto future = promise->get_future();

            taskInterface->AddTask([this, promise, reqId, formIds = std::move(formIds), count]() {
                if (!Util::IsGameSessionReady() || !Game::AlchemyController::IsAlchemyMenuOpen()) {
                    promise->set_value(
                        Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "BENCH_CLOSED")
                    );
                    return;
                }

                auto result = Game::AlchemyController::SelectIngredients(formIds, /*autoCraft=*/true, count);
                std::string errorCode = result.success ? "" : (result.message.empty() ? "CRAFT_FAILED" : result.message);

                promise->set_value(
                    Sync::ProtoPacketBuilder::BuildCommandResult(reqId, result.success, result.craftedCount, errorCode)
                );

                // 제조 성공 시 스킬 레벨업 / 신규 효과 해금 동기화
                if (result.success) {
                    BroadcastBinary(Sync::ProtoPacketBuilder::BuildDiscoveredEffects(formIds));
                    Game::InventoryEventHandler::GetSingleton()->RequestAlchemyStatsSync();
                }
            });

            if (future.wait_for(std::chrono::milliseconds(2500)) == std::future_status::ready) {
                webSocket.sendBinary(future.get());
            } else {
                SKSE::log::warn("HandleProtoCommand: craft_potion (reqId={}) timed out", reqId);
                webSocket.sendBinary(Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "TIMEOUT"));
            }
        }
    }

    void WebSocketServer::BroadcastGameSessionLoaded() {
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_server) return;

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
    if (!_running.load() || !_server || binaryPayload.empty()) return;

    for (auto& client : _server->getClients()) {
        if (client) {
            client->sendBinary(binaryPayload);
        }
    }
}

void WebSocketServer::BroadcastProtoSnapshot() {
    if (!HasConnectedClients()) return;

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) return;

    taskInterface->AddTask([this]() {
        try {
            if (!_running.load() || !_server || !HasConnectedClients()) return;
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
        if (!_running.load() || !_server || !HasConnectedClients()) return;

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
