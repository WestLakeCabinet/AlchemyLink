#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

    class AlchemyDataExtractor {
    public:
        /**
         * Extracts all playable ingredients loaded in the game (INGR),
         * and strictly only the MagicEffects (MGEF / BaseEffect) referenced by those ingredients.
         *
         * This guarantees compliance with the recipe engine's strict limits
         * (baseEffect <= 127, MagicEffect <= 512).
         */
        static nlohmann::json GetAlchemyGameDataJson();
    };

}
