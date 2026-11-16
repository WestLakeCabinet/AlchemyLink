# SkyPotion SKSE Plugin (SkyPotionSKSE)

Skyrim SE / AE / GOG 전용 경량 WebSocket 컴패니언 플러그인입니다.  
**SkyPotion 안드로이드 앱**과 실시간 통신하여 게임 내 재료, 장착 장비, 연금술 스킬, 그리고 **플레이어 퍽(Perk, Alchemist 1~5 / Physician / Benefactor 등)** 정보를 실시간으로 양방향 동기화합니다.

---

## 🌟 주요 기능 (Features)

1. **내장 WebSocket 서버 (기본 포트 `8765`)**:
   - 외부 프로그램이나 모드 매니저 없이 스카이림 게임 실행 시 자동으로 백그라운드 소켓 서버 구동
2. **연금술 퍽(Perk) 완벽 추출**:
   - `Alchemist (1~5)` 랭크
   - `Physician`, `Benefactor`, `Poisoner`, `Purity`, `Green Thumb`, `Snakeblood`
3. **가방(Inventory) & 장비(Apparel) 실시간 동기화**:
   - 보유 재료 수량 및 플레이어가 해금한 효과(`isKnown`) 정보
   - 연금술 강화(Fortify Alchemy) 인챈트 장비 및 실시간 착용 여부(`isEquipped`)
4. **연금술 스킬 수치**:
   - 현재 연금술 스킬치(`ActorValue::kAlchemy`) 및 기본값(`Base`)
5. **완벽한 메인 스레드 안전성**:
   - SKSE `TaskInterface`를 통한 비동기 쿼리 디스패칭으로 CTD 없는 안전한 데이터 추출

---

## 📡 WebSocket 프로토콜 (Query API)

웹소켓 클라이언트(안드로이드 앱)에서 아래와 같은 JSON 쿼리를 전송하면 게임 데이터를 즉시 응답합니다:

### 1. 연금술 퍽 조회 (`player.perks`)
- **요청 (Request)**:
  ```json
  { "type": "query", "id": "player.perks" }
  ```
- **응답 (Response)**:
  ```json
  {
    "type": "data",
    "id": "player.perks",
    "fields": {
      "alchemyPerks": {
        "alchemist": 4,
        "physician": true,
        "benefactor": true,
        "poisoner": false,
        "purity": false,
        "experimenter": 2,
        "greenThumb": false,
        "snakeblood": false
      },
      "perks": [
        { "formId": "0x000C07CC", "name": "Alchemist", "rank": 4 }
      ]
    },
    "ts": 1794687968031
  }
  ```

### 2. 연금술 스킬 수치 (`skypotion.alchemy`)
- **요청**:
  ```json
  { "type": "query", "id": "skypotion.alchemy" }
  ```
- **응답**:
  ```json
  {
    "type": "data",
    "id": "skypotion.alchemy",
    "fields": {
      "alchemy": 83.0,
      "alchemyBase": 83.0
    },
    "ts": 1794687968031
  }
  ```

### 3. 재료 목록 (`inventory.ingredients`)
- **요청**:
  ```json
  { "type": "query", "id": "inventory.ingredients" }
  ```
- **응답**:
  ```json
  {
    "type": "data",
    "id": "inventory.ingredients",
    "fields": {
      "items": [
        {
          "formId": "0x0001B3BD",
          "name": "Snowberries",
          "count": 14,
          "effects": [
            { "name": "Resist Fire", "known": true },
            { "name": "Fortify Enchanting", "known": true }
          ]
        }
      ]
    },
    "ts": 1794687968031
  }
  ```

### 4. 연금술 장비 (`inventory.apparel`)
- **요청**:
  ```json
  { "type": "query", "id": "inventory.apparel" }
  ```
- **응답**:
  ```json
  {
    "type": "data",
    "id": "inventory.apparel",
    "fields": {
      "items": [
        {
          "formId": "0x0008B65C",
          "name": "Ring of Pure Mixtures",
          "count": 1,
          "isEquipped": true,
          "enchantment": {
            "effects": [
              { "name": "Fortify Alchemy", "magnitude": 17.0 }
            ]
          }
        }
      ]
    },
    "ts": 1794687968031
  }
  ```

