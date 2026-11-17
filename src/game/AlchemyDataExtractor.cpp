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

            bool isPhysician = nameStr.find("Restore") != std::string::npos || nameStr.find("restore") != std::string::npos;
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
