#include "game/StatExtractor.h"

namespace SkyPotion::Game {

    nlohmann::json StatExtractor::GetAlchemyStatsJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nlohmann::json::object();

        auto avOwner = player->AsActorValueOwner();
        if (!avOwner) return nlohmann::json::object();

        double currentSkill = avOwner->GetActorValue(RE::ActorValue::kAlchemy);
        double baseSkill = avOwner->GetBaseActorValue(RE::ActorValue::kAlchemy);

        return {
            { "alchemy", Util::Round3(currentSkill) },
            { "alchemyBase", Util::Round3(baseSkill) }
        };
    }

    nlohmann::json StatExtractor::GetPlayerStatsJson() {
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
}
