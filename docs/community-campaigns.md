# Community campaigns (Gruntmods Edition 1.6.7)

How the Gruntmods install defines and runs community campaigns, which ones it ships, and what our
engine supports. Sources: `Documentation/Dune2000MissionLauncherManual.pdf` (Feda),
`Documentation/MissionLauncher-readme.txt`, `Documentation/Custom Campaign Creation Tutorial
Preview.pdf` (planning / map-making text only, no file formats), the strings and type names of
`MissionLauncher.exe` (a .NET Framework 4.8 WinForms app, "Dune 2000 Mission Launcher version 1.3.0 -
by Feda and FunkyFr3sh"; inspected statically, no decompilation), the community patch sources in
`ref/dune2000-sp-patches` (`src/extended-maps/*.c`, factual reference only) and D2kEditor
(`ref/D2kEditor/_missionini.pas`, `_mission.pas`, `_utils.pas find_file`, MIT).

Code: `src/formats/mission_ini.*` (mission .ini, CustomCampaignData paths, overlay lookup),
`src/formats/community.*` (campaign grouping, progress), `src/platform/catalog.*`,
`src/platform/game_data.cpp` (per-mission data swaps, rules), `src/platform/community_menu.*`,
`menu_community.cpp` (Enhanced pages), `original_community.cpp` (Original pages),
`sound_bank.*` (mission sounds). Tests: `tests/formats_test.cpp` (`testCommunity*`).

## 1. How the launcher runs a mission

1. **Lists** every `data/Missions/*.ini` that has a map (its "All Missions" view) plus one
   hard-coded window per campaign (§3). Missions are freely selectable — the launcher keeps **no
   progress**; it only shows a briefing, an intel window and a tactical map.
2. **Installs the mission's files** (manual §3; strings `MissionLauncherBackups`, `.bak`,
   `CUSTOM FILE RESTORE EXCEPTION`): every file under
   `CustomCampaignData/<CampaignFolder>/<ModsFolder>/` is copied over the game folder (the mods
   folder mirrors it: `data/DATA.R16`, `data/bin/Templates.bin`, `data/UI_DATA/*.uib`,
   `data/GAMESFX/*.AUD`, ...), the originals go to `MissionLauncherBackups/` (and
   `data/bin/*.bak`); `CustomCampaignData/<CampaignFolder>/Colours/<ColoursFile>` becomes
   `data/bin/COLOURS.BIN`. Resolution-specific `UIBB_<w>x<h>.R16` files of the mods folder are
   handled too. For War of the Landsraad it can also swap in `WarOfTheLandsraad/Legacy/DUNE2000.EXE`
   (an older game executable; strings `Legacy`, `DUNE2000.EXE`, `D2K.BACKUP`). Everything is
   restored when the game exits.
3. **Starts the game** through the spawner: writes `spawn.ini` `[Settings] Scenario=<stem>,
   MySideID=<SideId>, MissionNumber=, DifficultyLevel=, Seed=, TextUib=` and runs
   `dune2000.exe -SPAWN` (the 1.06p patch reads it: `src/spawner/spawner.asm`).

The installed `data/` is vanilla (the backups in `MissionLauncherBackups/` are byte-identical to
`data/`), so nothing of a campaign leaks into the original game.

## 2. Mission .ini keys (`data/Missions/<stem>.ini`)

