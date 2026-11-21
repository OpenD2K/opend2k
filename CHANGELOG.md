# Changelog

All notable changes to OpenD2K. Versions follow `MAJOR.MINOR.PATCH`; while the major version is 0,
any release may change settings or save games (old saves keep loading where possible).

## 0.0.3 — 2026-11-21

### Graphics presets
- **New preset line-up** (Options → Graphics): *Original* / *Enhanced* / *3D (Reimagined)* / *3D Classic*
  (the original 1998 tiles on the 3D battlefield) / *Custom*. The welcome page shows the same four looks.
- **3D (Reimagined)**, the new 3D look: taller sculpted sandstone cliffs, a low warm sun with soft long
  shadows, detailed terrain, cinematic fireballs, smoke and craters. Everything is render-only: the game
  itself plays exactly as before.

### 3D Reimagined
- **Realistic grade** (optional; vivid colours are the default): a desaturated, filmic look with khaki sand and
  cinnamon spice — a switch on the Options → Graphics → 3D tab.
- **Photo terrain**: the ground made from photographed sand, rock and cliff scans (free CC0 scans from
  Poly Haven), without visible tiling.
- **Ambient occlusion**: soft contact shadows where buildings, units and rocks meet the ground.
- **Lit smoke**: smoke lit by the sun, the sky and the fire under it, ageing from brown to grey; dust takes the
  colour of the ground.
- **Vehicle wrecks** and **aircraft crashes**: destroyed vehicles burn out as charred hulks (turrets pop off),
  shot-down aircraft spin and dive into the ground, their cargo falls too.
- **Ground marks on slopes**: scorch marks, craters and track marks follow the terrain instead of floating over
  slopes and cliff edges, and fade on steep faces.
- **Track marks** in the sand (tread and tyre imprints that slowly fade), **dust trails** behind vehicles.
- **Spice relief**: spice fields as wind-shaped mounds and drifts with cinnamon clumps; the spice sparkle stays
  on the ground (no more flicker while scrolling).
- **Ground clutter**: boulders, rubble, pebbles, gravel and dry scrub on the ground.
- **Building life**: wind trap turbines and fans spin, turrets scan, stacks and vents steam, beacons blink at night.
- **World beyond the map**: the landscape continues past the map edge — *Off*, *Near* (the explored edge for a
  few cells, then dark) or *Full*.
- **Dust haze**: drifting dust in the air and a depth haze (setting *Aerial haze*).
- **Redesigned buildings**: the wind traps of all three houses, the Atreides refinery and construction yard (with
  its working crane) and a new, bigger harvester — with real materials and human-scale detail (doors, ladders,
  railings, lamps). Wheels and tracks turn while driving.
- New buildings can **assemble** from the ground up or **drop in** and unfold (placement setting).
- 3D uses far less memory (about 1.4 GB instead of 8 GB with Reimagined).

### Getting the 3D content
- **Options → Art pack** now lists the extras for 3D (Reimagined) — the redesigned 3D models, the photo terrain
  and the ground clutter — each with its size and its own Download button, plus **Get all for 3D**. Choosing the
  3D preset (on the Graphics page or the welcome page) offers whatever is still missing in one download. Each
  extra is optional; the game works without them.

### Languages
- **Russian AI voices** as an optional download (Options → Art pack → Extras, about 4 MB): Russian voices created
  with AI, similar to the original — an unofficial fan work — for the language *Русский (AI, оригинальные голоса)*.
  The pack has voices only: Russian text needs your own Russian translation, otherwise the text stays English.

### Smoother play
- **Smoother units at lower game speeds**: infantry walk smoothly between steps, vehicles drive round corners,
  flyers keep one cruising height (no bobbing over hills, no clipping through cliffs) and turn smoothly.
- **Smooth camera** option: camera inertia, eased zoom and glides.
- **Effects intensity**: *Clean* (default) / *Balanced* / *Cinematic*.

### Controls and game rules (all optional)
- **Unit movement: Modern** (not original, a per-game rule; the Enhanced and 3D presets use it): ground vehicles
  move at any angle, slide along edges and avoid each other; Original stays bit-exact. Vehicles meeting head-on
  no longer get stuck facing each other.
