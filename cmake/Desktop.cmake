# Windows / Linux desktop packaging (included from CMakeLists.txt when not APPLE).
#   Windows: d2k.exe (console: logs, --simtest, --screenshot) and OpenD2K.exe (GUI subsystem, no
#            console window - the one players start), both with cmake/windows/opend2k.manifest
#            (UTF-8 code page, long paths); SDL3.dll copied next to them after each build.
#   Linux:   d2k (RPATH $ORIGIN, so a bundled libSDL3.so.0 next to it is found).
#   Both:    `cmake --install build --prefix <dir>` makes a portable folder:
#            <dir>/d2k[.exe] (+ OpenD2K.exe), SDL3 runtime, assets/models, assets/terrain, assets/previews, licenses/,
#            README.md, CHANGELOG.md, LICENSE.txt (tools/release/package_*.sh zip / tar it).
# No original game data is ever installed or copied.

set(D2K_EXTRA_APP_TARGETS "")
if(WIN32)
    set(_manifest "${CMAKE_SOURCE_DIR}/cmake/windows/opend2k.manifest")
    if(MINGW)
        # MinGW: CMake does not merge .manifest sources; embed it as an RT_MANIFEST resource.
        file(WRITE "${CMAKE_BINARY_DIR}/generated/opend2k_manifest.rc"
             "1 24 \"${_manifest}\"\n")
        set(_manifest "${CMAKE_BINARY_DIR}/generated/opend2k_manifest.rc")
    endif()
    # The icon (cmake/windows/opend2k.ico, tools/make_icons.py) as a resource in both executables.
    set(_icon_rc "${CMAKE_SOURCE_DIR}/cmake/windows/opend2k.rc")
    target_sources(d2k PRIVATE "${_manifest}" "${_icon_rc}")
    add_executable(d2k_gui WIN32 src/platform/main.cpp "${_manifest}" "${_icon_rc}")
    target_link_libraries(d2k_gui PRIVATE d2k_frontend)
    set_target_properties(d2k_gui PROPERTIES OUTPUT_NAME "OpenD2K")
    list(APPEND D2K_EXTRA_APP_TARGETS d2k_gui)
    if(MINGW)
        # MinGW (cross-compile checks; MSVC is the supported Windows compiler): static runtimes,
        # so libstdc++-6.dll / libgcc_s*.dll / libwinpthread-1.dll need not be shipped (SDL3 is
        # linked through its import library by full path, unaffected).
        foreach(t d2k d2k_gui)
            target_link_options(${t} PRIVATE -static)
        endforeach()
    endif()
    if(TARGET SDL3::SDL3-shared)
        foreach(t d2k d2k_gui)
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:${t}>"
                VERBATIM)
        endforeach()
    endif()
else()
    set_target_properties(d2k PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS d2k ${D2K_EXTRA_APP_TARGETS} RUNTIME DESTINATION .)
if(TARGET SDL3-shared)  # built by cmake/FetchSDL.cmake
    install(TARGETS SDL3-shared RUNTIME DESTINATION . LIBRARY DESTINATION . NAMELINK_SKIP)
elseif(TARGET SDL3::SDL3-shared)  # installed SDL3 (vcpkg, SDL's VC package, distro package)
    install(IMPORTED_RUNTIME_ARTIFACTS SDL3::SDL3-shared RUNTIME DESTINATION . LIBRARY DESTINATION .)
endif()
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/models" DESTINATION assets)
if(NOT WIN32)  # Linux: the icon for a .desktop entry / file managers (tools/make_icons.py)
    install(FILES "${CMAKE_SOURCE_DIR}/assets/icon/png/opend2k-256.png" DESTINATION . RENAME opend2k.png)
endif()
# Designed 3D terrain: rock meshes + CC0 textures (tools/fetch_terrain_assets.py; licences in it).
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/terrain" DESTINATION assets)
# The pre-rendered Graphics / welcome page previews (platform/preset_previews.hpp; assets/LICENSES.md).
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/previews" DESTINATION assets)
install(FILES "${CMAKE_SOURCE_DIR}/third_party/fonts/play/OFL.txt" DESTINATION licenses RENAME Play-Font-OFL.txt)
install(FILES "${CMAKE_SOURCE_DIR}/third_party/cgltf/LICENSE" DESTINATION licenses RENAME cgltf-LICENSE.txt)
# Third-party notices for everything compiled in (stb and the LZMA SDK are public domain; listed anyway).
install(FILES "${CMAKE_SOURCE_DIR}/third_party/lzma/README.md" DESTINATION licenses RENAME LZMA-SDK-README.md)
install(FILES "${CMAKE_SOURCE_DIR}/third_party/LICENSES.md" DESTINATION licenses RENAME THIRD-PARTY.md OPTIONAL)
install(FILES "${CMAKE_SOURCE_DIR}/assets/LICENSES.md" DESTINATION assets OPTIONAL)
foreach(_doc README.md CHANGELOG.md)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_doc}")
        install(FILES "${CMAKE_SOURCE_DIR}/${_doc}" DESTINATION .)
    endif()
endforeach()
# OpenD2K's own licence (GPL-3.0-or-later) next to the program.
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION . RENAME LICENSE.txt)
if(TARGET SDL3-shared AND EXISTS "${sdl3_SOURCE_DIR}/LICENSE.txt")
    install(FILES "${sdl3_SOURCE_DIR}/LICENSE.txt" DESTINATION licenses RENAME SDL3-LICENSE.txt)
endif()
