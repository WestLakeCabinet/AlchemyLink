# SkyPotion SKSE Plugin (SkyPotionSKSE)

A lightweight WebSocket companion plugin for **Skyrim SE / AE / GOG**.  
It communicates with the **SkyPotion Android App** over binary WebSocket frames using **Protocol Buffers (`proto/game_sync.proto`)** to synchronize player profile, alchemy ingredients, perks, stats, and workbench state in real time.

---

## 🌟 Features

1. **Built-in WebSocket Server (Default Port: `8765`)**:
   - Automatically starts a background WebSocket server when Skyrim launches—no external helper programs required.
   - Uses compact binary **Protobuf** serialization (`GameSyncPacket` / `AppCommandPacket`).
2. **Player Profile & Master Data Snapshot (`CatalogSnapshot`)**:
   - Character name, level, race, gold amount, and game language.
   - All valid in-game ingredients (`INGR`) and their referenced `BaseEffect` (`MGEF`) definitions.
3. **Real-Time Inventory & Crafting Stats (`InventoryUpdate` / `PlayerCraftingStats`)**:
   - Live ingredient stock counts and discovered effect masks (`discovered_mask`).
   - Alchemy skill level, Fortify Alchemy equipment/buff total, and perks (`Alchemist 1–5`, `Physician`, `Benefactor`, `Poisoner`, `Purity`).
   - Automatic updates when ingredients, gold, or equipped armor change.
4. **Alchemy Workbench State & Remote Control**:
   - Tracks workbench open/close state and currently selected ingredients (`CraftingBenchState`).
   - Supports remote ingredient selection (`SelectIngredientsCommand`) and potion crafting (`CraftPotionCommand`) with structured `CommandResult` responses.
5. **Main-Thread Safety**:
   - Dispatches game-state reads and crafting actions through SKSE `TaskInterface` to prevent CTDs.

---

## 📡 WebSocket Protocol (Protobuf)

All messages are sent as **binary WebSocket frames** defined in [`proto/game_sync.proto`](proto/game_sync.proto) (`package com.dolzzo.skypotion.proto.sync`).

### `[Game -> App]` `GameSyncPacket`
| Payload Field | Tag | Description |
| :--- | :---: | :--- |
| `catalog_snapshot` | `10` | Initial master data (session, character name/level/race/gold, language, `BaseEffectDef`, `IngredientDef`, `initial_stats`) |
| `inventory_update` | `11` | Real-time ingredient stock snapshot (`is_full_snapshot`, `items`) |
| `player_stats` | `12` | Real-time `PlayerCraftingStats` (profile, alchemy skill, fortify bonus, perks) |
| `discovered` | `13` | `DiscoveredEffectsUpdate` (`form_id -> 4-bit mask`) |
| `heartbeat` | `14` | Connection keep-alive ping/pong (`seq`) |
| `bench_state` | `15` | `CraftingBenchState` (workbench open/closed and selected ingredient FormIDs) |
| `command_result` | `16` | `CommandResult` (`request_id`, `success`, `crafted_count`, `error_code`) |

### `[App -> Game]` `AppCommandPacket`
| Command Field | Tag | Description |
| :--- | :---: | :--- |
| `select_ingredients` | `10` | `SelectIngredientsCommand` — select up to 3 ingredient FormIDs at the alchemy bench |
| `craft_potion` | `11` | `CraftPotionCommand` — craft `count` potions using the specified ingredient FormIDs |

---

## 🛠️ How to Build

### Option 1: GitHub Actions (Recommended)
The repository includes [`.github/workflows/build.yml`](.github/workflows/build.yml).  
Pushing to `main` or `master` (or triggering **Run workflow** manually) automatically builds `SkyPotionSKSE.dll` on `windows-latest` and uploads it as the `SkyPotionSKSE-Release` artifact.

### Option 2: One-Click Local Build Script (`build.ps1`)
Automatically detects Visual Studio, CMake, and vcpkg, builds the Release DLL, and copies it to your Skyrim plugins folder:
```powershell
.\build.ps1

# Optional: specify a custom Skyrim Plugins directory
.\build.ps1 -skyrimPluginDir "D:\Steam\steamapps\common\Skyrim Special Edition\Data\SKSE\Plugins"
```

### Option 3: CMake + vcpkg (Manifest Mode)
```powershell
cmake -B build -S . `
  -DBUILD_TESTS=OFF `
  -DCMAKE_TOOLCHAIN_FILE="<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md

cmake --build build --config Release --target SkyPotionSKSE --parallel
```

### Option 4: XMake
```powershell
xmake f -m release
xmake
```

---

## 📦 Installation

1. Copy the built `SkyPotionSKSE.dll` to:
   ```text
   <Skyrim Special Edition>/Data/SKSE/Plugins/SkyPotionSKSE.dll
   ```
2. Required dependencies:
   - **SKSE64** (Skyrim Script Extender)
   - **Address Library for SKSE Plugins** (matching your Skyrim version)
3. Launch Skyrim via SKSE64. The WebSocket server will listen on port `8765` by default and accept connections from the SkyPotion Android app.
