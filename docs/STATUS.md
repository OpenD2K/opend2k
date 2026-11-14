# d2k-mac — status

Native macOS (Apple Silicon) rebuild of Dune 2000, reimplemented from the original
DUNE2000.DAT (decompiled with Ghidra via REA) and run on SDL3/Metal. The game logic follows the
original code closely, down to RNG call order and known vanilla quirks. See `re/notes/*.md` for
each subsystem.

## Run
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
open "build/Dune 2000.app"          # the macOS app (double-click works too)
./build/d2k                         # same, from the terminal: opens the main menu
./build/d2k --map A1V1.MAP          # straight into Atreides mission 1 (any map in data/maps or data/Missions)
./build/d2k --map 4PLAY1.MAP --skirmish   # straight into a skirmish (lobby values from the settings)
./build/d2k --map H5V1.MAP --graphics enhanced   # Enhanced graphics (default: Original)
```
Options: `--data DIR`, `--mute`, `--no-music`, `--graphics original|enhanced|3d` (`--hq` = enhanced,
`--camera-tilt DEG` / `--camera-yaw DEG` set the 3D camera,
`--classic-ui` = original), `--art-pack original|upscaled|NAME` and `--effects classic|modern` (Enhanced only,
see `docs/enhanced-graphics.md`), `--time-of-day day|dawn|dusk|night|cycle` and `--lighting classic|modern` (Enhanced / 3D;
docs/enhanced-graphics.md "Lighting"; one-frame runs default to Day + Classic = the old look), `--skirmish-ai N` (with `--skirmish`:
N computer players, this run), `--frames N` (one-frame modes run N real-time frames, screenshot the last,
log frame times; one-frame, recording and `--simtest` runs use a hidden window and take no input), `--at X Y` (centre on a tile), `--zoom Z`,
`--ticks N` (pre-simulate), `--battle` (demo: all armed units attack), `--select` (select all own units and buildings),
`--screenshot out.bmp` / `--smoke` (render one frame and exit), `--simtest` (headless self-test),
`--record DIR --frames N [--record-fps F]` (deterministic recording: every frame advances the game clock by exactly
1/F s, default 30, independent of the real frame time, and is saved as `DIR/frame_00000.bmp`...; same map / ticks /
camera / options give bit-identical frames, so a Classic and a Modern run of the same battle line up frame for frame;
`ffmpeg -framerate 30 -i DIR/frame_%05d.bmp -vf crop=1040:800:0:0 -c:v libx264 -pix_fmt yuv420p out.mp4`;
`--record-fps F` with `--screenshot` and `--frames N` instead: the same fixed steps, only the last frame saved -
e.g. a `--vfx-demo` scene N/F game seconds in; for trailers: `D2K_CAMERA_MOVE="secs=12,yaw=-30:30,tilt=48:40,zoom=3:3.3,at=11:10:12:11"` eases
the camera (each key optional; `at` = view centre in cells, zoom ignored by Graphics: Original) and `D2K_CYCLE_START=S`
starts the day / night cycle S game seconds in (with `--time-of-day cycle`, `D2K_CYCLE_SECONDS`)),
`--vfx-demo` (with `--at X Y`: a weapon effects gallery - rows of shots of every weapon kind and repeating
vehicle-death / Death Hand / devastator explosions around the tile; changes the world, render checks only),
`--menu-screenshot out.bmp --menu-page NAME` (render one menu page and exit; pages: main, campaign, house-confirm,
campaign-map[-a|-h|-o], campaign-map-save, campaign-map-load (Save.uil / Load.uil over the map), briefing-original, quit-campaign, score, score-lost, score-counting,
missions[-a|-h|-o], briefing, custom, skirmish, options, graphics[-original|-enhanced|-3d|-custom|-custom-end], datadir, load, and the original pages credits,
skirmish-score, difficulty, select-mission, custom-missions, mptype, settings, game-options, sound, no-saves; pause, pause-options,
pause-save, pause-load, pause-delete, pause-abort, pause-restart, pause-briefing, pause-replay — the pause pages
need `--map` or `--load`; with `--graphics original` main/skirmish/options/load and the pause pages are the
original ones), `--load N` (start straight into save slot N; with
`--ticks M` M more ticks; a campaign-map save opens its territory map), `--save N` (write the `--map` game,
after `--ticks`, to slot N; with `--menu-page campaign-map[-a|-h|-o]` a campaign-map save of that map),
`--saves-dir DIR` (use DIR for the save slots instead of the settings folder), `--window WxH` (initial window
size in points), `--resolution auto|native|WxH` (Original game resolution for this run),
`--cursor-at X Y` (with `--screenshot`: the mouse rests there, so hover tooltips show),
`--sidebar-page starport|upgrade|select-upgradable` (open that sidebar page / select an upgradable
building, for screenshots), `--classic-peek` (Enhanced / 3D: the classic view key held, for before/after shots).

### Game data
The app never ships game files; it uses your own copy. The `Dune 2000/data` folder is looked up in
this order: `--data DIR`, the folder saved in the settings, next to the executable / the folder that
contains `Dune 2000.app` (and up to a few parent folders; `Dune 2000/data`, `data` or
`game/Dune 2000/data` inside them), then `game/Dune 2000/data` under the current directory. If
nothing is found, a screen asks for the folder (macOS folder picker) and remembers it.

**From the Gruntmods installer.** Gruntmods ships the game only as an installer
(`Dune 2000 Gruntmods Edition 1.6.7.exe`, NSIS). The data-folder screen (shown when nothing is
found; reachable from Options → *Change…* in Enhanced and Settings → *Data Folder* in Original) also
offers **Choose installer (.exe)…**: the app unpacks the installer itself (no Windows, no Wine;
`src/formats/nsis.*`; LZMA decoding by Igor Pavlov's public-domain LZMA SDK in `third_party/lzma`) with a progress bar and Cancel into
`~/Library/Application Support/d2k-mac/Dune 2000/GameData/<installer name>/Dune 2000/` (the D2K+
Windows editors are skipped) and uses its `data` folder. A finished copy has a `.d2k-extracted`
marker and is reused when the same installer is chosen again; an interrupted one (`*.partial`) is
redone. Data discovery also falls back to the newest finished copy there. The Gruntmods **cutscene
installers** (`Dune_2000_<Language>_Cutscene_Installer_1.1_Silent.exe` from files.gruntmods.com,
433–557 MB, 43 VQA movies) can be chosen the same way, whatever the data source: their movies go
into `~/Library/Application Support/d2k-mac/Dune 2000/Movies/`.
Command line: `--install-from INSTALLER.exe` (then continues with that data; `--install-only` exits
after unpacking; `--game-data-root DIR` unpacks under DIR instead, and then the settings are not
changed). Timing (M-series Mac, chunks decoded on all cores): 1.6.7 installer → 14003 files /
662 MB in ~6 s; English cutscenes → 518 MB in ~2 s.

Gruntmods 1.6.7 installer vs. the installed `game/Dune 2000` (checked byte for byte): identical
game content — no campaign/mission/map/UI/bin differences. Only in the installed copy: files the
launcher/MissionLauncher generate (`data/UIBB_<res>.R16/R8`, `*.bak`, `MissionLauncherBackups/`,
`spawn.ini`, logs); changed: `ddraw.dll`, `ddraw.ini`, `dune2000.ini` (cnc-ddraw/user settings);
only in the installer: `dotnetfx35setup.exe`. The installer's `movies/*.VQA` are 0-byte placeholders
like the installed ones; the real cutscenes come only from the separate cutscene installers
(`Dune2000Cutscenes.exe` is an NSIS downloader for them).
Settings (data folder, graphics mode, fullscreen, volumes, scroll speed, last skirmish lobby) live in
`~/Library/Application Support/d2k-mac/Dune 2000/settings.ini`; save games next to it in
`saves/SAVE0.SAV` … `SAVE9.SAV`.

**Updating (the user data folder).** Everything the app writes lives in the per-user folder
`SDL_GetPrefPath("d2k-mac", "Dune 2000")` (`platform/user_data.hpp`: `prefPath()`, fixed strings — not the app name,
so development builds ("Dune 2000") and release builds ("OpenD2K", `-DD2K_APP_NAME`) share it): `settings.ini`,
`saves/`, `highscores.txt`, `community_progress.ini`, `GameData/` (installer copies), `Movies/` (imported cutscenes),
`Voices/`, `previews/`, `artpacks/` (downloaded art pack). Nothing is written into the .app or next to the
executable, so replacing the app keeps all of it. Paths: macOS `~/Library/Application Support/d2k-mac/Dune 2000/`,
Windows `%APPDATA%\d2k-mac\Dune 2000\`, Linux `$XDG_DATA_HOME/d2k-mac/Dune 2000/` (`~/.local/share/...`), iOS the
app sandbox's `Library/Application Support/d2k-mac/Dune 2000/`, Android the app's internal storage (both survive
app updates). A saved data folder that no longer exists (the game folder was next to an old app that was
deleted) is skipped and discovery continues (next to the app, the cwd, usual installs, Documents, the newest
`GameData/` copy); the folder found is saved again.

**Options → Updates** (Enhanced front end, `platform/app_update.*`, `menu_update.cpp`): when Options opens (once per
run; never at startup; the shared switch "Check for updates when Options opens") the game asks GitHub's API for the
latest release (`api.github.com/repos/OpenD2K/opend2k/releases/latest`, only the generic `User-Agent`), compares
its tag with the compiled version (semver) and, if newer, shows "OpenD2K vX.Y.Z is available" with **Download**
(opens this platform's release file in the browser: macOS zip, Windows zip, Linux tar.gz, Android apk; iOS → the
release page) and **Release notes**. The Options row reads "Updates: OpenD2K vX available / art pack update
available / up to date". The art pack's state sits on the same page (its own page downloads it; an update is
offered only for a newer pack version or the same version republished with a different SHA-256). No self-update.
Test: `--update-url URL` / `D2K_UPDATE_URL` / settings.ini `appUpdateUrl` (plain http only to localhost),
`--update-check` (headless), `--menu-page updates[-available|-uptodate|-checking|-failed|-disabled|-live]`.

### App bundle
`cmake --build build` also produces `build/Dune 2000.app` (target `d2k_app`, option `D2K_APP_BUNDLE`,
bundle id `com.d2kmac.dune2000`, high-DPI capable). libSDL3 is copied into `Contents/Frameworks` and
linked via `@rpath` (`@executable_path/../Frameworks`), then the bundle is ad-hoc signed, so the app
runs outside the build tree. `LSMinimumSystemVersion` is `CMAKE_OSX_DEPLOYMENT_TARGET` (default: the
SDK version, because Homebrew's SDL3 is built for the current macOS); for older macOS build SDL3
from source with a lower deployment target and pass `-DCMAKE_OSX_DEPLOYMENT_TARGET=…`.

## Menus
In **Graphics: Original** the whole front end and the in-game Esc menu are the game's own page
scripts (`UI_DATA/*.uil`, re/notes/campaign-flow.md §6): main menu (Start New Game → difficulty →
house selection, Load Mission, Select Mission = the original mission select page (54 campaign
mission buttons; its top-left button slot holds our **Community** page: tabs Campaigns / Maps &
Missions in original-style lists — a campaign opens its mission list with progress (won / locked /
choice / bonus) and the briefing, Start / Cancel; see docs/community-campaigns.md), Multiplayer Game →
game type → Practice = the original skirmish page with name, house, colour, map list, difficulty,
unit count, tech level, credits, AI players, crates and worms; Game Options = the settings box
(fullscreen, smooth motion, mouse controls, cutscenes, *Graphics:* preset button cycling Original → Enhanced → 3D —
Enhanced / 3D apply the preset and open the modern Graphics page; Custom is shown when the values are mixed —, resolution, Game Settings = game speed /
scroll rate / music / SFX volume + our Extended queue, Cheats, Modern effects, HD art and Rally points check boxes → music selection, data
folder — our rows are additions), Exit
Game, the Westwood logo → scrolling credits); after a skirmish the original result table (score.uil)
→ the lobby, a lost skirmish first asks "Replay mission?". In game, Esc opens the original Options
box: Load / Save (description + slot, overwrite question) / Delete Mission, Game Settings, Abort
Mission (Abort / Restart / Cancel), Briefing (campaign: the in-game briefing list), Resume, and our
Cheats button (when the Cheats option is on).
**Enhanced** keeps the modern menus below.
- **Main menu** (Enhanced; original title art): Campaign, Skirmish, Load game, Options, Quit.
- **Load game**: the 10 save slots (name = mission title / description, "Campaign map" or "Battle", date/time
  of the save) with details (battle: campaign/skirmish, map, game time; campaign map: house, mission number,
  difficulty); Load restores the saved game or the territory map. In the original Load/Save lists battle saves
  are white, campaign-map saves in the list colour (as in the original).
- **Campaign** — the original flow, drawn from the game's own page scripts (`UI_DATA/*.uil`,
  re/notes/campaign-flow.md): house selection (HouseSel) → "Do you want to play House X?" →
  the **territory map** (DUNEMAP + the house-coloured territories of `campaign.uib`; the map and
  the newly won territories fade in one by one with their announcements, then the animated
  pointers on the two territories to attack; Prev/Next/Load/Save/Cancel as in the original — **Save**
  writes a *campaign-map save* (house, mission number, difficulty; Save.uil with description and slot,
  over the map), **Load** opens Load.uil over the map (Enhanced: the modern load page; Cancel/Back return
  to the map); loading a campaign-map save — from the menus or from a battle — returns to that territory
  map) → the
  **picture-book briefing** (`L<h><n>MIS`: Mission title + Text.UIB briefing, arrows to the unit
  pages, Start) → battle → **score page** (units / buildings destroyed and spice harvested as
  150/100/50-point shares times the mission's multiplier, counted up with S_SCORETICK, house bars,
  totals, rank and the high-score table with name entry; score.aud) → the territory map with the
  next mission (after a loss: the same mission again, numbers halved) … after mission 9 → main
  menu. The cutscenes play where the original plays them (see **Cutscenes**). Same pages in
  Enhanced mode (art smoothly upscaled).
- **Community campaigns & maps** (Enhanced main menu "Community"; Original: Select Mission → the
  top-left "Community" button; the house screen's "Mercenaries" button opens its Maps tab): the 24
  community campaigns of Gruntmods 1.6.7 (Summers' Solstice from its SplashArtData arcs; Coalition
  of Nobles + New Enemies, Butlerian Jihad trilogy, War of the Landsraad A/H/O/S, War of Assassins
  A/H/O, WoA Sandstorm A/H/O, AntiFremen, Fremen Warriors, Smugglers, Spacing Guild, New House,
  Ixian, Mercenary, Rise of the Mercenaries — grouped from the mission names) with progress saved
  in `community_progress.ini` next to settings.ini: a win opens the next step; "Option"/version
  steps offer a choice, bonus missions are optional. Maps & missions lists all 275 custom missions.
  Per-mission data swaps like the Gruntmods launcher: the mods folder's replacement files (DATA.R16,
  tilesets, Templates/TILEATR/ARMOUR/SPEED/BUILEXP.BIN, UI_ENG/UIBB, samples + GAMESFX sounds), the
  campaign's COLOURS.BIN, the mission's TextUib, `[Text]` message strings, `[Vars]` rules (VARS.BIN
  set + InfiniteSpice, cost/build-speed percentages, guardModeRadius,
  buildingsAlwaysNeedPrerequisites, returnCreditsToSpiceStorage, infantryReleaseLimit/Chance,
  uncloakRemainingStealthUnit), intel texts in the briefing. All 275 missions load. The 1.06p+
  **extended event system** runs extended .MIS files (Summers' Solstice, 11 New Enemies missions):
  up to 1024 events / 256 conditions, run-once / blocked / OR flags, object filters, the new
  event and condition types the shipped missions use, extended-map unit/building/crate specials,
  `Music=*` (docs/extended-events.md; a few unused types are not implemented).
- **Play as any side** (not original; docs/play-as.md): the Enhanced briefing page (campaign mission list, custom
  and community missions) has a "Play as" choice — the mission's own side (default) or any other side that starts
  with units or buildings (e.g. the Emperor in Atreides 9a); Graphics: Original's custom / community pages have an
  "As: <house>" button between Cancel and Start (the original 54-button mission select page starts as written).
  The chosen side is human, the mission's side gets a computer player from its .MIS AI block; Mission Win / Fail
  events count inverted when the two are enemies (the same when allied/neutral); you also lose with nothing left
  and win when no hostile side is left. Chosen per start (nothing saved in settings), kept in save games; such a
  game does not advance campaign / community progress. `--map X --play-as N` starts as side N.
- **Skirmish**: map list (`maps/MultiplayerMaps.ini`) with a map preview, your house, 1–7 computer
  opponents (limited by the map), starting credits, tech level 1–7, AI difficulty, starting units.
  Stock MP maps with a forced tileset use it (2Play11 BLOXTREE, 2Play12 BLOXBASE, 6Play3 BLOXWAST,
  the Christmas maps BLOXXMAS — converted from the 8-bit .R8 through bin/Palette.bin like LoadTileset 0x462d10).
- **Options**: Graphics (opens the Graphics page; the button names the active preset), cutscenes, music/sound volume,
  language, game speed (original GameSpeed 0..100, default 75 = 20 ticks/s; 95+ = 60 ticks/s), scroll speed (original
  ScrollRate 4..40, default 20), mouse controls (Original / Modern), smooth unit motion | frame time, production queue
  (Original / Extended, default Extended — see Controls), cheats (On / Off, default On) | factory rally points (On /
  Off, default On), game data folder. The rows scroll inside the panel (mouse wheel, draggable scroll bar, the keyboard
  focus is scrolled into view); Back stays fixed below it.
- **Graphics page** (Options → Graphics; `--menu-page graphics|graphics-original|graphics-enhanced|graphics-3d|graphics-custom[-end]`):
  tabs Original / Enhanced / 3D / Custom at the top (big enough to tap). The first three are one-tap presets — a tap
  applies it; the tab shows a preview (shipped pre-rendered in `assets/previews` for the default setups and every
  Custom row value; other combinations rendered offscreen and cached in `<pref>/previews`; docs/enhanced-graphics.md
  "Presets"), one line on the look and its main values.
  **Original** = graphics Original (the exact 1998 look); **Enhanced** (the default for new players) = graphics Enhanced,
  terrain Faithful (Designed (faithful), terrain v3; until 2026-11-11: Designed), Modern effects, soft shroud, health
  bars Selected, panel opaque, classic view Hold, art = the
  chosen redesign pack (pack.ini `kind=rendered`) or the original sprites — never an upscale; **3D** = the Enhanced
  values + graphics 3D, 3D terrain Faithful, sun shadows on, 3D models New (the OpenD2K 3D art pack; never the
original sprites — without the pack the 3D tab only shows "needs the OpenD2K 3D art pack" and `--graphics 3d` runs as
Enhanced, docs/3d-mode.md "Objects: models only"). **Custom** lists every graphics
  value grouped: Mode, Terrain, Art, Effects, Shroud, Interface (panel transparency, health bars), 3D (terrain,
  shadows, models), Display, Classic view; changing any of them (here or in the pause menu) makes the setup Custom.
  Display (resolution in Original / UI scale in Enhanced and 3D, fullscreen, render resolution) is not part of a
  preset and shows under every tab. The active preset is underlined; settings.ini `graphicsPreset`
  (original|enhanced|3d|custom; values that no longer match read as the preset they match, else Custom). The page
  stays modern while Original is picked; Back returns to Options (the original SETTINGS page in Original).
- **In game**: Esc (or P) opens the pause menu (Resume / Options / Save game / Load game / Restart
  mission / Quit to main menu / Quit game / Cheats when enabled); Save game / Load game show the 10 slots (saving picks the
  first empty slot, else the oldest; "Overwrite" replaces a used one); Options has the gameplay rows plus the graphics values switchable in battle (resolution / UI scale, render, art, effects, health bars, shroud, sun shadows, panel transparency, terrain, classic view key — not the graphics mode, chosen in the main menu only) and the volumes; they scroll when the window is too small, Back stays fixed. After the MISSION ACCOMPLISHED / FAILED banner, a click or Enter returns to the menu (campaign missions: the score page, then the territory map).
- Keyboard: arrows/Tab move the focus, Left/Right change values, Enter activates, Esc goes back.

## Cutscenes
The original's VQA movies play where the original plays them (re/notes/campaign-flow.md §7): the
intro at startup (the full intro the first time, the title movie afterwards), before house
selection (Campaign), the house movie, the map-unfolding movie, the briefing movie before each
briefing page, the defeat movie after a lost mission, the house ending after mission 9, and the
original main menu's "Replay intro". Esc, Space, Enter or a click skips one. Settings: Enhanced
Options → *Cutscenes* Off / On / On + intro; Original Settings → *Cutscenes*. The Enhanced main
menu has **Movies** to rewatch any of them.
- Decoders are our own: `src/formats/vqa.*` (PC hicolor VQA: Format80 codebooks, RGB555 vector
  pointers, IMA ADPCM sound) and `src/formats/ps1_str.*` (PS1 STR: MDEC v2 video, XA ADPCM).
  Player: `src/platform/movie_player.*` (frames streamed into a texture, synced to the audio clock;
  nearest scaling in Original graphics, linear in Enhanced, letterboxed). docs/formats.md.
- Where the movies come from, first match: `<data>/../movies/*.VQA` (0-byte placeholders are
  skipped), `~/Library/Application Support/d2k-mac/Dune 2000/Movies/*.VQA` (the data-folder page's
  *Choose installer* takes a Gruntmods cutscene installer and unpacks it there), then a **PS1 disc
  image** (data-folder page → *PS1 disc image…*, a `.cue` or `.bin` of Dune 2000 for the
  PlayStation; settings.ini `ps1Image`; `--ps1 IMAGE` for one run). The PS1 disc has 38 of the 41
  movies (intro, 27 briefings, endings, defeats, house montages) at 320x208; it has no title,
  map-unfolding or Tiberian Sun clip.
- Command line: `--movie NAME|FILE.VQA` plays one at startup (e.g. `--movie mve_briefing_ordos2`;
  with `--menu-screenshot` it captures the movie after `--frames` frames), `--movie-house H`
  for `mve_lost`.

## Languages
English / Français / Deutsch / Español / Русский (PS1) / Русский (перевод) / Русский (AI) — one pick switches text, fonts, the
sidebar art (ui_<lng>.r16), speech and cutscenes (docs/languages.md). Enhanced Options → *Language* plus the
Advanced rows *Text | Voices* and *Cutscenes* (each from any language); Original Settings → *Language* (our
page D2K_LANGUAGE, `--menu-page language`). A change reloads the front end; a running battle keeps its language.
- English/French/German/Spanish come from the Gruntmods data (Text<Lng>.UIB, FONT<Lng>, GAMESFX<Lng>,
  ui_<LNG>.r16, `*_F/_G/_S.VQA` from that language's cutscene installer, else the English movies).
- Русский (перевод): a Russian PC translation folder: the Fargus CP1251 text + Cyrillic fonts, the Stream dub
  and its Russian-dubbed VQAs. It is found at `<Dune 2000>/../ru-pc` or set on the data-folder page
  (*Translation folder…*, settings.ini `translationDir`, `--translation DIR`). The 96 Gruntmods-only keys stay English.
- Русский (PS1): speech and movies from the Russian PS1 disc image (`ps1Image`). 201 PC speech files map to PS1
  slots (docs/ps1-assets.md). Lines the disc lacks come from the Stream dub, else English. Text as Русский (перевод).
- Русский (AI, оригинальные голоса): the Russian text as Русский (перевод) (Fargus + fixes), speech from our
  generated AI voice set (251 lines in the original speakers' voices), the Stream dub's Russian cutscenes. How to
  get it: run `tools/voice/gen_ru_ai.py` once with your own Yandex SpeechKit key (docs/ai-voices.md "Full set");
  it writes `<pref path>/Voices/ru-ai/GAMESFX/*.AUD` (macOS: `~/Library/Application Support/d2k-mac/Dune 2000/`),
  then pick the language. Without that folder the entry is dimmed with the hint. Lines the set lacks play in
  English (never the Stream dub); SFX stay the game's. Other folder: settings.ini `aiVoicesDir`, `--ai-voices DIR`.
- Command line: `--language en|fr|de|es|ru-ps1|ru-pc|ru-ai`, `--text-language` / `--voices-language` /
  `--movies-language CODE`, `--translation DIR`, `--ai-voices DIR`. `--sound-test` logs where each speech sample comes from.
- Code pages per source: the language's string table is drawn in its code page (CP1251 Russian / CP1252); text
  from community files (mission .ini names / briefings / [Text], their own TextUib, intel .txt, MultiplayerMaps.ini,
  Players/*.ini) is Latin unless the file is UTF-8 or Russian, and is converted where it is loaded (the catalog keeps
  UTF-8; in-game strings are recoded to the active code page). The Play font draws accents as they are; the
  Cyrillic bitmap font gets the bare letter ("Coup de Grâce" → "Grace", not "Grвce").

## Graphics: Original / Enhanced
Options → Graphics in the main menu (the Graphics page's presets / Custom "Graphics mode"; the original SETTINGS page's
Graphics button; a running game keeps its mode), `--graphics` on the command line. New players (no settings.ini) start
with the Enhanced preset; one-frame / screenshot / simtest runs keep the built-in defaults (Original).
- **Original** (default of the one-frame runs): the 1998 presentation. Sprites and tiles pixel-exact (nearest scaling),
  the original selection brackets / health bars / pips / group digits, the original placement
  markers, and the original in-game frame (UIBB.R16 + UI_ENG.R16, `src/render/classic_sidebar.*`,
  re/notes/ui.md): OPTIONS button and credits counter on top, the radar (minimap, or the house
  logo fading in without an outpost / below 100 % power), repair / sell / guard / retreat buttons,
  UPGRADE (upgrade page) and STARPORT (starport page: left click picks a unit, right click puts it
  back, the picked count on the icon, PURCHASE sends the order, MAIN cancels it) buttons, the
  power bar, the building (left) and unit (right)
  strips with scroll arrows, the grey icons + build "clock", Ready / Hold in the cycling green, the
  mission timer and message lines in FONTCOL. **Resolution** (Options → Resolution, like the
  Gruntmods/CnCNet high-res patch's GameWidth/GameHeight): Auto (default: the largest integer scale
  with at least 640x400 game pixels, filling the window), 640x400 … 1920x1080, or Native (the window's
  pixel size at scale 1). The frame is drawn at that game resolution into an offscreen target and
  scaled to the window (whole multiples: nearest; other factors: pixel-art filtering; letterboxed).
  Above 640x400 the battlefield grows, the sidebar keeps its size at the right edge and gets more
  icon rows, and UIBB.R16 is extended the way the high-res patch builds UIBB_<w>x<h>.R16 (options bar
  tiled, sidebar build block tiled, power bar anchored to the bottom).
  Mouse: left click an icon builds / picks up a ready building, right click holds then cancels;
  arrows scroll by one (right click: a page; the wheel works too); repair / sell toggle a click
  mode for your buildings (repair also sends a damaged vehicle to a repair pad); guard / retreat act
  on the selected units; radar click centres the view (with units selected: moves them there),
  right click deselects; OPTIONS opens the pause menu. The view has the fixed original scale.
  **Tooltips** as in the original (FUN_0044b5c0): over the build strips / starport / upgrade
  pages at once (name + price, yellow boxes left of the icon column); after the mouse rests ~18
  frames over repair / sell / guard / retreat (and the radar button's MapToggle in skirmish), the
  power bar ("Power 155%") and the battlefield ("Unrevealed Terrain", own unit / building names,
  "Enemy Unit" / "Enemy Structure", grey tile tooltips such as "Spice").
- **Mouse cursors** (both modes): the original animated MOUSE.R16 cursors replace the system
  cursor — move / attack / can't move / select / enter (dock, repair pad, capture) / deploy /
  sabotage / infantry-only, sell / repair (and their "can't" variants) in the click modes, air strike
  and death hand targets, the radar-move ring, scroll arrows (crossed out at the map edge); menus
  show the arrow. Chosen like the original every frame (`src/render/cursor.*`, re/notes/ui.md
  "Cursors"); with Modern controls they describe the right-click order. Original: UI scale,
  pixel-exact (one game pixel per cursor pixel); Enhanced: Scale2x frames at the original size in
screen points (like a system cursor; not scaled by the window or the zoom). `--cursor-at X Y` / `--cursor-mode N`
  place a virtual cursor for `--screenshot` runs.
- **Enhanced**: Scale2x-upscaled sprites/tiles with smooth filtering, free zoom (wheel), and the
  Enhanced in-game UI, a modern procedural skin (docs/enhanced-graphics.md "In-game UI"; scales with
  the window: 1.5x at 1280x800, 2x at 1920x1080, or the UI scale setting):
  - right-hand column: the minimap in a modern frame ("radar offline" screen while the radar is off -
    outpost + 100 % power rule, original object rules), repair / sell / guard / retreat buttons and
    UPGRADE / STARPORT (enabled under the original's conditions); below it the production panel: a
    power meter (the original percent rule and colours), four tabs (Buildings / Infantry / Vehicles /
    Special) as a segmented control with a ready badge and a progress line, auto-selecting a tab that
    has items; a 2-column grid of large icons in bezels with empty bezels below, the original clock
    wedge, pulsing READY / HOLD, costs, extended-queue counts / infinity, greyed icons exactly like the
    original sidebar. Special = palace weapons / air strike, upgrades (left click upgrades / resumes,
    right click holds then cancels) and the starport (PURCHASE / CANCEL, prices, stock, picks; leaving
    the tab with picks cancels them). Two scroll buttons (and the wheel) scroll the grid.
  - top: OPTIONS (pause menu), power readout with a lamp, credits readout, the mission timer when one runs.
  - no selection panel: health bars above selected units and buildings (a selected building: brackets
    around its whole footprint, the bar centred above it, the PRIMARY "1"); deploy / self-destruct /
    thump / stealth with D or a second click, primary with a second click on a factory, upgrades on
    the Special tab, control groups on the keys (Ctrl+1..0 / 1..0).
  - modern procedural chrome at native resolution (no original UI bitmaps) in Dune 2000's palette: a
    dark brushed-bronze / leather column with a sand-amber edge (a faint house-colour glow), volumetric buttons with vector icons (hover / pressed / active / disabled), a
    segmented tab control, modern credits / power / timer readouts, a power meter, icon bezels with the
    AI-upscaled icons when an art pack is installed, "radar offline" screen; text in the embedded Play
    font (OFL; Cyrillic) with our labels translated (EN/FR/DE/ES/RU); Settings "UI scale" (Auto /
    100..300 %, `--ui-scale`). Spice pips: harvester load (green / black), refinery / silo storage.
  Hover tooltips on everything (icons, tabs, buttons, plates).
  - Options "Panel transparency" (Off default / Light / Medium, settings.ini `panelTransparency`,
    `--panel-transparency off|light|medium`; not original): the column's background becomes translucent
    over a dark veil and the battlefield is drawn under it (icons, text, buttons stay opaque; clicks on
    the column still never reach the map; the camera limits use the visible part).
  - The modern front end (main menu, missions, skirmish lobby, options, data folder, load, movies,
    community pages), the Enhanced pause menu / in-game options / save-load slots, the cheat menu and
    the end-of-game prompt use the same Play font and are translated too (EN/FR/DE/ES/RU, our strings
    only; game texts stay the game's). The original .uil pages keep the original bitmap fonts.
- **Art packs and modern effects** (Enhanced only; `docs/enhanced-graphics.md`): replacement images at any
  resolution from `artpacks/<pack>/` (frames by DATA.R16 index, terrain tiles per tileset, optional
  house-colour and shadow masks, falling back to the upscaled original; mip-mapped), and render-side
  effects: particle fireballs / smoke plumes / sparks / debris, muzzle flashes, rocket smoke and sonic
  rings, dust trails and tread marks, burning buildings and vehicles, spice sparkle, light glows,
  screen shake, smooth vehicle and turret turning, a vignette. They hold 120 Hz on a
  ProMotion display. The Original mode ignores both settings.
- **Art pack download** (Options → **Art pack**; `platform/artpack_download.*`, `menu_artpack.cpp`, docs/release.md
  "Art pack"): shows the installed version / not installed / "update vX available", Download with a progress bar
  (MB, MB/s), Cancel (the next Download resumes), errors with Retry and "Open download page". The Graphics page's
  3D tab offers "OpenD2K 3D art pack, NNN MB — Download / Later" while the pack is missing; its Enhanced tab says
  "Art: Original sprites (no art pack)" with **Get the art pack**, the Custom tab's Art header has the same button, and
  the Original front end's SETTINGS page has an **Art pack** button (half of the Resolution row).
  **First-run welcome page** (`Menu::pageWelcome`, once per install, settings.ini `firstRunArtPackAsked`; both front
  ends): right after the game data is set up - what OpenD2K is, "Choose your look" (Original / Enhanced / 3D cards with
  the preset previews; a tap applies the preset; 3D disabled until the pack is installed, a tap marks it "after the
  download" and the 3D preset is applied once the pack installs), and the art pack offer (size from the manifest fetched
  when the page opens, else "about 600 MB"; Wi-Fi note on phones/tablets; Download with progress, it continues in the
  background) with Not now / Continue. Older settings.ini files without the key never show it. `--menu-page
  welcome[-downloading|-installed|-failed|-nomanifest|-graphics|-graphics-custom]`; `D2K_PREF_DIR=<folder>` runs with a
  temporary settings folder (a new one = a new player). The manifest
  (`artpack-manifest.json` of the latest GitHub release of OpenD2K/opend2k) is fetched only when Options opens
  (toggle "Check for updates when Options opens") or on "Check now"; nothing downloads before Download is pressed.
  Size + SHA-256 checked, unpacked with a path-traversal guard into `<settings folder>/artpacks/<name>` (temp
  folder, then swapped in), then selected as Art + 3D models: New without a restart. HTTPS via NSURLSession
  (macOS/iOS), WinHTTP (Windows, untested on hardware), libcurl loaded at run time (Linux, tested on macOS via
  `D2K_HTTP_BACKEND=curl`). Headless: `--artpack-check`, `--artpack-install`, `--artpack-manifest URL`,
  `--artpack-dir DIR`, `--http-get URL FILE`.
- **Terrain** (Enhanced; its own switch, independent of Art and Effects; `docs/enhanced-graphics.md`
  "Designed terrain", "Designed (faithful)"): Options / in-game options → Terrain: Original tiles / Upscaled
  (built-in or the art pack's tiles) / **Designed** (terrain v2): the 3D mode's designed terrain - CC0
  materials, rock-scan crags along the cliffs, sun shadows - baked from straight above into 16x16-cell chunks
  at map start (~0.6 s), exactly on the tile grid; harvested / regrown spice and concrete re-bake small
  regions / **Faithful** (terrain v3, the Enhanced preset's terrain): the same bake laid out after the
  original tiles - the original's rock outlines, cliffs as shaded relief in its own colours (lit tops, dark
  faces, rim, soft shadows), non-buildable rock visibly broken, the original's 32x16 concrete slabs (the same
  with and without a building on them); sand and buildable rock as in Designed. The look follows the game's
  passability: dark raised cliffs only where no vehicle can go, rock vehicles can use always light and level,
  blocked rock that the original paints pale marked by boulders. Spice lies in organic patches in both. 3D has the same choice ("3D
  terrain": Original tiles / Designed / Faithful). `--terrain2d original|upscaled|designed|faithful`,
  `--terrain3d tiles|designed|faithful`.
- **Lighting** (Enhanced and 3D; not original; docs/enhanced-graphics.md "Lighting"): Options → Graphics → Custom →
  Lighting and the in-game options: **Time of day** Day / Dawn (low pink-orange sun) / Dusk (red-gold, long shadows) /
  Night (cool blue moonlight, still readable) / Cycle (a 16-minute day over a long battle, from the game clock) and
  **Lighting** Classic (the old light) / Modern (warm sun against a cooler sky, more contrast, bloom on highlights,
  stronger glints on 3D metal; the Enhanced / 3D presets). Lamps at dusk / night (faint by day with Modern):
  vehicle headlight beams and red tail lights, harvester beacons and work lamps, flyer navigation lights, building
  lamps (lit doors / bays, floodlights, beacons, refinery glow, starport landing lights, repair pad lights; dimmed
  and flickering on low power); 3D models' `fx_light*` anchors. Redesigned buildings (3D models / the redesign pack)
  glow from inside instead: emissive lamps, wind trap energy cores (pulsing), cyan strips, the refinery's windows
  (3D: glTF emission; 2D: the pack's `.glow.png` layers), off without power / when destroyed, no more lamps floating
  beside them (docs/enhanced-graphics.md "Emission"). 2D: the terrain bake follows the light (one
  re-bake on a change), a grade over the map; 3D: the sun moves, spot / point lights. `--time-of-day`, `--lighting`.
- **Quality** (Enhanced and 3D; not original; docs/enhanced-graphics.md "Quality"): Options → Graphics → Custom →
  Quality: **Quality** Low / Medium / High (a shortcut: HDR off/on/on, MSAA 1x/2x/4x, anisotropy off/4x/16x; the
  Enhanced / 3D presets set High), **HDR rendering** (with Modern lighting: half-float battlefield, bloom of the
  unclamped values, filmic tone map), **Anti-aliasing (3D)** Off / 2x / 4x, **Texture filtering (3D)** Off / 4x / 16x,
  **HDR display** Off / On (EDR on Mac XDR screens / Windows HDR: highlights above the UI's white; read at start -
  "Restart to apply"; only when the game starts in Enhanced / 3D; SDR silently where the display has none).
  Display: **VSync** On / Off / Adaptive (Adaptive falls back to On on the GPU renderer) and **Frame limit**
  Unlimited / 30 / 60 / 120. `--quality low|medium|high`, `--hdr on|off`, `--hdr-output on|off`, `--msaa 1|2|4`,
  `--aniso 1|4|16`, `--vsync on|off|adaptive`, `--fps-limit 0|30|60|120` (one-frame runs: struct defaults - no HDR,
  MSAA 4x, anisotropy 16x - unless given).
- **Classic view** (Enhanced and 3D; not original, like the C&C Remastered swap key): hold **Tab** and the
  battlefield shows the original 1998 graphics - original tiles, pixel-exact sprites, Classic effects, the
  original shroud edge and selection overlay - instantly, mid-game and while paused; a small "Original 1998
  graphics" caption sits at the top centre. Only the battlefield swaps (the Enhanced panel, radar and top bar
  stay); simulation, camera, selection and orders are untouched (3D: the 2D original view of the same spot,
  input follows the 2D view while it shows). Options / in-game options → *Classic view key (Tab)*: Off /
  **Hold** (default) / Toggle (a press switches it on / off); settings.ini `classicView`. Touch: the **1998**
  button. The Original mode ignores it. Tab, not Space: the original opens its in-game menu on Space (like Esc;
  Game__MainLoopFrame 0x448550) and binds nothing to Tab. `--classic-peek` captures it (`docs/enhanced-graphics.md`
  "Classic view").
- **Fullscreen edge scrolling** works against the display edges: also with the pointer in the
  menu-bar band around a MacBook notch (the game cursor stays clamped to the content area).
- Carryalls show their cargo: a carried harvester hangs under the carryall, and a delivery carryall
  bringing one unit (the free harvester) draws it below itself (0x42a5d0).

## Graphics: 3D (2.5D prototype)
Options → Graphics → **3D** (main menu) or `--graphics 3d`: the Enhanced UI over a 3D battlefield
(docs/3d-mode.md) — heightfield terrain from the map (rock plateaus, cliff slopes, rolling dunes,
outcrops) with the original tiles, sun light and soft terrain shadows, soft shroud, units /
buildings / projectiles / explosions as billboards using the existing sprites (facing frames turn
with the camera). Camera: pan as usual, wheel zoom, **Q/E** or middle drag rotate, **Ctrl+wheel**,
Page Up/Down or middle drag tilt, **Home** resets. All mouse input (selection, orders, placement,
minimap) works through 3D picking. Needs the SDL GPU renderer (Metal); otherwise the game shows the
Enhanced map and logs why. Modern effects, the rally flag, health bars and the cheat spawn preview are drawn in 3D
too; the PS1 disc's building/vehicle models or the built-in "new" model pack can replace the billboards
(Options → *3D models*, docs/3d-models.md).

## Render resolution, iPad
Options → *Render*: **Auto** / Full / Half (settings.ini `renderResolution`, `--render-resolution`; applies at the
next start, the row says "Restart to apply" until then). Full creates the window with
`SDL_WINDOW_HIGH_PIXEL_DENSITY`: a Retina Mac or iPad draws at its real pixels (2560x1600 output for a
1280x800-point window). Half draws at point resolution (a quarter of the pixels). Auto = Full, except Half on the
A10/A12-class iPads (`platform_info` DeviceTier::Low). Mouse/touch positions are converted from points to output
pixels as they arrive (`platform/pointer.hpp`); the Original game resolution, the Enhanced UI scale, the map zoom,
the touch targets and the scroll speed are chosen in points, so both look the same size, Full is sharper.
One-frame runs (`--screenshot`, `--menu-screenshot`, `--simtest`) use Half unless told otherwise, so screenshots keep
the `--window` size. The iPad build (iPadOS 17+, touch controls, data import from the Files app): docs/ios.md.

## Phone layout (iPhone)
Enhanced / 3D on a phone-sized screen (Options → *Touch UI* **Auto** / Phone / Tablet, settings.ini `touchLayout`;
Auto = Phone on an iPhone or a short side under 500 points, `--phone-layout` forces it on the Mac): the battlefield
fills the screen; a slim semi-transparent top bar (menu, click-mode chip, credits, power bar, mission timer, a
quick-build strip of the 5-6 most-built units with pins, the palace / air-strike slot, radar and hide-UI buttons),
a small see-through radar top-left (only with a working radar), the full sidebar as a right-edge drawer, small
command buttons bottom-left; the HUD fades when idle. *HUD size* Tiny / Small / Normal (`phoneUiSize`).
Details, gestures and screenshot flags: docs/ios.md "Phone layout".

## Controls
Mouse scheme: **Original** (default, as in Dune 2000) or **Modern** (Options → Mouse controls).

| Input | Original | Modern |
|---|---|---|
| Left click own unit | select it (Shift: add/remove); click a selected MCV again: deploy | select it |
| Left drag | box-select own units (replaces the selection; Shift adds) | box-select (Shift adds) |
| Left click with units selected | ground: move; enemy unit/building: attack; own refinery (harvesters): dock; repair pad (vehicles): repair | select |
| Left click own building | select it; again on a selected factory: make primary | select it |
| Ctrl + left click | force attack (any unit or building, else the ground) | — |
| Alt + left click on enemy infantry | force move: crushers drive over it (move order, no attack) | — |
| Left click with a turret selected (no units) | enemy unit/building in reach: the turret fires at it (Ctrl: also own/allied ones); no attack-ground | select |
| Right click | deselect all | smart order for units or a selected turret: attack enemy under cursor, else move; Ctrl force attack, Alt force move (as Original) |

Both schemes:

| Input | Action |
|---|---|
| Right drag, arrow keys (screen edges in fullscreen) | scroll the map |
| Mouse wheel | zoom (over the map) / scroll the build lists (over the sidebar) |
| Minimap click/drag | jump the camera |
| Sidebar left click | build; when READY, pick up the building to place it |
| Sidebar right click | pause, then cancel (refund) |
| Unit icons, Production queue **Extended** (default; not original) | left click +1, Shift+left +10; right click: build forever (∞); right click on an ∞ type: cancel it (refund like the original cancel); Ctrl+right (or Cmd+right): one fewer (an ∞ type finishes its current unit and stops; the last one in production is cancelled with refund). Per factory the types are built in the order chosen; finite counts are completed in place, ∞ types alternate round-robin (re/notes/economy.md §10). Icons show the count / ∞. Palace weapons keep the original clicks. **Original**: the original pick / hold / cancel |
| Placement | left click places (green = valid), right click / Esc cancels |
| S / D | stop / deploy MCV |
| Ctrl+1..9 / 1..9 | assign / recall control groups |
| M | toggle debug markers (spawn points, worm spawners, blooms) |
| Esc / P | cancel placement / pause menu (with in-game options) |
| F | toggle the shroud |
| Tab (Enhanced / 3D) | classic view: the battlefield in the original 1998 graphics while held (Options → Classic view key: Off / Hold / Toggle) |
| Ctrl+Shift+C | cheats page (again: close); only with the Cheats option on |
| Rally point (not original; Options → Factory rally points, default On) | select one own factory (barracks, light/heavy factory, high-tech, WOR, starport) with no units selected, then **Original**: Alt+left click on the ground (plain left/right clicks keep their original meaning — right click still deselects); **Modern**: right click on the ground. The same click on the factory itself clears it. The selected factory shows a flag and a dashed line. Units leaving it (and frigate deliveries to that starport) drive there once out; harvesters only follow a point on spice (`src/game/rally.cpp`, orders + saved state) |

## Cheats
Not part of the original. Open with **Ctrl+Shift+C** during a game or the **Cheats** button of the
pause menu (both graphics modes); the Options row **Cheats: On/Off** (default On) removes both; the page uses the modern widgets in both graphics modes. Everything is a
queued order (`World::orderCheat*`, `src/game/cheats.cpp`) executed at the next tick, so it is
deterministic and saved; `World::cheatsUsed()` records that cheats were used (saved; for the score
screen later). The page acts on the side picked at the top (any side, AI and neutral included).

| Section | What it does |
|---|---|
| Money | +1000 … +100000, Zero, or an exact amount (credits = cash + stored spice; pending payouts dropped) |
| Tech level | 1..9 (9 also unlocks construction yards); the sidebar is rebuilt at once |
| Build list | per building/unit template: Default (original rules) / Allowed (listed regardless of prerequisites, tech, house) / Blocked. An allowed unit without a factory it can leave from is still built and arrives by carryall |
| Free delivery | any unit types in any counts, free: by starport frigate when the side has a starport (no stock/price/6-unit limit; 39 units per trip, one frigate at a time), otherwise by carryall to a free tile near the base. Flyers/worms appear directly |
| Reinforce | carryall reinforcements like the missions' Reinforcement events (same code path, `World::startReinforcement`): pick unit types/counts, **When** (Now, or in 5 s … 30 min of game time at the current game speed), **From edge** (nearest / north / east / south / west), then "Order - drop at the base" (a free tile near the side's base when they are due) or "Order - pick the drop point..." (left click on the map; Shift keeps the mode). 25 units per carryall, up to the side's 10 delivery slots (the rest follow). Pending reinforcements of all sides are listed with a countdown; "Cancel selected" removes one. Scheduled and saved in the world (`ScheduledReinforcement`, save version 5) |
| Spawn | unit (count per click) or building (optionally on concrete) for the chosen side: "Place on map", then left click spawns (Shift keeps the mode), right click / Esc stops. Units take the nearest free tiles; buildings need a free on-map footprint (adjacency and terrain ignored) |
| Toggles | reveal the map; instant build (production finishes in one tick, still paid) |

Layout: labels, values and switches are placed from measured text widths (one value text per toggle, the switch right-aligned); sections without a grid (Money, Tech level, Toggles) scroll when the window is too small.
Screenshots: `--menu-page pause-cheats[-money|-tech|-build|-deliver|-reinforce|-spawn|-spawn-buildings|-toggles]` (needs `--map`).

## What works (from the original code)
- All data formats: tilesets, sprites, UI graphics, maps, missions, Templates.bin and other rules
  tables, fonts, string tables, all audio (AUD + SOUND.RS).
- Mission setup in original order (concrete under buildings, initial states, object pool).
- Ground movement and the original pathfinder; infantry sub-cells and animation; crushing.
- Combat: targeting, weapons, bullets (homing/arcs/debris), explosions, warhead×armour damage,
  terrain damage, deaths, turrets.
- Economy: harvesting, refineries/silos, spice→credits, power, production queues with
  progressive payment, placement, sell/repair/primary, MCV deploy, carryall harvester delivery,
  the starport (fluctuating prices, stock, picks paid up front, PURCHASE -> the frigate flies in
  after starportFrigateDelay ticks, lands on the starport and unloads; FRIGATEIN countdown speech;
  refund when the starport is lost) and ornithopter air strikes (three ornithopters from the map
  edge nearest to the base, 12 shots each, then they leave). re/notes/starport.md
- Rendering per the original draw code (display list order, base/damaged frames, building
  animations, shadows, house colours, explosions/projectiles/flyers, terrain craters, sandworm sand
  spray particles + wormsign, repair icon), minimap, sidebar, HUD.
- Sound per the original sample manager (re/notes/audio.md): 5 effect channels with PlaySoundAt's
  pan/volume (audible off screen, quieter, never silent) and the quietest-replaced rule; one speech
  channel with the two-priority speech queue (mentat lines by house, unit voices, sidebar clicks,
  money ticks) and the per-sample repeat guards (UNDERATTACK / HARVATTACK 300 ticks, LOWPOWER /
  SILOSNEEDED / WORMSIGN 2000, FRIGATEINn); every simulation call site plus the UI ones (GUARDING,
  RETREAT, PRIMARY, CANTBUILD, GAMESAVED / GAMELOADED, menu "BUTTON" clicks). Music as the original
  picks it: options.aud on the main menu (other menu pages silent), the mission's track from
  campaign.uib (campaign: then random tracks of the house's list; skirmish: arakatak looped).
  `D2K_SOUND_LOG=1` logs every started sample.
- Shroud with the original edge pieces; mission events/triggers, messages, timer, win/lose.
- Computer AI (production, base building/rebuild, attack/protect/defend groups, starport purchases,
  palace weapons / air strikes) and skirmish setup.
- Save / load (our own format, `src/game/savegame.*`): a versioned binary snapshot ("D2KSAVE",
  version, metadata, world section, FNV-1a checksum) of the complete mutable World state — every
  field of units/buildings/bullets/explosions and their pools/free lists/links, cells (flags, spice,
  damage, shroud), sides (economy, queues, carryall pickups, deliveries, starport), AI state, mission
  events/conditions/timer, crates/blooms, particles, pending orders, RNG, pathfinder buffers — plus
  the session metadata (map, mission/skirmish setup, player side, camera). Loading rebuilds the
  static data from the game files and overwrites the state; a loaded game continues bit-identically
  (checked by save→load→continue round trips in `game_test` and `--simtest`). The mission message
  log on screen and the sounds of the current tick are not saved.

## Not done yet
- Original UI: the multiplayer score view (radar orb); strip and radar clicks act on press instead
  of release. Sound: no in-game music track picker, no MP taunts.
- Community missions: extended events not used by the shipped missions (IF/loops/expressions,
  most variable queries, template/memory edits, UI queries, building attack orders; per-type kill
  statistics), the NeutralBecomeHostile message; MissionNumber custom IG*MIS.UIL briefing pages; side names from PlayersFile (parsed, not shown);
  splash art / tactical-map images (docs/community-campaigns.md §6).
- Multiplayer. Cutscenes: no subtitles; PS1 music (song22.xa) not used.

## Graphics direction
Enhanced mode takes art packs (`docs/enhanced-graphics.md`): AI-upscaled sprites (e.g. Real-ESRGAN,
offline pipeline in `tools/`) or hand-made replacements, at any scale, with the built-in Scale2x look as
the fallback. Pipeline v2 (`tools/upscale_art_v2.py`, pack folder `upscaled-v2`: HAT for sprites/icons,
SwinIR-L for terrain, AI-upscaled alpha/house masks, spice-aware tile context; docs/art-pipeline.md) is the
better pack; v1 (`upscale_art.py`, `upscaled-sample`) stays for comparison. The modern effects layer (particles, lights, shake, smooth turning) is independent of the art.