- **Attack-move**: hold A and click — units walk there and fight what they meet on the way.
- Letter hotkeys work with any keyboard layout (for example Russian).
- The Enhanced pause menu shows the **mission briefing**.

### Fixes
- Crushed infantry lie flat on the ground in 3D; smoke no longer flickers; artillery hits throw up earth instead
  of a fireball; the zoom carries over between missions.
- Direct3D 12 (Windows): no more black or dark battlefield with the Reimagined grade or the display adjustments
  (Brightness / Contrast / Gamma / Saturation are not available on Direct3D 12 for now; Vulkan has them).
  <!-- Windows fullscreen freeze (ModDB report): not reproduced on the Legion - pending (Legion); add a line here if a fix lands. -->

### Your files
- The settings / saves folder is now called **OpenD2K** (it was `d2k-mac`); your settings, saves and imported
  game data are **moved automatically** on the first start. Where it is: README "Where are my settings and saves?".
- **Open data folder** button (Options → Game data folder) opens that folder.

## 0.0.2 — 2026-11-14

### Original game behaviour
- **Order feedback as in 1998**: the small circle where you send units, and the attacked unit or building
  blinks grey (units too, not only buildings); the blink no longer runs too fast on 120 Hz screens.
- **Radar orders**: with units selected, a click on the radar sends them there (Original controls), with
  the radar's ring cursor; Modern controls: right click on the minimap orders, left click moves the camera.
- **Right click always deselects** with Original controls, even if the mouse moves a little.
- **Enemy shooters are revealed** in Enhanced / 3D like in the original: a turret firing at your carryall
  or ornithopter becomes visible on the battlefield, not only on the radar.
- Only destroyed buildings shake the screen (no shake on unit explosions), as in the original.
- New options **Order limit** and **Unit limit** (Original: 64 units obey an order, 1000 objects per side
  — the exact 1998 limits; Unlimited is the default in the Enhanced / 3D presets).

### Graphics
- **Real building fire**: flames, embers, smoke lit from below, firelight and soot; the purple tint of
  fire at night is fixed.
- **Building working animations** on the redesigned art: welding in the factories, repair sparks, door
  and palace lamps, the Death Hand launch, star port landing lights, research centre lights.
- **New build-up**: in Enhanced the building rises out of a heap of rocks like in 1998; 3D keeps its
  glowing construction effect.
- **Vehicle animation** (Modern): tanks squat when starting, dip when stopping, roll in turns, recoil;
  shots start at the real gun barrel.
- **Shadows follow the sun** for units and buildings; dawn and dusk shadows are long and point the right
  way; headlights are warm white, tail lights small and red, and large armies keep their lights.
- **Many more lights in 3D**: every vehicle's headlights at night, even in big armies (hundreds of lights
  instead of 10–20), and lights no longer switch off at the sides of wide screens.
- A slightly brighter night; the day / night cycle and your zoom carry over to the next mission.
- **Display settings**: Brightness, Contrast, Gamma and Saturation (Enhanced / 3D); a softer picture on
  non-HDR monitors.
- **Group numbers** next to units in control groups (Options → Graphics → Custom → Interface).
- **Instant Custom previews**: Options → Graphics → Custom now compares each option on a standard scene, so
  its pictures come pre-rendered whatever your other settings are (no "Rendering…").

### Performance and smoothness
- No more micro-freezes from loading unit art and voices during play; the cursor stays smooth.
- **3D is much faster** (about 2–3× on a MacBook Pro: 100+ fps on a 5K ultrawide display) and units turn
  smoothly; new "3D resolution" option (Auto scales the 3D view only when needed).
- Smaller fixes: the selection box stays put while the view scrolls; in 3D, clicking any visible part of
  a building or unit targets it; Direct3D 12 works on Windows (Vulkan stays the default).

### Other
- Update check shows "Checking…" and the time of the last check.
- The art pack is updated to 0.0.2 (the game offers the update in Options → Art pack).

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
