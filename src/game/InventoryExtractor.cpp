#include "game/InventoryExtractor.h"
#include "server/WebSocketServer.h"
#include <chrono>

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
                { "weight", Util::Round3(ingredient->GetWeight()) },
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
                    { "magnitude", Util::Round3(magnitude) }
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

        constexpr RE::FormID PLAYER_FORM_ID = 0x00000014;
        if (a_event->oldContainer != PLAYER_FORM_ID && a_event->newContainer != PLAYER_FORM_ID) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* form = RE::TESForm::LookupByID(a_event->baseObj);
        if (!form) return RE::BSEventNotifyControl::kContinue;

        bool isIngredient = form->Is(RE::FormType::Ingredient);
        bool isApparel = form->Is(RE::FormType::Armor);

        if (isIngredient || isApparel) {
            RequestInventorySync(isIngredient, isApparel);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    void InventoryEventHandler::RequestInventorySync(bool /*isIngredient*/, bool isApparel) {
        if (isApparel) {
            _syncApparel.store(true);
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
        _syncPending.store(false);
        bool needApparel = _syncApparel.exchange(false);

        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // 1. Broadcast updated ingredients
        nlohmann::json ingPacket = {
            { "type", "data" },
            { "id", "inventory.ingredients" },
            { "fields", { { "items", InventoryExtractor::GetIngredientsJson() } } },
            { "ts", nowMs }
        };
        Server::WebSocketServer::GetSingleton().Broadcast(ingPacket);

        // 2. Broadcast updated apparel if apparel changed
        if (needApparel) {
            nlohmann::json appPacket = {
                { "type", "data" },
                { "id", "inventory.apparel" },
                { "fields", { { "items", InventoryExtractor::GetApparelJson() } } },
                { "ts", nowMs }
            };
            Server::WebSocketServer::GetSingleton().Broadcast(appPacket);
        }

        SKSE::log::info("InventoryEventHandler: Broadcasted updated inventory (apparel: {})", needApparel);
    }
}
