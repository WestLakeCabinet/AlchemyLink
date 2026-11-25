This file is a merged representation of a subset of the codebase, containing specifically included files, combined into a single document by Repomix.

<file_summary>
This section contains a summary of this file.

<purpose>
This file contains a packed representation of a subset of the repository's contents that is considered the most important context.
It is designed to be easily consumable by AI systems for analysis, code review,
or other automated processes.
</purpose>

<file_format>
The content is organized as follows:
1. This summary section
2. Repository information
3. Directory structure
4. Repository files (if enabled)
5. Multiple file entries, each consisting of:
  - File path as an attribute
  - Full contents of the file
</file_format>

<usage_guidelines>
- This file should be treated as read-only. Any changes should be made to the
  original repository files, not this packed version.
- When processing this file, use the file path to distinguish
  between different files in the repository.
- Be aware that this file may contain sensitive information. Handle it with
  the same level of security as you would the original repository.
</usage_guidelines>

<notes>
- Some files may have been excluded based on .gitignore rules and Repomix's configuration
- Binary files are not included in this packed representation. Please refer to the Repository Structure section for a complete list of file paths, including binary files
- Only files matching these patterns are included: **/*.cpp, **/*.h, CMakeLists.*, vcpkg.json, build
- Files matching patterns in .gitignore are excluded
- Files matching default ignore patterns are excluded
- Files are sorted by Git change count (files with more changes are at the bottom)
</notes>

</file_summary>

<directory_structure>
src/
  game/
    AlchemyController.cpp
    AlchemyController.h
    AlchemyDataExtractor.cpp
    AlchemyDataExtractor.h
    InventoryExtractor.cpp
    InventoryExtractor.h
    PerkExtractor.cpp
    PerkExtractor.h
    StatExtractor.cpp
    StatExtractor.h
  server/
    WebSocketServer.cpp
    WebSocketServer.h
  sync/
    ProtoPacketBuilder.cpp
    ProtoPacketBuilder.h
  Config.cpp
  Config.h
  main.cpp
  Pch.h
CMakeLists.txt
vcpkg.json
</directory_structure>

<files>
This section contains the contents of the repository's files.

<file path="src/game/PerkExtractor.h">
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
</file>

<file path="src/game/StatExtractor.h">
#pragma once

#include "Pch.h"

namespace SkyPotion::Game {
    class StatExtractor {
    public:
        static nlohmann::json GetAlchemyStatsJson();
        static nlohmann::json GetPlayerStatsJson();
    };
}
</file>

<file path="src/Config.cpp">
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
</file>

<file path="src/Config.h">
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
</file>

<file path="src/game/AlchemyDataExtractor.h">
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
</file>

<file path="src/sync/ProtoPacketBuilder.h">
#pragma once

#include "Pch.h"
#include "game_sync.pb.h"

namespace SkyPotion::Sync {

namespace pb = com::dolzzo::skypotion::proto::sync;

class ProtoPacketBuilder {
public:
    static constexpr uint32_t kProtocolVersion = 1;

    // 1. 초기 접속 / 세이브 로드 시 전체 마스터 데이터 + 초기 재고 + 스탯 스냅샷
    static std::string BuildCatalogSnapshot();

    // 2. 실시간 인벤토리 전체 스냅샷 (is_full_snapshot = true)
    static std::string BuildInventoryFullSnapshot();

    // 3. 실시간 플레이어 연금술 스탯 & 퍽
    static std::string BuildPlayerAlchemyStats();

    // 4. 발견된 효과 마스크 업데이트
    static std::string BuildDiscoveredEffects(const std::vector<RE::FormID>& formIds);

    // 5. 연금술대 열림/닫힘 및 선택 재료 상태
    static std::string BuildAlchemyBenchState(bool isOpen, const std::vector<RE::FormID>& selectedFormIds);

    // 6. 앱 명령 실행 결과 응답
    static std::string BuildCommandResult(
        uint64_t requestId,
        bool success,
        int32_t craftedCount = 0,
        const std::string& errorCode = ""
    );

private:
    static int64_t NowMs();
    static void FillPlayerAlchemyStats(pb::PlayerCraftingStats* out);
    static double GetTotalFortifyAlchemy(RE::PlayerCharacter* player);
    static std::string ResolvePlayerName(RE::PlayerCharacter* player);
    static std::string ResolveGameLanguage();
};

} // namespace SkyPotion::Sync
</file>

<file path="src/game/AlchemyController.h">
#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

    struct AlchemySelectResult {
        bool success{ false };
        std::string message;
        std::vector<std::string> selectedIngredients;
        std::vector<std::string> missingIngredients;
        bool crafted{ false };
        int craftedCount{ 0 };

        nlohmann::json ToJson() const {
            return {
                { "success", success },
                { "message", message },
                { "selectedIngredients", selectedIngredients },
                { "missingIngredients", missingIngredients },
                { "crafted", crafted },
                { "craftedCount", craftedCount }
            };
        }
    };

    class AlchemyController {
    public:
        // Checks if the Alchemy workbench menu is currently open
        static bool IsAlchemyMenuOpen();

        // Checks if the Alchemy menu AND its Flash item list are fully populated and visible
        static bool IsAlchemyItemListReady();

        // Retrieves the active AlchemyMenu pointer, or nullptr if not open
        static RE::CraftingSubMenus::AlchemyMenu* GetAlchemyMenu();

        // Clears any currently selected ingredients from the alchemy slots
        static bool ClearSelectedIngredients();

        // Gets the FormIDs of all currently selected ingredients in the alchemy slots
        static std::vector<RE::FormID> GetSelectedIngredientFormIDs();

        // Crafts potions with the currently selected ingredients
        static AlchemySelectResult CraftPotions(int count = 1);

        // Stops any currently running bulk crafting session
        static bool StopCrafting();

        // Checks if bulk crafting is currently running
        static bool IsCrafting();

        // Dismisses any open or queued message box dialogs (e.g. effect discovery prompts)
        static void DismissOpenMessageBoxes();

        // Selects ingredients by string FormIDs (hex "0x..." or decimal)
        static AlchemySelectResult SelectIngredients(
            const std::vector<std::string>& formIdStrings,
            bool autoCraft = false,
            int craftCount = 1
        );

        // Selects ingredients by numeric FormIDs
        static AlchemySelectResult SelectIngredients(
            const std::vector<RE::FormID>& formIds,
            bool autoCraft = false,
            int craftCount = 1
        );
    };

    class MenuOpenCloseHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuOpenCloseHandler* GetSingleton();
        void Register();

        static void StartMonitoring();
        static void StopMonitoring();
        static bool IsMonitoring();

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* a_event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;
    };

}
</file>

<file path="src/game/PerkExtractor.cpp">
#include "game/PerkExtractor.h"

namespace SkyPotion::Game {

    nlohmann::json PerkExtractor::GetAllPerksJson() {
        nlohmann::json perksArray = nlohmann::json::array();

        if (!Util::IsGameSessionReady()) {
            return perksArray;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return perksArray;

        const std::vector<std::pair<RE::FormID, const char*>> trackedPerks = {
            { 0x000BE127, "Alchemist 1" },
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
            { 0x00105F2E, "Green Thumb" },
            { 0x00105F2C, "Snakeblood" }
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

        if (!Util::IsGameSessionReady()) {
            return summary;
        }

        auto player = RE::PlayerCharacter::GetSingleton();
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
        else if (hasPerk(0x000BE127)) summary["alchemist"] = 1;

        // Core alchemy perks
        summary["physician"] = hasPerk(0x00058215);
        summary["benefactor"] = hasPerk(0x00058216);
        summary["poisoner"] = hasPerk(0x00058217);
        summary["purity"] = hasPerk(0x0005821D);

        // Experimenter 1 ~ 3
        if (hasPerk(0x00105F2B)) summary["experimenter"] = 3;
        else if (hasPerk(0x00105F2A)) summary["experimenter"] = 2;
        else if (hasPerk(0x00058218)) summary["experimenter"] = 1;

        // Utility perks
        summary["greenThumb"] = hasPerk(0x00105F2E);
        summary["snakeblood"] = hasPerk(0x00105F2C);

        return summary;
    }

}
</file>

<file path="vcpkg.json">
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "skypotion-skse",
  "version-string": "1.0.0",
  "description": "Skyrim SE/AE WebSocket Plugin for SkyPotion Android App",
  "builtin-baseline": "1de2026f28ead93ff1773e6e680387643e914ea1",
  "dependencies": [
    "nlohmann-json",
    "ixwebsocket",
    "spdlog",
    "rapidcsv",
    "directxtk",
    "protobuf"
  ]
}
</file>

<file path="src/game/AlchemyDataExtractor.cpp">
#include "game/AlchemyDataExtractor.h"
#include <map>
#include <mutex>

namespace SkyPotion::Game {

    static std::shared_ptr<const nlohmann::json> s_cachedGameDataPtr;
    static std::mutex s_cacheMutex;

    bool AlchemyDataExtractor::HasCache() {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        return s_cachedGameDataPtr != nullptr;
    }

    std::shared_ptr<const nlohmann::json> AlchemyDataExtractor::GetCachedGameData() {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        return s_cachedGameDataPtr;
    }

    void AlchemyDataExtractor::ClearCache() {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        s_cachedGameDataPtr.reset();
        SKSE::log::info("AlchemyDataExtractor: cache cleared");
    }

    nlohmann::json AlchemyDataExtractor::GetAlchemyGameDataJson(bool forceRefresh) {
        {
            std::lock_guard<std::mutex> lock(s_cacheMutex);
            if (!forceRefresh && s_cachedGameDataPtr) {
                return *s_cachedGameDataPtr;
            }
        }

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::warn("TESDataHandler not available when querying alchemy gamedata");
            return {
                { "baseEffects", nlohmann::json::array() },
                { "ingredients", nlohmann::json::array() },
                { "baseEffectCount", 0 },
                { "ingredientCount", 0 }
            };
        }

        const auto& allIngredients = dataHandler->GetFormArray<RE::IngredientItem>();

        // Whitelist container for BaseEffects referenced by valid ingredients (ordered by FormID)
        std::map<RE::FormID, RE::EffectSetting*> usedEffects;
        nlohmann::json ingredientsJson = nlohmann::json::array();

        // 1. Traverse all INGR records and collect valid ingredients & used MGEFs
        for (auto* ing : allIngredients) {
            if (!ing) continue;
            if (ing->IsDeleted()) continue;

            const char* name = ing->GetName();
            if (!name || name[0] == '\0') continue;

            // Check if ingredient has at least one valid effect
            bool hasValidEffect = false;
            for (auto* eff : ing->effects) {
                if (eff && eff->baseEffect) {
                    hasValidEffect = true;
                    break;
                }
            }
            if (!hasValidEffect) continue;

            // Extract 4 effect slots
            nlohmann::json slotsArray = nlohmann::json::array();
            for (uint32_t i = 0; i < 4; ++i) {
                if (i < ing->effects.size() && ing->effects[i] && ing->effects[i]->baseEffect) {
                    auto* eff = ing->effects[i];
                    auto* mgef = eff->baseEffect;

                    // Add to whitelist of used BaseEffects
                    usedEffects[mgef->GetFormID()] = mgef;

                    slotsArray.push_back({
                        { "formId", std::format("0x{:08X}", mgef->GetFormID()) },
                        { "magnitude", Util::Round3(eff->GetMagnitude()) },
                        { "duration", static_cast<int32_t>(eff->GetDuration()) }
                    });
                } else {
                    slotsArray.push_back({
                        { "formId", "0x00000000" },
                        { "magnitude", 0.0 },
                        { "duration", 0 }
                    });
                }
            }

            // Determine source plugin filename
            auto* file = ing->GetFile(0);
            std::string fileName = file ? std::string(file->GetFilename()) : "Unknown";

            ingredientsJson.push_back({
                { "formId", std::format("0x{:08X}", ing->GetFormID()) },
                { "name", name },
                { "value", ing->GetGoldValue() },
                { "weight", Util::Round3(ing->GetWeight()) },
                { "effectValue", ing->data.costOverride },
                { "fileName", fileName },
                { "slots", slotsArray }
            });
        }

