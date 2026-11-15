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
            { "alchemy", currentSkill },
            { "alchemyBase", baseSkill }
        };
    }

    nlohmann::json StatExtractor::GetPlayerStatsJson() {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nlohmann::json::object();

        auto avOwner = player->AsActorValueOwner();
        if (!avOwner) return nlohmann::json::object();

        return {
            { "level", player->GetLevel() },
            { "health", avOwner->GetActorValue(RE::ActorValue::kHealth) },
            { "healthBase", avOwner->GetBaseActorValue(RE::ActorValue::kHealth) },
            { "magicka", avOwner->GetActorValue(RE::ActorValue::kMagicka) },
            { "magickaBase", avOwner->GetBaseActorValue(RE::ActorValue::kMagicka) },
            { "stamina", avOwner->GetActorValue(RE::ActorValue::kStamina) },
            { "staminaBase", avOwner->GetBaseActorValue(RE::ActorValue::kStamina) },
            { "inventoryWeight", player->GetWeightInContainer() }
        };
    }
}
