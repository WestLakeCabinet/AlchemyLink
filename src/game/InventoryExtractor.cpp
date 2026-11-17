#include "game/InventoryExtractor.h"
#include "server/WebSocketServer.h"
#include <chrono>

namespace SkyPotion::Game {

nlohmann::json InventoryExtractor::GetIngredientsJson() {
    nlohmann::json itemsArray = nlohmann::json::array();

    if (!Util::IsGameSessionReady()) {
        return itemsArray;
    }

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return itemsArray;

    // 재료(Ingredient)만 필터링하여 인벤토리 맵 복사 비용 최소화
    const auto inventory = player->GetInventory([](RE::TESBoundObject& a_obj) {
        return a_obj.Is(RE::FormType::Ingredient);
    });

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

            // Check if player has discovered this effect (bit flag in gamedata)
            bool isKnown = (ingredient->gamedata.knownEffectFlags & (1 << i)) != 0;

            effectsJson.push_back({
                { "formId", std::format("0x{:08X}", effectItem->baseEffect->GetFormID()) },
                { "known", isKnown }
            });
        }

        itemsArray.push_back({
            { "formId", std::format("0x{:08X}", ingredient->GetFormID()) },
            { "count", count },
            { "effects", std::move(effectsJson) }
        });
    }

    return itemsArray;
}

InventoryEventHandler* InventoryEventHandler::GetSingleton() {
    static InventoryEventHandler instance;
    return &instance;
}

void InventoryEventHandler::Register() {
    auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventHolder) {
        eventHolder->AddEventSink<RE::TESContainerChangedEvent>(this);
        SKSE::log::info("InventoryEventHandler successfully registered with ScriptEventSourceHolder");
    } else {
        SKSE::log::warn("ScriptEventSourceHolder unavailable to register InventoryEventHandler");
    }
}

RE::BSEventNotifyControl InventoryEventHandler::ProcessEvent(
    const RE::TESContainerChangedEvent* a_event,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
    if (!a_event) return RE::BSEventNotifyControl::kContinue;

    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    constexpr RE::FormID PLAYER_FORM_ID = 0x00000014;
    if (a_event->oldContainer != PLAYER_FORM_ID && a_event->newContainer != PLAYER_FORM_ID) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(a_event->baseObj);
    if (!form) return RE::BSEventNotifyControl::kContinue;

    if (form->Is(RE::FormType::Ingredient)) {
        RequestInventorySync();
    }

    return RE::BSEventNotifyControl::kContinue;
}

void InventoryEventHandler::RequestInventorySync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return;
    }

    if (!_syncPending.exchange(true)) {
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
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

    int64_t elapsed = nowMs - _lastSyncTimeMs.load();
    if (elapsed < 150) {
        _syncPending.store(false);
        return;
    }

    _syncPending.store(false);
    _lastSyncTimeMs.store(nowMs);

    auto ingredients = InventoryExtractor::GetIngredientsJson();

    nlohmann::json ingPacket = {
        { "type", "data" },
        { "id", "inventory.ingredients" },
        { "fields", { { "items", std::move(ingredients) } } },
        { "ts", nowMs }
    };
    Server::WebSocketServer::GetSingleton().Broadcast(ingPacket);

    SKSE::log::info("InventoryEventHandler: Broadcasted updated ingredients");
}

} // namespace SkyPotion::Game