        // 2. Serialize strictly only the BaseEffects referenced by ingredients
        nlohmann::json baseEffectsJson = nlohmann::json::array();
        for (const auto& [formId, mgef] : usedEffects) {
            if (!mgef) continue;

            const char* mgefName = mgef->GetName();
            std::string nameStr = mgefName ? mgefName : "";

            const char* edid = mgef->GetFormEditorID();
            std::string editorId = edid ? edid : "";

            bool noMag = mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoMagnitude);
            bool noDur = mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoDuration);
            bool affectMag = mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude);
            bool affectDur = mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration);
            bool isHostile = mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kHostile);

            // Extract Keywords (KWDA)
            std::string keywordsStr;
            nlohmann::json keywordsArr = nlohmann::json::array();
            for (auto* kw : mgef->GetKeywords()) {
                if (!kw) continue;
                const char* kwEdid = kw->GetFormEditorID();
                std::string kwName;
                if (kwEdid && kwEdid[0] != '\0') {
                    kwName = kwEdid;
                } else {
                    const char* kwFullName = kw->GetName();
                    if (kwFullName && kwFullName[0] != '\0') {
                        kwName = kwFullName;
                    }
                }

                if (!kwName.empty()) {
                    keywordsArr.push_back(kwName);
                    if (!keywordsStr.empty()) keywordsStr += ",";
                    keywordsStr += kwName;
                }
            }

            bool isBeneficial = false;
            bool isHarmful = false;
            if (keywordsStr.find("Beneficial") != std::string::npos || keywordsStr.find("beneficial") != std::string::npos) {
                isBeneficial = true;
            }
            if (keywordsStr.find("Harmful") != std::string::npos || keywordsStr.find("harmful") != std::string::npos) {
                isHarmful = true;
            }

            bool isPhysician = keywordsStr.find("Restore") != std::string::npos || keywordsStr.find("restore") != std::string::npos;
            const char* desc = mgef->magicItemDescription.c_str();

            baseEffectsJson.push_back({
                { "formId", std::format("0x{:08X}", formId) },
                { "editorId", editorId },
                { "name", nameStr },
                { "baseCost", Util::Round4(mgef->data.baseCost) },
                { "noMag", noMag },
                { "noDur", noDur },
                { "affectMag", affectMag },
                { "affectDur", affectDur },
                { "isHostile", isHostile },
                { "isBeneficial", isBeneficial },
                { "isHarmful", isHarmful },
                { "isPhysician", isPhysician },
                { "keywords", keywordsStr },
                { "description", desc ? desc : "" }
            });
        }

        SKSE::log::info("AlchemyDataExtractor: extracted {} ingredients and {} unique alchemy baseEffects",
            ingredientsJson.size(), baseEffectsJson.size());

        nlohmann::json result = {
            { "baseEffects", baseEffectsJson },
            { "ingredients", ingredientsJson },
            { "baseEffectCount", baseEffectsJson.size() },
            { "ingredientCount", ingredientsJson.size() }
        };

        {
            std::lock_guard<std::mutex> lock(s_cacheMutex);
            s_cachedGameDataPtr = std::make_shared<const nlohmann::json>(result);
        }

        return result;
    }

}
</file>

<file path="src/server/WebSocketServer.h">
#pragma once

#include "Pch.h"
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXWebSocket.h>

namespace SkyPotion::Server {

    class WebSocketServer {
    public:
        static WebSocketServer& GetSingleton() {
            static WebSocketServer instance;
            return instance;
        }

        bool Start(uint16_t port = 47778, const std::string& host = "0.0.0.0");
        void Stop();
        bool IsRunning() const;
        bool HasConnectedClients() const;

        void Broadcast(const nlohmann::json& message);
        void BroadcastFullSnapshot();
        void BroadcastGameData();
        void BroadcastGameSessionLoaded();

        void BroadcastBinary(const std::string& binaryPayload);
        void BroadcastProtoSnapshot(); // 초기 접속 / 세이브 로드 시 CatalogSnapshot + BenchState 전송
        void ScheduleSessionSnapshot(int attempt = 0); // 세이브 로드 / 접속 시 준비 대기 및 자동 재시도 스냅샷

    private:
        WebSocketServer();
        ~WebSocketServer();

        WebSocketServer(const WebSocketServer&) = delete;
        WebSocketServer& operator=(const WebSocketServer&) = delete;

        void OnMessage(std::shared_ptr<ix::ConnectionState> connectionState,
                       ix::WebSocket& webSocket,
                       const ix::WebSocketMessagePtr& msg);

        static nlohmann::json ProcessQuery(const std::string& id, const nlohmann::json& fields);
        static nlohmann::json ProcessCommand(const std::string& action, const nlohmann::json& payload);

        std::unique_ptr<ix::WebSocketServer> _server;
        std::atomic<bool> _running{false};
        uint16_t _port{47778};
        std::string _host{"0.0.0.0"};
        std::atomic<int> _clientCount{ 0 };

        void HandleProtoCommand(ix::WebSocket& webSocket, const std::string& rawBytes);
    };

}
</file>

<file path="src/sync/ProtoPacketBuilder.cpp">
#include "sync/ProtoPacketBuilder.h"
#include <RE/G/GameSettingCollection.h>
#include <chrono>
#include <map>

namespace SkyPotion::Sync {

namespace {
    std::string ResolveGameSettingString(const char* a_name, const std::string& a_fallback) {
        auto* gmst = RE::GameSettingCollection::GetSingleton();
        if (gmst) {
            if (auto* setting = gmst->GetSetting(a_name)) {
                if (const char* str = setting->GetString()) {
                    return str;
                }
            }
        }
        return a_fallback;
    }
}

int64_t ProtoPacketBuilder::NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string ProtoPacketBuilder::ResolvePlayerName(RE::PlayerCharacter* player) {
    if (!player) return "Player";
    if (player->GetActorBase() && player->GetActorBase()->GetName() && player->GetActorBase()->GetName()[0] != '\0') {
        return player->GetActorBase()->GetName();
    }
    if (player->GetName() && player->GetName()[0] != '\0') {
        return player->GetName();
    }
    return "Player";
}

std::string ProtoPacketBuilder::ResolveGameLanguage() {
    auto* ini = RE::INISettingCollection::GetSingleton();
    auto* setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
    if (setting && setting->GetType() == RE::Setting::Type::kString && setting->data.s) {
        return setting->data.s;
    }
    return "ENGLISH";
}

double ProtoPacketBuilder::GetTotalFortifyAlchemy(RE::PlayerCharacter* player) {
    if (!player || !Util::IsGameSessionReady()) return 0.0;

    double totalBonus = 0.0;
    if (auto* magicTarget = player->AsMagicTarget()) {
        if (auto* activeEffects = magicTarget->GetActiveEffectList()) {
            for (auto* ae : *activeEffects) {
                if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kInactive) || !ae->effect) {
                    continue;
                }
                auto* mgef = ae->GetBaseObject();
                if (!mgef) continue;

                const auto arch = mgef->GetArchetype();
                if ((arch == RE::EffectSetting::Archetype::kPeakValueModifier ||
                     arch == RE::EffectSetting::Archetype::kValueModifier) &&
                    mgef->GetMagickSkill() == RE::ActorValue::kAlchemy) {
                    totalBonus += ae->magnitude;
                }
            }
        }
    }

    if (totalBonus <= 0.0) {
        if (auto* avOwner = player->AsActorValueOwner()) {
            float mod = avOwner->GetActorValue(RE::ActorValue::kAlchemyModifier);
            float pwr = avOwner->GetActorValue(RE::ActorValue::kAlchemyPowerModifier);
            totalBonus = std::max(mod, pwr);
        }
    }

    return totalBonus;
}

void ProtoPacketBuilder::FillPlayerAlchemyStats(pb::PlayerCraftingStats* out) {
    if (!out || !Util::IsGameSessionReady()) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    out->set_character_name(ResolvePlayerName(player));

    if (auto* avOwner = player->AsActorValueOwner()) {
        float currentSkill = avOwner->GetActorValue(RE::ActorValue::kAlchemy);
        float baseSkill = avOwner->GetBaseActorValue(RE::ActorValue::kAlchemy);
        float effectiveSkill = (currentSkill > 0.0f) ? currentSkill : baseSkill;
        out->set_skill_level(static_cast<int32_t>(std::round(effectiveSkill)));
        out->set_fortify_alchemy_total(static_cast<int32_t>(std::round(GetTotalFortifyAlchemy(player))));
    }

    auto hasPerk = [player](RE::FormID id) -> bool {
        auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(id);
        return perk && player->HasPerk(perk);
    };

    int32_t alchemistRank = 0;
    if (hasPerk(0x000C07CD)) alchemistRank = 5;
    else if (hasPerk(0x000C07CC)) alchemistRank = 4;
    else if (hasPerk(0x000C07CB)) alchemistRank = 3;
    else if (hasPerk(0x000C07CA)) alchemistRank = 2;
    else if (hasPerk(0x000BE127)) alchemistRank = 1;

    out->set_alchemist_rank(alchemistRank);
    out->set_physician(hasPerk(0x00058215));
    out->set_benefactor(hasPerk(0x00058216));
    out->set_poisoner(hasPerk(0x00058217));
    out->set_purity(hasPerk(0x0005821D));
}

std::string ProtoPacketBuilder::BuildCatalogSnapshot() {
    if (!Util::IsGameSessionReady()) return {};

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!player || !dataHandler) return {};

    pb::GameSyncPacket packet;
    packet.set_protocol_version(kProtocolVersion);
    packet.set_timestamp_ms(NowMs());

    auto* snapshot = packet.mutable_catalog_snapshot();
    snapshot->set_character_name(ResolvePlayerName(player));
    snapshot->set_game_language(ResolveGameLanguage());
    snapshot->set_potion_prefix(ResolveGameSettingString("sCreatedPotionNamePrefix", "Potion of "));
    snapshot->set_poison_prefix(ResolveGameSettingString("sCreatedPoisonNamePrefix", "Poison of "));
    FillPlayerAlchemyStats(snapshot->mutable_initial_stats());

    // 현재 인벤토리 재료 수량 맵 (1회 순회)
    std::unordered_map<RE::FormID, int32_t> stockMap;
    const auto inventory = player->GetInventory([](RE::TESBoundObject& a_obj) {
        return a_obj.Is(RE::FormType::Ingredient);
    });
    for (const auto& [boundObj, data] : inventory) {
        if (boundObj && data.first > 0) {
            stockMap[boundObj->GetFormID()] = data.first;
        }
    }

    std::map<RE::FormID, RE::EffectSetting*> usedEffects;
    const auto& allIngredients = dataHandler->GetFormArray<RE::IngredientItem>();

    // 1. 유효한 모든 재료(INGR) 수집
    for (auto* ing : allIngredients) {
        if (!ing || ing->IsDeleted()) continue;
        const char* name = ing->GetName();
        if (!name || name[0] == '\0') continue;

        bool hasValidEffect = false;
        for (auto* eff : ing->effects) {
            if (eff && eff->baseEffect) {
                hasValidEffect = true;
                break;
            }
        }
        if (!hasValidEffect) continue;

        auto* ingDef = snapshot->add_ingredients();
        ingDef->set_form_id(ing->GetFormID());
        ingDef->set_name(name);
        ingDef->set_price(ing->GetGoldValue());
        ingDef->set_weight(Util::Round3(ing->GetWeight()));
        ingDef->set_effect_value(ing->data.costOverride);

        if (auto* file = ing->GetFile(0)) {
            ingDef->set_origin(std::string(file->GetFilename()));
        } else {
            ingDef->set_origin("Unknown");
        }

        if (auto it = stockMap.find(ing->GetFormID()); it != stockMap.end()) {
            ingDef->set_current_stock(it->second);
        } else {
            ingDef->set_current_stock(0);
        }

        ingDef->set_discovered_mask(static_cast<uint32_t>(ing->gamedata.knownEffectFlags & 0x0F));

        for (uint32_t i = 0; i < 4 && i < ing->effects.size(); ++i) {
            auto* eff = ing->effects[i];
            if (!eff || !eff->baseEffect) continue;

            auto* mgef = eff->baseEffect;
            usedEffects[mgef->GetFormID()] = mgef;

            auto* slot = ingDef->add_slots();
            slot->set_form_id(mgef->GetFormID());
            slot->set_magnitude(Util::Round3(eff->GetMagnitude()));
            slot->set_duration(static_cast<int32_t>(eff->GetDuration()));
        }
    }

    // 2. 재료가 참조하는 BaseEffect(MGEF)만 직렬화
    for (const auto& [formId, mgef] : usedEffects) {
        if (!mgef) continue;

        auto* effDef = snapshot->add_base_effects();
        effDef->set_form_id(formId);
        effDef->set_name(mgef->GetName() ? mgef->GetName() : "");

        if (!mgef->magicItemDescription.empty()) {
            effDef->set_description(mgef->magicItemDescription.c_str());
        }

        effDef->set_base_cost(Util::Round4(mgef->data.baseCost));

        const auto flags = mgef->data.flags;
        effDef->set_no_mag(flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoMagnitude));
        effDef->set_no_dur(flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoDuration));
        effDef->set_affect_mag(flags.any(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsMagnitude));
        effDef->set_affect_dur(flags.any(RE::EffectSetting::EffectSettingData::Flag::kPowerAffectsDuration));
        effDef->set_is_poison(flags.any(RE::EffectSetting::EffectSettingData::Flag::kHostile));

        std::string joinedKeywords;
        std::unordered_set<std::string> seenKeywords;

        for (const auto* kw : mgef->GetKeywords()) {
            if (!kw) continue;

            std::string token;
            const char* kwEdid = kw->GetFormEditorID();
            if (kwEdid && kwEdid[0] != '\0') {
                token = kwEdid;
            } else {
                const char* kwName = kw->GetName();
                if (kwName && kwName[0] != '\0') {
                    token = kwName;
                }
            }

            if (token.empty()) continue;
            if (!seenKeywords.insert(token).second) continue;

            if (!joinedKeywords.empty()) {
                joinedKeywords += ",";
            }
            joinedKeywords += token;
        }

        effDef->set_keywords(joinedKeywords);
    }

    return packet.SerializeAsString();
}

