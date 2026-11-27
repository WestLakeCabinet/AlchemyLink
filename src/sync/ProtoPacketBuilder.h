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

    // 5. 작업대(연금술대/마법부여대 등) 열림/닫힘 및 선택 상태
    static std::string BuildCraftingBenchState(
        pb::BenchType benchType,
        bool isOpen,
        const std::vector<RE::FormID>& selectedFormIds = {}
    );
    static std::string BuildAlchemyBenchState(bool isOpen, const std::vector<RE::FormID>& selectedFormIds);

    // 6. 앱 명령 실행 결과 응답
    static std::string BuildCommandResult(
        uint64_t requestId,
        bool success,
        int32_t craftedCount = 0,
        const std::string& errorCode = "",
        const std::vector<RE::FormID>& missingFormIds = {}
    );

private:
    static int64_t NowMs();
    static void FillPlayerAlchemyStats(pb::PlayerCraftingStats* out);
    static float GetTotalFortifyAlchemy(RE::PlayerCharacter* player);
    static std::string ResolvePlayerName(RE::PlayerCharacter* player);
    static std::string ResolvePlayerRace(RE::PlayerCharacter* player);
    static std::string ResolveGameLanguage();
};

} // namespace SkyPotion::Sync