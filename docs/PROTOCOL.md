# AlchemyLink WebSocket Protocol Specification

AlchemyLink communicates with companion clients (such as the **SkyPotion** Android app) over **WebSocket** using **binary frames** serialized with **Protocol Buffers (`proto3`)**.

- **Schema**: [`proto/game_sync.proto`](../proto/game_sync.proto)
- **Package**: `com.dolzzo.skypotion.proto.sync`
- **Default Endpoint**: `ws://<host>:8765`
- **Frame Type**: Binary WebSocket frames (`GameSyncPacket` / `AppCommandPacket`)

---

## 1. Connection & Message Flow

1. **Connect**: The client connects to the plugin's WebSocket server (default port `8765`).
2. **Initial Snapshot**: On connection or save load, the plugin sends a `GameSyncPacket` containing `CatalogSnapshot` (character profile, game language, master data for ingredients/effects/enchantments/recipes/spells/shouts/loadouts, and `initial_stats`).
3. **Real-Time Sync**: As in-game state changes (inventory, stats, crafting bench UI, vitals, equipped spells, active loadouts), the plugin pushes `GameSyncPacket` updates.
4. **Keep-Alive**: `Heartbeat` (`seq`) messages are exchanged to keep the connection alive.
5. **Remote Commands**: The client sends `AppCommandPacket` with a unique `request_id`. The plugin executes the action on Skyrim's main thread (via SKSE `TaskInterface`) and responds with a `CommandResult` sharing the same `request_id`.

---

## 2. Game → App (`GameSyncPacket`)

Every message from the plugin to the client is wrapped in `GameSyncPacket`:

```protobuf
message GameSyncPacket {
  uint32 protocol_version = 1;
  int64 timestamp_ms = 2;
  oneof payload { ... }
}
```

| Payload Field | Tag | Message Type | Description |
| :--- | :---: | :--- | :--- |
| `catalog_snapshot` | `10` | `CatalogSnapshot` | Initial master data & session snapshot (profile, GMST prefixes, `BaseEffectDef`, `IngredientDef`, `KnownEnchantmentDef`, `SmithingRecipeDef`, `KnownSpellDef`, `KnownShoutDef`, `EquipmentLoadout`, `initial_stats`). |
| `inventory_update` | `11` | `InventoryUpdate` | Real-time inventory state (`is_full_snapshot`, alchemy `items`, `smithing_materials`, `soul_gems`, `equipment`, `potions`). |
| `player_stats` | `12` | `PlayerCraftingStats` | Character profile (level, race, gold) and crafting skills, fortify bonuses, and perks for Alchemy, Enchanting, and Smithing. |
| `discovered` | `13` | `DiscoveredEffectsUpdate` | Map of ingredient `form_id -> 4-bit discovered_mask` (`0x00`–`0x0F`). |
| `heartbeat` | `14` | `Heartbeat` | Keep-alive ping/pong (`seq`). |
| `bench_state` | `15` | `CraftingBenchState` | Active crafting bench (`BenchType`: Alchemy, Enchanting, Smithing Forge, Workbench, Grindstone), open/closed state, and currently selected items/effects/recipes. |
| `command_result` | `16` | `CommandResult` | Execution result for an `AppCommandPacket` (`request_id`, `success`, `crafted_count`, `error_code`, `message`, `missing_form_ids`). |
| `player_vitals` | `17` | `PlayerVitalsUpdate` | Current/max Health, Magicka, Stamina, `is_in_combat`, and `active_buffs` (`ActiveBuffEntry`). |
| `active_loadouts` | `18` | `ActiveLoadoutsUpdate` | Registered equipment hotkey presets (`EquipmentLoadout`) and currently active `active_hotkey` / `active_loadout_id`. |
| `equipped_spells` | `19` | `EquippedSpellsUpdate` | Currently equipped left-hand spell, right-hand spell, voice/shout FormID, and remaining shout cooldown. |

---

