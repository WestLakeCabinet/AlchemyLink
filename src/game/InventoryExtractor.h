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
}