### 5. 연금술 마스터 게임 데이터 (`gamedata.alchemy`)
- **설명**: 게임 내 모든 유효한 연금술 재료(`INGR`)와, 해당 재료에 바인딩된 고유 `BaseEffect(MGEF)`만 화이트리스트로 추출하여 반환합니다. (레시피 엔진 제약조건: baseEffect <= 127, MagicEffect <= 512 완벽 준수)
- **요청**:
  ```json
  { "type": "query", "id": "gamedata.alchemy" }
  ```
- **응답**:
  ```json
  {
    "type": "data",
    "id": "gamedata.alchemy",
    "fields": {
      "baseEffectCount": 60,
      "ingredientCount": 115,
      "baseEffects": [
        {
          "formId": "0x0003EB42",
          "editorId": "AlchDamageHealth",
          "name": "Damage Health",
          "baseCost": 3.0,
          "noMag": false,
          "noDur": false,
          "affectMag": true,
          "affectDur": false,
          "isHostile": true,
          "isPoison": true,
          "isBeneficial": false,
          "isHarmful": true,
          "isPhysician": false,
          "keywords": "MagicAlchHarmful",
          "description": "Causes <mag> points of poison damage."
        }
      ],
      "ingredients": [
        {
          "formId": "0x000134AA",
          "name": "Thistle Branch",
          "value": 1,
          "weight": 0.1,
          "ingredientValue": 23,
          "effectValue": 23,
          "origin": "Skyrim.esm",
          "fileName": "Skyrim.esm",
          "defaultStock": 1,
          "slots": [
            { "formId": "0x0003EAEB", "magnitude": 3.0, "duration": 60 },
            { "formId": "0x00073F23", "magnitude": 2.0, "duration": 10 },
            { "formId": "0x00090041", "magnitude": 3.0, "duration": 0 },
            { "formId": "0x0003EB1E", "magnitude": 2.0, "duration": 60 }
          ]
        }
      ]
    },
    "ts": 1794687968031
  }
  ```

---

## 🛠️ 빌드 방법 (How to Build)

### 방법 1: GitHub Actions (가장 쉽고 추천하는 방법)
저장소에 `.github/workflows/build.yml`이 포함되어 있으므로, GitHub 저장소로 푸시하면 GitHub의 클라우드 머신에서 자동으로 `SkyPotionSKSE.dll`을 빌드하여 **Artifacts** 탭에서 다운로드할 수 있습니다.

### 방법 2: 원클릭 자동 빌드 및 배포 스크립트 (`build.ps1`, 가장 편리 ⭐)
Visual Studio 경로, CMake, vcpkg를 자동으로 감지하여 빌드하고 스카이림 플러그인 폴더로 자동 복사합니다:
```powershell
.\build.ps1

# (선택) 복사 대상 스카이림 경로 변경 시:
.\build.ps1 -skyrimPluginDir "D:\Steam\steamapps\common\Skyrim Special Edition\Data\SKSE\Plugins"
```

### 방법 3: CMake + vcpkg 직접 빌드
프로젝트 루트에 `vcpkg.json` 파일이 있으므로, vcpkg는 **매니페스트 모드(Manifest Mode)**로 동작합니다.
CMake가 기본 환경변수에 등록되어 있지 않다면 아래 명령으로 경로를 추가하거나 Visual Studio 내장 CMake를 직접 호출합니다:

```powershell
# 현재 PowerShell 세션에 Visual Studio CMake 경로 일시 추가
$env:Path += ";D:\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

# 1. CMake 구성 (vcpkg 의존성 자동 연동, /MD 런타임 매칭)
cmake -B build -S . `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTS=OFF `
  -DCMAKE_TOOLCHAIN_FILE="D:/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md

# 2. DLL 빌드
cmake --build build --config Release
```

### 방법 3: XMake (간편 로컬 빌드)
```powershell
xmake f -m release
xmake
```

---

## 📦 게임 설치 (Installation)

1. 빌드된 `SkyPotionSKSE.dll` 파일을 아래 경로에 복사합니다:
   ```
   <Skyrim Special Edition 설치 경로>/Data/SKSE/Plugins/SkyPotionSKSE.dll
   ```
2. 필수 선행 모드:
   - **SKSE64** (Skyrim Script Extender)
   - **Address Library for SKSE Plugins** (Nexus Mods에서 본인 스카이림 버전에 맞게 다운로드)
3. 게임을 SKSE로 실행하면 기본 포트 `8765`에서 대기하며, SkyPotion 안드로이드 앱의 `Skyrim Monitor` 탭에서 연결할 수 있습니다!
