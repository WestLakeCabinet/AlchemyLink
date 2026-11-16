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
#include <SKSE/SKSE.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>

namespace SkyPotion::Util {
    inline double Round2(double value) {
        return std::round(value * 100.0) / 100.0;
    }
}
