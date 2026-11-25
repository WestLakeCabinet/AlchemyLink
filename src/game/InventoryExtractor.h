#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

class InventoryExtractor {
public:
};

class InventoryEventHandler :
    public RE::BSTEventSink<RE::TESContainerChangedEvent>,
    public RE::BSTEventSink<RE::TESEquipEvent> {
public:
    static InventoryEventHandler* GetSingleton();
    void Register();

    // 로딩 중 이벤트 무시용 상태 제어
    void SetLoading(bool loading) { _isLoading.store(loading); }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESContainerChangedEvent* a_event,
        RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESEquipEvent* a_event,
        RE::BSTEventSource<RE::TESEquipEvent>* a_eventSource) override;

    void RequestInventorySync();
    void ExecuteInventorySync();

    void RequestAlchemyStatsSync();
    void ExecuteAlchemyStatsSync();

private:
    std::atomic<bool> _isLoading{ true }; // 초기값 true (게임 로드 완료 전까지 차단)
    std::atomic<bool> _syncPending{ false };
    std::atomic<int64_t> _lastSyncTimeMs{ 0 };

    std::atomic<bool> _alchemySyncPending{ false };
    std::atomic<int64_t> _lastAlchemySyncTimeMs{ 0 };
};

} // namespace SkyPotion::Game