std::string ProtoPacketBuilder::BuildInventoryFullSnapshot() {
    if (!Util::IsGameSessionReady()) return {};
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return {};

    pb::GameSyncPacket packet;
    packet.set_protocol_version(kProtocolVersion);
    packet.set_timestamp_ms(NowMs());

    auto* update = packet.mutable_inventory_update();
    update->set_is_full_snapshot(true);

    const auto inventory = player->GetInventory([](RE::TESBoundObject& a_obj) {
        return a_obj.Is(RE::FormType::Ingredient);
    });

    for (const auto& [boundObj, data] : inventory) {
        if (!boundObj || data.first <= 0) continue;
        auto* entry = update->add_items();
        entry->set_form_id(boundObj->GetFormID());
        entry->set_count(data.first);
    }

    return packet.SerializeAsString();
}

std::string ProtoPacketBuilder::BuildPlayerAlchemyStats() {
    if (!Util::IsGameSessionReady()) return {};

    pb::GameSyncPacket packet;
    packet.set_protocol_version(kProtocolVersion);
    packet.set_timestamp_ms(NowMs());

    FillPlayerAlchemyStats(packet.mutable_player_stats());
    return packet.SerializeAsString();
}

std::string ProtoPacketBuilder::BuildDiscoveredEffects(const std::vector<RE::FormID>& formIds) {
    if (!Util::IsGameSessionReady() || formIds.empty()) return {};

    pb::GameSyncPacket packet;
    packet.set_protocol_version(kProtocolVersion);
    packet.set_timestamp_ms(NowMs());

    auto* masks = packet.mutable_discovered()->mutable_discovered_masks();
    for (RE::FormID id : formIds) {
        if (auto* ing = RE::TESForm::LookupByID<RE::IngredientItem>(id)) {
            (*masks)[id] = static_cast<uint32_t>(ing->gamedata.knownEffectFlags & 0x0F);
        }
    }
    return packet.SerializeAsString();
}

std::string ProtoPacketBuilder::BuildAlchemyBenchState(bool isOpen, const std::vector<RE::FormID>& selectedFormIds) {
    pb::GameSyncPacket packet;
    packet.set_protocol_version(kProtocolVersion);
    packet.set_timestamp_ms(NowMs());

    auto* bench = packet.mutable_bench_state();
    bench->set_is_open(isOpen);
    for (RE::FormID id : selectedFormIds) {
        bench->add_selected_form_ids(id);
    }
    return packet.SerializeAsString();
}

std::string ProtoPacketBuilder::BuildCommandResult(
    uint64_t requestId,
    bool success,
    int32_t craftedCount,
    const std::string& errorCode
) {
    pb::GameSyncPacket packet;
    packet.set_protocol_version(kProtocolVersion);
    packet.set_timestamp_ms(NowMs());

    auto* res = packet.mutable_command_result();
    res->set_request_id(requestId);
    res->set_success(success);
    res->set_crafted_count(craftedCount);
    res->set_error_code(errorCode);
    return packet.SerializeAsString();
}

} // namespace SkyPotion::Sync
</file>

<file path="src/Pch.h">
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

namespace SkyPotion {
    inline std::atomic<bool> g_isShuttingDown{ false };
}

namespace SkyPotion::Util {
    inline double Round3(double value) {
        return std::round(value * 1000.0) / 1000.0;
    }

    inline double Round4(double value) {
        return std::round(value * 10000.0) / 10000.0;
    }

    inline bool IsGameSessionReady() {
        if (g_isShuttingDown.load()) {
            return false;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
            ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        if (!player->Is3DLoaded() || !player->GetParentCell()) {
            return false;
        }

        return true;
    }
}
</file>

<file path="src/game/AlchemyController.cpp">
#include "game/AlchemyController.h"
#include "game/InventoryExtractor.h"   // 추가
#include "server/WebSocketServer.h"
#include "sync/ProtoPacketBuilder.h"   // 추가
#include "RE/M/MessageBoxMenu.h"
#include "RE/M/MessageBoxData.h"
#include <chrono>
#include <future>
#include <thread>
#include <atomic>

namespace SkyPotion::Game {

    static std::atomic<bool> s_craftCancel{ false };
    static std::atomic<bool> s_isCrafting{ false };

    RE::CraftingSubMenus::AlchemyMenu* AlchemyController::GetAlchemyMenu() {
        if (!Util::IsGameSessionReady()) {
            return nullptr;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
            return nullptr;
        }

        auto craftingMenu = ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME);
        if (!craftingMenu) {
            return nullptr;
        }

        auto* subMenu = craftingMenu->GetCraftingSubMenu();
        if (!subMenu) {
            return nullptr;
        }

        return skyrim_cast<RE::CraftingSubMenus::AlchemyMenu*>(subMenu);
    }

    bool AlchemyController::IsAlchemyMenuOpen() {
        return GetAlchemyMenu() != nullptr;
    }

    bool AlchemyController::IsCrafting() {
        return s_isCrafting.load();
    }

    bool AlchemyController::StopCrafting() {
        SKSE::log::info("AlchemyController StopCrafting");
        if (s_isCrafting.load()) {
            s_craftCancel.store(true);
            SKSE::log::info("AlchemyController: StopCrafting requested");
            return true;
        }
        return false;
    }

    void AlchemyController::DismissOpenMessageBoxes() {
        int dismissed = 0;
        while (dismissed < 50 && RE::MessageBoxMenu::GetCurrentMessageBoxData() != nullptr) {
            auto* msgData = RE::MessageBoxMenu::GetCurrentMessageBoxData();
            std::string text;
            if (msgData && msgData->bodyText.c_str() != nullptr) {
                text = msgData->bodyText.c_str();
            }

            // DO NOT auto-dismiss exit/quit confirmation dialogs!
            if (text.find("Quit") != std::string::npos ||
                text.find("quit") != std::string::npos ||
                text.find("종료") != std::string::npos ||
                text.find("나가") != std::string::npos) {
                SKSE::log::info("DismissOpenMessageBoxes: Skipping quit/exit confirmation: '{}'", text);
                break;
            }

            SKSE::log::info("DismissOpenMessageBoxes: Auto-dismissing MessageBoxMenu ({}): '{}'", dismissed + 1, text);

            if (!text.empty()) {
                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                nlohmann::json evt = {
                    { "type", "event" },
                    { "event", "alchemy.effect_discovered" },
                    { "message", text },
                    { "ts", nowMs }
                };
                Server::WebSocketServer::GetSingleton().Broadcast(evt);
            }

            RE::MessageBoxMenu::SelectOption(0);
            dismissed++;
        }
        if (dismissed > 0) {
            SKSE::log::info("DismissOpenMessageBoxes: Successfully auto-dismissed {} popups", dismissed);
        }
    }

    RE::GFxValue GetTargetItemList(RE::CraftingSubMenus::AlchemyMenu* alchemyMenu, RE::CraftingMenu* craftingMenu) {
        if (alchemyMenu && alchemyMenu->itemList.IsObject()) {
            return alchemyMenu->itemList;
        }

        RE::GFxValue list;
        auto* movie = craftingMenu ? craftingMenu->uiMovie.get() : (alchemyMenu ? alchemyMenu->view.get() : nullptr);
        if (movie) {
            if (movie->GetVariable(&list, "_root.Menu.InventoryLists.ItemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.CraftingMenu.InventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.Menu_mc.inventoryLists.itemList") && list.IsObject()) return list;
            if (movie->GetVariable(&list, "_root.CraftingMenu.inventoryLists.itemList") && list.IsObject()) return list;
        }
        return list;
    }

    bool AlchemyController::IsAlchemyItemListReady() {
        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            return false;
        }

        // 1. C++ 엔진 레벨에서 재료 배열이 채워졌는지 확인
        const size_t cppCount = alchemyMenu->ingredientEntries.size();
        if (cppCount == 0) {
            return false;
        }

