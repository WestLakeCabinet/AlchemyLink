#include "server/WebSocketServer.h"
#include "game/InventoryExtractor.h"
#include "game/PerkExtractor.h"
#include "game/StatExtractor.h"
#include "game/AlchemyDataExtractor.h"
#include "game/AlchemyController.h"

#include <chrono>
#include <future>

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
        if (_server) {
            _server->stop();
            _server.reset();
        }
        _running.store(false);
    }

    bool WebSocketServer::IsRunning() const {
        return _running.load();
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
            SKSE::log::info("Client connected: {} (URI: {})", 
                connectionState ? connectionState->getId() : "unknown", 
                msg->openInfo.uri);

            BroadcastGameSessionLoaded();
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Close) {
            SKSE::log::info("Client disconnected: {}", 
                connectionState ? connectionState->getId() : "unknown");
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Error) {
            SKSE::log::warn("WebSocket client error: {}", msg->errorInfo.reason);
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Message) {
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

                    // Safely query game data on the Skyrim main thread
                    auto taskInterface = SKSE::GetTaskInterface();
                    if (taskInterface) {
                        auto promise = std::make_shared<std::promise<nlohmann::json>>();
                        auto future = promise->get_future();

                        taskInterface->AddTask([promise, id, fields]() {
                            try {
                                auto response = ProcessQuery(id, fields);
                                promise->set_value(std::move(response));
                            } catch (const std::exception& e) {
                                SKSE::log::error("Error processing query in main thread: {}", e.what());
                                promise->set_value(nlohmann::json::object());
                            } catch (...) {
                                promise->set_value(nlohmann::json::object());
                            }
                        });

                        // Wait up to 500ms for main thread completion
                        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready) {
                            nlohmann::json response = future.get();
                            webSocket.sendText(response.dump());
                        } else {
                            SKSE::log::warn("Query '{}' timed out waiting for main thread", id);
                        }
                    } else {
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

                        // Wait up to 500ms for main thread completion
                        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready) {
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
        // 2. Apparel Query (Fortify Alchemy gear)
        else if (id == "inventory.apparel" || 
                 (fields.is_object() && fields.value("items", "") == "Inventory::Items::Apparel") ||
                 (fields.is_object() && fields.value("apparel", "") == "Actor::Inventory::Apparel")) {
            respFields["items"] = Game::InventoryExtractor::GetApparelJson();
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
            respFields["apparel"] = Game::InventoryExtractor::GetApparelJson();
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
            auto result = Game::AlchemyController::SelectIngredients(ingredients, craft);

            nlohmann::json resp = result.ToJson();
            resp["type"] = "command_result";
            resp["action"] = action;
            resp["ts"] = nowMs;
            return resp;
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
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_server) return;

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 1. character.stats (includes name and language)
                    Broadcast({
                        { "type", "data" },
                        { "id", "character.stats" },
                        { "fields", Game::StatExtractor::GetPlayerStatsJson() },
                        { "ts", nowMs }
                    });

                    // 2. skypotion.alchemy
                    Broadcast({
                        { "type", "data" },
                        { "id", "skypotion.alchemy" },
                        { "fields", Game::StatExtractor::GetAlchemyStatsJson() },
                        { "ts", nowMs }
                    });

                    // 3. player.perks
                    Broadcast({
                        { "type", "data" },
                        { "id", "player.perks" },
                        { "fields", {
                            { "perks", Game::PerkExtractor::GetAllPerksJson() },
                            { "alchemyPerks", Game::PerkExtractor::GetAlchemyPerksSummary() }
                        }},
                        { "ts", nowMs }
                    });

                    // 4. inventory.ingredients
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.ingredients" },
                        { "fields", {
                            { "items", Game::InventoryExtractor::GetIngredientsJson() }
                        }},
                        { "ts", nowMs }
                    });

                    // 5. inventory.apparel
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.apparel" },
                        { "fields", {
                            { "items", Game::InventoryExtractor::GetApparelJson() }
                        }},
                        { "ts", nowMs }
                    });

                    // 6. alchemy.menu event
                    bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();
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

    void WebSocketServer::BroadcastGameSessionLoaded() {
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_server) return;

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 1. game.loaded event
                    Broadcast({
                        { "type", "event" },
                        { "event", "game.loaded" },
                        { "ts", nowMs }
                    });

                    // 2. gamedata.alchemy (Master BaseEffects + Playable Ingredients)
                    auto gameData = Game::AlchemyDataExtractor::GetAlchemyGameDataJson();
                    Broadcast({
                        { "type", "data" },
                        { "id", "gamedata.alchemy" },
                        { "fields", std::move(gameData) },
                        { "ts", nowMs }
                    });

                    // 3. character.stats (includes name, level, language)
                    Broadcast({
                        { "type", "data" },
                        { "id", "character.stats" },
                        { "fields", Game::StatExtractor::GetPlayerStatsJson() },
                        { "ts", nowMs }
                    });

                    // 4. skypotion.alchemy (skill level)
                    Broadcast({
                        { "type", "data" },
                        { "id", "skypotion.alchemy" },
                        { "fields", Game::StatExtractor::GetAlchemyStatsJson() },
                        { "ts", nowMs }
                    });

                    // 5. player.perks
                    Broadcast({
                        { "type", "data" },
                        { "id", "player.perks" },
                        { "fields", {
                            { "perks", Game::PerkExtractor::GetAllPerksJson() },
                            { "alchemyPerks", Game::PerkExtractor::GetAlchemyPerksSummary() }
                        }},
                        { "ts", nowMs }
                    });

                    // 6. inventory.ingredients
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.ingredients" },
                        { "fields", {
                            { "items", Game::InventoryExtractor::GetIngredientsJson() }
                        }},
                        { "ts", nowMs }
                    });

                    // 7. inventory.apparel
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.apparel" },
                        { "fields", {
                            { "items", Game::InventoryExtractor::GetApparelJson() }
                        }},
                        { "ts", nowMs }
                    });

                    // 8. alchemy.menu event
                    bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();
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

}