| Section / key | Meaning | Used by | Ours |
|---|---|---|---|
| `[Basic] Name` | list name, also the campaign scheme (§3) | launcher | yes |
| `Author` | | launcher | yes (lists) |
| `SideId` | player side (0 A, 1 H, 2 O, 3 Emperor, 4 Fremen, 5 Smugglers, 6 Mercenaries) | spawn.ini MySideID | yes |
| `MissionNumber` | >0: the in-game briefing page is `IG<side><n>MIS.UIL` | game | no (we show the briefing text) |
| `Briefing` | briefing text, `_` = line break; overrides the key below | launcher, in-game-briefing.c | yes |
| `TextUibBriefingKey` | briefing entry in the text table | launcher | yes |
| `TextUib` | string table replacing `UI_DATA/Text.UIB` (mods folder first) | spawn.ini → game | yes |
| `Music` | first music track (`*` = random) | music-func.c | yes (`*` not) |
| `[Data] CampaignFolder` | `CustomCampaignData/<folder>` | launcher | yes |
| `ModsFolder` | replacement data files `<folder>/<mods>/data/...` | launcher | yes (overlay, §5) |
| `ColoursFile` | `<folder>/Colours/<file>` → `bin/COLOURS.BIN` | launcher | yes |
| `IntelId` | `<folder>/Intel/Entries/<id>_1..4.txt` (+ `Images/<id>_n.jpg`, `Tactical_<id>.jpg`) | launcher | texts in the briefing; images no |
| `PlayersFile` | `<folder>/Players/<file>` `[Player0..7] name=/short=` side names | no reader in launcher 1.3.0 or the patch strings | parsed (`GameData::sideNames`), not shown |
| `[Vars]` | rule overrides (§4) | rules.ini.c LoadRulesFromMap | partly (§4) |
| `[Text]` | `<index>=text` replaces message strings | show-message-event.c, event-actions.c | yes |
| `[Notes]`, `[Variables]`, `[Sides]` | editor notes / variable names / side names | D2kEditor | ignored (`[Sides]` parsed) |

Counts over the 318 .ini files: Name/SideId 318, Briefing 210, Music 204, TextUib 154,
CampaignFolder 147, IntelId 146, ModsFolder 118, TextUibBriefingKey 97, ColoursFile 97,
MissionNumber 95, PlayersFile 38, `[Vars]` 112, `[Text]` 204.

## 3. The campaigns