## 3. App → Game (`AppCommandPacket`)

Every command from the client to the plugin is wrapped in `AppCommandPacket`:

```protobuf
message AppCommandPacket {
  uint32 protocol_version = 1;
  uint64 request_id = 2;
  oneof command { ... }
}
```

### Alchemy
| Command Field | Tag | Message Type | Description |
| :--- | :---: | :--- | :--- |
| `select_ingredients` | `10` | `SelectIngredientsCommand` | Select ingredient `form_ids` (up to 3) at the alchemy bench. |
| `craft_potion` | `11` | `CraftPotionCommand` | Craft `count` potions using the specified `form_ids`. |

### Enchanting
| Command Field | Tag | Message Type | Description |
| :--- | :---: | :--- | :--- |
| `craft_enchantment` | `20` | `CraftEnchantmentCommand` | Enchant `item_form_id` with 1–2 `effect_form_ids`, `soul_gem_form_id`, `custom_name`, and `magnitude_slider` (`0.0`–`1.0`). |
| `disenchant_item` | `21` | `DisenchantItemCommand` | Disenchant `item_form_id` to learn its enchantment. |

### Smithing
| Command Field | Tag | Message Type | Description |
| :--- | :---: | :--- | :--- |
| `craft_smithing` | `30` | `CraftSmithingCommand` | Craft `count` items from ConstructibleObject `cobj_form_id`. |
| `temper_equipment` | `31` | `TemperEquipmentCommand` | Temper `equipment_form_id` at a grindstone or workbench. |

### Potions & Poisons
| Command Field | Tag | Message Type | Description |
| :--- | :---: | :--- | :--- |
| `drink_potion` | `40` | `DrinkPotionCommand` | Drink `count` of `potion_form_id` (or auto-select best potion by `quick_category` if `potion_form_id == 0`). |
| `apply_poison` | `41` | `ApplyPoisonCommand` | Apply `poison_form_id` to `target_slot` (`EQUIP_SLOT_RIGHT_HAND` or `EQUIP_SLOT_LEFT_HAND`). |

### Equipment & Loadouts
| Command Field | Tag | Message Type | Description |
| :--- | :---: | :--- | :--- |
| `apply_loadout` | `50` | `ApplyLoadoutCommand` | Equip a preset by `hotkey_index` (1–8), `loadout_id`, or inline `custom_loadout`. |
| `save_loadout` | `51` | `SaveLoadoutCommand` | Save a loadout preset (`capture_current_equipped` or explicit `items`). |
| `equip_item` | `52` | `EquipItemCommand` | Equip or unequip (`is_unequip`) a specific `form_id` in `slot`. |

### Spells & Shouts
| Command Field | Tag | Message Type | Description |
| :--- | :---: | :--- | :--- |
| `cast_spell` | `60` | `CastSpellCommand` | Cast or equip `spell_form_id` on `hand_slot` (`HAND_SLOT_LEFT`, `RIGHT`, `BOTH`, `VOICE`), optionally `instant_cast` on `target_actor_id`. |
| `equip_spell` | `61` | `EquipSpellCommand` | Equip `spell_form_id` to `hand_slot`. |
| `equip_shout` | `62` | `EquipShoutCommand` | Equip `shout_form_id` to the voice/power slot. |

---

## 4. Command Responses (`CommandResult`)

Every `AppCommandPacket` receives a `CommandResult` with the matching `request_id`:

- `success` (`bool`): Whether the command succeeded.
- `crafted_count` (`int32`): Actual number of items crafted/tempered/consumed.
- `error_code` (`string`): Machine-readable error code when `success == false` (e.g., `"BENCH_CLOSED"`, `"OUT_OF_STOCK"`, `"SPELL_NOT_FOUND"`, `"COOLDOWN"`).
- `message` (`string`): Human-readable detail message.
- `missing_form_ids` (`repeated uint32`): Missing material FormIDs when crafting fails due to insufficient materials.
