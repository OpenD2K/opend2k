#include "platform/platform_info.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace d2k::platform {

std::string deviceModel() {
    if (const char* sim = std::getenv("SIMULATOR_MODEL_IDENTIFIER"); sim && *sim) return sim;
#if defined(__APPLE__)
    char buf[64] = {};
    std::size_t n = sizeof buf - 1;
    if (sysctlbyname("hw.machine", buf, &n, nullptr, 0) == 0) return std::string(buf);
#endif
#if defined(__ANDROID__)
    char man[PROP_VALUE_MAX] = {}, mod[PROP_VALUE_MAX] = {};
    __system_property_get("ro.product.manufacturer", man);
    __system_property_get("ro.product.model", mod);
    if (*mod) return *man ? std::string(man) + " " + mod : std::string(mod);
#endif
    return {};
}

DeviceTier deviceTier() {
    const std::string m = deviceModel();
    // "iPadN,M": N = the SoC generation. 1..7: A4..A10X; 11: A12 (mini 5, Air 3, iPad 8);
    // 8: A12X/Z Pro 2018/2020, 12: A13 (iPad 9), 13+: A14 / M1 and later.
    if (m.rfind("iPad", 0) == 0) {
        const int major = std::atoi(m.c_str() + 4);
        if (major > 0 && (major <= 7 || major == 11)) return DeviceTier::Low;
        if (major > 0) return DeviceTier::Standard;
    }
    if constexpr (kTouchPlatform) {
        if (SDL_GetSystemRAM() > 0 && SDL_GetSystemRAM() < 3500) return DeviceTier::Low;
    }
    return DeviceTier::Standard;
}

bool isPhoneDevice() {
#if defined(__ANDROID__)
    // Android: a phone = the screen's short side under 600 dp (the platform's phone / tablet line);
    // SDL reports the display in pixels and the density as the content scale.
    static const bool phone = [] {
        const SDL_DisplayID d = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* m = d ? SDL_GetDesktopDisplayMode(d) : nullptr;
        const float s = d ? SDL_GetDisplayContentScale(d) : 0.0f;
        if (!m || s <= 0.0f) return true;
        const float dp = float(std::min(m->w, m->h)) / s;
        SDL_Log("Android display: %dx%d px, density %.2f -> short side %.0f dp (%s)", m->w, m->h, double(s), double(dp),
                dp < 600 ? "phone layout" : "tablet layout");
        return dp < 600.0f;
    }();
    return phone;
#else
    static const bool phone = deviceModel().rfind("iPhone", 0) == 0 || deviceModel().rfind("iPod", 0) == 0;
    return phone;
#endif
}

const char* deviceTierName(DeviceTier t) { return t == DeviceTier::Low ? "lite" : "standard"; }

}  // namespace d2k::platform
