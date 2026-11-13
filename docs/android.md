# Android

OpenD2K builds as an Android app (`android/`, app id `com.opend2k.app`, minSdk 26 / Android 8.0,
arm64-v8a + x86_64, landscape). Like the iPad build it is a *touch platform* (`kTouchPlatform`): touch
controls, Enhanced graphics by default, phone layout (`render/phone_hud.*`) when the screen's short side
is under 600 dp, the iPad/tablet touch layout above that (`Settings::touchLayout` overrides it).

## Game data
The data page's **Choose installer (.exe)...** opens the system file picker (Storage Access Framework, no
storage permission): the player picks the Gruntmods Edition installer, e.g. from Downloads. SDL returns a
`content://` URI; `InstallerImportTask` copies it into the app's external files folder, unpacks it like any
installer (into the app's private storage, `files/GameData/...`) and deletes the copy.
Alternatively a computer can copy a `Dune 2000` folder or the installer into
`Android/data/com.opend2k.app/files/` over USB (hidden from most on-device file managers since Android 11);
`data_dir.cpp` scans it like the iPad's Documents folder. Uninstalling the app deletes both.

## Art pack download
`src/platform/http_client_android.cpp` is the HTTP backend (`http_client.hpp`): it calls
`org.opend2k.HttpBridge.get` (`android/app/src/main/java`, java.net.HttpURLConnection) through JNI from the
download's worker thread; the class comes from the activity's class loader, the body comes back through
`nativeOnData`, cancels through `nativeCancelled`. Redirects are followed by hand (never to a non-HTTPS URL),
`Range: bytes=N-` resumes, User-Agent `OpenD2K/<version>`. Packs install into the app's private storage
(`SDL_GetPrefPath`/artpacks). `res/xml/network_security_config.xml` allows plain http only to localhost /
127.0.0.1 (a local test server: `adb reverse tcp:8765 tcp:8765` + `artPackManifestUrl=` in settings.ini).

## Build
Needs JDK 17 (`JAVA_HOME`) and the Android SDK (`ANDROID_HOME`) with `platforms;android-35`,
`build-tools;35.0.0`, `ndk;27.2.12479018`, `cmake;3.31.6`. Gradle comes with the wrapper.

```
tools/release/package_android.sh            # build-release/OpenD2K-<ver>-android.apk (+ .sha256)
tools/release/package_android.sh --debug    # debug-signed, for sideloading tests
```

Gradle's `externalNativeBuild` runs the repository's `CMakeLists.txt`; its `ANDROID` branch builds
`libmain.so` (`main.cpp` via SDL_main) next to SDL's `libSDL3.so` (FetchContent, `cmake/FetchSDL.cmake`).
The Java side is SDL's own `SDLActivity`, taken from the SDL source tree (`-Psdl.dir=`; the script
downloads the tarball pinned in FetchSDL.cmake), so Java and native SDL always match.
In Android Studio: open `android/`, put `sdl.dir=<SDL source>` in `android/local.properties`.

## Signing
Release APKs are signed with the project key, which is **never in the repository**. It is held by the
owner (generated 2026-11-13 on the Windows build machine, PKCS12, alias `opend2k`, RSA 4096, 30 years;
certificate SHA-256 `99:34:B6:C6:81:44:F9:8E:DC:9C:0F:A2:EB:A5:26:AB:60:1A:9A:DF:13:85:31:AE:7A:8C:53:A0:94:A8:9F:0A`).
Every future update must be signed with the same key or Android refuses to install it over the old one.
`package_android.sh` takes it from the environment: `D2K_KEYSTORE` (path), `D2K_KEYSTORE_PASS`
(optional `D2K_KEY_ALIAS`, `D2K_KEY_PASS`), handed to Gradle as `ORG_GRADLE_PROJECT_*` properties.

## Notes
- Renderer: SDL's GPU renderer on Vulkan where available (the 3D mode needs it); otherwise SDL falls back
  to OpenGL ES (2D only).
- Back button / gesture: SDL_HINT_ANDROID_TRAP_BACK_BUTTON; the app turns SDL's AC Back key into Escape
  (the in-game menu, a page back).
- Orientation: SDL_HINT_ORIENTATIONS "LandscapeLeft LandscapeRight" (SDLActivity ignores the manifest's
  screenOrientation for a resizable window).
- Swapchain: the surface usually starts smaller (system bars, cutout) and grows when immersive fullscreen
  applies; SDL sends RESIZED but no PIXEL_SIZE_CHANGED, so SDL_gpu 3.4 keeps the first Vulkan swapchain
  (cropped, stretched picture, taps off). App re-sets the frames in flight on every resize, which rebuilds it.
- Scale: SDL's window coordinates are pixels on Android; the density is the display scale.
  `pointer.hpp`: `windowToPixels()` (window coordinates -> pixels, 1 here) vs `pixelsPerPoint()` (UI scale =
  the density here); identical on the other platforms. Phone layout below 600 dp short side.
