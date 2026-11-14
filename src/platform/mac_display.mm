#include "platform/mac_display.hpp"

#include <TargetConditionals.h>

#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

#include <cstdio>

namespace d2k::platform {

#if TARGET_OS_OSX
namespace {

NSWindow* nsWindow(SDL_Window* w) {
    if (!w) return nil;
    return (__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}

CAMetalLayer* findMetalLayer(CALayer* l) {
    if (!l) return nil;
    if ([l isKindOfClass:[CAMetalLayer class]]) return (CAMetalLayer*)l;
    for (CALayer* s in l.sublayers)
        if (CAMetalLayer* m = findMetalLayer(s)) return m;
    return nil;
}

CAMetalLayer* findMetalLayer(NSView* v) {
    if (!v) return nil;
    if (CAMetalLayer* m = findMetalLayer(v.layer)) return m;
    for (NSView* s in v.subviews)
        if (CAMetalLayer* m = findMetalLayer(s)) return m;
    return nil;
}

}  // namespace

float potentialEdrHeadroom(SDL_Window* w) {
    @autoreleasepool {
        NSWindow* win = nsWindow(w);
        NSScreen* screen = win ? win.screen : nil;
        if (!screen) screen = [NSScreen mainScreen];
        if (!screen) return -1.0f;
        return float(screen.maximumPotentialExtendedDynamicRangeColorComponentValue);
    }
}

std::string metalLayerInfo(SDL_Window* w) {
    @autoreleasepool {
        NSWindow* win = nsWindow(w);
        CAMetalLayer* m = win ? findMetalLayer(win.contentView) : nil;
        if (!m) return {};
        // The layer's colour space: nil = not colour matched (raw values on the display's own gamut / curve).
        char cs[96] = "nil (not colour matched)";
        if (CGColorSpaceRef c = m.colorspace) {
            if (CFStringRef n = CGColorSpaceCopyName(c)) {
                CFStringGetCString(n, cs, sizeof cs, kCFStringEncodingUTF8);
                CFRelease(n);
            } else {
                std::snprintf(cs, sizeof cs, "unnamed");
            }
        }
        char buf[384];
        std::snprintf(buf, sizeof buf,
                      "CAMetalLayer %.0fx%.0f, pixel format %d, %u drawables, display sync %s, EDR %s, opaque %s, colour space %s",
                      m.drawableSize.width, m.drawableSize.height, int(m.pixelFormat), unsigned(m.maximumDrawableCount),
                      m.displaySyncEnabled ? "on" : "off", m.wantsExtendedDynamicRangeContent ? "on" : "off",
                      m.opaque ? "yes" : "no", cs);
        return buf;
    }
}

#else

float potentialEdrHeadroom(SDL_Window*) { return -1.0f; }
std::string metalLayerInfo(SDL_Window*) { return {}; }

#endif

}  // namespace d2k::platform
