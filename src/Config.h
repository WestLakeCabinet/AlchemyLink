#pragma once

#include <string>
#include <cstdint>

namespace SkyPotion {
    class Config {
    public:
        static Config& GetSingleton() {
            static Config instance;
            return instance;
        }

        void Load();

        [[nodiscard]] uint16_t GetPort() const noexcept { return _port; }
        [[nodiscard]] const std::string& GetBindAddress() const noexcept { return _bindAddress; }

    private:
        Config() = default;

        uint16_t _port{ 47778 };
        std::string _bindAddress{ "0.0.0.0" };
    };
}
