#include "game/StatExtractor.h"

namespace SkyPotion::Game {

namespace {
    double GetTotalFortifyAlchemy(RE::PlayerCharacter* player) {
        if (!player) return 0.0;

        double totalBonus = 0.0;
        auto* magicTarget = player->AsMagicTarget();
        if (!magicTarget) return 0.0;

        auto* activeEffects = magicTarget->GetActiveEffectList();
        if (!activeEffects) return 0.0;

        for (auto* ae : *activeEffects) {
            if (!ae || ae->flags.any(RE::ActiveEffect::Flag::kInactive)) {
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