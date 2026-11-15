#include "game/PerkExtractor.h"

namespace SkyPotion::Game {

    nlohmann::json PerkExtractor::GetAllPerksJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        nlohmann::json perksArray = nlohmann::json::array();

        if (!player) return perksArray;

        const std::vector<std::pair<RE::FormID, const char*>> trackedPerks = {
            { 0x000BE956, "Alchemist 1" },
            { 0x000C07CA, "Alchemist 2" },
            { 0x000C07CB, "Alchemist 3" },
            { 0x000C07CC, "Alchemist 4" },
            { 0x000C07CD, "Alchemist 5" },
            { 0x00058215, "Physician" },
            { 0x00058216, "Benefactor" },
            { 0x00058217, "Poisoner" },
            { 0x0005821D, "Purity" },
            { 0x00058218, "Experimenter 1" },
            { 0x00105F2A, "Experimenter 2" },
            { 0x00105F2B, "Experimenter 3" },
            { 0x00105F2C, "Green Thumb" },
            { 0x00105F2E, "Snakeblood" }
        };

        for (const auto& [formId, fallbackName] : trackedPerks) {
            auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(formId);
            if (perk && player->HasPerk(perk)) {
                const char* realName = perk->GetName();
                perksArray.push_back({
                    { "formId", std::format("0x{:08X}", formId) },
                    { "name", (realName && realName[0] != '\0') ? realName : fallbackName },
                    { "rank", 1 }
                });
            }
        }

        return perksArray;
    }

    nlohmann::json PerkExtractor::GetAlchemyPerksSummary() {
        auto player = RE::PlayerCharacter::GetSingleton();
        nlohmann::json summary = {
            { "alchemist", 0 },
            { "physician", false },
            { "benefactor", false },
            { "poisoner", false },
            { "purity", false },
            { "experimenter", 0 },
            { "greenThumb", false },
            { "snakeblood", false }
        };

        if (!player) return summary;

        auto hasPerk = [player](RE::FormID formId) -> bool {
            auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(formId);
            return perk && player->HasPerk(perk);
        };

        // Alchemist 1 ~ 5
        if (hasPerk(0x000C07CD)) summary["alchemist"] = 5;
        else if (hasPerk(0x000C07CC)) summary["alchemist"] = 4;
        else if (hasPerk(0x000C07CB)) summary["alchemist"] = 3;
        else if (hasPerk(0x000C07CA)) summary["alchemist"] = 2;
        else if (hasPerk(0x000BE956)) summary["alchemist"] = 1;

        // Core alchemy perks
        summary["physician"] = hasPerk(0x00058215);
        summary["benefactor"] = hasPerk(0x00058216);
        summary["poisoner"] = hasPerk(0x00058217);
        summary["purity"] = hasPerk(0x0005821D);

        // Experimenter 1 ~ 3
        if (hasPerk(0x00105F2B)) summary["experimenter"] = 3;
        else if (hasPerk(0x00105F2A)) summary["experimenter"] = 2;
        else if (hasPerk(0x00058218)) summary["experimenter"] = 1;

        // Green Thumb & Snakeblood
        summary["greenThumb"] = hasPerk(0x00105F2C);
        summary["snakeblood"] = hasPerk(0x00105F2E);

        return summary;
    }
}
