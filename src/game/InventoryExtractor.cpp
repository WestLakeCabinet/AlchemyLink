#include "game/InventoryExtractor.h"
#include "game/StatExtractor.h"
#include "server/WebSocketServer.h"
#include "sync/ProtoPacketBuilder.h"
#include <chrono>

namespace SkyPotion::Game {

InventoryEventHandler* InventoryEventHandler::GetSingleton() {
    static InventoryEventHandler instance;
    return &instance;
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
    if (!a_event || !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (_isLoading.load() || !Util::IsGameSessionReady()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    constexpr RE::FormID PLAYER_FORM_ID = 0x00000014;
    if (a_event->oldContainer != PLAYER_FORM_ID && a_event->newContainer != PLAYER_FORM_ID) {
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
    if (!a_event || !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (_isLoading.load() || !Util::IsGameSessionReady()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* actor = a_event->actor.get();
    if (!actor || !actor->IsPlayerRef()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(a_event->baseObject);
    if (form && form->Is(RE::FormType::Armor)) {
        RequestAlchemyStatsSync();
    }

    return RE::BSEventNotifyControl::kContinue;
}

void InventoryEventHandler::RequestInventorySync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return;
    }

    if (!_syncPending.exchange(true)) {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                ExecuteInventorySync();
            });
        }
    }
}

void InventoryEventHandler::ExecuteInventorySync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients() || !Util::IsGameSessionReady()) {
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
    Server::WebSocketServer::GetSingleton().BroadcastBinary(bin);
}

void InventoryEventHandler::ExecuteAlchemyStatsSync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients() || !Util::IsGameSessionReady()) {
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
    Server::WebSocketServer::GetSingleton().BroadcastBinary(bin);
}

void InventoryEventHandler::RequestAlchemyStatsSync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return;
    }

    if (!_alchemySyncPending.exchange(true)) {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                ExecuteAlchemyStatsSync();
            });
        }
    }
}
} // namespace SkyPotion::Game