        auto* ui = RE::UI::GetSingleton();
        auto craftingMenu = (ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME))
            ? ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME)
            : nullptr;

        auto* movie = craftingMenu ? craftingMenu->uiMovie.get() : (alchemyMenu->view ? alchemyMenu->view.get() : nullptr);

        // 2. Scaleform craftingMenu / inventoryLists의 bInCategories 확인 (바닐라 및 SkyUI 공통 플래그)
        RE::GFxValue inCategoriesVal;
        if (alchemyMenu->craftingMenu.IsObject()) {
            if (alchemyMenu->craftingMenu.GetMember("bInCategories", &inCategoriesVal) && inCategoriesVal.IsBool()) {
                if (inCategoriesVal.GetBool()) {
                    return false;
                }
            }
        }

        if (movie) {
            if ((movie->GetVariable(&inCategoriesVal, "_root.Menu.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.CraftingMenu.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.Menu_mc.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.Menu_mc.inventoryLists.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.InventoryLists.bInCategories") ||
                 movie->GetVariable(&inCategoriesVal, "_root.Menu.InventoryLists.bInCategories")) &&
                inCategoriesVal.IsBool() && inCategoriesVal.GetBool()) {
                return false;
            }

            // SkyUI inventoryLists._currentState 확인 (1 = SHOW_CATEGORIES, 2 = SHOW_PANEL)
            RE::GFxValue panelState;
            if (movie->GetVariable(&panelState, "_root.Menu_mc.inventoryLists._currentState") ||
                movie->GetVariable(&panelState, "_root.Menu.InventoryLists._currentState") ||
                movie->GetVariable(&panelState, "_root.CraftingMenu.InventoryLists._currentState")) {
                if (panelState.IsNumber() && static_cast<int>(panelState.GetNumber()) < 2) {
                    return false;
                }
            }

            // SkyUI categoryList 포커스 여부
            RE::GFxValue catFocus;
            if ((movie->GetVariable(&catFocus, "_root.Menu_mc.inventoryLists.categoryList.bHasFocus") ||
                 movie->GetVariable(&catFocus, "_root.Menu.InventoryLists.categoryList.bHasFocus") ||
                 movie->GetVariable(&catFocus, "_root.CraftingMenu.InventoryLists.categoryList.bHasFocus")) &&
                catFocus.IsBool() && catFocus.GetBool()) {
                return false;
            }
        }

        // 3. categories 리스트가 포커스를 가지고 있는지 확인
        if (alchemyMenu->categories.IsObject()) {
            RE::GFxValue catFocus;
            if (alchemyMenu->categories.GetMember("bHasFocus", &catFocus) && catFocus.IsBool() && catFocus.GetBool()) {
                return false;
            }
        }

        // 4. itemList 객체 상태 및 포커스 검사
        RE::GFxValue targetList = GetTargetItemList(alchemyMenu, craftingMenu.get());
        if (!targetList.IsObject()) {
            return false;
        }

        // SkyUI에서는 카테고리 단계일 때 itemList의 disableInput 또는 disableSelection이 true로 설정됨
        RE::GFxValue disableVal;
        if (targetList.GetMember("disableInput", &disableVal) && disableVal.IsBool() && disableVal.GetBool()) {
            return false;
        }
        if (targetList.GetMember("disableSelection", &disableVal) && disableVal.IsBool() && disableVal.GetBool()) {
            return false;
        }

        // itemList의 bHasFocus가 명시적인 bool이고 false인 경우 (카테고리 뷰 포커스)
        RE::GFxValue itemFocus;
        if (targetList.GetMember("bHasFocus", &itemFocus) && itemFocus.IsBool() && !itemFocus.GetBool()) {
            return false;
        }

        // itemList가 숨겨져 있는지(_visible == false)
        RE::GFxValue visibleVal;
        if (targetList.GetMember("_visible", &visibleVal) && visibleVal.IsBool() && !visibleVal.GetBool()) {
            return false;
        }

        // itemList 알파값이 0인지
        RE::GFxValue alphaVal;
        if (targetList.GetMember("_alpha", &alphaVal) && alphaVal.IsNumber() && alphaVal.GetNumber() <= 0.0) {
            return false;
        }

        // entryList 또는 _dataProvider 배열 확인
        uint32_t flashCount = 0;
        RE::GFxValue listArray;
        if (targetList.GetMember("entryList", &listArray) && listArray.IsArray()) {
            flashCount = listArray.GetArraySize();
        } else if (targetList.GetMember("_dataProvider", &listArray) && listArray.IsArray()) {
            flashCount = listArray.GetArraySize();
        } else if (targetList.GetMember("dataProvider", &listArray) && listArray.IsArray()) {
            flashCount = listArray.GetArraySize();
        } else {
            return false;
        }

        if (flashCount == 0) {
            return false;
        }

        return true;
    }

    void RefreshAlchemyUI(RE::CraftingSubMenus::AlchemyMenu* alchemyMenu) {
        if (!alchemyMenu) return;

        if (alchemyMenu->itemList.IsObject()) {
            alchemyMenu->itemList.Invoke("UpdateList");
        } else if (alchemyMenu->view) {
            alchemyMenu->view->Invoke("_root.CraftingMenu.InventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
            alchemyMenu->view->Invoke("_root.Menu_mc.inventoryLists.itemList.UpdateList", nullptr, nullptr, 0);
        }
    }

    bool AlchemyController::ClearSelectedIngredients() {
        StopCrafting();
        return SelectIngredients(std::vector<RE::FormID>{}, false).success;
    }

    std::vector<RE::FormID> AlchemyController::GetSelectedIngredientFormIDs() {
        std::vector<RE::FormID> result;
        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            return result;
        }

        for (std::uint32_t idx : alchemyMenu->selectedIndexes) {
            if (idx < alchemyMenu->ingredientEntries.size()) {
                auto& entry = alchemyMenu->ingredientEntries[idx];
                if (entry.ingredient && entry.ingredient->object) {
                    result.push_back(entry.ingredient->object->GetFormID());
                }
            }
        }
        return result;
    }

    AlchemySelectResult AlchemyController::CraftPotions(int count) {
        AlchemySelectResult result;
        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            result.success = false;
            result.message = "Alchemy workbench is not open";
            return result;
        }

        if (alchemyMenu->selectedIndexes.size() < 2) {
            result.success = false;
            result.message = "Not enough ingredients selected to craft";
            return result;
        }

        for (std::uint32_t idx : alchemyMenu->selectedIndexes) {
            if (idx < alchemyMenu->ingredientEntries.size()) {
                auto& entry = alchemyMenu->ingredientEntries[idx];
                const char* name = (entry.ingredient && entry.ingredient->object) ? entry.ingredient->object->GetName() : nullptr;
                if (name) result.selectedIngredients.push_back(name);
            }
        }

        if (count <= 1) {
            auto* userEvents = RE::UserEvents::GetSingleton();
            if (userEvents) {
                AlchemyController::DismissOpenMessageBoxes();
                alchemyMenu->ProcessUserEvent(&userEvents->xButton);
                AlchemyController::DismissOpenMessageBoxes();

                result.crafted = true;
                result.craftedCount = 1;
                result.success = true;
                result.message = "Crafted 1 potion";

                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                nlohmann::json evt = {
                    { "type", "event" },
                    { "event", "alchemy.craft_complete" },
                    { "craftedCount", 1 },
                    { "ts", nowMs }
                };
                Server::WebSocketServer::GetSingleton().Broadcast(evt);
                return result;
            }
            result.success = false;
            result.message = "UserEvents not found";
            return result;
        }

        // Prevent starting a new bulk craft if one is already in progress (avoids deadlock)
        if (s_isCrafting.load()) {
            SKSE::log::warn("CraftPotions: Bulk crafting is already in progress");
            result.success = false;
            result.crafted = false;
            result.craftedCount = 0;
            result.message = "Crafting already in progress";
            return result;
        }

        s_craftCancel.store(false);
        s_isCrafting.store(true);

        std::thread([count]() {
            int crafted = 0;
            SKSE::log::info("CraftPotions: Starting bulk craft of up to {} potions", count);
            for (int i = 0; i < count; ++i) {
                if (s_craftCancel.load() || SkyPotion::g_isShuttingDown.load()) {
                    SKSE::log::info("CraftPotions: Cancelled after {} potions", crafted);
                    break;
                }

                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();

                auto* taskInterface = SKSE::GetTaskInterface();
                if (!taskInterface) break;

                taskInterface->AddTask([promise]() {
                    if (SkyPotion::g_isShuttingDown.load() || !Util::IsGameSessionReady()) {
                        promise->set_value(false);
                        return;
                    }

                    AlchemyController::DismissOpenMessageBoxes();

                    auto* menu = AlchemyController::GetAlchemyMenu();
                    if (!menu || menu->selectedIndexes.size() < 2) {
                        promise->set_value(false); // Out of ingredients or menu closed
                        return;
                    }
                    auto* userEvents = RE::UserEvents::GetSingleton();
                    if (!userEvents) {
                        promise->set_value(false);
                        return;
                    }

                    menu->ProcessUserEvent(&userEvents->xButton);
                    AlchemyController::DismissOpenMessageBoxes();
                    promise->set_value(true);
                });

                if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
                    if (!future.get()) {
                        SKSE::log::info("CraftPotions: Finished at potion {} (ingredients depleted or menu closed)", crafted);
                        break;
                    }
                    crafted++;
                } else {
                    SKSE::log::warn("CraftPotions: Timeout waiting for craft task");
                    break;
                }

                // 200ms delay between crafts for UI/inventory/sound settling
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            s_isCrafting.store(false);
            SKSE::log::info("CraftPotions: Bulk craft completed (total crafted: {})", crafted);

            if (!SkyPotion::g_isShuttingDown.load()) {
                auto* taskInterface = SKSE::GetTaskInterface();
                if (taskInterface) {
                    taskInterface->AddTask([]() {
                        if (!SkyPotion::g_isShuttingDown.load()) {
                            AlchemyController::DismissOpenMessageBoxes();
                        }
                    });
                }

                int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                nlohmann::json evt = {
                    { "type", "event" },
                    { "event", "alchemy.craft_complete" },
                    { "craftedCount", crafted },
                    { "ts", nowMs }
                };
                Server::WebSocketServer::GetSingleton().Broadcast(evt);
            }
        }).detach();

        result.success = true;
        result.crafted = true;
        result.craftedCount = count;
        result.message = std::format("Started bulk craft of up to {} potions", count);
        return result;
    }

    AlchemySelectResult AlchemyController::SelectIngredients(
        const std::vector<std::string>& formIdStrings,
        bool autoCraft,
        int craftCount
    ) {
        std::vector<RE::FormID> formIds;
        formIds.reserve(formIdStrings.size());

        for (const auto& str : formIdStrings) {
            try {
                uint64_t val = std::stoull(str, nullptr, 0);
                RE::FormID id = static_cast<RE::FormID>(val & 0xFFFFFFFFULL);
                formIds.push_back(id);
            } catch (const std::exception& e) {
                SKSE::log::warn("Invalid FormID string '{}': {}", str, e.what());
            }
        }

        return SelectIngredients(formIds, autoCraft, craftCount);
    }

    AlchemySelectResult AlchemyController::SelectIngredients(
        const std::vector<RE::FormID>& formIds,
        bool autoCraft,
        int craftCount
    ) {
        AlchemySelectResult result;

        auto* alchemyMenu = GetAlchemyMenu();
        if (!alchemyMenu) {
            result.success = false;
            result.message = "Alchemy workbench is not open";
            return result;
        }

        if (!IsAlchemyItemListReady()) {
            result.success = false;
            result.message = "Alchemy item list is not active (still in categories or transitioning)";
            return result;
        }

        auto* ui = RE::UI::GetSingleton();
        auto craftingMenu = (ui && ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME))
            ? ui->GetMenu<RE::CraftingMenu>(RE::CraftingMenu::MENU_NAME)
            : nullptr;

        RE::GFxValue targetList = GetTargetItemList(alchemyMenu, craftingMenu.get());
        if (!targetList.IsObject()) {
            result.success = false;
            result.message = "Target item list not found";
            return result;
        }

        // 1. Gather currently selected FormIDs & their entry indices
        std::map<RE::FormID, std::uint32_t> currentSelectedMap;
        for (std::uint32_t idx : alchemyMenu->selectedIndexes) {
            if (idx < alchemyMenu->ingredientEntries.size()) {
                auto& entry = alchemyMenu->ingredientEntries[idx];
                if (entry.ingredient && entry.ingredient->object) {
                    currentSelectedMap[entry.ingredient->object->GetFormID()] = idx;
                }
            }
        }
        for (std::uint32_t i = 0; i < alchemyMenu->ingredientEntries.size(); ++i) {
            auto& entry = alchemyMenu->ingredientEntries[i];
            if (entry.isSelected && entry.ingredient && entry.ingredient->object) {
                currentSelectedMap[entry.ingredient->object->GetFormID()] = i;
            }
        }

        // 2. Prepare target FormIDs (deduplicated, max 3)
        std::vector<RE::FormID> targetFormIds;
        for (auto id : formIds) {
            if (targetFormIds.size() >= 3) break;
            if (std::find(targetFormIds.begin(), targetFormIds.end(), id) == targetFormIds.end()) {
                targetFormIds.push_back(id);
            }
        }

        // 3. Compute Diff
        // Items to remove: currently selected but not in target
        std::vector<std::pair<RE::FormID, std::uint32_t>> toRemove;
        for (const auto& [currId, currIdx] : currentSelectedMap) {
            if (std::find(targetFormIds.begin(), targetFormIds.end(), currId) == targetFormIds.end()) {
                toRemove.emplace_back(currId, currIdx);
            }
        }

        // Items to add: in target but not currently selected
        std::vector<RE::FormID> toAdd;
        for (auto targetId : targetFormIds) {
            if (currentSelectedMap.find(targetId) == currentSelectedMap.end()) {
                toAdd.push_back(targetId);
            }
        }

        SKSE::log::info("SelectIngredients Diff: curr={}, target={}, remove={}, add={}",
            currentSelectedMap.size(), targetFormIds.size(), toRemove.size(), toAdd.size());

        // Fast-path: if diff is empty, nothing to toggle!
        if (toRemove.empty() && toAdd.empty()) {
            for (auto id : targetFormIds) {
                auto it = currentSelectedMap.find(id);
                if (it != currentSelectedMap.end() && it->second < alchemyMenu->ingredientEntries.size()) {
                    auto& entry = alchemyMenu->ingredientEntries[it->second];
                    const char* name = (entry.ingredient && entry.ingredient->object) ? entry.ingredient->object->GetName() : nullptr;
                    result.selectedIngredients.push_back(name ? name : std::format("0x{:08X}", id));
                }
            }
            result.success = true;
            result.message = "Selections already match";

            if (autoCraft && craftCount > 0 && result.selectedIngredients.size() >= 2) {
                return CraftPotions(craftCount);
            }
            return result;
        }

        // 4. Untoggle items via Scaleform onItemPress (natural engine untoggle)
        for (const auto& [remId, remIdx] : toRemove) {
            SKSE::log::info("SelectIngredients: Untoggling 0x{:08X} at index {}", remId, remIdx);
            RE::GFxValue indexVal(static_cast<double>(remIdx));
            targetList.SetMember("selectedIndex", indexVal);

            RE::GFxValue args[2];
            args[0] = static_cast<double>(remIdx);
            args[1] = 0.0;
            targetList.Invoke("onItemSelect", nullptr, args, 1);
            targetList.Invoke("onItemPress", nullptr, args, 2);
        }

        // 5. Toggle ON items to add via Scaleform onItemPress
        for (auto addId : toAdd) {
            bool found = false;
            for (std::uint32_t i = 0; i < alchemyMenu->ingredientEntries.size(); ++i) {
                auto& entry = alchemyMenu->ingredientEntries[i];
                if (!entry.ingredient || !entry.ingredient->object) continue;

                if (entry.ingredient->object->GetFormID() == addId) {
                    found = true;
                    SKSE::log::info("SelectIngredients: Toggling ON 0x{:08X} at index {}", addId, i);
                    RE::GFxValue indexVal(static_cast<double>(i));
                    targetList.SetMember("selectedIndex", indexVal);

                    RE::GFxValue args[2];
                    args[0] = static_cast<double>(i);
                    args[1] = 0.0;
                    targetList.Invoke("onItemSelect", nullptr, args, 1);
                    targetList.Invoke("onItemPress", nullptr, args, 2);

                    const char* name = entry.ingredient->object->GetName();
                    result.selectedIngredients.push_back((name && name[0] != '\0') ? name : std::format("0x{:08X}", addId));
                    break;
                }
            }
            if (!found) {
                result.missingIngredients.push_back(std::format("0x{:08X}", addId));
            }
        }

        // 6. Refresh UI once at the end
        targetList.Invoke("UpdateList");
        RefreshAlchemyUI(alchemyMenu);

        // Include retained items in the result
        for (auto targetId : targetFormIds) {
            if (std::find(toAdd.begin(), toAdd.end(), targetId) == toAdd.end()) {
                auto it = currentSelectedMap.find(targetId);
                if (it != currentSelectedMap.end() && it->second < alchemyMenu->ingredientEntries.size()) {
                    auto& entry = alchemyMenu->ingredientEntries[it->second];
                    const char* name = (entry.ingredient && entry.ingredient->object) ? entry.ingredient->object->GetName() : nullptr;
                    result.selectedIngredients.push_back(name ? name : std::format("0x{:08X}", targetId));
                }
            }
        }

        // 7. Auto-craft if requested
        if (autoCraft && result.missingIngredients.empty() && result.selectedIngredients.size() >= 2) {
            return CraftPotions(craftCount);
        } else {
            result.message = std::format("Selected {} (active slots: {})",
                result.selectedIngredients.size(), alchemyMenu->selectedIndexes.size());
        }

        result.success = true;
        return result;
    }

    MenuOpenCloseHandler* MenuOpenCloseHandler::GetSingleton() {
        static MenuOpenCloseHandler instance;
        return &instance;
    }

    void MenuOpenCloseHandler::Register() {
        auto* ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
            SKSE::log::info("MenuOpenCloseHandler successfully registered with UI event source");
        }
    }

    struct MonitorSession {
        std::atomic<bool> active{ true };
        int checkCount{ 0 };
        bool wasListReady{ false };
        std::vector<RE::FormID> lastSelected;
    };

    static std::shared_ptr<MonitorSession> s_monitorSession;
    static std::mutex s_sessionMutex;

    void MenuOpenCloseHandler::StartMonitoring() {
        if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
            return;
        }

        std::shared_ptr<MonitorSession> session;
        {
            std::lock_guard<std::mutex> lock(s_sessionMutex);
            if (s_monitorSession && s_monitorSession->active.load()) {
                return; // Already monitoring
            }
            if (s_monitorSession) {
                s_monitorSession->active.store(false);
            }
            s_monitorSession = std::make_shared<MonitorSession>();
            session = s_monitorSession;
        }

        std::thread([session]() {
            while (session->active.load() && !SkyPotion::g_isShuttingDown.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                if (!session->active.load() || SkyPotion::g_isShuttingDown.load() ||
                    !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
                    break;
                }

                auto* taskInterface = SKSE::GetTaskInterface();
                if (!taskInterface) {
                    break;
                }

                auto promise = std::make_shared<std::promise<bool>>();
                auto future = promise->get_future();

                taskInterface->AddTask([session, promise]() {
                    if (!session->active.load() || SkyPotion::g_isShuttingDown.load() || !Util::IsGameSessionReady() ||
                        !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
                        promise->set_value(false);
                        return;
                    }

                    ++session->checkCount;

                    auto* ui = RE::UI::GetSingleton();
                    if (!ui || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
                        promise->set_value(false);
                        return;
                    }

                    auto* alchemyMenu = AlchemyController::GetAlchemyMenu();
                    if (!alchemyMenu) {
                        if (session->checkCount >= 10) {
                            SKSE::log::info("CraftingMenu: Active submenu is not AlchemyMenu. Stopping monitor.");
                            promise->set_value(false);
                        } else {
                            promise->set_value(true);
                        }
                        return;
                    }

                    bool isReady = AlchemyController::IsAlchemyItemListReady();

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    if (isReady && !session->wasListReady) {
                        session->wasListReady = true;
                        session->lastSelected = AlchemyController::GetSelectedIngredientFormIDs();
                        SKSE::log::info("CraftingMenu: Alchemy ItemList OPENED! (cppEntries={}, check={})",
                            alchemyMenu->ingredientEntries.size(), session->checkCount);

                        Server::WebSocketServer::GetSingleton().BroadcastBinary(
                            Sync::ProtoPacketBuilder::BuildAlchemyBenchState(true, session->lastSelected)
                        );
                    } else if (!isReady && session->wasListReady) {
                        session->wasListReady = false;
                        session->lastSelected.clear();
                        SKSE::log::info("CraftingMenu: Alchemy ItemList CLOSED (back to categories).");

                        Server::WebSocketServer::GetSingleton().BroadcastBinary(
                            Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
                        );
                    } else if (isReady && session->wasListReady) {
                        auto currentSelected = AlchemyController::GetSelectedIngredientFormIDs();
                        if (currentSelected != session->lastSelected) {
                            session->lastSelected = currentSelected;
                            SKSE::log::info("CraftingMenu: Alchemy selection changed in Skyrim (count={})",
                                session->lastSelected.size());

                            Server::WebSocketServer::GetSingleton().BroadcastBinary(
                                Sync::ProtoPacketBuilder::BuildAlchemyBenchState(true, session->lastSelected)
                            );
                        }
                    }

                    promise->set_value(true);
                });

                // Wait for task completion. If main thread is paused (Alt-Tab, System Menu, etc.),
                // keep waiting without killing the monitor thread or accumulating duplicate tasks.
                bool taskDone = false;
                while (session->active.load() && !SkyPotion::g_isShuttingDown.load() &&
                       Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
                    if (future.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready) {
                        taskDone = true;
                        if (!future.get()) {
                            // Menu closed or invalid state, exit cleanly
                            session->active.store(false);
                            return;
                        }
                        break;
                    }
                }

                if (!taskDone) {
                    break; // Shutting down, session cancelled, or client disconnected
                }
            }

            session->active.store(false);
        }).detach();
    }

    void MenuOpenCloseHandler::StopMonitoring() {
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        if (s_monitorSession) {
            s_monitorSession->active.store(false);
            s_monitorSession.reset();
        }
    }

    bool MenuOpenCloseHandler::IsMonitoring() {
        std::lock_guard<std::mutex> lock(s_sessionMutex);
        return s_monitorSession && s_monitorSession->active.load();
    }

    RE::BSEventNotifyControl MenuOpenCloseHandler::ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
        if (!a_event) return RE::BSEventNotifyControl::kContinue;

        // If no clients are connected to WebSocket, do absolutely nothing!
        if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::MessageBoxMenu::MENU_NAME && a_event->opening) {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                taskInterface->AddTask([]() {
                    if (AlchemyController::IsAlchemyMenuOpen()) {
                        SKSE::log::info("MenuOpenCloseEvent: MessageBoxMenu opened at alchemy workbench. Auto-dismissing.");
                        AlchemyController::DismissOpenMessageBoxes();
                    }
                });
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::MainMenu::MENU_NAME && a_event->opening) {
            SKSE::log::info("MenuOpenCloseEvent: MainMenu opening. Stopping craft and monitor session.");
            AlchemyController::StopCrafting();
            StopMonitoring();

            Server::WebSocketServer::GetSingleton().BroadcastBinary(
                Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
            );
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
            if (a_event->opening) {
                SKSE::log::info("MenuOpenCloseEvent: LoadingMenu opening. Stopping craft and monitor session.");
                AlchemyController::StopCrafting();
                StopMonitoring();

                Server::WebSocketServer::GetSingleton().BroadcastBinary(
                    Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
                );
            } else {
                SKSE::log::info("MenuOpenCloseEvent: LoadingMenu closed. Scheduling session snapshot.");
                Server::WebSocketServer::GetSingleton().ScheduleSessionSnapshot();
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName == RE::CraftingMenu::MENU_NAME) {
            bool opening = a_event->opening;
            SKSE::log::info("MenuOpenCloseEvent: CraftingMenu opening={}", opening);

            if (opening) {
                StartMonitoring();
            } else {
                AlchemyController::StopCrafting();
                StopMonitoring();

                Server::WebSocketServer::GetSingleton().BroadcastBinary(
                    Sync::ProtoPacketBuilder::BuildAlchemyBenchState(false, {})
                );
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        // 퍽/스킬 레벨업 메뉴 닫힐 때 연금술 스탯 즉시 동기화
        if (a_event->menuName == RE::StatsMenu::MENU_NAME && !a_event->opening) {
            InventoryEventHandler::GetSingleton()->RequestAlchemyStatsSync();
        }

        return RE::BSEventNotifyControl::kContinue;
    }

}
</file>

<file path="src/game/InventoryExtractor.h">
#pragma once

#include "Pch.h"

namespace SkyPotion::Game {

class InventoryExtractor {
public:
    // Returns list of ingredients with count and known effects
    static nlohmann::json GetIngredientsJson();
};

class InventoryEventHandler :
    public RE::BSTEventSink<RE::TESContainerChangedEvent>,
    public RE::BSTEventSink<RE::TESEquipEvent> {
public:
    static InventoryEventHandler* GetSingleton();
    void Register();

    // 로딩 중 이벤트 무시용 상태 제어
    void SetLoading(bool loading) { _isLoading.store(loading); }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESContainerChangedEvent* a_event,
        RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override;

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESEquipEvent* a_event,
        RE::BSTEventSource<RE::TESEquipEvent>* a_eventSource) override;

    void RequestInventorySync();
    void ExecuteInventorySync();

    void RequestAlchemyStatsSync();
    void ExecuteAlchemyStatsSync();

private:
    std::atomic<bool> _isLoading{ true }; // 초기값 true (게임 로드 완료 전까지 차단)
    std::atomic<bool> _syncPending{ false };
    std::atomic<int64_t> _lastSyncTimeMs{ 0 };

    std::atomic<bool> _alchemySyncPending{ false };
    std::atomic<int64_t> _lastAlchemySyncTimeMs{ 0 };
};

} // namespace SkyPotion::Game
</file>

