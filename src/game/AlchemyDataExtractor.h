#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

    class AlchemyDataExtractor {
    public:
        /**
         * Extracts all playable ingredients loaded in the game (INGR),
         * and strictly only the MagicEffects (MGEF / BaseEffect) referenced by those ingredients.
         *
         * Uses an in-memory cache to ensure zero main-thread overhead on repeated queries.
         */
        static nlohmann::json GetAlchemyGameDataJson(bool forceRefresh = false);

        /**
         * Returns a thread-safe snapshot of the cached JSON if available.
         * If no cache exists, returns nullptr (NEVER accesses TESDataHandler on background threads).
         */
        static std::shared_ptr<const nlohmann::json> GetCachedGameData();

        /**
         * Checks if the game data has already been cached in memory.
         */
        static bool HasCache();

        /**
         * Clears the in-memory cache (e.g. on game reload).
         */
        static void ClearCache();
    };

}
