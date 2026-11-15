#include "game/InventoryExtractor.h"

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
                { "weight", ingredient->GetWeight() },
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
                    { "magnitude", magnitude }
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
}