<file path="src/game/StatExtractor.cpp">
#include "game/StatExtractor.h"

namespace SkyPotion::Game {

namespace {
    double GetTotalFortifyAlchemy(RE::PlayerCharacter* player) {
        if (!player || !Util::IsGameSessionReady()) return 0.0;

        double totalBonus = 0.0;
        auto* magicTarget = player->AsMagicTarget();
        if (!magicTarget) return 0.0;

        auto* activeEffects = magicTarget->GetActiveEffectList();
        if (!activeEffects) return 0.0;

        for (auto* ae : *activeEffects) {
            if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kInactive) || !ae->effect) {
                continue;
            }

            auto* mgef = ae->GetBaseObject();
            if (!mgef) continue;

            auto arch = mgef->GetArchetype();
            if ((arch == RE::EffectSetting::Archetype::kPeakValueModifier ||
                arch == RE::EffectSetting::Archetype::kValueModifier) &&
                mgef->GetMagickSkill() == RE::ActorValue::kAlchemy) {
                totalBonus += ae->magnitude;
            }
        }

        return totalBonus;
    }
}

nlohmann::json StatExtractor::GetAlchemyStatsJson() {
    if (!Util::IsGameSessionReady()) {
        return nlohmann::json::object();
    }

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return nlohmann::json::object();

    auto avOwner = player->AsActorValueOwner();
    if (!avOwner) return nlohmann::json::object();

    double currentSkill = avOwner->GetActorValue(RE::ActorValue::kAlchemy);
    double baseSkill = avOwner->GetBaseActorValue(RE::ActorValue::kAlchemy);
    double fortifyAlchemy = GetTotalFortifyAlchemy(player);

    return {
        { "alchemy", Util::Round3(currentSkill) },
        { "alchemyBase", Util::Round3(baseSkill) },
        { "fortifyAlchemy", Util::Round3(fortifyAlchemy) }
    };
}

nlohmann::json StatExtractor::GetPlayerStatsJson() {
    if (!Util::IsGameSessionReady()) {
        return nlohmann::json::object();
    }

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return nlohmann::json::object();

    auto avOwner = player->AsActorValueOwner();
    if (!avOwner) return nlohmann::json::object();

    // 1. Resolve player name
    std::string playerName = "Player";
    if (player->GetActorBase() && player->GetActorBase()->GetName() && player->GetActorBase()->GetName()[0] != '\0') {
        playerName = player->GetActorBase()->GetName();
    } else if (player->GetName() && player->GetName()[0] != '\0') {
        playerName = player->GetName();
    }

    // 2. Resolve configured game language from Skyrim INI
    std::string language = "ENGLISH";
    const auto ini = RE::INISettingCollection::GetSingleton();
    auto setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
    if (setting && setting->GetType() == RE::Setting::Type::kString && setting->data.s) {
        language = setting->data.s;
    }

    return {
        { "name", playerName },
        { "playerName", playerName },
        { "language", language },
        { "level", player->GetLevel() },
        { "health", Util::Round3(avOwner->GetActorValue(RE::ActorValue::kHealth)) },
        { "healthBase", Util::Round3(avOwner->GetBaseActorValue(RE::ActorValue::kHealth)) },
        { "magicka", Util::Round3(avOwner->GetActorValue(RE::ActorValue::kMagicka)) },
        { "magickaBase", Util::Round3(avOwner->GetBaseActorValue(RE::ActorValue::kMagicka)) },
        { "stamina", Util::Round3(avOwner->GetActorValue(RE::ActorValue::kStamina)) },
        { "staminaBase", Util::Round3(avOwner->GetBaseActorValue(RE::ActorValue::kStamina)) },
        { "inventoryWeight", Util::Round3(player->GetWeightInContainer()) }
    };
}

} // namespace SkyPotion::Game
</file>

<file path="src/main.cpp">
#include "Pch.h"
#include "Config.h"
#include "server/WebSocketServer.h"
#include "game/AlchemyController.h"
#include "game/InventoryExtractor.h"

#include <spdlog/sinks/basic_file_sink.h>

using namespace std::literals;

SKSEPluginInfo(
    .Version = REL::Version{ 1, 0, 0, 0 },
    .Name = "SkyPotionSKSE"sv,
    .Author = "Dolzzo"sv,
    .SupportEmail = ""sv,
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 }
);

