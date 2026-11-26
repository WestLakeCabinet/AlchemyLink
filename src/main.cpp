#include "Pch.h"
#include "Config.h"
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
            {
                const auto& config = SkyPotion::Config::GetSingleton();
                SKSE::log::info("Skyrim data loaded. Starting SkyPotion WebSocket server on {}:{}...",
                    config.GetBindAddress(), config.GetPort());
                SkyPotion::Server::WebSocketServer::GetSingleton().Start(config.GetPort(), config.GetBindAddress());
                SkyPotion::Game::MenuOpenCloseHandler::GetSingleton()->Register();
                SkyPotion::Game::InventoryEventHandler::GetSingleton()->Register();
            }
            break;

        case SKSE::MessagingInterface::kPreLoadGame:
            SKSE::log::info("SKSE Message: kPreLoadGame. Halting any active craft.");
            SkyPotion::Game::InventoryEventHandler::GetSingleton()->SetLoading(true);
            SkyPotion::Server::WebSocketServer::GetSingleton().CancelScheduledSnapshot();
            SkyPotion::Game::AlchemyController::StopCrafting();
            SKSE::log::info("SKSE Message: kPreLoadGame completed.");
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            SKSE::log::info("Game session started or loaded. Scheduling session snapshot.");
            SkyPotion::Game::InventoryEventHandler::GetSingleton()->OnGameSessionLoaded();
            SkyPotion::Server::WebSocketServer::GetSingleton().ScheduleSessionSnapshot();
            break;

            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    InitializeLogging();
    SkyPotion::Config::GetSingleton().Load();

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

extern "C" [[maybe_unused]] __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE /*hinstDLL*/, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_DETACH) {
        SkyPotion::g_isShuttingDown.store(true);
        const bool canJoin = (lpvReserved == nullptr);
        SkyPotion::Game::AlchemyController::StopCrafting(canJoin);
        SkyPotion::Game::MenuOpenCloseHandler::StopMonitoring(canJoin);
        SkyPotion::Server::WebSocketServer::GetSingleton().Stop();
    }
    return TRUE;
}
