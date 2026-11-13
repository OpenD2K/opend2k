#pragma once
// Mouse pointer helpers that also work while the real pointer is outside the window's content
// area in fullscreen, e.g. in the menu-bar band around a MacBook's camera notch: there SDL stops
// reporting the pointer to the window, so these read the global pointer and the display bounds.
// Header-only; harmless on platforms without a notch (the safe area is the whole window).

#include "platform/platform_info.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace d2k::platform {

// Window points -> output pixels. Mouse / touch positions arrive in window points; everything
// in the app (layout, hit tests, drawing) works in render output pixels. They differ on a Retina
// display when the window has SDL_WINDOW_HIGH_PIXEL_DENSITY (Settings "Render resolution": Full
// on an iPad / Retina Mac; 1 at Half = point resolution). The App converts every mouse event
// with eventToPixels() as it arrives; code that reads SDL_GetMouseState() scales by this itself.
// windowToPixels: SDL window coordinates (events, SDL_GetMouseState, window size, safe area) -> output
// pixels. pixelsPerPoint: output pixels per UI point (layout sizes). The same everywhere except Android,
// where SDL's window coordinates ARE pixels and the screen density (pixels per dp) is only the display
// scale: there windowToPixels = 1 and pixelsPerPoint = the density, so the UI keeps its physical size.
inline float windowToPixels(SDL_Window* win) {
    if (!win) return 1.0f;
    int ww = 0, wh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    return ww > 0 && pw > 0 ? float(pw) / float(ww) : 1.0f;
}
inline float windowToPixels(SDL_Renderer* r) { return r ? windowToPixels(SDL_GetRenderWindow(r)) : 1.0f; }
inline float pixelsPerPoint(SDL_Window* win) {
    if constexpr (kAndroid) {
        const float s = win ? SDL_GetWindowDisplayScale(win) : 0.0f;
        return s > 0.0f ? s : 1.0f;
    }
    return windowToPixels(win);
}
inline float pixelsPerPoint(SDL_Renderer* r) { return r ? pixelsPerPoint(SDL_GetRenderWindow(r)) : 1.0f; }

inline void eventToPixels(SDL_Event& e, float k) {
    if (k == 1.0f) return;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        e.motion.x *= k;
        e.motion.y *= k;
        e.motion.xrel *= k;
        e.motion.yrel *= k;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        e.button.x *= k;
        e.button.y *= k;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        e.wheel.mouse_x *= k;
        e.wheel.mouse_y *= k;
        break;
    default: break;
    }
}

// Edge scrolling against the DISPLAY edges (fullscreen): a pointer at or beyond the content
// edge, including inside the notch / menu-bar band above it, scrolls that way. The scroll zone
// starts at the visible content edge (SDL_GetWindowSafeArea). Needs the keyboard focus (the game
// is the active app). Returns false when the window is not focused.
struct ScreenEdges {
    bool left = false, right = false, up = false, down = false;
};
inline bool screenEdges(SDL_Window* win, ScreenEdges& e) {
    e = {};
    // iPad: the "pointer" is mostly the last touch (a finger on the sidebar is at the right
    // edge); touch scrolls with gestures instead (platform/touch_controls.hpp).
    if (kTouchPlatform) return false;
    if (!win || SDL_GetKeyboardFocus() != win) return false;
    float gx = 0, gy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(win, &wx, &wy);
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_Rect safe{0, 0, ww, wh};
    if (!SDL_GetWindowSafeArea(win, &safe) || safe.w <= 0 || safe.h <= 0) safe = {0, 0, ww, wh};
    // Content edges in global points.
    const float l = float(wx + safe.x), t = float(wy + safe.y);
    const float r = float(wx + safe.x + safe.w), b = float(wy + safe.y + safe.h);
    e.left = gx < l + 1;
    e.right = gx >= r - 1;
    e.up = gy < t + 1;
    e.down = gy >= b - 1;
    return true;
}

// Mouse position in window points for drawing the game cursor: the window's own position when it
// has the mouse; in fullscreen, while the pointer is outside the content area (the notch band),
// the global position clamped to the content. False when there is nothing to draw.
inline bool cursorPosition(SDL_Window* win, bool fullscreen, float& x, float& y) {
    if (!win) return false;
    if (SDL_GetMouseFocus() == win) {
        SDL_GetMouseState(&x, &y);
        return true;
    }
    if (!fullscreen || SDL_GetKeyboardFocus() != win) return false;
    float gx = 0, gy = 0;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(win, &wx, &wy);
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_Rect safe{0, 0, ww, wh};
    if (!SDL_GetWindowSafeArea(win, &safe) || safe.w <= 0 || safe.h <= 0) safe = {0, 0, ww, wh};
    x = std::clamp(gx - float(wx), float(safe.x), float(safe.x + safe.w - 1));
    y = std::clamp(gy - float(wy), float(safe.y), float(safe.y + safe.h - 1));
    return true;
}

}  // namespace d2k::platform