namespace {
    void InitializeLogging() {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        *path /= "SkyPotionSKSE.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
    }

    void OnMessage(SKSE::MessagingInterface::Message* a_msg) {
        if (!a_msg) return;

        switch (a_msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            {
                const auto& config = SkyPotion::Config::GetSingleton();
                SKSE::log::info("Skyrim data loaded. Starting SkyPotion WebSocket server on {}:{}...",
                    config.GetBindAddress(), config.GetPort());
                SkyPotion::Server::WebSocketServer::GetSingleton().Start(config.GetPort(), config.GetBindAddress());
                SkyPotion::Game::MenuOpenCloseHandler::GetSingleton()->Register();
                SkyPotion::Game::InventoryEventHandler::GetSingleton()->Register();
            }
            break;

        case SKSE::MessagingInterface::kPreLoadGame:
            SKSE::log::info("SKSE Message: kPreLoadGame. Halting any active craft.");
            SkyPotion::Game::InventoryEventHandler::GetSingleton()->SetLoading(true);
            SkyPotion::Game::AlchemyController::StopCrafting();
            SKSE::log::info("SKSE Message: kPreLoadGame completed.");
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            SKSE::log::info("Game session started or loaded. Scheduling session snapshot.");
            SkyPotion::Game::InventoryEventHandler::GetSingleton()->SetLoading(false);
            SkyPotion::Server::WebSocketServer::GetSingleton().ScheduleSessionSnapshot();
            break;

            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    InitializeLogging();
    SkyPotion::Config::GetSingleton().Load();

    SKSE::Init(a_skse);

    SKSE::log::info("----------------------------------------");
    SKSE::log::info("SkyPotion SKSE Plugin v1.0.0 loaded!");
    SKSE::log::info("Targeting Skyrim SE / AE / GOG");
    SKSE::log::info("----------------------------------------");

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::error("Failed to get SKSE messaging interface!");
        return false;
    }

    if (!messaging->RegisterListener(OnMessage)) {
        SKSE::log::error("Failed to register SKSE message listener!");
        return false;
    }

    return true;
}

extern "C" [[maybe_unused]] __declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE /*hinstDLL*/, DWORD fdwReason, LPVOID /*lpvReserved*/) {
    if (fdwReason == DLL_PROCESS_DETACH) {
        SkyPotion::g_isShuttingDown.store(true);
        SkyPotion::Server::WebSocketServer::GetSingleton().Stop();
        SkyPotion::Game::AlchemyController::StopCrafting();
    }
    return TRUE;
}
</file>

<file path="CMakeLists.txt">
cmake_minimum_required(VERSION 3.25)

project(
    SkyPotionSKSE
    VERSION 1.0.0
    DESCRIPTION "Skyrim SE/AE WebSocket Plugin for SkyPotion"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure CRT matches CommonLibSSE-NG (/MD dynamic CRT with static library linkage)
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg triplet matching /MD CRT")

# Enable MSVC multi-processor parallel compilation (/MP) and UTF-8 source/exec charset
if(MSVC)
    add_compile_options(/MP /utf-8)
endif()

# 여기에 추가 (라인 20 부근)
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

find_package(protobuf CONFIG REQUIRED)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# proto 파일은 별도 정적 라이브러리로 분리 (PCH 충돌 방지)
add_library(skypotion_proto STATIC "${PROTO_SRC_DIR}/game_sync.proto")
target_link_libraries(skypotion_proto PUBLIC protobuf::libprotobuf)
target_include_directories(skypotion_proto PUBLIC "${PROTO_GEN_DIR}")

# CommonLibSSE-NG build options (SE/AE enabled, tests and VR disabled)
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE unit tests" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Enable Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Enable Skyrim AE (1.6+ / 1.7+) support" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Disable Skyrim VR support" FORCE)

# CommonLibSSE-NG (Maintained upstream supporting 1.7.104 & Address Library Format 5)
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Skip git fetch on reconfigure" FORCE)
FetchContent_Declare(
    CommonLibSSE-NG
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG        ng
)
FetchContent_MakeAvailable(CommonLibSSE-NG)


protobuf_generate(
    TARGET skypotion_proto
    LANGUAGE cpp
    IMPORT_DIRS "${PROTO_SRC_DIR}"
    PROTOC_OUT_DIR "${PROTO_GEN_DIR}"
)


# Add SkyPotion SKSE plugin library
add_library(
    ${PROJECT_NAME} SHARED
    src/Pch.h
    src/Config.h
    src/Config.cpp
    src/main.cpp
    src/server/WebSocketServer.h
    src/server/WebSocketServer.cpp
    src/sync/ProtoPacketBuilder.h
    src/sync/ProtoPacketBuilder.cpp
    src/game/PerkExtractor.h
    src/game/PerkExtractor.cpp
    src/game/InventoryExtractor.h
    src/game/InventoryExtractor.cpp
    src/game/StatExtractor.h
    src/game/StatExtractor.cpp
    src/game/AlchemyDataExtractor.h
    src/game/AlchemyDataExtractor.cpp
    src/game/AlchemyController.h
    src/game/AlchemyController.cpp
)

# Enable Precompiled Headers for ultra-fast compilation & type consistency
target_precompile_headers(${PROJECT_NAME} PRIVATE src/Pch.h)

target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    skypotion_proto
    nlohmann_json::nlohmann_json
    ixwebsocket::ixwebsocket
    spdlog::spdlog
    ws2_32
    crypt32
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
</file>

<file path="src/game/InventoryExtractor.cpp">
#include "game/InventoryExtractor.h"
#include "game/StatExtractor.h"
#include "server/WebSocketServer.h"
#include "sync/ProtoPacketBuilder.h"
#include <chrono>

namespace SkyPotion::Game {

nlohmann::json InventoryExtractor::GetIngredientsJson() {
    nlohmann::json itemsArray = nlohmann::json::array();

    if (!Util::IsGameSessionReady()) {
        return itemsArray;
    }

    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return itemsArray;

    const auto inventory = player->GetInventory([](RE::TESBoundObject& a_obj) {
        return a_obj.Is(RE::FormType::Ingredient);
    });

    for (const auto& [boundObj, data] : inventory) {
        const auto count = data.first;
        if (count <= 0 || !boundObj) continue;

        auto* ingredient = boundObj->As<RE::IngredientItem>();
        if (!ingredient) continue;

        nlohmann::json effectsJson = nlohmann::json::array();
        const auto& effects = ingredient->effects;

        for (uint32_t i = 0; i < effects.size(); ++i) {
            auto* effectItem = effects[i];
            if (!effectItem || !effectItem->baseEffect) continue;

            bool isKnown = (ingredient->gamedata.knownEffectFlags & (1 << i)) != 0;

            effectsJson.push_back({
                { "formId", std::format("0x{:08X}", effectItem->baseEffect->GetFormID()) },
                { "known", isKnown }
            });
        }

        itemsArray.push_back({
            { "formId", std::format("0x{:08X}", ingredient->GetFormID()) },
            { "count", count },
            { "effects", std::move(effectsJson) }
        });
    }

    return itemsArray;
}

InventoryEventHandler* InventoryEventHandler::GetSingleton() {
    static InventoryEventHandler instance;
    return &instance;
}

void InventoryEventHandler::Register() {
    auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventHolder) {
        eventHolder->AddEventSink<RE::TESContainerChangedEvent>(this);
        eventHolder->AddEventSink<RE::TESEquipEvent>(this);
        SKSE::log::info("InventoryEventHandler registered for TESContainerChangedEvent & TESEquipEvent");
    } else {
        SKSE::log::warn("ScriptEventSourceHolder unavailable to register InventoryEventHandler");
    }
}

RE::BSEventNotifyControl InventoryEventHandler::ProcessEvent(
    const RE::TESContainerChangedEvent* a_event,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*) {
    if (!a_event || !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (_isLoading.load() || !Util::IsGameSessionReady()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    constexpr RE::FormID PLAYER_FORM_ID = 0x00000014;
    if (a_event->oldContainer != PLAYER_FORM_ID && a_event->newContainer != PLAYER_FORM_ID) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(a_event->baseObj);
    if (form && form->Is(RE::FormType::Ingredient)) {
        RequestInventorySync();
    }

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl InventoryEventHandler::ProcessEvent(
    const RE::TESEquipEvent* a_event,
    RE::BSTEventSource<RE::TESEquipEvent>*) {
    if (!a_event || !Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    if (_isLoading.load() || !Util::IsGameSessionReady()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* actor = a_event->actor.get();
    if (!actor || !actor->IsPlayerRef()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = RE::TESForm::LookupByID(a_event->baseObject);
    if (form && form->Is(RE::FormType::Armor)) {
        RequestAlchemyStatsSync();
    }

    return RE::BSEventNotifyControl::kContinue;
}

void InventoryEventHandler::RequestInventorySync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return;
    }

    if (!_syncPending.exchange(true)) {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                ExecuteInventorySync();
            });
        }
    }
}

void InventoryEventHandler::ExecuteInventorySync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients() || !Util::IsGameSessionReady()) {
        _syncPending.store(false);
        return;
    }

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (nowMs - _lastSyncTimeMs.load() < 150) {
        _syncPending.store(false);
        return;
    }

    _syncPending.store(false);
    _lastSyncTimeMs.store(nowMs);

    auto bin = Sync::ProtoPacketBuilder::BuildInventoryFullSnapshot();
    Server::WebSocketServer::GetSingleton().BroadcastBinary(bin);
}

void InventoryEventHandler::ExecuteAlchemyStatsSync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients() || !Util::IsGameSessionReady()) {
        _alchemySyncPending.store(false);
        return;
    }

    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (nowMs - _lastAlchemySyncTimeMs.load() < 100) {
        _alchemySyncPending.store(false);
        return;
    }

    _alchemySyncPending.store(false);
    _lastAlchemySyncTimeMs.store(nowMs);

    auto bin = Sync::ProtoPacketBuilder::BuildPlayerAlchemyStats();
    Server::WebSocketServer::GetSingleton().BroadcastBinary(bin);
}

void InventoryEventHandler::RequestAlchemyStatsSync() {
    if (!Server::WebSocketServer::GetSingleton().HasConnectedClients()) {
        return;
    }

    if (!_alchemySyncPending.exchange(true)) {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([this]() {
                ExecuteAlchemyStatsSync();
            });
        }
    }
}
} // namespace SkyPotion::Game
</file>

<file path="src/server/WebSocketServer.cpp">
#include "server/WebSocketServer.h"
#include "game/InventoryExtractor.h"
#include "game/PerkExtractor.h"
#include "game/StatExtractor.h"
#include "game/AlchemyDataExtractor.h"
#include "game/AlchemyController.h"
#include "sync/ProtoPacketBuilder.h"

#include <chrono>
#include <future>
#include <thread>

namespace SkyPotion::Server {

    WebSocketServer::WebSocketServer() = default;

    WebSocketServer::~WebSocketServer() {
        Stop();
    }

    bool WebSocketServer::Start(uint16_t port, const std::string& host) {
        if (_running.load()) {
            SKSE::log::warn("WebSocketServer already running on {}:{}", _host, _port);
            return true;
        }

        _port = port;
        _host = host;

        try {
            _server = std::make_unique<ix::WebSocketServer>(_port, _host);

            // Disable per-message deflate compression to prevent handshake failure with Android OkHttp
            // ("unexpected Sec-WebSocket-Extensions in response header")
            _server->disablePerMessageDeflate();

            _server->setOnClientMessageCallback(
                [this](std::shared_ptr<ix::ConnectionState> connectionState,
                       ix::WebSocket& webSocket,
                       const ix::WebSocketMessagePtr& msg) {
                    OnMessage(connectionState, webSocket, msg);
                }
            );

            auto res = _server->listen();
            if (!res.first) {
                SKSE::log::error("WebSocketServer failed to listen on {}:{}: {}", _host, _port, res.second);
                return false;
            }

            _server->start();
            _running.store(true);

            SKSE::log::info("SkyPotion WebSocketServer successfully started on {}:{}", _host, _port);
            return true;
        } catch (const std::exception& e) {
            SKSE::log::error("Exception in WebSocketServer::Start: {}", e.what());
            return false;
        }
    }

    void WebSocketServer::Stop() {
        if (!_running.load()) return;

        SKSE::log::info("Stopping SkyPotion WebSocketServer...");
        _clientCount.store(0, std::memory_order_relaxed);
        if (_server) {
            _server->stop();
            _server.reset();
        }
        _running.store(false);
    }

    bool WebSocketServer::IsRunning() const {
        return _running.load();
    }

    bool WebSocketServer::HasConnectedClients() const {
        return _running.load(std::memory_order_relaxed) &&
            _clientCount.load(std::memory_order_relaxed) > 0;
    }

    void WebSocketServer::Broadcast(const nlohmann::json& message) {
        if (!_running.load() || !_server) return;

        std::string jsonStr = message.dump();
        for (auto& client : _server->getClients()) {
            if (client) {
                client->sendText(jsonStr);
            }
        }
    }

