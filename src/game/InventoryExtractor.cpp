#include "game/InventoryExtractor.h"
#include "game/StatExtractor.h"
#include "server/WebSocketServer.h"
#include "sync/ProtoPacketBuilder.h"
#include <chrono>
#include <thread>

namespace SkyPotion::Game {

InventoryEventHandler* InventoryEventHandler::GetSingleton() {
    static InventoryEventHandler instance;
    return &instance;
}

void InventoryEventHandler::SetLoading(bool loading) {
    if (loading) {
        _loadGeneration.fetch_add(1);
    }
    _isLoading.store(loading);
}

void InventoryEventHandler::OnGameSessionLoaded(int attempt) {
    if (SkyPotion::g_isShuttingDown.load()) {
        return;
    }

    const uint64_t gen = _loadGeneration.load();
    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        _isLoading.store(false);
        return;
    }

    taskInterface->AddTask([this, attempt, gen]() {
        if (SkyPotion::g_isShuttingDown.load() || gen != _loadGeneration.load()) {
            return;
        }

        if (!Util::IsGameSessionReady()) {
            if (attempt < 40) {
                std::thread([this, attempt, gen]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (!SkyPotion::g_isShuttingDown.load() && gen == _loadGeneration.load()) {
                        OnGameSessionLoaded(attempt + 1);
                    }
                }).detach();
            } else {
                SKSE::log::warn("InventoryEventHandler: Timed out waiting for game session readiness (attempt={}), releasing loading guard.", attempt);
                _isLoading.store(false);
            }
            return;
        }

        _isLoading.store(false);
        SKSE::log::info("InventoryEventHandler: Game session ready (attempt={}), loading guard released.", attempt);
    });
}

void InventoryEventHandler::Register() {
    auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventHolder) {
        eventHolder->AddEventSink<RE::TESContainerChangedEvent>(this);
        eventHolder->AddEventSink<RE::TESEquipEvent>(this);
        SKSE::log::info("InventoryEventHandler registered for TESContainerChangedEvent & TESEquipEvent");
    } else {
        SKSE::log::warn("ScriptEventSourceHolder unavailable to register InventoryEventHandler");
    }
}

RE::BSEventNotifyControl InventoryEventHandler::ProcessEvent(
    const RE::TESContainerChangedEvent* a_event,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
    if (!a_event || _isLoading.load() || SkyPotion::g_isShuttingDown.load()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    constexpr RE::FormID PLAYER_FORM_ID = 0x00000014;
    if (a_event->oldContainer != PLAYER_FORM_ID && a_event->newContainer != PLAYER_FORM_ID) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (!Util::IsGameSessionReady()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    constexpr RE::FormID GOLD_FORM_ID = 0x0000000F;
    if (a_event->baseObj == GOLD_FORM_ID) {
        RequestAlchemyStatsSync();
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(a_event->baseObj);
    if (form && form->Is(RE::FormType::Ingredient)) {
        RequestInventorySync();
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl InventoryEventHandler::ProcessEvent(
    const RE::TESEquipEvent* a_event,
    RE::BSTEventSource<RE::TESEquipEvent>*) {
    if (!a_event || _isLoading.load() || SkyPotion::g_isShuttingDown.load()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // 포인터 역참조나 가상 함수 호출 전에 플레이어 싱글톤 포인터와 직접 비교
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || a_event->actor.get() != player) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (!Util::IsGameSessionReady()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(a_event->baseObject);
    if (form && form->Is(RE::FormType::Armor)) {
        RequestAlchemyStatsSync();
    }

    return RE::BSEventNotifyControl::kContinue;
}

void InventoryEventHandler::RequestInventorySync() {
    if (_isLoading.load() || SkyPotion::g_isShuttingDown.load() ||
        !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return;
    }

    if (!_syncPending.exchange(true)) {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                ExecuteInventorySync();
            });
        } else {
            _syncPending.store(false);
        }
    }
}

void InventoryEventHandler::ExecuteInventorySync() {
    if (_isLoading.load() || SkyPotion::g_isShuttingDown.load() ||
        !Server::WebSocketServer::GetSingleton().HasConnectedClients() ||
        !Util::IsGameSessionReady()) {
        _syncPending.store(false);
        return;
    }

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (nowMs - _lastSyncTimeMs.load() < 150) {
        _syncPending.store(false);
        return;
    }

    _syncPending.store(false);
    _lastSyncTimeMs.store(nowMs);

    auto bin = Sync::ProtoPacketBuilder::BuildInventoryFullSnapshot();
    if (!bin.empty()) {
        Server::WebSocketServer::GetSingleton().BroadcastBinary(bin);
    }
}

void InventoryEventHandler::ExecuteAlchemyStatsSync() {
    if (_isLoading.load() || SkyPotion::g_isShuttingDown.load() ||
        !Server::WebSocketServer::GetSingleton().HasConnectedClients() ||
        !Util::IsGameSessionReady()) {
        _alchemySyncPending.store(false);
        return;
    }

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (nowMs - _lastAlchemySyncTimeMs.load() < 100) {
        _alchemySyncPending.store(false);
        return;
    }

    _alchemySyncPending.store(false);
    _lastAlchemySyncTimeMs.store(nowMs);

    auto bin = Sync::ProtoPacketBuilder::BuildPlayerAlchemyStats();
    if (!bin.empty()) {
        Server::WebSocketServer::GetSingleton().BroadcastBinary(bin);
    }
}

void InventoryEventHandler::RequestAlchemyStatsSync() {
    if (_isLoading.load() || SkyPotion::g_isShuttingDown.load() ||
        !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return;
    }

    if (!_alchemySyncPending.exchange(true)) {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                ExecuteAlchemyStatsSync();
            });
        } else {
            _alchemySyncPending.store(false);
        }
    }
}
} // namespace SkyPotion::Game