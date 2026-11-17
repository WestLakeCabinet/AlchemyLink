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
        nlohmann::json itemsArray = nlohmann::json::array();

        if (!Util::IsGameSessionReady()) {
            return itemsArray;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
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

            // Only inspect armors that have enchantments
            if (!enchantment) continue;

            double alchemyBonus = 0.0;
            for (auto* effect : enchantment->effects) {
                if (!effect || !effect->baseEffect) continue;

                auto* mgef = effect->baseEffect;
                // Fortify Alchemy check: archetype kPeakValueModifier with ActorValue kAlchemy
                if (mgef->GetArchetype() == RE::EffectSetting::Archetype::kPeakValueModifier &&
                    mgef->GetMagickSkill() == RE::ActorValue::kAlchemy) {
                    alchemyBonus += effect->GetMagnitude();
                }
                // Fallback name check for modded Fortify Alchemy keywords
                else if (mgef->GetName()) {
                    std::string effectName = mgef->GetName();
                    if (effectName.find("Alchemy") != std::string::npos ||
                        effectName.find("alchemy") != std::string::npos) {
                        alchemyBonus += effect->GetMagnitude();
                    }
                }
            }

            if (alchemyBonus > 0.0) {
                itemsArray.push_back({
                    { "categoryType", "Apparel" },
                    { "formId", std::format("0x{:08X}", armor->GetFormID()) },
                    { "name", armor->GetName() ? armor->GetName() : "" },
                    { "count", count },
                    { "weight", Util::Round3(armor->GetWeight()) },
                    { "value", armor->GetGoldValue() },
                    { "isEquipped", isEquipped },
                    { "alchemyBonus", Util::Round3(alchemyBonus) }
                });
            }
        }

        return itemsArray;
    }

    std::pair<nlohmann::json, nlohmann::json> InventoryExtractor::GetInventorySnapshotJson() {
        nlohmann::json ingredientsArray = nlohmann::json::array();
        nlohmann::json apparelArray = nlohmann::json::array();

        if (!Util::IsGameSessionReady()) {
            return { ingredientsArray, apparelArray };
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return { ingredientsArray, apparelArray };
        }

        // Single pass over player's inventory
        const auto inventory = player->GetInventory();

        for (const auto& [boundObj, data] : inventory) {
            const auto count = data.first;
            if (count <= 0 || !boundObj) continue;

            // 1. Check if item is Ingredient
            if (auto* ingredient = boundObj->As<RE::IngredientItem>()) {
                nlohmann::json effectsJson = nlohmann::json::array();
                const auto& effects = ingredient->effects;

                for (uint32_t i = 0; i < effects.size(); ++i) {
                    auto* effectItem = effects[i];
                    if (!effectItem || !effectItem->baseEffect) continue;

                    const char* effectName = effectItem->baseEffect->GetName();
                    bool isKnown = (ingredient->gamedata.knownEffectFlags & (1 << i)) != 0;

                    effectsJson.push_back({
                        { "name", effectName ? effectName : "" },
                        { "known", isKnown }
                    });
                }

                ingredientsArray.push_back({
                    { "categoryType", "Ingredient" },
                    { "formId", std::format("0x{:08X}", ingredient->GetFormID()) },
                    { "name", ingredient->GetName() ? ingredient->GetName() : "" },
                    { "count", count },
                    { "weight", Util::Round3(ingredient->GetWeight()) },
                    { "value", ingredient->GetGoldValue() },
                    { "effects", effectsJson }
                });
                continue;
            }

            // 2. Check if item is Armor with Fortify Alchemy
            if (auto* armor = boundObj->As<RE::TESObjectARMO>()) {
                auto* entryData = data.second.get();
                bool isEquipped = entryData && entryData->IsWorn();

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

                double alchemyBonus = 0.0;
                for (auto* effect : enchantment->effects) {
                    if (!effect || !effect->baseEffect) continue;

                    auto* mgef = effect->baseEffect;
                    if (mgef->GetArchetype() == RE::EffectSetting::Archetype::kPeakValueModifier &&
                        mgef->GetMagickSkill() == RE::ActorValue::kAlchemy) {
                        alchemyBonus += effect->GetMagnitude();
                    } else if (mgef->GetName()) {
                        std::string effectName = mgef->GetName();
                        if (effectName.find("Alchemy") != std::string::npos ||
                            effectName.find("alchemy") != std::string::npos) {
                            alchemyBonus += effect->GetMagnitude();
                        }
                    }
                }

                if (alchemyBonus > 0.0) {
                    apparelArray.push_back({
                        { "categoryType", "Apparel" },
                        { "formId", std::format("0x{:08X}", armor->GetFormID()) },
                        { "name", armor->GetName() ? armor->GetName() : "" },
                        { "count", count },
                        { "weight", Util::Round3(armor->GetWeight()) },
                        { "value", armor->GetGoldValue() },
                        { "isEquipped", isEquipped },
                        { "alchemyBonus", Util::Round3(alchemyBonus) }
                    });
                }
            }
        }

        return { ingredientsArray, apparelArray };
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

        // If no clients are connected to WebSocket, do absolutely nothing!
        if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
            return RE::BSEventNotifyControl::kContinue;
        }

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
        if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
            return;
        }

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
        if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
            _syncPending.store(false);
            _syncApparel.store(false);
            return;
        }

        if (!Util::IsGameSessionReady()) {
            _syncPending.store(false);
            _syncApparel.store(false);
            return;
        }

        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // 150ms debounce: If invoked too rapidly, simply do nothing (subsequent events or polling will sync)
        int64_t elapsed = nowMs - _lastSyncTimeMs.load();
        if (elapsed < 150) {
            _syncPending.store(false);
            return;
        }

        _syncPending.store(false);
        bool needApparel = _syncApparel.exchange(false);
        _lastSyncTimeMs.store(nowMs);

        // 1. Extract dynamic game data on main thread
        auto ingredients = InventoryExtractor::GetIngredientsJson();
        auto apparel = needApparel ? InventoryExtractor::GetApparelJson() : nlohmann::json::array();

        // 2. Broadcast JSON packet on main thread
        nlohmann::json ingPacket = {
            { "type", "data" },
            { "id", "inventory.ingredients" },
            { "fields", { { "items", std::move(ingredients) } } },
            { "ts", nowMs }
        };
        Server::WebSocketServer::GetSingleton().Broadcast(ingPacket);

        if (needApparel) {
            nlohmann::json appPacket = {
                { "type", "data" },
                { "id", "inventory.apparel" },
                { "fields", { { "items", std::move(apparel) } } },
                { "ts", nowMs }
            };
            Server::WebSocketServer::GetSingleton().Broadcast(appPacket);
        }

        SKSE::log::info("InventoryEventHandler: Broadcasted updated inventory (apparel: {})", needApparel);
    }

}
