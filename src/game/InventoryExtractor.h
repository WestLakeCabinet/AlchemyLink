#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

class InventoryExtractor {
public:
    // Returns list of ingredients with count and known effects
    static nlohmann::json GetIngredientsJson();
};

class InventoryEventHandler :
    public RE::BSTEventSink<RE::TESContainerChangedEvent>,
    public RE::BSTEventSink<RE::TESEquipEvent> {
public:
    static InventoryEventHandler* GetSingleton();
    void Register();

    // 1. 재료 획득/소모 감지
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESContainerChangedEvent* a_event,
        RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;

    // 2. 장비 착용/해제 감지
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESEquipEvent* a_event,
        RE::BSTEventSource<RE::TESEquipEvent>* a_eventSource) override;

    void RequestInventorySync();
    void ExecuteInventorySync();

    void RequestAlchemyStatsSync();
    void ExecuteAlchemyStatsSync();

private:
    std::atomic<bool> _syncPending{ false };
    std::atomic<int64_t> _lastSyncTimeMs{ 0 };

    std::atomic<bool> _alchemySyncPending{ false };
    std::atomic<int64_t> _lastAlchemySyncTimeMs{ 0 };
};

} // namespace SkyPotion::Game