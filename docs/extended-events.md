# Extended mission format and event system (community patch 1.06p+)

The Gruntmods / FunkyFr3sh community patch ("extended maps") replaces the original
`Mission__CheckEvents` with a much larger event system. Missions made with D2kEditor in this
format need it to play as designed (Summers' Solstice, Coalition: New Enemies, …).
Sources (factual reference only, no code copied): `ref/dune2000-sp-patches/src/event-system/*`
(event-core.c/.h, event-conditions.c, event-actions.c/.h, event-filters.c/.h, event-utils.c,
extend-event-limits.asm), `src/extended-maps/setupmapstuff.c`, `delivery-tag.c`, `rules.ini.c`,
and the D2kEditor definitions `ref/D2kEditor/config/event_types.ini`, `condition_types.ini`,
`game_structs.ini`, `filter_criteria.ini` (MIT, github.com/jkoncick/D2kEditor).
Our implementation: `src/game/mission_ext.cpp` (+ `src/formats/mis.*`, `src/game/spawn.cpp`).

## 1. Where the data lives

Everything is in the **.MIS** file; the mission `.ini` only adds `[Text]` strings (message
overrides), `[Vars]` rules, `[Basic]` (side, music, …).

| offset | size | content |
|---|---|---|
| 0 … 68065 | 68066 | the original .MIS: tech, money, houses, 8 AI segments, diplomacy, events 0..63 (72 B), conditions 0..47 (28 B), tileset names, u8 event/condition counts (ignored when extended), time limit, messages |
| 68066 | 2 | u16 event count (≤ 1024) |
| 68068 | 2 | u16 condition count (≤ 256) |
| 68070 | 208 × 28 | conditions 48..255 |
| 73894 | 960 × 72 | events 64..1023 |

Total 143014 bytes (`extend-event-limits.asm`, LoadMission 0x44fdc7). Savegames store all 256 /
1024 records.

### Event record (72 bytes)

| off | field |
|---|---|
| 0..3 | coord_x[4] |
| 4..7 | coord_y[4] |
| 8 | i32 value ("A_VAL1"; arg index 5) |
| 12 | num_conditions (≤ 12) |
| 13 | event_type |
| 14..18 | args[0..4]: amount ("A_AMNT"), side ("A_SIDE"), item, enum, bool |
| 19..30 | condition_index[12] |
| 31 | coord_var_flags (bit 2i / 2i+1: coord_x[i] / coord_y[i] is a variable index) |
| 32 | arg_var_flags (bit 0 = side, 1 = amount, 2..4 = item/enum/bool, 5 = value) |
| 33..44 | condition_negation[12] |
| 45 | filter_skip (skip the first N matching objects; or the variable holding an object index) |
| 46 | event_flags: 1 auto-block (run once), 2 blocked, 4 conditions OR'ed, 8 object index from variable, 16 skip is a variable, 32 limit is a variable |
| 47..71 | data[25]: unit list (deliveries/spawns), or data[0] = object limit and data[1..24] = object filter, message data (`sample_id` u32 @+1, variable types/indexes, `string_index` u32 @+21), tile lists (u16 @+1), music name, … ; "A_VAL2" = i32 @data+1 |

### Condition record (28 bytes)

| off | field |
|---|---|
| 0 | i32 val2 |
| 4 | i32 val1 |
| 8 | i32 val3 (flag value, run counts) |
| 12,13 | coord_x[2] |
| 14 | coord_var_flags |
| 15 | arg_var_flags (0 side, 1 arg1, 2 arg2, 3 val1, 4 val2, 5 val3, 6 val4) |
| 16,17 | coord_y[2] |
| 20 | i32 val4 / float (casualty ratio; internal state: timer base time, countdown) |
| 24 | side (8 = any side for Check*) |
| 25 | condition_type |
| 26 | arg1 |
| 27 | arg2 |

For **Check Units / Buildings / Crates / Tiles** the first 24 bytes are an object filter (§4),
arg1 bit 0 = "exactly" (else "at least"), arg2 = amount (min 1).

## 2. Evaluation (Mod__HandleConditionsAndEvents)

Campaign only (skirmish keeps the default rules). Every tick, after the per-tick
`EvaluateIfBuildingsOrUnitsExistForSide`:
1. `tick_random_value = rand()` (one RNG draw per tick);
2. every condition is evaluated in index order (some update their own internal state);
3. events run in index order (`ExecuteEventsInRange(0, count)`): an event runs when all its
   conditions differ from their negation flags (flag 4: when any does). A **blocked** event
   (flag 2) is skipped; an **auto-block** event (flag 1) blocks itself when it runs.
4. Block structure: `CALLABLE_BLOCK_START` / `HOOK_BLOCK_START` / `IF` / loops up to the
   matching `END` are skipped as one unit (next index cached at load); `EXECUTE_BLOCK` runs a
   callable block; `EXIT` / `BREAK` / `CONTINUE` unwind.

Mission start (setupmapstuff.c): Timer and Interval conditions get val4 (base time) = 0, Flag
conditions val3 = 0, all 256 variables = 0.

Variables: 256 × {value, old value, tick of last write}; any coordinate/argument with its
var-flag bit set reads `variables[x].value` instead of the constant.

## 3. Types

Event types (0..255; `ET_*` in event-core.h). **Bold** = used by the shipped community missions
(41 extended missions: 30 Summers' Solstice, 11 Coalition: New Enemies); all bold ones are
implemented, ✗ = not implemented.

- General: **0 Reinforcement** (carryall; tag, deploy action, delay), **1 Starport Delivery**, **2
  Allegiance** (+both sides), **3 Leave** (BlowupAll), **4 Berserk**, **5 Play Sound** (global /
  point), **6 Set Build Rate**, **7 Set Attack Building Rate**, **8 Set Cash** (value operation on
  the total credits), **9 Set Tech** (+immediate icon update), **10 Win**, **11 Lose**, **12 Switch
  My Side**, **13 Hide Map**, **14 Reveal Map**, **15 Set Timer**, **16 Hide Timer**, **17 Show
  Message** (sound mode, .ini [Text] override), **18 Unit Spawn** (facing, tag), **19 Set Flag**,
  **20 Un/block Event**, **21 Play Music** ("*" random), **22 Damage Tiles**, **23 Add Unit**
  (movement, facing, stealth, tag, result variable), **24 Add Building** (6 placement methods,
  primary, turret facing, tag), **25 Add Bullet**, **26 Add Explosion**, **27 Add Crate**, **28 Add
  Concrete**, **29 Spice Bloom** (classic / square / circle / Dune 2 modes, randomizer), **30 Shake
  Screen**, **31 Change Viewport**, **32 Change Map Block** (≤ 12 tiles; normal / visual / restore),
  **33 Transform Tiles**, **34 Add Building Destruction**, 35 Add Homing Bullet, **36 Activate
  Timer**, **37 Remove Message** (UI, no-op), 38 Set Message Color ✗, 39 Set Tooltip ✗.
- Side: **40 Transfer Credits**, **41 Set Building Upgrades**, **42 Set Starport Stock**, **43 Set
  Starport Cost**, **44 Change Starport Unit**, 45 Show Side Data ✗(debug).
- AI: **46 Set AI Property** (raw CAI offset; our AiState keeps the original 7608-byte layout),
  47 Show AI Data ✗. Memory: 48/49 ✗ (raw process memory).
- Units (object filter or variable index): **50 Destroy** (silent option), **51 Damage/Heal**,
  **52 Change Owner**, **53 Change Type**, **54 Set Flag**, **55 Set Property** (original Unit
  offsets mapped to our fields, see `unitField` in mission_ext.cpp), **56 Select**, **57 Airlift**,
  59 Show Data ✗.
- Buildings: **60 Destroy**, **61 Damage/Heal**, **62 Change Owner** (capture), **63 Change
  Type**, **64 Set Flag**, **65 Set Property**, **66 Select**, 67 Show ✗.
- 68 Set Bullet Property ✗, 69 Set Explosion Property ✗, **70 Remove Crate**, 71 Pickup Crate ✗,
  72 Set Crate Property ✗.
- Tiles: 74 Change Tile, **75 Set Tile Attribute**, **76 Set Tile Property** (tile/damage/shroud/
  flags), **77 Reveal Tile**, 78 Hide Tile.
- Orders (on the side's selected units, like the original Generate*Order functions): **80 Move**,
  **81 Dock**, 82 Repair Selected, **83 Repair Single**, **84 Attack Unit**, **85 Attack Building**,
  **86 Attack Tile**, **87 Guard**, **88 Scatter** (new: ExecScatterOrder 0x457580), **89 Retreat**,
  **90 Deploy**, 91 Building Attack Unit ✗, **92 Building Attack Building** ✗, **93 Set Primary**,
  94 Repair Building, **95 Sell**, **96 Stop** (units), **97..101 build pick/cancel/place, unit
  pick/cancel**, **102..105 starport pick/unpick/purchase/cancel**, **106/107 upgrade
  pick/cancel**, **108 Special Weapon**.
- 110..119 template edits ✗; 120..195 variable operations and queries: 120 Set Variable, 121 Get
  Variable, 125 Random Value, 130..138 counts, 160..162 ticks / side / difficulty implemented;
  the rest ✗.
- 233..252 blocks / conditional expressions / loops: block skipping, Execute Block, Exit, Break,
  Continue implemented; IF / ELSE / loops ✗ (no shipped mission uses them).

Condition types: **0 Building Exists**, **1 Unit Exists**, **2 Interval** (arg1 inactive, val1
start delay, val2 period, val3 run count, val4 base tick; rule intervalsAreOffByOneTick), **3
Timer** (inactive, comparison > / < / == / modulo with shift), **4 Casualties**, **5 Base
Destroyed**, **6 Units Destroyed**, **7 Tile Revealed**, **8 Credits** (total / spice / cash /
storage, any side), **9 Flag**, **10 Random Chance** (tick random, fixed tick), **11 Random
Interval**, **12 Diplomacy**, **13 Difficulty**, **14 Check Units**, **15 Check Buildings**, **16
Check Crates**, **17 Check Tiles**, 18 Spice in Area, **19 Damage in Area**, **20 Power**, **21
Building Upgrades**, **22 Starport Stock**, 23 Starport Cost, **24 Starport Pick**, 25 Starport
Delivery, **27 Building Queue**, **28 Unit Queue**, **29 Upgrade Queue**, 30 Spice Harvested,
**31 Units Built**, **32 Buildings Built**, **33 Units Lost**, **34 Buildings Lost**, 35 Units
Killed / **36 Buildings Killed** (totals only: per type/enemy kill tables are not kept), 37 Side
Property ✗, 38 AI Property, 39 Memory ✗, 40 Variable Value, 41 Variable Changed.

## 4. Object filters (ObjectFilterStruct, 24 bytes)

`pos_min_x, pos_min_y, pos_max_x, pos_max_y, pos_flags, criteria_var_flags, u16 and_or,
criteria_type[8], criteria_value[8]`. pos_flags: bit 0 check position, bit 1 negate, bits 2-3
area kind (0 rectangle, 1 square radius, 2 circle radius in tiles, 3 circle radius in pixels),
bits 4-7 the four position bytes are variables. Each criterion: low 6 bits type, bit 6 negate,
bit 7 ">=" instead of "=="; the 8 results are combined with the 2-bit operators in and_or (the
patch reduces operator codes 3, 2, 1, 0 in that order; odd = OR, even = AND). Criteria tables per
object kind (unit 0..63, building 0..63, bullet, explosion, crate, tile 0..25, side 0..24) are
in event-filters.h; our port implements them all except the mouse-cursor categories (no cursor in
the model) and the patch's custom bytes (always 0 here).

Object events loop over sides 0..7 (or the event's side; 8 = all) in list order, skip
`filter_skip` matches, stop after `data[0]` (limit; 0 = unlimited).

## 5. Map format additions (setupmapstuff.c)

Special values beyond the TILEDATA table: `1TTTTIIIEEEEEEEE` crate (type, image, ext data),
`01TCDDDSSSUUUUUU` unit (tagged, stealth, direction, side, type), `001TPHSSSBBBBBBB` building
(tagged, primary / turret direction, no free harvester, side, type). Summers' Solstice places its
starting units this way. We place them after the map pass in row-major order (the patch does it
inside its second pass; the RNG order differs only for maps that mix these with spice-bloom
specials).

## 6. Rules ([Vars] in rules.ini / the mission .ini)

Implemented: InfiniteSpice, buildingsAlwaysNeedPrerequisites, guardModeRadius, cost/build speed
percentages, **returnCreditsToSpiceStorage** (refunds into free spice storage first),
**infantryReleaseLimit / infantryReleaseChance** (survivors of destroyed/sold buildings),
**uncloakRemainingStealthUnit**, intervalsAreOffByOneTick; showNeutralBecomeHostileMsg is
parsed (the message itself is not modelled). Mission .ini `Music=*` and the Play Music event "*"
pick a random track.

## 7. Not modelled / differences

- Mouse/keyboard/UI queries, tooltips, message colours/positions, debug "Show … Data" events,
  raw memory events, template edits, hooks (`HOOK_BLOCK_START` bodies never run).
- IF/ELSE/loop/expression events and most Get* variable queries (no shipped mission uses them).
- Building attack orders (91, 92), Pickup Crate, crate ext data and the patch's extended crate
  types (CT_EXECUTE_EVENT …) behave like our vanilla crates.
- Per-type kill statistics (Units/Buildings Killed by type and enemy) are not kept.
- The patch's Show Message variables (`@` substitution) are not substituted; [Text] overrides are.