The launcher (1.3.0) has a window per campaign with **hard-coded mission lists** (stems built
with `D2K_{0}{1}`, `SS_{0}{1}`, `WOL_{0}{1}`, `Butlerian Jihad {0}`, the 39 `COALITION OF NOBLES ..`
names, `m1v1..m3v1`). Only Summers' Solstice is data-driven: `SplashArtData/config.ini`
(`[Misc] Arcs=4`; per stem `Index=` step, `VersionNumber`, `SplashArt=<png>`, `Title`,
`Description1/2`; keys `HasTwoVersions`, `HasBonus`, `EndOfArc` exist) and `arc<N>.txt` (the stems
of arc N in order). Every hard-coded list follows the mission names, so we group by name
(`formats/community.hpp`): `<Series> - [<House>] Mission <n>[letter] [(Option k)|(part k)]` or
`<Series> - [Mission] Bonus`; a house word makes one campaign per house; letters / options are
alternatives of one step (like the original campaign's V1/V2), parts are consecutive steps, bonus
steps are optional. Exception: `Butlerian Jihad Zone Zero Z` ("The End Of The Butlerian Jihad")
is step 10 of Zone Zero. `Original Campaign - ... by dato` (Emperor / Fremen / Smugglers /
Mercenaries versions of original missions) and single missions stay in Maps & missions.

| Campaign | Author | Steps (maps) | Data folder / overrides | Notes |
|---|---|---|---|---|
| Summers' Solstice (4 arcs) | Fey | 21 (30), 3 bonus | SummersSolstice: 9 mods folders (tilesets BLOXHUNT/MEGR/PHPE/RETR..., Templates, TILEATR*, ARMOUR/SPEED/BUILEXP, 93–138 voice/sound AUDs + samples.uib), COLOURS per mission, FeyTextS*.uib, intel | all 30 use the **extended .MIS** (not playable yet); side 5 (Smugglers); [Vars]: 22 keys incl. patch rules |
| Coalition of Nobles | Cm_Blast | 16 + bonus (20) | CoalicionNobles: 11 mods folders (DATA.R16, tilesets BLOXBGP/HILN/ORD/TEM, Templates, UI), COLOURS_NOBLES*.BIN, Players/Nobles1..7.ini, Text*Nobles*.uib | steps 3 and 6 are choices (2 / 3 options); side 2 |
| Coalition of Nobles: New Enemies | Cm_Blast | 16 + bonus (19) | CoalicionNoblesSecuela, COLOURS_NOBLESSEQ | 11 missions use the extended .MIS; step 12 = 3 options |
| Butlerian Jihad / War Machines / Zone Zero | Cm_Blast | 9 / 9 / 10 | ButlerianJihad (intel only) | vanilla data |
| War of the Landsraad – Atreides / Harkonnen / Ordos / Sardaukar | Feda | 15 / 15 / 15 / 5 | WarOfTheLandsraad: WOL_A/H/O/S (DATA.R16, tilesets, Templates, ARMOUR 2750 B, UI, WOL_Text/Caladan/Giedi/Ixian*.uib), colours_*.bin | Legacy exe swap ignored |
| War Of Assassins – A / H / O | (Feda) | 9 each | — (woatext.uib in UI_DATA) | briefings by TextUibBriefingKey |
| WoA Sandstorm – A / H / O | Feda | 7 each | — | its music files (elusive.aud ...) are not in this install |
| AntiFremen | Cm_Blast | 10 | — | mission 9 in two parts |
| Fremen Warriors, Spacing Guild, New House, Smugglers | Cm_Blast | 9, 9, 8, 4 | — | |
| Ixian Campaign, Mercenary Campaign | FedaYkin (+Galaxy) | 9 each | — (FedaIxianText / FedaMercText.uib) | MissionNumber set |
| Rise Of The Mercenaries | gruntmods.com | 2 | — | `m3v1.map` is missing from the install |

24 campaigns, 255 of the 275 custom missions. The full step lists are built at runtime and checked
in `testCommunityData`.

## 4. `[Vars]` rules

LoadRulesFromMap (rules.ini.c): VARS.BIN, then the game folder's `rules.ini [Vars]`, then the
mission .ini `[Vars]` (InfiniteSpice is only taken from the mission). Byte variables keep the low
byte (`SandWormShotSleep=40000` → 64).

| Key (missions using it) | Ours |
|---|---|
| the 27 VARS.BIN variables (DeathHandAccuracy 61, NumberOfFremen 55, SandWorm*Sleep ~40, HarvesterDriveDistance/RepairDriveDistance 31, Building/UnitRepairValue 30, DevastatorExplodeDelay 30, harvest*/starport* ...) | **yes** (`applyMissionVars` → `formats::Vars`) |
| InfiniteSpice (16) | **yes** — the patch skips the level-3 clearing store at 0x4995a5, so a cell never drops below level 3 (economy.cpp) |
| costPercentageEasy/Hard, buildSpeedPercentageEasy/Hard (30, SS) | **yes** (`World::difficultyCostPercent/SpeedPercent`) |
| guardModeRadius (30, SS) | **yes** (guard chase radius, world.cpp) |
| buildingsAlwaysNeedPrerequisites (30, SS) | **yes** (buildable flags rebuilt every update, 0x46bfea) |
| returnCreditsToSpiceStorage (31, SS + CoN) | no — refunds of cancelled builds / starport orders go into spice storage (9 call sites) |
| infantryReleaseLimit / infantryReleaseChance (30, SS) | no — the patch replaces ReleaseSurvivors (per-side infantry type, limit, retries) |
| uncloakRemainingStealthUnit (30, SS: No) | no — our model has no "uncloak the last stealth unit" step to switch off |
| showNeutralBecomeHostileMsg (30, SS: No) | no (message not implemented) |
| alwaysShowRadar, maxChatMessages, intervalsAreOffByOneTick | not used by any mission |

## 5. What we load per mission (`loadGameData`)

The Missions/<stem>.ini is read for every non-skirmish start (also `--map NAME`):
- **Overlay**: each data file is looked up in `CustomCampaignData/<CampaignFolder>/<ModsFolder>/data/<sub>/`
  first, then in `data/` (case-insensitive) — DATA.R16, tilesets (.R16 / BLOXXMAS .R8), Templates,
  TILEDATA, TILEATR*, SPEED, ARMOUR, BUILEXP, VARS, CIRCLES, TECHPOS, PALETTE, FONT/FONTCOL,
  UI_ENG.R16, UIBB.R16, UI_DATA text tables. Same as the launcher's copy, without touching the
  install. ARMOUR.BIN longer than 2702 bytes is accepted (first 2702 used, like the game).
- **Sounds**: the mods folder's `UI_DATA/samples.uib`, `GAMESFX/SOUND.RS` and `GAMESFX[Eng]/*.AUD`
  are used while that game runs (`SoundBank::setOverlay`).
- `ColoursFile`, `TextUib`, `[Text]`, `[Vars]`, `SideId`, `Music`, `PlayersFile`/`[Sides]` (§2).
- Briefing (catalog): `Briefing=` else `TextUibBriefingKey` in the mission's TextUib, else Text.UIB;
  plus the intel entries.

## 6. Not supported yet (for later)

- **Extended .MIS / event system**: implemented (src/game/mission_ext.cpp, spec
  docs/extended-events.md). All 41 extended missions (30 Summers' Solstice, 11 New Enemies) run
  their events; not implemented are only types no shipped mission uses (IF/ELSE/loops and
  expressions, most Get* variable queries, template / memory edits, UI queries) plus building
  attack orders (2 uses), crate ext data / pickup events, per-type kill statistics and `@`
  variable substitution in messages.
- Rule showNeutralBecomeHostileMsg is parsed but the message is not modelled (the other rules of
  §4 — returnCreditsToSpiceStorage, infantryReleaseLimit/Chance, uncloakRemainingStealthUnit —
  and `Music=*` work).
- `MissionNumber` in-game briefing pages (`IG<side><n>MIS.UIL` from the mods folder) — we show the
  briefing text in the in-game briefing instead.
- Side names (PlayersFile / `[Sides]`) on the score screen / messages; splash art (PNG), intel
  images and tactical maps (JPG) — we have no PNG/JPG decoder in the front end.
- The War of the Landsraad Legacy exe swap; mods-folder `UIBB_<w>x<h>.R16` variants (we extend the
  mod's UIBB.R16 ourselves); custom mods folders for missions without a CampaignFolder (none here).
- Extended Templates.bin / ARMOUR.BIN layouts (more unit types / warheads) beyond what the
  loaders accept — none of the shipped mods needed them to load.

## 7. Our front end and progress

- Enhanced: main menu **Community** → tabs Campaigns / Maps & missions → campaign page (missions
  with Won / Locked / Choice / Alternative / Bonus) → briefing (+ intel, extended-MIS warning) →
  start. Original: Select Mission → **Community** (re/notes/campaign-flow.md §6.4).
- Progress: `<settings folder>/community_progress.ini`, `[<campaign id>] won=<stems>`. A win of a
  community mission (from either list, also after loading a save) marks it in every campaign that
  contains it. Step 1 is open; a step opens when the previous non-bonus step is won (any of its
  alternatives); bonus steps open with the following step and are never required. The launcher
  itself has no progress — this is our addition.
- Screenshot names: `community`, `community-maps`, `community-campaign`, `community-briefing`,
  `original-community`, `original-community-maps`, `original-community-campaign` (screenshot runs
  use sample progress: the first two steps of each campaign won).

## 8. Art packs, 3D models and a campaign's own art

Art packs (Enhanced "Art") and the 3D models are keyed by the **base install's** DATA.R16 frame numbers and
Templates.bin type names. A mods folder can replace DATA.R16 (new / changed sprites, often with frames
inserted so everything after them moves), Templates.bin (types renamed, reordered, given other art) and the
tileset. We do not redesign mods (owner: mod authors may add OpenD2K art later), so while such a mission
runs the campaign's own art wins. Code: `src/render/mod_art.*` (analysis), `platform/game_data.cpp`
(`GameData::modArt`, computed at load when a mods folder is used), `render::ArtPack::setModArt`
(every frame / tile query of the pack goes through it), `render3d::ModelRenderer::setModelKeys`,
`Renderer3D::objectSprite`. Render-only; the simulation never sees it.

- **DATA.R16**: every frame is hashed (FNV-1a over the header fields, pixels and - 8 bpp - its palette's
  colours); the base install's hashes are computed once per run and cached. A mod frame equal to the base
  frame at the same index keeps the pack's art. A mod frame equal to a base frame at **another** index keeps
  the pack's art of that base frame only inside a run of at least 8 consecutive frames with the same shift
  (frames inserted / removed before it: Summers' Solstice moves ~5000-6000 frames); a lone frame the
  campaign replaced with a copy of another frame stays the campaign's. Every other frame is **the
  campaign's own art**: never taken from an art pack (also not for the production icons), drawn from the
  mod's sprite like the original-art path (Enhanced: the built-in HQ upscale).
- **Templates.bin**: each mod type is matched to the base type of the same name whose render-relevant data
  agree - frames (by content, also when moved) of its art, barrel, build-up and animation art, size /
  footprint, infantry flag, special behaviour, direction frames; the group alone never decides (WOL swaps
  groups as a technical trick). Failing that (renamed / repurposed types) the first base type with the same
  art, preferring the same owner houses (Summers' Solstice: `LIGHT WIND TRAP` = base `ATREIDES WIND TRAP`).
  The 3D models of a matched type are looked up by the **base type's** name / group; an unmatched type is
  the campaign's own art: no model, and in Graphics: 3D a billboard of its sprite (the pack's redesigned
  frame where that frame is unchanged, else the campaign's sprite) - the explicit exception to "no original
  sprites in 3D" (docs/3d-mode.md).
- **Tileset / TILEATR**: a tileset from the mods folder keeps an art pack's tile only where the base tileset of
  that name has the same pixels (by index or in a moved run; a new tileset name has no pack tiles anyway).
  The designed terrain (Designed / Faithful, 2D and 3D) needs no fallback: it is laid out from the map's
  actual tile pixels (the colour lock and the faithful guide read the loaded tileset through the atlas,
  render3d/terrain_guide.*) and the World's cell flags (the mission's own TILEATR), never from base-tileset
  tables. Checked on Coalition of Nobles 13 (BLOXBGP, all 800 tiles differ from the base set).
- **COLOURS**: the mission's ColoursFile is `GameData::colours`, which tints both the pack frames
  (`SpriteCache`, house masks / derived house pixels) and the models (`ModelRenderer`), so it still applies.
- **Log** (once at mission start, nothing in the UI): `Mod art: 1688 DATA.R16 frames (+6188 moved), 80 types (32
  units, 48 buildings) use the campaign's own art; terrain: base tileset, campaign TILEATR; designed terrain
  follows the mission's own tiles`. In 3D also `3D models: campaign templates - N types looked up by their base
  type, M with the campaign's own art (no model)` and `3D coverage: K types drawn with the campaign's own
  sprites`. `D2K_MOD_ART=1` lists every such type with the reason (and the own frame ranges);
  `D2K_MOD_ART=0` switches the analysis off (A/B checks: then identical to the
  behaviour before).

Shipped campaigns (2026-11-13, sample missions):

| Mission (mods folder) | Own frames | Own types | Base-matched types | Tileset |
|---|---|---|---|---|
| FEYS03V1 Summers' Solstice (SSNormal01) | 2844 (+5032 moved) | 32 units, 48 buildings | 67 (renamed / reordered) | base, campaign TILEATR |
| Coalition of Nobles 05 (Basico) | 0 | 1 (IMPERIAL HEAVY FACTORY) | THUMPER = base RAIDER art, MERCENARY HEAVY FACTORY = base HT factory art | base |
| Coalition of Nobles 12 (Caladan) | 85 (UI / crates / starport icons) | 0 | - | BLOXTEM, identical to the base set |
| Coalition of Nobles 13 (GiediPrime) | 86 | 1 | - | BLOXBGP, all tiles differ |
| WOL_A1 / WOL_S2 (War of the Landsraad) | 64 / 69 | 0 / 3 medium gun turrets (barrel art) | 4 (group swaps) | base |

Vanilla missions (no mods folder) are untouched: A1V1 / H5V1 screenshots in Enhanced and 3D are
pixel-identical before / after.
