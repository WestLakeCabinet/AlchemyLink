#pragma once

#include "Pch.h"

namespace SkyPotion::Game {
    class StatExtractor {
    public:
        static nlohmann::json GetAlchemyStatsJson();
        static nlohmann::json GetPlayerStatsJson();
    };
}
