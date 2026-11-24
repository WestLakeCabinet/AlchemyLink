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

/*
double ProtoPacketBuilder::GetTotalFortifyAlchemy(RE::PlayerCharacter* player) {
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

        const auto arch = mgef->GetArchetype();
        if ((arch == RE::EffectSetting::Archetype::kPeakValueModifier ||
             arch == RE::EffectSetting::Archetype::kValueModifier) &&
            mgef->GetMagickSkill() == RE::ActorValue::kAlchemy) {
            totalBonus += ae->magnitude;
        }
    }
    return totalBonus;
}
*/

double ProtoPacketBuilder::GetTotalFortifyAlchemy(RE::PlayerCharacter* player) {
    if (!player || !Util::IsGameSessionReady()) return 0.0;
    auto* avOwner = player->AsActorValueOwner();
    if (!avOwner) return 0.0;
                     //env.alchemyModifierPct = avo->GetActorValue(RE::ActorValue::kAlchemyModifier);
                     //env.alchemyPowerModifierPct = avo->GetActorValue(RE::ActorValue::kAlchemyPowerModifier);
    return static_cast<double>(avOwner->GetActorValue(RE::ActorValue::kAlchemyModifier));
}

void ProtoPacketBuilder::FillPlayerAlchemyStats(pb::PlayerCraftingStats* out) {
    if (!out || !Util::IsGameSessionReady()) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    out->set_character_name(ResolvePlayerName(player));

    if (auto* avOwner = player->AsActorValueOwner()) {
        out->set_skill_level(static_cast<int32_t>(std::round(avOwner->GetActorValue(RE::ActorValue::kAlchemy))));
        out->set_fortify_alchemy_total(static_cast<int32_t>(std::round(avOwner->GetActorValue(RE::ActorValue::kAlchemyModifier))));
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