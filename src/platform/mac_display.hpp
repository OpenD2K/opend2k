#pragma once
// macOS display facts SDL does not expose (platform/mac_display.mm; other platforms: the stubs).
//
// potentialEdrHeadroom: NSScreen.maximumPotentialExtendedDynamicRangeColorComponentValue of the
// window's screen - the most EDR headroom the display can ever give (1 = an SDR display: an
// extended-range swapchain there only costs compositing time). -1 = unknown (not macOS).
// metalLayerInfo: the window's CAMetalLayer as SDL_gpu set it up (drawable count, display sync,
// pixel format, EDR), for the log ("" when there is none).

#include <SDL3/SDL.h>

#include <string>

namespace d2k::platform {

#ifdef __APPLE__
float potentialEdrHeadroom(SDL_Window* w);
std::string metalLayerInfo(SDL_Window* w);
#else
inline float potentialEdrHeadroom(SDL_Window*) { return -1.0f; }
inline std::string metalLayerInfo(SDL_Window*) { return {}; }
#endif

}  // namespace d2k::platform