    void WebSocketServer::OnMessage(std::shared_ptr<ix::ConnectionState> connectionState,
                                    ix::WebSocket& webSocket,
                                    const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            _clientCount.fetch_add(1, std::memory_order_relaxed);
            SKSE::log::info("Client connected: {} (URI: {})",
                connectionState ? connectionState->getId() : "unknown",
                msg->openInfo.uri);

            // 안드로이드 앱 접속 시 세션 스냅샷 스케줄링 (준비 대기 및 재시도 포함)
            ScheduleSessionSnapshot();
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Close) {
            SKSE::log::info("Client disconnected: {}", 
                connectionState ? connectionState->getId() : "unknown");

            int prev = _clientCount.fetch_sub(1, std::memory_order_relaxed);
            if (prev <= 1) {
                _clientCount.store(0, std::memory_order_relaxed);
                Game::AlchemyController::StopCrafting();
                Game::MenuOpenCloseHandler::StopMonitoring();
            }
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Error) {
            SKSE::log::warn("WebSocket client error: {}", msg->errorInfo.reason);

            if (!HasConnectedClients()) {
                SKSE::log::info("All clients disconnected. Halting crafting and monitoring.");
                Game::AlchemyController::StopCrafting();
                Game::MenuOpenCloseHandler::StopMonitoring();
            }
            return;
        }

        if (msg->type == ix::WebSocketMessageType::Message) {
            // 바이너리 프레임 -> Protobuf AppCommandPacket 처리
            if (msg->binary) {
                HandleProtoCommand(webSocket, msg->str);
                return;
            }

            try {
                auto root = nlohmann::json::parse(msg->str);
                std::string type = root.value("type", "");

                if (type == "ping") {
                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    nlohmann::json pong = {
                        { "type", "pong" },
                        { "ts", nowMs }
                    };
                    webSocket.sendText(pong.dump());
                    return;
                }

                if (type == "query") {
                    std::string id = root.value("id", "");
                    nlohmann::json fields = root.contains("fields") ? root["fields"] : nlohmann::json::object();

                    // Fast-path: If querying static alchemy gamedata and cache is already populated,
                    // serve it safely from the WebSocket worker thread without scheduling on Skyrim's main thread!
                    if (id == "gamedata.alchemy" || id == "gamedata.all" || id == "alchemy.database") {
                        auto cachedPtr = Game::AlchemyDataExtractor::GetCachedGameData();
                        if (cachedPtr) {
                            int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            nlohmann::json response = {
                                { "type", "data" },
                                { "id", id },
                                { "fields", *cachedPtr },
                                { "ts", nowMs }
                            };
                            webSocket.sendText(response.dump());
                            return;
                        }
                    }

                    // In-flight guard for polling queries: Drop duplicate requests if previous one is still queued
                    static std::atomic<bool> s_queryInFlight{ false };
                    if (s_queryInFlight.exchange(true)) {
                        SKSE::log::warn("Query '{}' dropped: previous query still in flight", id);
                        return;
                    }

                    // Safely query game data on the Skyrim main thread
                    auto taskInterface = SKSE::GetTaskInterface();
                    if (taskInterface) {
                        auto promise = std::make_shared<std::promise<nlohmann::json>>();
                        auto future = promise->get_future();

                        taskInterface->AddTask([promise, id, fields]() {
                            try {
                                if (SkyPotion::g_isShuttingDown.load()) {
                                    s_queryInFlight.store(false);
                                    promise->set_value(nlohmann::json::object());
                                    return;
                                }

                                if (!Util::IsGameSessionReady()) {
                                    s_queryInFlight.store(false);
                                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                    promise->set_value(nlohmann::json{
                                        { "type", "data" },
                                        { "id", id },
                                        { "fields", nlohmann::json::object() },
                                        { "status", "not_ready" },
                                        { "warning", "Game session not ready (in main menu or loading)" },
                                        { "ts", nowMs }
                                    });
                                    return;
                                }

                                auto response = ProcessQuery(id, fields);
                                s_queryInFlight.store(false);
                                promise->set_value(std::move(response));
                            } catch (const std::exception& e) {
                                s_queryInFlight.store(false);
                                SKSE::log::error("Error processing query in main thread: {}", e.what());
                                promise->set_value(nlohmann::json::object());
                            } catch (...) {
                                s_queryInFlight.store(false);
                                promise->set_value(nlohmann::json::object());
                            }
                        });

                        // Wait up to 500ms for main thread completion on network worker thread
                        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready) {
                            nlohmann::json response = future.get();
                            if (!response.empty()) {
                                webSocket.sendText(response.dump());
                            }
                        } else {
                            SKSE::log::warn("Query '{}' timed out waiting for main thread", id);
                        }
                    } else {
                        s_queryInFlight.store(false);
                        // Fallback direct execution if task interface is not ready
                        auto response = ProcessQuery(id, fields);
                        webSocket.sendText(response.dump());
                    }
                    return;
                }

                if (type == "command") {
                    std::string action = root.value("action", "");
                    nlohmann::json payload = root;

                    auto taskInterface = SKSE::GetTaskInterface();
                    if (taskInterface) {
                        auto promise = std::make_shared<std::promise<nlohmann::json>>();
                        auto future = promise->get_future();

                        taskInterface->AddTask([promise, action, payload]() {
                            try {
                                if (!Util::IsGameSessionReady()) {
                                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                    promise->set_value(nlohmann::json{
                                        { "type", "command_result" },
                                        { "action", action },
                                        { "success", false },
                                        { "message", "Game session not ready (in main menu or loading)" },
                                        { "ts", nowMs }
                                    });
                                    return;
                                }

                                auto response = ProcessCommand(action, payload);
                                promise->set_value(std::move(response));
                            } catch (const std::exception& e) {
                                SKSE::log::error("Error processing command in main thread: {}", e.what());
                                promise->set_value(nlohmann::json{
                                    { "type", "command_result" },
                                    { "action", action },
                                    { "success", false },
                                    { "message", std::string("Exception: ") + e.what() }
                                });
                            } catch (...) {
                                promise->set_value(nlohmann::json{
                                    { "type", "command_result" },
                                    { "action", action },
                                    { "success", false },
                                    { "message", "Unknown exception in main thread" }
                                });
                            }
                        });

                        // Wait up to 2000ms for main thread completion
                        if (future.wait_for(std::chrono::milliseconds(2000)) == std::future_status::ready) {
                            nlohmann::json response = future.get();
                            webSocket.sendText(response.dump());
                        } else {
                            SKSE::log::warn("Command '{}' timed out waiting for main thread", action);
                            nlohmann::json timeoutResp = {
                                { "type", "command_result" },
                                { "action", action },
                                { "success", false },
                                { "message", "Timeout waiting for main thread" }
                            };
                            webSocket.sendText(timeoutResp.dump());
                        }
                    } else {
                        auto response = ProcessCommand(action, payload);
                        webSocket.sendText(response.dump());
                    }
                    return;
                }
            } catch (const std::exception& e) {
                SKSE::log::warn("Failed to parse incoming WebSocket message: {}", e.what());
                nlohmann::json err = {
                    { "type", "error" },
                    { "message", std::string("Malformed JSON: ") + e.what() }
                };
                webSocket.sendText(err.dump());
            }
        }
    }

    nlohmann::json WebSocketServer::ProcessQuery(const std::string& id, const nlohmann::json& fields) {
        nlohmann::json respFields = nlohmann::json::object();

        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // 1. Ingredients Query
        if (id == "inventory.ingredients" || 
            (fields.is_object() && fields.value("items", "") == "Inventory::Items::Ingredients") ||
            (fields.is_object() && fields.value("ingredients", "") == "Actor::Inventory::Ingredients")) {
            respFields["items"] = Game::InventoryExtractor::GetIngredientsJson();
        }
        // 3. Alchemy Stats Query
        else if (id == "skypotion.alchemy" || 
                 (fields.is_object() && (fields.contains("alchemy") || fields.contains("alchemyBase")))) {
            respFields = Game::StatExtractor::GetAlchemyStatsJson();
        }
        // 4. Perks Query (Player perks + Alchemist perk rank & summary)
        else if (id == "player.perks" || id == "skypotion.perks" || 
                 (fields.is_object() && (fields.contains("perks") || fields.value("perks", "") == "Player::Perks"))) {
            respFields["perks"] = Game::PerkExtractor::GetAllPerksJson();
            respFields["alchemyPerks"] = Game::PerkExtractor::GetAlchemyPerksSummary();
        }
        // 5. Complete Snapshot Query
        else if (id == "skypotion.all") {
            respFields["ingredients"] = Game::InventoryExtractor::GetIngredientsJson();
            respFields["alchemy"] = Game::StatExtractor::GetAlchemyStatsJson();
            respFields["alchemyPerks"] = Game::PerkExtractor::GetAlchemyPerksSummary();
            respFields["stats"] = Game::StatExtractor::GetPlayerStatsJson();
        }
        // 6. Generic/Actor Stats Query
        else if (id == "character.stats") {
            respFields = Game::StatExtractor::GetPlayerStatsJson();
        }
        // 7. Master Game Data Query (Alchemy BaseEffects + Playable Ingredients)
        else if (id == "gamedata.alchemy" || id == "gamedata.all" || id == "alchemy.database") {
            respFields = Game::AlchemyDataExtractor::GetAlchemyGameDataJson();
        }
        // 8. Alchemy Workbench Status Query
        else if (id == "alchemy.status" || id == "alchemy.is_open") {
            respFields["isOpen"] = Game::AlchemyController::IsAlchemyItemListReady();
        }
        else {
            respFields["warning"] = "Unrecognized query id";
            respFields["requestedId"] = id;
        }

        return {
            { "type", "data" },
            { "id", id },
            { "fields", respFields },
            { "ts", nowMs }
        };
    }

    nlohmann::json WebSocketServer::ProcessCommand(const std::string& action, const nlohmann::json& payload) {
        int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (action == "alchemy.select") {
            std::vector<std::string> ingredients;
            if (payload.contains("ingredients") && payload["ingredients"].is_array()) {
                for (const auto& item : payload["ingredients"]) {
                    if (item.is_string()) {
                        ingredients.push_back(item.get<std::string>());
                    } else if (item.is_number_integer()) {
                        ingredients.push_back(std::format("0x{:08X}", item.get<uint32_t>()));
                    }
                }
            }
            bool craft = payload.value("craft", false);
            int count = payload.value("count", craft ? 1 : 0);
            auto result = Game::AlchemyController::SelectIngredients(ingredients, craft, count);

            nlohmann::json resp = result.ToJson();
            resp["type"] = "command_result";
            resp["action"] = action;
            resp["ts"] = nowMs;
            return resp;
        }
        else if (action == "alchemy.craft") {
            int count = payload.value("count", 1);
            auto result = Game::AlchemyController::CraftPotions(count);
            nlohmann::json resp = result.ToJson();
            resp["type"] = "command_result";
            resp["action"] = action;
            resp["ts"] = nowMs;
            return resp;
        }
        else if (action == "alchemy.stop_craft") {
            bool ok = Game::AlchemyController::StopCrafting();
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", ok },
                { "message", ok ? "Stopped crafting" : "No active craft to stop" },
                { "ts", nowMs }
            };
        }
        else if (action == "alchemy.dismiss_popups") {
            auto* taskInterface = SKSE::GetTaskInterface();
            if (taskInterface) {
                taskInterface->AddTask([]() {
                    Game::AlchemyController::DismissOpenMessageBoxes();
                });
            }
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", true },
                { "message", "Dismissed open popups" },
                { "ts", nowMs }
            };
        }
        else if (action == "alchemy.clear") {
            bool ok = Game::AlchemyController::ClearSelectedIngredients();
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", ok },
                { "message", ok ? "Cleared ingredient selections" : "Failed to clear (alchemy workbench not open?)" },
                { "ts", nowMs }
            };
        }
        else if (action == "alchemy.status") {
            bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();
            return {
                { "type", "command_result" },
                { "action", action },
                { "success", true },
                { "isOpen", isOpen },
                { "ts", nowMs }
            };
        }

        return {
            { "type", "command_result" },
            { "action", action },
            { "success", false },
            { "message", std::format("Unrecognized command action '{}'", action) },
            { "ts", nowMs }
        };
    }

    void WebSocketServer::BroadcastFullSnapshot() {
        if (!HasConnectedClients()) return;

        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_server || !HasConnectedClients()) return;

                    if (!Util::IsGameSessionReady()) {
                        SKSE::log::info("BroadcastFullSnapshot: Game session not ready, deferring");
                        return;
                    }

                    auto charStats = Game::StatExtractor::GetPlayerStatsJson();
                    auto alchemyStats = Game::StatExtractor::GetAlchemyStatsJson();
                    auto allPerks = Game::PerkExtractor::GetAllPerksJson();
                    auto alchemyPerks = Game::PerkExtractor::GetAlchemyPerksSummary();
                    auto ingredients = Game::InventoryExtractor::GetIngredientsJson();
                    bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();

                    if (isOpen) {
                        Game::MenuOpenCloseHandler::StartMonitoring();
                    }

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 1. character.stats (includes name and language)
                    Broadcast({
                        { "type", "data" },
                        { "id", "character.stats" },
                        { "fields", std::move(charStats) },
                        { "ts", nowMs }
                    });

                    // 2. skypotion.alchemy
                    Broadcast({
                        { "type", "data" },
                        { "id", "skypotion.alchemy" },
                        { "fields", std::move(alchemyStats) },
                        { "ts", nowMs }
                    });

                    // 3. player.perks
                    Broadcast({
                        { "type", "data" },
                        { "id", "player.perks" },
                        { "fields", {
                            { "perks", std::move(allPerks) },
                            { "alchemyPerks", std::move(alchemyPerks) }
                        }},
                        { "ts", nowMs }
                    });

                    // 4. inventory.ingredients
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.ingredients" },
                        { "fields", {
                            { "items", std::move(ingredients) }
                        }},
                        { "ts", nowMs }
                    });

                    // 6. alchemy.menu event
                    Broadcast({
                        { "type", "event" },
                        { "event", "alchemy.menu" },
                        { "isOpen", isOpen },
                        { "ts", nowMs }
                    });

                    SKSE::log::info("BroadcastFullSnapshot: broadcasted full game snapshot to clients (alchemy isOpen={})", isOpen);
                } catch (const std::exception& e) {
                    SKSE::log::error("Error in BroadcastFullSnapshot: {}", e.what());
                } catch (...) {
                    SKSE::log::error("Unknown exception in BroadcastFullSnapshot");
                }
            });
        }
    }

    void WebSocketServer::BroadcastGameData() {
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_server) return;

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    auto gameData = Game::AlchemyDataExtractor::GetAlchemyGameDataJson();
                    Broadcast({
                        { "type", "data" },
                        { "id", "gamedata.alchemy" },
                        { "fields", std::move(gameData) },
                        { "ts", nowMs }
                    });

                    SKSE::log::info("BroadcastGameData: broadcasted gamedata.alchemy to clients");
                } catch (const std::exception& e) {
                    SKSE::log::error("Error in BroadcastGameData: {}", e.what());
                } catch (...) {
                    SKSE::log::error("Unknown exception in BroadcastGameData");
                }
            });
        }
    }

    void WebSocketServer::HandleProtoCommand(ix::WebSocket& webSocket, const std::string& rawBytes) {
        Sync::pb::AppCommandPacket cmd;
        if (!cmd.ParseFromString(rawBytes)) {
            SKSE::log::warn("HandleProtoCommand: failed to parse AppCommandPacket ({} bytes)", rawBytes.size());
            return;
        }

        const uint64_t reqId = cmd.request_id();
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            webSocket.sendBinary(Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "NO_TASK_INTERFACE"));
            return;
        }

        // 1) 재료 선택 동기화 (Fire-and-Forget)
        if (cmd.has_select_ingredients()) {
            std::vector<RE::FormID> formIds;
            formIds.reserve(cmd.select_ingredients().form_ids_size());
            for (uint32_t id : cmd.select_ingredients().form_ids()) {
                formIds.push_back(static_cast<RE::FormID>(id));
            }

            taskInterface->AddTask([formIds = std::move(formIds)]() {
                if (!Util::IsGameSessionReady() || !Game::AlchemyController::IsAlchemyMenuOpen()) {
                    return;
                }
                if (formIds.empty()) {
                    Game::AlchemyController::ClearSelectedIngredients();
                } else {
                    Game::AlchemyController::SelectIngredients(formIds, /*autoCraft=*/false, /*craftCount=*/0);
                }
            });
            return;
        }

        // 2) 포션 제조 요청 (응답 대기 -> CommandResult 반환)
        if (cmd.has_craft_potion()) {
            std::vector<RE::FormID> formIds;
            formIds.reserve(cmd.craft_potion().form_ids_size());
            for (uint32_t id : cmd.craft_potion().form_ids()) {
                formIds.push_back(static_cast<RE::FormID>(id));
            }
            const int32_t count = cmd.craft_potion().count();

            auto promise = std::make_shared<std::promise<std::string>>();
            auto future = promise->get_future();

            taskInterface->AddTask([this, promise, reqId, formIds = std::move(formIds), count]() {
                if (!Util::IsGameSessionReady() || !Game::AlchemyController::IsAlchemyMenuOpen()) {
                    promise->set_value(
                        Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "BENCH_CLOSED")
                    );
                    return;
                }

                auto result = Game::AlchemyController::SelectIngredients(formIds, /*autoCraft=*/true, count);
                std::string errorCode = result.success ? "" : (result.message.empty() ? "CRAFT_FAILED" : result.message);

                promise->set_value(
                    Sync::ProtoPacketBuilder::BuildCommandResult(reqId, result.success, result.craftedCount, errorCode)
                );

                // 제조 성공 시 스킬 레벨업 / 신규 효과 해금 동기화
                if (result.success) {
                    BroadcastBinary(Sync::ProtoPacketBuilder::BuildDiscoveredEffects(formIds));
                    Game::InventoryEventHandler::GetSingleton()->RequestAlchemyStatsSync();
                }
            });

            if (future.wait_for(std::chrono::milliseconds(2500)) == std::future_status::ready) {
                webSocket.sendBinary(future.get());
            } else {
                SKSE::log::warn("HandleProtoCommand: craft_potion (reqId={}) timed out", reqId);
                webSocket.sendBinary(Sync::ProtoPacketBuilder::BuildCommandResult(reqId, false, 0, "TIMEOUT"));
            }
        }
    }

    void WebSocketServer::BroadcastGameSessionLoaded() {
        auto taskInterface = SKSE::GetTaskInterface();
        if (taskInterface) {
            taskInterface->AddTask([this]() {
                try {
                    if (!_running.load() || !_server) return;

                    if (!Util::IsGameSessionReady()) {
                        SKSE::log::info("BroadcastGameSessionLoaded: Game session not ready, deferring until it closes");
                        return;
                    }

                    // Pre-warm static gamedata cache on main thread if not already done
                    if (!Game::AlchemyDataExtractor::HasCache()) {
                        Game::AlchemyDataExtractor::GetAlchemyGameDataJson();
                    }

                    // Fast read of dynamic player pointers on main thread (< 1ms)
                    SKSE::log::info("[TRACE] Snapshot 1: GetPlayerStatsJson");
                    auto charStats = Game::StatExtractor::GetPlayerStatsJson();
                    SKSE::log::info("[TRACE] Snapshot 2: GetAlchemyStatsJson");
                    auto alchemyStats = Game::StatExtractor::GetAlchemyStatsJson();
                    SKSE::log::info("[TRACE] Snapshot 3: GetAllPerksJson & Summary");
                    auto allPerks = Game::PerkExtractor::GetAllPerksJson();
                    auto alchemyPerks = Game::PerkExtractor::GetAlchemyPerksSummary();
                    SKSE::log::info("[TRACE] Snapshot 4: GetIngredientsJson");
                    auto ingredients = Game::InventoryExtractor::GetIngredientsJson();
                    SKSE::log::info("[TRACE] Snapshot 5: IsAlchemyItemListReady");
                    bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();

                    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    // 1. game.loaded event
                    Broadcast({
                        { "type", "event" },
                        { "event", "game.loaded" },
                        { "ts", nowMs }
                    });

                    // 2. gamedata.alchemy (served from memory cache)
                    Broadcast({
                        { "type", "data" },
                        { "id", "gamedata.alchemy" },
                        { "fields", Game::AlchemyDataExtractor::GetAlchemyGameDataJson() },
                        { "ts", nowMs }
                    });

                    // 3. character.stats (includes name, level, language)
                    Broadcast({
                        { "type", "data" },
                        { "id", "character.stats" },
                        { "fields", std::move(charStats) },
                        { "ts", nowMs }
                    });

                    // 4. skypotion.alchemy (skill level)
                    Broadcast({
                        { "type", "data" },
                        { "id", "skypotion.alchemy" },
                        { "fields", std::move(alchemyStats) },
                        { "ts", nowMs }
                    });

                    // 5. player.perks
                    Broadcast({
                        { "type", "data" },
                        { "id", "player.perks" },
                        { "fields", {
                            { "perks", std::move(allPerks) },
                            { "alchemyPerks", std::move(alchemyPerks) }
                        }},
                        { "ts", nowMs }
                    });

                    // 6. inventory.ingredients
                    Broadcast({
                        { "type", "data" },
                        { "id", "inventory.ingredients" },
                        { "fields", {
                            { "items", std::move(ingredients) }
                        }},
                        { "ts", nowMs }
                    });

                    // 8. alchemy.menu event
                    Broadcast({
                        { "type", "event" },
                        { "event", "alchemy.menu" },
                        { "isOpen", isOpen },
                        { "ts", nowMs }
                    });

                    SKSE::log::info("BroadcastGameSessionLoaded: broadcasted master game data and player snapshot to clients (alchemy isOpen={})", isOpen);
                } catch (const std::exception& e) {
                    SKSE::log::error("Error in BroadcastGameSessionLoaded: {}", e.what());
                } catch (...) {
                    SKSE::log::error("Unknown exception in BroadcastGameSessionLoaded");
                }
            });
        }
    }

    void WebSocketServer::BroadcastBinary(const std::string& binaryPayload) {
    if (!_running.load() || !_server || binaryPayload.empty()) return;

    for (auto& client : _server->getClients()) {
        if (client) {
            client->sendBinary(binaryPayload);
        }
    }
}

