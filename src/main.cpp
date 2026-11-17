#include "Pch.h"
#include "server/WebSocketServer.h"
#include "game/AlchemyController.h"
#include "game/InventoryExtractor.h"

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
                SkyPotion::Game::MenuOpenCloseHandler::GetSingleton()->Register();
                SkyPotion::Game::InventoryEventHandler::GetSingleton()->Register();
                break;

            case SKSE::MessagingInterface::kPostLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                SKSE::log::info("Game session started or loaded. Broadcasting master game data and player snapshot...");
                SkyPotion::Server::WebSocketServer::GetSingleton().BroadcastGameSessionLoaded();
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
