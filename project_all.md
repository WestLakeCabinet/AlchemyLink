This file is a merged representation of a subset of the codebase, containing specifically included files, combined into a single document by Repomix.

<file_summary>
This section contains a summary of this file.

<purpose>
This file contains a packed representation of a subset of the repository's contents that is considered the most important context.
It is designed to be easily consumable by AI systems for analysis, code review,
or other automated processes.
</purpose>

<file_format>
The content is organized as follows:
1. This summary section
2. Repository information
3. Directory structure
4. Repository files (if enabled)
5. Multiple file entries, each consisting of:
  - File path as an attribute
  - Full contents of the file
</file_format>

<usage_guidelines>
- This file should be treated as read-only. Any changes should be made to the
  original repository files, not this packed version.
- When processing this file, use the file path to distinguish
  between different files in the repository.
- Be aware that this file may contain sensitive information. Handle it with
  the same level of security as you would the original repository.
</usage_guidelines>

<notes>
- Some files may have been excluded based on .gitignore rules and Repomix's configuration
- Binary files are not included in this packed representation. Please refer to the Repository Structure section for a complete list of file paths, including binary files
- Only files matching these patterns are included: **/*.cpp, **/*.h, CMakeLists.*, vcpkg.json, build
- Files matching patterns in .gitignore are excluded
- Files matching default ignore patterns are excluded
- Files are sorted by Git change count (files with more changes are at the bottom)
</notes>

</file_summary>

<directory_structure>
src/
  game/
    InventoryExtractor.cpp
    InventoryExtractor.h
    PerkExtractor.cpp
    PerkExtractor.h
    StatExtractor.cpp
    StatExtractor.h
  server/
    WebSocketServer.cpp
    WebSocketServer.h
  main.cpp
  Pch.h
CMakeLists.txt
vcpkg.json
</directory_structure>

<files>
This section contains the contents of the repository's files.

<file path="src/game/InventoryExtractor.cpp">
#include "game/InventoryExtractor.h"

namespace SkyPotion::Game {

    nlohmann::json InventoryExtractor::GetIngredientsJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        nlohmann::json itemsArray = nlohmann::json::array();

        if (!player) return itemsArray;

        const auto inventory = player->GetInventory();

        for (const auto& [boundObj, data] : inventory) {
            const auto count = data.first;
            if (count <= 0 || !boundObj) continue;

            auto* ingredient = boundObj->As<RE::IngredientItem>();
            if (!ingredient) continue;

            nlohmann::json effectsJson = nlohmann::json::array();
            const auto& effects = ingredient->effects;

            for (uint32_t i = 0; i < effects.size(); ++i) {
                auto* effectItem = effects[i];
                if (!effectItem || !effectItem->baseEffect) continue;

                const char* effectName = effectItem->baseEffect->GetName();
                // Check if player has discovered this effect (bit flag in gamedata)
                bool isKnown = (ingredient->gamedata.knownEffectFlags & (1 << i)) != 0;

                effectsJson.push_back({
                    { "name", effectName ? effectName : "" },
                    { "known", isKnown }
                });
            }

            itemsArray.push_back({
                { "categoryType", "Ingredient" },
                { "formId", std::format("0x{:08X}", ingredient->GetFormID()) },
                { "name", ingredient->GetName() ? ingredient->GetName() : "" },
                { "count", count },
                { "weight", ingredient->GetWeight() },
                { "value", ingredient->GetGoldValue() },
                { "effects", effectsJson }
            });
        }

