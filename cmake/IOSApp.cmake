# iPadOS / iPhone app target (docs/ios.md). Included from CMakeLists.txt when CMAKE_SYSTEM_NAME is iOS:
#   cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0
# then open build-ios/d2k.xcodeproj, pick the signing team, run on the iPad (or the Simulator).
# No original game data goes into the bundle: the player copies the Gruntmods installer (or a
# "Dune 2000" folder) into the app's Documents via the Files app / Finder.

set(D2K_IOS_BUNDLE_ID "com.opend2k.app" CACHE STRING "iOS bundle identifier (must be unique per Apple ID)")
set(D2K_IOS_TEAM "" CACHE STRING "Apple development team ID for automatic signing (empty: choose it in Xcode)")

set(D2K_IOS_ASSETS "${CMAKE_SOURCE_DIR}/cmake/ios/Assets.xcassets")  # AppIcon from assets/icon (our own art)
set_source_files_properties(${D2K_IOS_ASSETS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
add_executable(d2k_ios MACOSX_BUNDLE src/platform/main.cpp ${D2K_IOS_ASSETS})
target_link_libraries(d2k_ios PRIVATE d2k_frontend)
set_target_properties(d2k_ios PROPERTIES
    OUTPUT_NAME "OpenD2K"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info-iOS.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "OpenD2K"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${D2K_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${D2K_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"          # iPhone (phone layout, landscape) + iPad
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${D2K_IOS_TEAM}"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
    XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES"
    XCODE_GENERATE_SCHEME ON
    # Run = optimised (the Debug configuration at -O0, SDL3 included, made the game slow on an
    # older iPad) and without Metal API validation / GPU frame capture, which cost CPU per draw.
    XCODE_SCHEME_LAUNCH_CONFIGURATION "RelWithDebInfo"
    XCODE_SCHEME_ENABLE_GPU_API_VALIDATION OFF
    XCODE_SCHEME_ENABLE_GPU_SHADER_VALIDATION OFF
    XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE "Disabled"
    XCODE_SCHEME_ENVIRONMENT "")
# Simulator builds need no signing identity.
set_target_properties(d2k_ios PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED[sdk=iphonesimulator*] "NO")
target_compile_options(d2k_ios PRIVATE -Wall -Wextra)
foreach(t d2k_core d2k_frontend)
    target_compile_options(${t} PRIVATE -Wall -Wextra)
endforeach()

# Built-in 3D models (our own, tools/models/): <bundle>/assets/models (SDL_GetBasePath of an iOS
# app is the bundle root; render3d/models.cpp builtInModelsDir).
add_custom_command(TARGET d2k_ios POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/models"
        "$<TARGET_BUNDLE_DIR:d2k_ios>/assets/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/terrain"
        "$<TARGET_BUNDLE_DIR:d2k_ios>/assets/terrain"
    # The pre-rendered Graphics / welcome page previews (platform/preset_previews.hpp).
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_DIR:d2k_ios>/assets/previews"  # (no pictures of an older set)
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/previews"
        "$<TARGET_BUNDLE_DIR:d2k_ios>/assets/previews"
    COMMENT "Copying the built-in 3D models, terrain assets and previews into OpenD2K.app")
