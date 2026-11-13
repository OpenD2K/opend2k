# iPad / iPhone build (OpenD2K for iPadOS and iOS)

First iPad build: the same engine as the Mac app, SDL3 compiled from source for iOS, touch controls on
top of the Original mouse scheme, data copied in through the Files app. No game data is ever bundled.

There is no App Store version. Two ways to install (iOS / iPadOS 17 or later, arm64 devices):
- **(a) Sideload the ready-made unsigned IPA** from the Releases page (§0) — no Mac or Xcode needed.
- **(b) Build from source with Xcode** (§1-2) — needs a Mac.

Then copy the game data in (§3).

## 0. Sideload the unsigned IPA

`OpenD2K-<version>-ios-unsigned.ipa` (with a `.sha256`) is a normal app package without a signature: iOS only runs
apps signed for the device, so a sideloading tool signs it with **your** Apple ID while installing.

- **AltStore** ([altstore.io](https://altstore.io), needs AltServer on a Mac / PC on the same Wi-Fi) or
  **SideStore** ([sidestore.io](https://sidestore.io), refreshes on the device itself after a one-time setup):
  install the store app as its site describes, open the IPA on the iPhone / iPad (Files → share → AltStore /
  SideStore, or the store's "My Apps" → "+"), sign in with your Apple ID when asked. The first launch may need
  Settings → General → VPN & Device Management → your Apple ID → Trust, and on iOS 16+ Developer Mode
  (Settings → Privacy & Security → Developer Mode).
- **Sideloadly** ([sideloadly.io](https://sideloadly.io), Mac / Windows): connect the device by USB, drop the IPA
  in, enter your Apple ID, Start. Then Trust / Developer Mode as above.
- **TrollStore** ([github.com/opa334/TrollStore](https://github.com/opa334/TrollStore)): only on the iOS versions it
  supports (see its page); installs the IPA permanently, no Apple ID and no 7-day expiry.

With a **free Apple ID** the signature lasts **7 days**: after that the app no longer opens until it is re-signed
(AltStore / SideStore refresh it automatically in the background or with "Refresh All"; Sideloadly: install again).
Re-signing or installing a newer IPA over the old one keeps the data, settings and saves. A free Apple ID allows at
most **3 sideloaded apps** active at a time (AltStore / SideStore count as one) and 10 app IDs per week. A paid
developer account signs for a year.

The IPA is built by `tools/release/package_ios.sh` (Release, arm64, `CODE_SIGNING_ALLOWED=NO`, bundle id
`com.opend2k.app`; the script checks the bundle and that an ad-hoc re-signature of a copy verifies). If a tool
reports the bundle id as taken, let it change the id (AltStore / Sideloadly do this for free accounts).

## 1. One-time setup on the Mac

1. Xcode (26.x, with the iOS SDK) and CMake >= 3.24 (Homebrew `cmake`).
2. SDL3 sources (same version as Homebrew's, 3.4.20), outside the repo:
   ```
   mkdir -p ~/Tools/sdl-ios/dl && cd ~/Tools/sdl-ios/dl
   curl -LO https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
   shasum -a 256 SDL3-3.4.20.tar.gz   # f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f (= Homebrew's)
   cd .. && tar xzf dl/SDL3-3.4.20.tar.gz
   ```
   (Another location: pass `-DD2K_SDL_SOURCE=/path/to/SDL3-3.4.20`.) SDL is built as a static library inside
   the Xcode project, for the device and the Simulator alike.
3. Generate the Xcode project (from the repository root; takes ~3 minutes, mostly SDL's checks):
   ```
   cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_OSX_ARCHITECTURES=arm64
   ```
   Optional: `-DD2K_IOS_TEAM=<your team ID>` and `-DD2K_IOS_BUNDLE_ID=com.yourname.opend2k` so you do not have to
   set them in Xcode each time the project is regenerated.

## 2. Install on the iPad (free Apple ID is enough)

1. `open build-ios/d2k.xcodeproj`
2. Xcode > Settings > Accounts > "+" > Apple Account: sign in with your Apple ID (a "Personal Team" appears).
3. In the project navigator select the project "d2k", then the target **d2k_ios** > Signing & Capabilities:
   - tick "Automatically manage signing", Team = "<Your Name> (Personal Team)";
   - if Xcode says the bundle identifier is not available, change `com.opend2k.app` to something unique,
     e.g. `com.<yourname>.opend2k`.
4. Connect the iPad with a USB cable, unlock it and tap "Trust This Computer" (enter the passcode).
5. On the iPad: Settings > Privacy & Security > Developer Mode > On, restart, confirm "Turn On" after the restart.
   (The switch appears after the iPad has been connected to Xcode once.)
6. In Xcode's toolbar choose the scheme **d2k_ios** and your iPad as the run destination, press Run (Cmd+R).
   The first time Xcode prepares the device ("Copying shared cache symbols") — this can take a few minutes.
7. The first launch is refused as an untrusted developer: on the iPad open Settings > General >
   VPN & Device Management > Developer App > your Apple ID > Trust. Then start OpenD2K from the home screen.
8. Free (personal team) signing expires after **7 days**: the app then no longer opens. Connect the iPad and
   press Run in Xcode again (your data, settings and saves stay). A paid developer account signs for a year
   and allows TestFlight.

Builds are optimised: `RelWithDebInfo` (-O2 -g, SDL3 included) is the project's first configuration and the
scheme's Run configuration, and the scheme has Metal API validation and GPU frame capture off (both cost CPU
per draw). Debug (-O0) is about 2-4x slower per frame and made the game sluggish on a 2019 iPad; pick it only
for debugging (Product > Scheme > Edit Scheme > Run > Build Configuration). After pulling these changes,
regenerate the project once (step 1.3) so the scheme picks them up.

Command line instead of Xcode's Run button (after the team is set in the project):
```
xcodebuild -project build-ios/d2k.xcodeproj -scheme d2k_ios -configuration RelWithDebInfo -destination 'generic/platform=iOS' -allowProvisioningUpdates -jobs 4 build
```

## 3. Game data on the iPad

On first start OpenD2K shows the data page with these instructions. Put ONE of these into the app's folder:
- the Gruntmods Edition installer `Dune 2000 Gruntmods Edition 1.6.7.exe` (recommended: one file), or
- a complete `Dune 2000` game folder (with `data/` inside).

Ways to copy:
- **Finder (USB)**: connect the iPad, select it in Finder's sidebar > "Files" tab > drag the .exe onto "OpenD2K".
- **Files app** on the iPad: put the file in iCloud Drive / AirDrop it / Downloads, then long-press > Move >
  On My iPad > OpenD2K.

The page rescans the folder every 2 seconds and shows a button per item: "Unpack <installer>.exe" (unpacked once,
with a progress bar, into the app's private Application Support folder; you may delete the .exe afterwards),
"Use game folder ...", "Use PS1 disc image ..." (a `.cue` + `.bin` pair: cutscenes / Russian PS1 voices). The
Gruntmods cutscene installers (`Dune 2000 <Language> Cutscene Installer.exe`) are unpacked the same way.
Settings and save games live in Application Support (`SDL_GetPrefPath`), not in the shared folder.

The page shows a status line: "Game data: found / missing" and "Cutscenes: N of 41 / none".

### Cutscenes
The Gruntmods Edition installer contains **no movies**: the cutscenes are a separate download. Any of these
works (copy it into the OpenD2K folder the same way):
- the Gruntmods cutscene installer `Dune 2000 <Language> Cutscene Installer.exe`: tap "Add cutscenes from ..."
  (unpacked into the app's private Movies folder; the .exe can be deleted afterwards);
- loose `.VQA` files, straight in the OpenD2K folder or in a sub-folder (e.g. `movies/`, up to two levels deep;
  played from there, nothing is copied);
- a `movies` folder next to `data` inside a copied `Dune 2000` game folder;
- a PS1 disc image (`.cue` + `.bin`): "Use PS1 disc image ..." (its STR movies fill in for missing VQAs).

Checked in the Simulator: a VQA in `Documents/movies/` is found and plays with sound ("Movie sound: 22050 Hz,
1 ch, playing" in the log; the picture follows the sound clock).

## 4. Touch controls (Enhanced graphics is the iPad default, UI scale 175 %)

Built on the Original mouse scheme (left-click semantics), `platform/touch_controls.*`:

| Gesture | Action |
|---|---|
| tap | left click: select / move / attack, sidebar build / place, radar centre |
| tap on empty ground with no own unit selected | deselect (also a viewed enemy / own building) |
| **three-finger tap** (anywhere) | deselect all (the original's right click: DeselectAllForAllSides; also leaves a click mode) |
| four-finger tap | frame-time overlay on / off (also Options > "Frame time") |
| double tap on an own unit | select every own unit of that type on screen |
| one-finger drag | scroll the map; on the radar: drag the view; on the panel: scroll the build list |
| long press | hover (tooltip); keep holding and drag = selection box |
| long press on the panel | popup: Build, Build x10, Hold/Cancel (Extended queue: Repeat/Cancel), One fewer |
| two-finger drag / pinch / twist | scroll / zoom (Enhanced, 3D) / turn the camera (3D) |
| building placement | first tap shows the footprint, a second tap on it places |
| buttons (bottom left) | Menu (= Esc: cancels placement / click modes, else pause menu), Stop, Deploy, Guard, X (deselect all), **1998** (Enhanced / 3D: the classic view, original 1998 graphics - Options "Classic view": Hold = while the finger is down, Toggle = a tap switches it), groups 1-5 (tap selects, hold assigns) |

Menus take taps as clicks. Lists and scrolling pages (missions, community campaigns, saves, movies, options, the
cheat grids, the original pages' list boxes) scroll with a one-finger drag, with momentum after a flick; a drag
never selects or presses anything, a tap selects (rows are selected when the finger lifts), a horizontal drag
moves a slider (`platform/ui.*`). A Bluetooth / Magic Keyboard trackpad or mouse works like on the Mac (the game cursor
is drawn only while a real pointer is used; edge scrolling is off on the iPad). Going to the background pauses
the game (its pause menu is open on return), stops the sound and saves the settings.

## 4a. Phone layout (iPhone)

The iOS target is universal (`TARGETED_DEVICE_FAMILY` 1,2; landscape only on both, full-screen launch screen), so the
same project installs on an iPhone. On a phone-sized screen the Enhanced / 3D in-game UI switches to the **phone
layout** (`render/phone_hud.*`, wired in `Session::layoutPhone` / `renderPhoneUi` / `phoneMouseDown`):

- Options → *Touch UI*: **Auto** (Phone on an iPhone - `hw.machine` / the Simulator's model starts with "iPhone" - or
  any screen whose short side is under 500 points; Tablet otherwise, the Mac included) / Phone / Tablet
  (settings.ini `touchLayout`). *HUD size*: Tiny / **Small** / Normal = top-bar icons of 22 / 26 / 30 points
  (`phoneUiSize`); the drawer's panel scales with it (UI scale 1.15 / 1.3 / 1.45). Graphics: Original keeps the
  original letterboxed frame on any device.
- The battlefield is the whole screen. Everything sits inside the safe area (`SDL_GetWindowSafeArea`: notch /
  Dynamic Island side insets, home indicator), the bar band and the drawer background run to the screen edges.
- **Top bar** (icon + 6 points high, a dark band fading downwards): menu (pause menu) · a click-mode chip while
  placing / repair / sell / picking a target (tap = cancel) · credits · power (bolt + the original's percent / 200
  bar, red / yellow / green) · mission timer · **quick-build strip** centred (the buildable units: pinned first in
  pin order, then the most ordered - counted per unit type in settings.ini `quickBuildCounts` - then sidebar order;
  6 slots, 5 at Normal; the order is frozen while you are tapping so the icon under the finger never moves; the
  clock wedge plus a progress ring around the icon while it builds, the queue count / infinity badge, READY pulse,
  HOLD, a gold pin on pinned ones) · **specials** (palace weapons / air strike, gold star; charging as the clock,
  pulsing green when ready; tap = launch, the air strike / Death Hand then wait for a tap on the target) · radar
  button (small → large → off) · hide-UI.
- **Quick-build gestures**: tap = +1 (Original queue: build / resume); long press = a menu under the finger:
  Build, Build x10, Repeat / Cancel (infinite), One fewer, **Pin / Unpin** (`quickBuildPins`).
- **Radar**: top left under the bar, the map's aspect, small (3.8 icons) or large (6.8 icons), highly transparent
  (52 % / 72 % opaque, less when idle); only with a working radar outpost and power (World::radarActive), like the
  original. Tap = jump the camera there, drag = scroll. Mission messages move to its right.
- **Drawer**: the full Enhanced sidebar (tabs, buildings, units, upgrades, starport, the power meter) on the right
  edge with a row of the command buttons on top (repair, sell, guard, retreat, upgrades, starport). Open / close:
  tap the pull tab (right edge, middle) or swipe it left / swipe the drawer right. Picking a finished building
  closes it while you place the building (first tap shows the footprint, a second tap on it builds) and opens it
  again afterwards; repair / sell / air-strike modes close it (the chip in the bar cancels them).
- **Hide UI**: everything but a tiny eye tab in the top-right corner (tap = back).
- **Command buttons** (stop, deploy, guard, deselect, 1998 classic view, groups 1-5) shrink to the HUD size along the
  bottom-left edge; the menu is in the top bar.
- Buttons are drawn small but every touch area is at least 44 points and reaches the screen edge (top bar: from the
  top edge to below the bar; bottom buttons: down to the bottom edge; drawer tab: to the right edge); where areas
  overlap the nearest element wins. The bar and the buttons are semi-transparent and fade to 50 % after 4 s
  without a touch.

Testing on the Mac (a mouse works too: left = tap, right = the long-press "cancel"; the Retina window is 2x where an
iPhone 12 Pro Max is 3x, same layout in points):
```
./build/d2k --map A7V1.MAP --graphics enhanced --phone-layout --window 926x428 --render-resolution full \
    --safe-insets 47,0,47,21 --ui-size small --screenshot shot.bmp
```
`--sidebar-page` takes comma-separated screenshot states: `drawer`, `hide-ui`, `radar-large`, `radar-off`, `idle`, `awake` (never fades),
`place` (with `--cursor-at X Y`), and `demo-base` (debug: tech 9, 20000 credits, every structure spawned around the
base and a few units queued through the cheat orders, so the strip / drawer / specials have content).

Simulator (checked on "iPhone 17 Pro Max", iOS 26): build and install as in §6, copy the data, then
`xcrun simctl launch booted com.opend2k.app --map A7V1.MAP --sidebar-page demo-base,awake` (Touch UI Auto picks the
phone layout from the simulated model; the real safe area applies). The Simulator need not be shown; `xcrun simctl io
booted screenshot` captures the portrait framebuffer, rotate it by 90 degrees for the landscape picture.

## 5. Performance

- Frame-time overlay: four-finger tap in a game, or Options > "Frame time" (`--perf-overlay` for one run). It
  shows fps, the average / max frame time and the CPU time (update + draw calls, before present) over the last
  second, the output size and the device (`iPad7,11 lite`). Please report these numbers.
- Device profile: on the first start after this change the app reads the model (`hw.machine`). A10 / A12-class
  iPads (`iPad5..7,x` = up to iPad 7th gen / Pro 2017, `iPad11,x` = iPad mini 5, Air 3, iPad 8th gen) or under
  3.5 GB of memory get the **lite** profile once: Effects Classic, Shroud Classic, 3D -> Enhanced, built-in
  "Upscaled" art, panel transparency off (`Settings::applyLiteProfile`). Everything can be switched back in
  Options; `deviceProfile=` in settings.ini records that it ran.
- Measured on the Mac (Release, 1080x810 like a 10.2" iPad, H5V1, 400 frames, `--frames`): the CPU side is
  small and nearly flat across the options (Original 1.0 ms, Enhanced 0.7, Enhanced classic effects + classic
  shroud 0.6, 3D 0.8 ms per frame), while the Debug build costs 2-3x (Enhanced 1.7 ms avg, p95 5 ms vs 1.3). So
  the main fix for "slow on the 2019 iPad" is the optimised build; the lite profile trims GPU work (soft shroud
  = a blended full-screen gradient, Modern effects = extra blended particles / glows) that the Mac cannot
  measure — the overlay on the device will tell.
- Tips: Graphics: Original is the cheapest (pixel-exact, integer-scaled); Art: Original (unfiltered pixels)
  needs 1/16 of the texture memory of Upscaled; 4x art packs are not used on the iPad (none are bundled).
- Resolution: Options → *Render* Auto / Full / Half (`renderResolution` in settings.ini; applies at the next start).
  Full draws at the Retina pixels (2360x1640 on an 11" iPad Air), Half at point resolution (1180x820; a quarter of
  the fill cost). Auto = Half on the lite (A10/A12-class) devices, Full otherwise. Everything is laid out in points
  times the density (touch targets, UI scale, map zoom, Original game resolution), so both modes look the same
  size; touch and mouse positions are converted to output pixels on arrival (`platform/pointer.hpp`). Checked in
  the Simulator (iPad Air 11-inch M3): "window 1180x820 points, output 2360x1640 pixels (render resolution full)"
  in the log; with `renderResolution=half` the output is 1180x820 and the screen looks the same, only softer.

## 6. Simulator (no signing needed)

```
xcodebuild -project build-ios/d2k.xcodeproj -target d2k_ios -sdk iphonesimulator -configuration RelWithDebInfo -jobs 4 ARCHS=arm64 build
xcrun simctl boot "iPad Air 11-inch (M3)"; open -a Simulator
xcrun simctl install booted build-ios/RelWithDebInfo-iphonesimulator/OpenD2K.app
D=$(xcrun simctl get_app_container booted com.opend2k.app data); mkdir -p "$D/Documents/Dune 2000"
cp -R "game/Dune 2000/data" "$D/Documents/Dune 2000/"     # ~440 MB
xcrun simctl launch --console-pty booted com.opend2k.app --map A1V1.MAP
xcrun simctl io booted screenshot shot.png
xcrun simctl shutdown all
```
The Simulator has no SDL_GPU Metal support ("Device does not meet the hardware requirements"), so Graphics: 3D
falls back to Enhanced there; a real iPad (A12 or newer) should get the GPU renderer.

## Known gaps (first version)
- Touch gestures were only checked in the Simulator by screenshot, not on hardware.
- Text entry (save names, player name) relies on the on-screen keyboard appearing for SDL text input (untested).
- iPhone phone layout: checked on the Mac and in the Simulator only; the front-end menus and the pause / options
  pages are not phone-specific yet (rows ~20 points high, usable but small).