        return itemsArray;
    }

    nlohmann::json InventoryExtractor::GetApparelJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        nlohmann::json itemsArray = nlohmann::json::array();

        if (!player) return itemsArray;

        const auto inventory = player->GetInventory();

        for (const auto& [boundObj, data] : inventory) {
            const auto count = data.first;
            if (count <= 0 || !boundObj) continue;

            auto* armor = boundObj->As<RE::TESObjectARMO>();
            if (!armor) continue;

            auto* entryData = data.second.get();
            bool isEquipped = entryData && entryData->IsWorn();

            // Extract enchantments (base enchantment or player-crafted extra enchantment)
            RE::EnchantmentItem* enchantment = armor->formEnchanting;
            if (entryData && entryData->extraLists) {
                for (auto* xList : *entryData->extraLists) {
                    if (xList) {
                        auto* xEnch = xList->GetByType<RE::ExtraEnchantment>();
                        if (xEnch && xEnch->enchantment) {
                            enchantment = xEnch->enchantment;
                            break;
                        }
                    }
                }
            }

            if (!enchantment) continue;

            nlohmann::json effectsJson = nlohmann::json::array();
            bool hasAlchemyBonus = false;

            for (auto* eff : enchantment->effects) {
                if (!eff || !eff->baseEffect) continue;
                const char* effName = eff->baseEffect->GetName();
                double magnitude = eff->GetMagnitude();

                if (effName && std::string(effName).find("Alchemy") != std::string::npos) {
                    hasAlchemyBonus = true;
                }

                effectsJson.push_back({
                    { "name", effName ? effName : "" },
                    { "magnitude", magnitude }
                });
            }

            if (hasAlchemyBonus) {
                itemsArray.push_back({
                    { "categoryType", "Apparel" },
                    { "formId", std::format("0x{:08X}", armor->GetFormID()) },
                    { "name", armor->GetName() ? armor->GetName() : "" },
                    { "count", count },
                    { "isEquipped", isEquipped },
                    { "enchantment", { { "effects", effectsJson } } }
                });
            }
        }

        return itemsArray;
    }
}
</file>

<file path="src/game/InventoryExtractor.h">
#pragma once

#include "Pch.h"

namespace SkyPotion::Game {
    class InventoryExtractor {
    public:
        // Returns list of ingredients with count and known effects
        static nlohmann::json GetIngredientsJson();

        // Returns list of apparel items with Fortify Alchemy enchantments and equipped state
        static nlohmann::json GetApparelJson();
    };
}
</file>

<file path="src/game/PerkExtractor.h">
#pragma once

#include "Pch.h"

namespace SkyPotion::Game {
    class PerkExtractor {
    public:
        // Returns complete player perks list
        static nlohmann::json GetAllPerksJson();

        // Returns dedicated alchemy perks summary (rank and boolean flags)
        static nlohmann::json GetAlchemyPerksSummary();
    };
}
</file>

<file path="src/game/StatExtractor.cpp">
#include "game/StatExtractor.h"

namespace SkyPotion::Game {

    nlohmann::json StatExtractor::GetAlchemyStatsJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nlohmann::json::object();

        auto avOwner = player->AsActorValueOwner();
        if (!avOwner) return nlohmann::json::object();

        double currentSkill = avOwner->GetActorValue(RE::ActorValue::kAlchemy);
        double baseSkill = avOwner->GetBaseActorValue(RE::ActorValue::kAlchemy);

        return {
            { "alchemy", currentSkill },
            { "alchemyBase", baseSkill }
        };
    }

    nlohmann::json StatExtractor::GetPlayerStatsJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nlohmann::json::object();

        auto avOwner = player->AsActorValueOwner();
        if (!avOwner) return nlohmann::json::object();

        return {
            { "level", player->GetLevel() },
            { "health", avOwner->GetActorValue(RE::ActorValue::kHealth) },
            { "healthBase", avOwner->GetBaseActorValue(RE::ActorValue::kHealth) },
            { "magicka", avOwner->GetActorValue(RE::ActorValue::kMagicka) },
            { "magickaBase", avOwner->GetBaseActorValue(RE::ActorValue::kMagicka) },
            { "stamina", avOwner->GetActorValue(RE::ActorValue::kStamina) },
            { "staminaBase", avOwner->GetBaseActorValue(RE::ActorValue::kStamina) },
            { "inventoryWeight", player->GetWeightInContainer() }
        };
    }
}
</file>

<file path="src/game/StatExtractor.h">
#pragma once

#include "Pch.h"

namespace SkyPotion::Game {
    class StatExtractor {
    public:
        static nlohmann::json GetAlchemyStatsJson();
        static nlohmann::json GetPlayerStatsJson();
    };
}
</file>

<file path="src/server/WebSocketServer.h">
#pragma once

#include "Pch.h"
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXWebSocket.h>

