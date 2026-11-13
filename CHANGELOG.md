# Changelog

All notable changes to OpenD2K. Versions follow `MAJOR.MINOR.PATCH`; while the major version is 0,
any release may change settings or save games (old saves keep loading where possible).

## 0.0.1 — 2026-11-13

The first public release: the complete single-player *Dune 2000*, rebuilt natively, plus optional
modern graphics and a 3D mode. Requires your own copy of the game data (the free Gruntmods Edition
installer works directly).

### The original game
- All three campaigns (Atreides, Harkonnen, Ordos) with the original territory map, picture-book
  briefings, score screen, ranks and high scores; credits after the last mission.
- Skirmish against the original computer player (production, base building and rebuilding, attack /
  defend groups, starport purchases, palace weapons, air strikes).
- The simulation follows the original executable: movement and its pathfinder, combat, economy and
  production, power, carryalls, starport and frigate, sandworms, crates and spice blooms, stealth,
  capture and deviators, shroud, mission events and win/lose rules, with the original balance — down to
  the random-number calls, timing and game speed.
- Original controls, including Alt force move and turret attack orders; one original bug is fixed
  (infantry leaving a building could become invulnerable).
- The original front end and in-game interface (Graphics: *Original*): menus from the game's own page
  scripts, sidebar, radar, power bar, cursors, tooltips, pixel-exact sprites at an integer scale.
- Sound and music as the original picks them: effect channels, the mentat / unit speech queue, repeat
  guards, menu and mission music.
- Cutscenes: the PC VQA movies (from the Gruntmods cutscene installers) or the PlayStation versions
  from a disc image.
- Save anywhere: complete save games of battles and of the campaign map that continue bit-identically.
- Community content: Gruntmods community campaigns and custom missions, including most of the
  community patch's extended mission events; campaigns that change unit or building art keep their
  own art (never replaced by the redesign pack or the 3D models).
- Play any mission as any side.
- Languages: English, French, German, Spanish from the game data; Russian (the PC translation's text,
  fonts and voices, or the Russian PlayStation dub).

### Setup and updates
- Setup from the *Gruntmods Edition* installer `.exe` on every platform: OpenD2K unpacks it itself
  (nothing is executed; no Windows or Wine needed). An installed `Dune 2000` folder works too.
- The Gruntmods cutscene installers are imported the same way.
- First-run welcome page: pick Original, Enhanced or 3D from preview pictures and download the art pack.
- Options → Updates: tells you when a newer OpenD2K is released (asks GitHub only when you open
  Options; can be switched off). Settings, saves, imported data and the art pack live outside the
  app, so replacing the app by a newer one loses nothing.

### Optional (not original, each behind a setting)
- Graphics presets: *Original*, *Enhanced* (the default for new players), *3D* and *Custom*.
- *Enhanced 2D*: a modern interface (Play font, translated), high-quality sprite scaling or replacement
  art packs, soft shroud, health bars, designed terrain that follows each map's own layout, a "classic
  view" key (Tab) that shows the 1998 graphics while held.
- *3D* (2.5D battlefield): terrain relief with modelled cliffs and CC0 materials, sun light and
  shadows, a rotating / tilting camera, glTF models with animation (infantry poses, harvester,
  building variants and moving parts), sprites as billboards where no model exists. Metal on macOS /
  iOS, Vulkan (default) or Direct3D 12 on Windows, Vulkan on Linux and Android.
- Time of day and lighting (Enhanced and 3D): Day / Dawn / Dusk / Night or a day–night cycle; *Modern*
  lighting with a warm sun, cool sky, bloom, sun-following shadows, and lamps, headlights, building
  windows, muzzle flashes and fires that light their surroundings at night.
- Quality settings: Low / Medium / High, HDR rendering, HDR display output (EDR on Mac XDR screens,
  Windows HDR), MSAA, anisotropic filtering, V-sync and an FPS limit.
- Modern effects: new weapon effects, building destruction, refinery processing, harvest dust, spice
  blooms, damage smoke, sandworm sand humps, capture effects.
- Modern build-up and sell animations for redesigned buildings: in Enhanced they rise out of a heap of
  terrain-tinted rocks and dust, in the spirit of the 1998 build-up.
- Controls: Original or Modern mouse scheme; extended production queues; factory rally points; game
  speed and scroll options.
- Cheat menu (Ctrl+Shift+C): credits, tech level, build overrides, deliveries, reinforcements,
  spawning, reveal map, instant build.

### Redesigned art pack (separate download, CC BY-SA 4.0)
- `OpenD2K-redesign-pack-0.0.1.zip` (~580 MB): new 3D models for the units and buildings of all houses
  and the 2D sprites rendered from them for *Enhanced*. Download it in the game (Options → Art pack, or
  the welcome page) or by hand from the release page. Simple first versions; high-detail redesigns
  follow in later releases. The models are generated by the Blender scripts in `tools/models/`.

### Platforms
- macOS 11 or later, universal (Apple Silicon and Intel). Ad-hoc signed, not notarised.
- Windows 10 (1903) / 11, x64.
- Linux x86-64 (glibc 2.38 or newer: Ubuntu 24.04, Debian 13, Fedora 39 or later).
- Android 8.0 or later (arm64 and x86_64), sideloaded APK: touch controls, phone layout, in-app art
  pack download.
- iPadOS / iOS 17 or later: build from source with Xcode and install with your own Apple ID (touch
  controls, phone layout); no App Store version.

### Known issues
- No multiplayer yet.
- Redesign art pack: the high-tech factories are the weakest models; the Harkonnen research centre and
  the smugglers' starport are missing some working animations; several buildings still show the 1998
  animation overlays in Enhanced or no working animation in 3D.
- The Modern building fire effect looks like a flash rather than fire; a real fire effect comes in 0.0.2.
- The builds are not signed: macOS needs *Open Anyway* once (Gatekeeper), Windows SmartScreen warns
  about an unknown publisher, Android needs "install unknown apps" for the APK.
- Intel Macs have been tested under emulation (Rosetta) only.
- Linux needs glibc 2.38 (Ubuntu 24.04 or newer); the Windows and Linux builds have had little testing
  on real hardware — please report problems.
- Direct3D 12 on Windows runs without HDR and anisotropic filtering; Vulkan (the default) has both.
- iPhone / iPad: no download; you build it yourself with Xcode (docs/ios.md).
- Some rare extended mission events of community missions are not implemented.
