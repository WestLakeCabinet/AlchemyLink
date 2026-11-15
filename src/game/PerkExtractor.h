#pragma once

#include "Pch.h"

namespace SkyPotion::Game {
    class PerkExtractor {
    public:
        // Returns complete player perks list
        static nlohmann::json GetAllPerksJson();

        // Returns dedicated alchemy perks summary (rank and boolean flags)
        static nlohmann::json GetAlchemyPerksSummary();
    };
}