namespace SkyPotion::Server {

    class WebSocketServer {
    public:
        static WebSocketServer& GetSingleton() {
            static WebSocketServer instance;
            return instance;
        }

        bool Start(uint16_t port = 8765, const std::string& host = "0.0.0.0");
        void Stop();
        bool IsRunning() const;

        void Broadcast(const nlohmann::json& message);

    private:
        WebSocketServer();
        ~WebSocketServer();

        WebSocketServer(const WebSocketServer&) = delete;
        WebSocketServer& operator=(const WebSocketServer&) = delete;

        void OnMessage(std::shared_ptr<ix::ConnectionState> connectionState,
                       ix::WebSocket& webSocket,
                       const ix::WebSocketMessagePtr& msg);

        static nlohmann::json ProcessQuery(const std::string& id, const nlohmann::json& fields);

        std::unique_ptr<ix::WebSocketServer> _server;
        std::atomic<bool> _running{false};
        uint16_t _port{8765};
        std::string _host{"0.0.0.0"};
    };

}
</file>

<file path="src/main.cpp">
#include "Pch.h"
#include "server/WebSocketServer.h"

#include <spdlog/sinks/basic_file_sink.h>

using namespace std::literals;

SKSEPluginInfo(
    .Version = REL::Version{ 1, 0, 0, 0 },
    .Name = "SkyPotionSKSE"sv,
    .Author = "Dolzzo"sv,
    .SupportEmail = ""sv,
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 }
);

namespace {
    void InitializeLogging() {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        *path /= "SkyPotionSKSE.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_msg) {
        if (!a_msg) return;

        switch (a_msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                SKSE::log::info("Skyrim data loaded. Starting SkyPotion WebSocket server on port 8765...");
                SkyPotion::Server::WebSocketServer::GetSingleton().Start(8765, "0.0.0.0");
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                SKSE::log::info("Game session started or loaded.");
                break;

            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    InitializeLogging();

    SKSE::Init(a_skse);

    SKSE::log::info("----------------------------------------");
    SKSE::log::info("SkyPotion SKSE Plugin v1.0.0 loaded!");
    SKSE::log::info("Targeting Skyrim SE / AE / GOG");
    SKSE::log::info("----------------------------------------");

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::error("Failed to get SKSE messaging interface!");
        return false;
    }

    if (!messaging->RegisterListener(OnMessage)) {
        SKSE::log::error("Failed to register SKSE message listener!");
        return false;
    }

    return true;
}
</file>

<file path="src/game/PerkExtractor.cpp">
#include "game/PerkExtractor.h"

namespace SkyPotion::Game {

    nlohmann::json PerkExtractor::GetAllPerksJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        nlohmann::json perksArray = nlohmann::json::array();

        if (!player) return perksArray;

        const std::vector<std::pair<RE::FormID, const char*>> trackedPerks = {
            { 0x000BE127, "Alchemist 1" },
            { 0x000C07CA, "Alchemist 2" },
            { 0x000C07CB, "Alchemist 3" },
            { 0x000C07CC, "Alchemist 4" },
            { 0x000C07CD, "Alchemist 5" },
            { 0x00058215, "Physician" },
            { 0x00058216, "Benefactor" },
            { 0x00058217, "Poisoner" },
            { 0x0005821D, "Purity" },
            { 0x00058218, "Experimenter 1" },
            { 0x00105F2A, "Experimenter 2" },
            { 0x00105F2B, "Experimenter 3" },
            { 0x00105F2E, "Green Thumb" },
            { 0x00105F2C, "Snakeblood" }
        };

        for (const auto& [formId, fallbackName] : trackedPerks) {
            auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(formId);
            if (perk && player->HasPerk(perk)) {
                const char* realName = perk->GetName();
                perksArray.push_back({
                    { "formId", std::format("0x{:08X}", formId) },
                    { "name", (realName && realName[0] != '\0') ? realName : fallbackName },
                    { "rank", 1 }
                });
            }
        }

        return perksArray;
    }

