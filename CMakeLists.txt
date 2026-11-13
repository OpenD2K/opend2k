cmake_minimum_required(VERSION 3.24)
# iPadOS: optimised by default. RelWithDebInfo (-O2 -g) comes first, so it is the Xcode
# project's default configuration (also for SDL3, built inside the project); the scheme's Run
# action uses it too (cmake/IOSApp.cmake). Debug (-O0) stays selectable in the scheme editor.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo;Release;Debug" CACHE STRING "Xcode configurations (first = default)" FORCE)
endif()
project(d2k VERSION 0.0.1 LANGUAGES C CXX)  # the release version (CHANGELOG.md, docs/release.md)
# Apple: Objective-C++ (platform/http_client_apple.mm) enabled here, before the fetched SDL3 subdirectory is added:
# enabled only further down, the generate step fails with D2K_FETCH_SDL=ON ("CMAKE_OBJCXX_COMPILE_OBJECT" not set).
if(APPLE)
    enable_language(OBJCXX)
endif()

# MSVC: the static C/C++ runtime (/MT, /MTd for Debug) for every target, the fetched SDL3 included, so a
# release zip runs without the VC++ redistributable (MSVCP140 / VCRUNTIME140). Set before any target is
# created (CMP0091); -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL keeps the DLL runtime.
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# iPadOS (docs/ios.md): cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS ... builds the app only; SDL3 is
# compiled from its release sources (D2K_SDL_SOURCE) as a static library inside the project.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(D2K_IOS ON)
else()
    set(D2K_IOS OFF)
