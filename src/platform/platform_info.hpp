#pragma once
// Compile-time platform facts (no SDL): the iPad build (docs/ios.md) switches a few defaults and
// the input to touch. TARGET_OS_IOS is also set for Mac Catalyst, which we do not build.

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <string>

namespace d2k::platform {

#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
inline constexpr bool kIOS = true;
#else
inline constexpr bool kIOS = false;
#endif

#if defined(__ANDROID__)
inline constexpr bool kAndroid = true;
#else
inline constexpr bool kAndroid = false;
#endif

// Touch-first platform: touch controls (platform/touch_controls.hpp), no edge scrolling by the
// pointer, the data folder from the app's Documents (iPad: Files app; Android: the app's external
// files folder, Android/data/com.opend2k.app/files), Enhanced graphics by default.
inline constexpr bool kTouchPlatform = kIOS || kAndroid;

// The device model ("iPad7,11"; in the Simulator the simulated model; elsewhere hw.machine /
// "" when unknown) and a rough performance class (platform_info.cpp). Low: A10 / A12-class iPads
// (iPad 6/7/8, iPad mini 5, iPad Air 3, iPad Pro 2017) or under 3.5 GB of memory; their first
// run gets Settings::applyLiteProfile().
std::string deviceModel();
enum class DeviceTier { Low, Standard };
DeviceTier deviceTier();
const char* deviceTierName(DeviceTier t);
// An iPhone (or the iPhone Simulator): the phone layout's Auto choice (Settings::touchLayout).
bool isPhoneDevice();

}  // namespace d2k::platform
