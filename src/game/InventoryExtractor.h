#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

    class InventoryExtractor {
    public:
        // Returns list of ingredients with count and known effects
        static nlohmann::json GetIngredientsJson();
    };

    class InventoryEventHandler : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
    public:
        static InventoryEventHandler* GetSingleton();
        void Register();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESContainerChangedEvent* a_event,
            RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;

        void RequestInventorySync();
        void ExecuteInventorySync();

    private:
        std::atomic<bool> _syncPending{ false };
        std::atomic<int64_t> _lastSyncTimeMs{ 0 };
    };

} // namespace SkyPotion::Game