void WebSocketServer::BroadcastProtoSnapshot() {
    if (!HasConnectedClients()) return;

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) return;

    taskInterface->AddTask([this]() {
        try {
            if (!_running.load() || !_server || !HasConnectedClients()) return;
            if (!Util::IsGameSessionReady()) {
                SKSE::log::info("BroadcastProtoSnapshot: Game session not ready");
                return;
            }

            // 1) 마스터 카탈로그 (CatalogSnapshot: 캐릭터명, 재료 마스터, MGEF 마스터, 초기 스탯 등)
            std::string catalogBin = Sync::ProtoPacketBuilder::BuildCatalogSnapshot();
            if (!catalogBin.empty()) {
                BroadcastBinary(catalogBin);
            }

            // 2) 실시간 플레이어 스탯 & 퍽 (PlayerCraftingStats, Tag 12)
            // 안드로이드 앱의 스킬/스탯 관찰 리스너(Flow)가 즉시 갱신되도록 명시적 전송
            std::string statsBin = Sync::ProtoPacketBuilder::BuildPlayerAlchemyStats();
            if (!statsBin.empty()) {
                BroadcastBinary(statsBin);
            }

            // 3) 실시간 인벤토리 재료 수량 (InventoryUpdate, Tag 11)
            std::string invBin = Sync::ProtoPacketBuilder::BuildInventoryFullSnapshot();
            if (!invBin.empty()) {
                BroadcastBinary(invBin);
            }

            // 4) 현재 연금술대 열림 상태 및 선택 재료 전송 (BenchState, Tag 15)
            bool isOpen = Game::AlchemyController::IsAlchemyItemListReady();
            std::vector<RE::FormID> selected = isOpen
                ? Game::AlchemyController::GetSelectedIngredientFormIDs()
                : std::vector<RE::FormID>{};

            if (isOpen) {
                Game::MenuOpenCloseHandler::StartMonitoring();
            }

            BroadcastBinary(Sync::ProtoPacketBuilder::BuildAlchemyBenchState(isOpen, selected));
            SKSE::log::info("BroadcastProtoSnapshot: sent CatalogSnapshot, PlayerStats, InventorySnapshot, BenchState (isOpen={})", isOpen);
        } catch (const std::exception& e) {
            SKSE::log::error("Error in BroadcastProtoSnapshot: {}", e.what());
        }
    });
}

void WebSocketServer::ScheduleSessionSnapshot(int attempt) {
    if (!HasConnectedClients()) return;

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) return;

    taskInterface->AddTask([this, attempt]() {
        if (!_running.load() || !_server || !HasConnectedClients()) return;

        if (!Util::IsGameSessionReady()) {
            if (attempt < 20) {
                // 게임 세션 준비(LoadingMenu 닫힘, 3D 로드 등) 대기 후 재시도 (100ms 간격, 최대 2초)
                std::thread([this, attempt]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    ScheduleSessionSnapshot(attempt + 1);
                }).detach();
            } else {
                SKSE::log::warn("ScheduleSessionSnapshot: Timed out waiting for game session (attempt={})", attempt);
            }
            return;
        }

        SKSE::log::info("ScheduleSessionSnapshot: Game session ready (attempt={}), broadcasting snapshots...", attempt);
        BroadcastProtoSnapshot();
        BroadcastGameSessionLoaded(); // JSON 클라이언트 동시 지원
    });
}

}
</file>

</files>