endif()
if(D2K_IOS)
    set(D2K_SDL_SOURCE "$ENV{HOME}/Tools/sdl-ios/SDL3-3.4.20" CACHE PATH "SDL3 release source tree (iOS builds)")
    if(NOT EXISTS "${D2K_SDL_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR "iOS: SDL3 sources not found at D2K_SDL_SOURCE=${D2K_SDL_SOURCE} (see docs/ios.md)")
    endif()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${D2K_SDL_SOURCE}" "${CMAKE_BINARY_DIR}/SDL3" EXCLUDE_FROM_ALL)
else()
    # Desktop: an installed SDL3 (Homebrew, vcpkg, distro package, CMAKE_PREFIX_PATH) or, when none
    # is found (or D2K_FETCH_SDL=ON), the pinned release downloaded and built with the project
    # (cmake/FetchSDL.cmake; a shared library: SDL3.dll / libSDL3.so.0 next to the executable).
    option(D2K_FETCH_SDL "Always download and build SDL3 instead of using an installed one" OFF)
    if(NOT D2K_FETCH_SDL)
        find_package(SDL3 3.4 CONFIG QUIET)
    endif()
    if(SDL3_FOUND AND NOT D2K_FETCH_SDL)
        message(STATUS "SDL3: installed ${SDL3_VERSION} (${SDL3_DIR})")
    else()
        include(cmake/FetchSDL.cmake)
    endif()
endif()

# Compiler settings shared by our targets (not applied to SDL / third_party).
include(cmake/CompilerOptions.cmake)

# Pure game logic + file formats: no SDL, no OS calls (portable, testable).
add_library(d2k_core STATIC
    src/formats/r16.cpp
    src/formats/map.cpp
    src/formats/mis.cpp
    src/formats/templates.cpp
    src/formats/bintables.cpp
    src/formats/aud.cpp
    src/formats/font.cpp
    src/formats/uib.cpp
    src/formats/soundrs.cpp
    src/formats/tileatr.cpp
    src/formats/vars.cpp
    src/formats/techpos.cpp
    src/formats/tga.cpp
    src/formats/uil.cpp
    src/formats/nsis.cpp
    src/formats/vqa.cpp
    src/formats/ps1_disc.cpp
    src/formats/ps1_str.cpp
    src/formats/ps1_sound.cpp
    src/formats/ps1_models.cpp
    src/formats/mission_ini.cpp
    src/formats/community.cpp
    src/formats/language.cpp
    src/formats/sha256.cpp
    src/formats/zip_reader.cpp
    src/formats/artpack_manifest.cpp
    src/formats/json.cpp
    src/formats/release_info.cpp
    src/game/scenario.cpp
    src/game/world.cpp
    src/render/lighting.cpp  # pure (no SDL): the time of day model, tested by game_test
    src/game/spawn.cpp
    src/game/movement.cpp
    src/game/pathfind.cpp
    src/game/combat.cpp
    src/game/projectiles.cpp
    src/game/buildings.cpp
    src/game/economy.cpp
    src/game/production.cpp
    src/game/cheats.cpp
    src/game/rally.cpp
    src/game/shroud.cpp
    src/game/mission.cpp
    src/game/mission_ext.cpp
    src/game/ai.cpp
    src/game/sound_system.cpp
    src/game/savegame.cpp
    src/game/save_check.cpp
    src/game/campaign.cpp
)
target_include_directories(d2k_core PUBLIC src)

# LZMA SDK decoder (Igor Pavlov, public domain) for NSIS installers (src/formats/nsis.cpp).
add_library(d2k_lzma STATIC third_party/lzma/LzmaDec.c)
target_include_directories(d2k_lzma PUBLIC third_party/lzma)
if(NOT MSVC)
    target_compile_options(d2k_lzma PRIVATE -O2)  # fast unpacking even in Debug builds
endif()
target_link_libraries(d2k_core PRIVATE d2k_lzma)
# Optional zlib for deflate-compressed NSIS installers (Gruntmods uses LZMA).
if(D2K_IOS)
    # iOS: the SDK's libz by name (an absolute .tbd path would pin the device SDK in Simulator builds).
    target_link_libraries(d2k_core PRIVATE z)
    target_compile_definitions(d2k_core PRIVATE D2K_HAVE_ZLIB=1)
else()
    # macOS / Linux have a system zlib; Windows usually not (then deflate installers are refused,
    # the Gruntmods installers are LZMA and still work). D2K_FETCH_ZLIB=ON builds zlib from source.
    option(D2K_FETCH_ZLIB "Download and build zlib when it is not installed (deflate NSIS installers)" OFF)
    find_package(ZLIB QUIET)
    if(NOT ZLIB_FOUND AND D2K_FETCH_ZLIB)
        include(cmake/FetchZLIB.cmake)
    endif()
endif()
if(ZLIB_FOUND AND NOT D2K_IOS)
    target_link_libraries(d2k_core PRIVATE ZLIB::ZLIB)
    target_compile_definitions(d2k_core PRIVATE D2K_HAVE_ZLIB=1)
endif()
# Bit-exact float math in the simulation (audit-1 item 5; docs/building.md "Determinism").
d2k_deterministic_fp(d2k_core)

# Native executable: SDL3 platform layer + renderer.
set(D2K_APP_SOURCES
    src/platform/app.cpp
    src/platform/artpack_download.cpp
    src/platform/app_update.cpp
    src/platform/http_client.cpp
    src/platform/audio.cpp
    src/platform/campaign_screens.cpp
    src/platform/catalog.cpp
    src/platform/community_menu.cpp
    src/platform/menu_community.cpp
    src/platform/menu_artpack.cpp
    src/platform/menu_update.cpp
    src/platform/original_community.cpp
    src/platform/data_dir.cpp
    src/platform/installer_import.cpp
    src/platform/language.cpp
    src/platform/game_data.cpp
    src/platform/game_options.cpp
    src/platform/menu.cpp
    src/platform/music.cpp
    src/platform/movie_player.cpp
    src/platform/movies.cpp
    src/platform/options.cpp
    src/platform/session.cpp
    src/platform/preset_previews.cpp
    src/platform/cheat_menu.cpp
    src/platform/rally_ui.cpp
    src/platform/saves.cpp
    src/platform/settings.cpp
    src/platform/sim_test.cpp
    src/platform/sound_bank.cpp
    src/platform/touch_controls.cpp
    src/platform/platform_info.cpp
    src/platform/ui.cpp
    src/platform/uil_view.cpp
    src/platform/uil_pages.cpp
    src/platform/original_menus.cpp
    src/platform/ingame_pages.cpp
    src/render/tileset.cpp
    src/render/sprites.cpp
    src/render/art_pack.cpp
    src/render/mod_art.cpp
    src/render/effects.cpp
    src/render/effects_weapons.cpp
    src/render/effects_events.cpp
    src/render/effects_craters.cpp
    src/render/map_renderer.cpp
    src/render/soft_shroud.cpp
    src/render/scene_lights.cpp
    src/render/weld_fx.cpp
    src/render/buildup_fx.cpp
    src/render/bloom.cpp
    src/render/hdr.cpp
    src/render/minimap.cpp
    src/render/text.cpp
    src/render/ttf_font.cpp
    src/render/ui_text.cpp
    src/render/sidebar.cpp
    src/render/ui_skin.cpp
    src/render/command_panel.cpp
    src/render/phone_hud.cpp
    src/render/classic_sidebar.cpp
    src/render/image.cpp
    src/render/map_preview.cpp
    src/render/motion.cpp
    src/render/cursor.cpp
    src/render3d/gpu.cpp
    src/render3d/camera3d.cpp
    src/render3d/terrain.cpp
    src/render3d/terrain_v2.cpp
    src/render3d/terrain_guide.cpp
    src/render3d/terrain_bake.cpp
    src/render3d/renderer3d.cpp
    src/render3d/models.cpp
    src/render3d/model_gltf.cpp
    src/render3d/model_anim.cpp
    src/render3d/shader_blobs.cpp
)
# The faithful terrain's layout guide reads every tile pixel of the map at mission start (a few
# million pixels of image processing): fast even in Debug builds (as the LZMA decoder above).
if(NOT MSVC)
    set_source_files_properties(src/render3d/terrain_guide.cpp PROPERTIES COMPILE_OPTIONS "-O2")
endif()
# The Enhanced UI's fonts (third_party/fonts, OFL) compiled in; stb_truetype rasterises them.
include(cmake/EmbedFonts.cmake)
d2k_embed_fonts(D2K_EMBEDDED_FONTS)
list(APPEND D2K_APP_SOURCES ${D2K_EMBEDDED_FONTS})
# Compiled once, linked into the command-line binary and the macOS app bundle.
add_library(d2k_frontend OBJECT ${D2K_APP_SOURCES})
target_link_libraries(d2k_frontend PUBLIC d2k_core SDL3::SDL3)
# HTTPS for the in-app art pack download (platform/http_client.hpp): the platform's own stack.
# Apple: NSURLSession (ARC); Windows: WinHTTP; Android: HttpURLConnection (JNI); elsewhere libcurl loaded at run time (dlopen; no
# build dependency). macOS also builds the libcurl backend (D2K_HTTP_BACKEND=curl tests it).
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    target_sources(d2k_frontend PRIVATE src/platform/http_client_apple.mm)
    set_source_files_properties(src/platform/http_client_apple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(d2k_frontend PRIVATE D2K_HTTP_HAVE_NATIVE=1)
    target_link_libraries(d2k_frontend PUBLIC "-framework Foundation")
    if(NOT D2K_IOS)
        target_sources(d2k_frontend PRIVATE src/platform/http_client_curl.cpp)
        target_compile_definitions(d2k_frontend PRIVATE D2K_HTTP_HAVE_CURL=1)
    endif()
elseif(ANDROID)
    # java.net.HttpURLConnection through JNI (android/app/src/main/java/org/opend2k/HttpBridge.java).
    target_sources(d2k_frontend PRIVATE src/platform/http_client_android.cpp)
    target_compile_definitions(d2k_frontend PRIVATE D2K_HTTP_HAVE_NATIVE=1)
elseif(WIN32)
    target_sources(d2k_frontend PRIVATE src/platform/http_client_win.cpp)
    target_compile_definitions(d2k_frontend PRIVATE D2K_HTTP_HAVE_NATIVE=1)
    target_link_libraries(d2k_frontend PUBLIC winhttp)
else()
    target_sources(d2k_frontend PRIVATE src/platform/http_client_curl.cpp)
    target_compile_definitions(d2k_frontend PRIVATE D2K_HTTP_HAVE_CURL=1)
    target_link_libraries(d2k_frontend PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(d2k_frontend PRIVATE "D2K_VERSION=\"${PROJECT_VERSION}\"")
# Desktop app name: the macOS bundle name and the window title. Development builds keep "Dune 2000";
# release builds (tools/release/package_*.sh) pass -DD2K_APP_NAME=OpenD2K. Settings, saves and
# unpacked data stay in SDL_GetPrefPath("d2k-mac", "Dune 2000") either way.
set(D2K_APP_NAME "Dune 2000" CACHE STRING "Desktop app name (macOS bundle, window title)")
target_compile_definitions(d2k_frontend PRIVATE "D2K_APP_TITLE=\"${D2K_APP_NAME}\"")
# cgltf (MIT, Johannes Kuhlmann) for glTF model packs (src/render3d/model_gltf.cpp).
target_include_directories(d2k_frontend PRIVATE third_party/cgltf)
# 3D shaders: MSL (in the sources) + SPIR-V (src/render3d/shaders_spirv.inc, checked in) always;
# DXIL for Direct3D 12 when dxc is available (Windows SDK / Vulkan SDK; cmake/CompileShadersDXIL.cmake).
include(cmake/CompileShadersDXIL.cmake)
d2k_dxil_shaders(d2k_frontend)

if(D2K_IOS)
    include(cmake/IOSApp.cmake)
    return()  # iOS: the app target only (no command-line binary, no tests)
endif()

if(ANDROID)
    # Android (android/: the Gradle project, docs/android.md): SDL's Java activity loads libSDL3.so and
    # libmain.so and calls SDL_main (SDL_main.h in main.cpp). Built by Gradle's externalNativeBuild.
    add_library(main SHARED src/platform/main.cpp)
    target_link_libraries(main PRIVATE d2k_frontend)
    return()  # Android: the app library only (no command-line binary, no tests)
endif()

add_executable(d2k src/platform/main.cpp)
target_link_libraries(d2k PRIVATE d2k_frontend)

# macOS: "Dune 2000.app" (D2K_APP_NAME; release builds "OpenD2K.app"; double-clickable; finds the game data next to the bundle, in the
# saved settings, or asks for the folder). SDL3 is copied into Contents/Frameworks.
# No original game data goes into the bundle.
option(D2K_APP_BUNDLE "Build the macOS app bundle \"Dune 2000.app\"" ${APPLE})
if(APPLE AND D2K_APP_BUNDLE)
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(_d2k_min "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        execute_process(COMMAND xcrun --sdk macosx --show-sdk-version OUTPUT_VARIABLE _d2k_min
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT _d2k_min)
            set(_d2k_min "11.0")
        endif()
    endif()
    set(D2K_MACOS_MINIMUM "${_d2k_min}" CACHE STRING "LSMinimumSystemVersion of the app bundle")
    set(D2K_MACOS_BUNDLE_ID "com.d2kmac.dune2000" CACHE STRING "macOS bundle identifier (release builds: com.opend2k.app)")
    set(D2K_MACOS_ICON "${CMAKE_SOURCE_DIR}/cmake/macos/OpenD2K.icns")  # from assets/icon (our own art)
    set_source_files_properties(${D2K_MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(d2k_app MACOSX_BUNDLE src/platform/main.cpp ${D2K_MACOS_ICON})
    target_link_libraries(d2k_app PRIVATE d2k_frontend)
    set_target_properties(d2k_app PROPERTIES
        OUTPUT_NAME "${D2K_APP_NAME}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "${D2K_APP_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${D2K_MACOS_BUNDLE_ID}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_ICON_FILE "OpenD2K.icns"
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET d2k_app POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DAPP=$<TARGET_BUNDLE_DIR:d2k_app>
            -DEXE=$<TARGET_FILE:d2k_app>
            -DSDL=$<TARGET_FILE:SDL3::SDL3>
            -DSONAME=$<TARGET_SONAME_FILE_NAME:SDL3::SDL3>
            -P ${CMAKE_SOURCE_DIR}/cmake/BundleSDL.cmake
        COMMENT "Bundling SDL3 into ${D2K_APP_NAME}.app")
    # Built-in 3D models (our own, tools/models/): Contents/Resources/assets/models
    # (SDL_GetBasePath of a bundle = Contents/Resources; render3d/models.cpp builtInModelsDir).
    add_custom_command(TARGET d2k_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/models"
            "$<TARGET_BUNDLE_CONTENT_DIR:d2k_app>/Resources/assets/models"
        # Designed 3D terrain (render3d/terrain_v2.*): rock meshes + the CC0 textures fetched by
        # tools/fetch_terrain_assets.py (whatever is there; without the textures it uses the tiles).
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/terrain"
            "$<TARGET_BUNDLE_CONTENT_DIR:d2k_app>/Resources/assets/terrain"
        # The pre-rendered Graphics / welcome page previews (platform/preset_previews.hpp).
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:d2k_app>/Resources/assets/previews"  # (no pictures of an older set)
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/previews"
            "$<TARGET_BUNDLE_CONTENT_DIR:d2k_app>/Resources/assets/previews"
        COMMENT "Copying the built-in 3D models, terrain assets and previews into ${D2K_APP_NAME}.app")
endif()

# Windows / Linux: the game executable(s), SDL3 runtime next to them, built-in models, install
# rules for a portable folder (cmake --install build --prefix dist). cmake/Desktop.cmake.
if(NOT APPLE)
    include(cmake/Desktop.cmake)
endif()

foreach(t d2k_core d2k_frontend d2k ${D2K_EXTRA_APP_TARGETS})
    d2k_project_options(${t})
    d2k_warnings(${t})
endforeach()

# Tests (use the local original game data when present; without it the data tests are skipped).
set(D2K_TEST_DATA_DIR "${CMAKE_SOURCE_DIR}/game/Dune 2000/data" CACHE PATH
    "Original game data folder used by formats_test / game_test (skipped when missing)")
file(TO_CMAKE_PATH "${D2K_TEST_DATA_DIR}" D2K_TEST_DATA_DIR_FWD)  # forward slashes: it becomes a C string
enable_testing()
add_executable(formats_test tests/formats_test.cpp)
target_link_libraries(formats_test PRIVATE d2k_core)
target_compile_definitions(formats_test PRIVATE D2K_GAME_DATA_DIR="${D2K_TEST_DATA_DIR_FWD}")
add_test(NAME formats_test COMMAND formats_test)

add_executable(game_test tests/game_test.cpp)
target_link_libraries(game_test PRIVATE d2k_core)
target_compile_definitions(game_test PRIVATE D2K_GAME_DATA_DIR="${D2K_TEST_DATA_DIR_FWD}")
add_test(NAME game_test COMMAND game_test)
foreach(t formats_test game_test)
    d2k_project_options(${t})
endforeach()
