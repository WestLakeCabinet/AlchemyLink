#include "server/WebSocketServer.h"
#include "game/InventoryExtractor.h"
#include "game/PerkExtractor.h"
#include "game/StatExtractor.h"

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

}
