#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <cstddef>

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <format>
#include <algorithm>
#include <cmath>

#include <RE/Skyrim.h>
#include <RE/A/AlchemyMenu.h>
#include <RE/C/CraftingMenu.h>
#include <RE/U/UserEvents.h>
#include <RE/U/UI.h>
#include <RE/RTTI.h>
#include <SKSE/SKSE.h>

namespace RE::CraftingSubMenus {
    using AlchemyMenu = CraftingSubMenus::AlchemyMenu;
}

#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>

namespace SkyPotion::Util {
    inline double Round3(double value) {
        return std::round(value * 1000.0) / 1000.0;
    }

        inline double Round4(double value) {
        return std::round(value * 10000.0) / 10000.0;
    }
}
