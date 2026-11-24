#include "Pch.h"
#include "Config.h"

#include <filesystem>
#include <fstream>
#include <Windows.h>

namespace SkyPotion {
    void Config::Load() {
        const std::filesystem::path iniDir = "Data/SKSE/Plugins";
        const std::filesystem::path iniPath = iniDir / "SkyPotionSKSE.ini";

        std::error_code ec;
        if (!std::filesystem::exists(iniDir, ec)) {
            std::filesystem::create_directories(iniDir, ec);
        }

        // Create default INI file if not exists
        if (!std::filesystem::exists(iniPath, ec)) {
            std::ofstream iniFile(iniPath);
            if (iniFile.is_open()) {
                iniFile << "; SkyPotion SKSE Configuration\n";
                iniFile << "[Server]\n";
                iniFile << "; WebSocket port (default: 47778)\n";
                iniFile << "Port = 47778\n\n";
                iniFile << "; Binding IP address (default: 0.0.0.0 for all interfaces, 127.0.0.1 for localhost only)\n";
                iniFile << "BindAddress = 0.0.0.0\n";
                iniFile.close();
                SKSE::log::info("Created default configuration file: {}", iniPath.string());
            }
        }

        const std::string fullPath = std::filesystem::absolute(iniPath).string();

        // Read Port
        const UINT loadedPort = GetPrivateProfileIntA("Server", "Port", 47778, fullPath.c_str());
        if (loadedPort > 0 && loadedPort <= 65535) {
            _port = static_cast<uint16_t>(loadedPort);
        } else {
            SKSE::log::warn("Invalid port {} in INI, using default 47778", loadedPort);
            _port = 47778;
        }

        // Read BindAddress
        char buffer[256] = { 0 };
        const DWORD len = GetPrivateProfileStringA("Server", "BindAddress", "0.0.0.0", buffer, sizeof(buffer), fullPath.c_str());
        if (len > 0) {
            std::string raw(buffer, len);
            const size_t start = raw.find_first_not_of(" \t\r\n\"");
            const size_t end = raw.find_last_not_of(" \t\r\n\"");
            if (start != std::string::npos && end != std::string::npos) {
                _bindAddress = raw.substr(start, end - start + 1);
            } else {
                _bindAddress = "0.0.0.0";
            }
        } else {
            _bindAddress = "0.0.0.0";
        }

        SKSE::log::info("Loaded configuration from {}: Port={}, BindAddress={}", fullPath, _port, _bindAddress);
    }
}
