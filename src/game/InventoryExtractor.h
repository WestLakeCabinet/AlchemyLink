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

    class InventoryEventHandler : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
    public:
        static InventoryEventHandler* GetSingleton();
        void Register();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESContainerChangedEvent* a_event,
            RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;

        void RequestInventorySync(bool isIngredient, bool isApparel);
        void ExecuteInventorySync();

    private:
        std::atomic<bool> _syncPending{ false };
        std::atomic<bool> _syncApparel{ false };
    };
}