    nlohmann::json PerkExtractor::GetAlchemyPerksSummary() {
        auto player = RE::PlayerCharacter::GetSingleton();
        nlohmann::json summary = {
            { "alchemist", 0 },
            { "physician", false },
            { "benefactor", false },
            { "poisoner", false },
            { "purity", false },
            { "experimenter", 0 },
            { "greenThumb", false },
            { "snakeblood", false }
        };

        if (!player) return summary;

        auto hasPerk = [player](RE::FormID formId) -> bool {
            auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(formId);
            return perk && player->HasPerk(perk);
        };

        // Alchemist 1 ~ 5
        if (hasPerk(0x000C07CD)) summary["alchemist"] = 5;
        else if (hasPerk(0x000C07CC)) summary["alchemist"] = 4;
        else if (hasPerk(0x000C07CB)) summary["alchemist"] = 3;
        else if (hasPerk(0x000C07CA)) summary["alchemist"] = 2;
        else if (hasPerk(0x000BE127)) summary["alchemist"] = 1;

        // Core alchemy perks
        summary["physician"] = hasPerk(0x00058215);
        summary["benefactor"] = hasPerk(0x00058216);
        summary["poisoner"] = hasPerk(0x00058217);
        summary["purity"] = hasPerk(0x0005821D);

        // Experimenter 1 ~ 3
        if (hasPerk(0x00105F2B)) summary["experimenter"] = 3;
        else if (hasPerk(0x00105F2A)) summary["experimenter"] = 2;
        else if (hasPerk(0x00058218)) summary["experimenter"] = 1;

        // Green Thumb & Snakeblood
        summary["greenThumb"] = hasPerk(0x00105F2E);
        summary["snakeblood"] = hasPerk(0x00105F2C);

        return summary;
    }
}
</file>

<file path="src/server/WebSocketServer.cpp">
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
</file>

<file path="src/Pch.h">
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <cstddef>

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <format>
#include <algorithm>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>
</file>

<file path="CMakeLists.txt">
cmake_minimum_required(VERSION 3.25)

project(
    SkyPotionSKSE
    VERSION 1.0.0
    DESCRIPTION "Skyrim SE/AE WebSocket Plugin for SkyPotion"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure CRT matches CommonLibSSE-NG (/MD dynamic CRT with static library linkage)
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg triplet matching /MD CRT")

# Enable MSVC multi-processor parallel compilation (/MP) and UTF-8 source/exec charset
if(MSVC)
    add_compile_options(/MP /utf-8)
endif()

# 여기에 추가 (라인 20 부근)
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# CommonLibSSE-NG build options (SE/AE enabled, tests and VR disabled)
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE unit tests" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Enable Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Enable Skyrim AE (1.6+ / 1.7+) support" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Disable Skyrim VR support" FORCE)

# CommonLibSSE-NG (Maintained upstream supporting 1.7.104 & Address Library Format 5)
include(FetchContent)
FetchContent_Declare(
    CommonLibSSE-NG
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG        ng
)
FetchContent_MakeAvailable(CommonLibSSE-NG)

# Add SkyPotion SKSE plugin library
add_library(
    ${PROJECT_NAME} SHARED
    src/Pch.h
    src/main.cpp
    src/server/WebSocketServer.h
    src/server/WebSocketServer.cpp
    src/game/PerkExtractor.h
    src/game/PerkExtractor.cpp
    src/game/InventoryExtractor.h
    src/game/InventoryExtractor.cpp
    src/game/StatExtractor.h
    src/game/StatExtractor.cpp
)

# Enable Precompiled Headers for ultra-fast compilation & type consistency
target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE
        src/Pch.h
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        CommonLibSSE::CommonLibSSE
        nlohmann_json::nlohmann_json
        ixwebsocket::ixwebsocket
        spdlog::spdlog
        ws2_32
        crypt32
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        src
)
</file>

<file path="vcpkg.json">
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "skypotion-skse",
  "version-string": "1.0.0",
  "description": "Skyrim SE/AE WebSocket Plugin for SkyPotion Android App",
  "builtin-baseline": "1de2026f28ead93ff1773e6e680387643e914ea1",
  "dependencies": [
    "nlohmann-json",
    "ixwebsocket",
    "spdlog",
    "rapidcsv",
    "directxtk"
  ]
}
</file>

</files>
