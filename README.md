# AlchemyLink

A lightweight WebSocket companion SKSE plugin for **The Elder Scrolls V: Skyrim (SE / AE / GOG)**.  
**AlchemyLink** (`AlchemyLink.dll`) streams real-time game state—player profile, crafting skills & perks, inventory, vitals, equipment loadouts, spells, and workbench state—to companion apps such as **SkyPotion** over binary WebSocket frames using **Protocol Buffers**.

> 📡 **Protocol Documentation**: See [docs/PROTOCOL.md](docs/PROTOCOL.md) for the full WebSocket & Protobuf message specification (`proto/game_sync.proto`).

---

## 🌟 Features

- **Built-in WebSocket Server (Default Port: `47778`)**:
  - Starts automatically when Skyrim launches—no external helper process required.
  - Uses compact binary **Protobuf** serialization (`GameSyncPacket` / `AppCommandPacket`).
- **Master Data & Profile Snapshot (`CatalogSnapshot`)**:
  - Character name, level, race, gold, and game language.
  - In-game ingredients (`INGR`), magic effects (`MGEF`), known enchantments, smithing recipes (`COBJ`), known spells/shouts, and saved equipment loadouts.
- **Real-Time Inventory, Stats & Vitals**:
  - Live stock counts for alchemy ingredients, smithing materials, soul gems, equipment, and potions/poisons.
  - Crafting skills, fortify bonuses, and perks for **Alchemy**, **Enchanting**, and **Smithing**.
  - Player vitals (Health, Magicka, Stamina, combat state, active buffs) and equipped spells/shouts.
- **Workbench Sync & Remote Control**:
  - Tracks open/close state and selections across Alchemy, Enchanting, and Smithing workbenches.
  - Supports remote ingredient selection, potion crafting, enchanting/disenchanting, smithing/tempering, potion drinking, poison application, loadout switching, and spell/shout equipping/casting.
- **Main-Thread Safety**:
  - Dispatches game-state reads and crafting/equip actions through SKSE `TaskInterface` to prevent CTDs.

---

## 🛠️ How to Build

### Option 1: GitHub Actions (Recommended)
The repository includes [`.github/workflows/build.yml`](.github/workflows/build.yml).  
Pushing to `main` or `master` (or triggering **Run workflow** manually) automatically builds `AlchemyLink.dll` on `windows-latest` and uploads it as a release artifact.

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

1. Copy the built `AlchemyLink.dll` to:
   ```text
   <Skyrim Special Edition>/Data/SKSE/Plugins/AlchemyLink.dll
   ```
2. Required dependencies:
   - **SKSE64** (Skyrim Script Extender)
   - **Address Library for SKSE Plugins** (matching your Skyrim version)
3. Launch Skyrim via SKSE64. The WebSocket server will listen on port `47778` by default and accept connections from companion apps.

---

## ⚙️ Configuration (`AlchemyLink.ini`)

On first launch, AlchemyLink automatically creates `Data/SKSE/Plugins/AlchemyLink.ini` if it does not exist:

```ini
[Server]
; WebSocket port (default: 47778)
Port = 47778

; Binding IP address (default: 0.0.0.0 for all interfaces, 127.0.0.1 for localhost only)
BindAddress = 0.0.0.0
```

| Key | Default | Description |
| :--- | :---: | :--- |
| `Port` | `47778` | WebSocket server port (`1`–`65535`). |
| `BindAddress` | `0.0.0.0` | Network interface to bind (`0.0.0.0` for LAN/mobile access, `127.0.0.1` for local-only). |
