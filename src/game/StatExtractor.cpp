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
            { "alchemy", Util::Round2(currentSkill) },
            { "alchemyBase", Util::Round2(baseSkill) }
        };
    }

    nlohmann::json StatExtractor::GetPlayerStatsJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nlohmann::json::object();

        auto avOwner = player->AsActorValueOwner();
        if (!avOwner) return nlohmann::json::object();

        return {
            { "level", player->GetLevel() },
            { "health", Util::Round2(avOwner->GetActorValue(RE::ActorValue::kHealth)) },
            { "healthBase", Util::Round2(avOwner->GetBaseActorValue(RE::ActorValue::kHealth)) },
            { "magicka", Util::Round2(avOwner->GetActorValue(RE::ActorValue::kMagicka)) },
            { "magickaBase", Util::Round2(avOwner->GetBaseActorValue(RE::ActorValue::kMagicka)) },
            { "stamina", Util::Round2(avOwner->GetActorValue(RE::ActorValue::kStamina)) },
            { "staminaBase", Util::Round2(avOwner->GetBaseActorValue(RE::ActorValue::kStamina)) },
            { "inventoryWeight", Util::Round2(player->GetWeightInContainer()) }
        };
    }
}
