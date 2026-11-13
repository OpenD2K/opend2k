#pragma once
// The gameplay option rows shared by the main Options page and the in-game options page of the
// pause menu (the original has the same game speed / scroll rate sliders in both places).

#include <SDL3/SDL.h>

#include <functional>
#include <string>

namespace d2k::platform {

class Ui;
struct Settings;

// Main menu Options: the gameplay rows only - game speed, scroll speed, mouse controls, smooth
// motion | frame-time overlay, production queue, cheats | factory rally points. The graphics
// values live on the Graphics page (graphicsPresetTabs / graphicsOptionList below).
// row(i) gives the i-th row rect. Returns the number of rows used (kGameplayOptionRows).
int gameplayOptionRows(Ui& ui, Settings& settings, const std::function<SDL_FRect(int)>& row);
constexpr int kGameplayOptionRows = 6;

// The in-game options of the pause menu (unchanged by the graphics presets page): resolution
// (Original) / UI scale (Enhanced, 3D), game speed, scroll speed, controls, smooth motion | frame
// time, production queue, cheats | render resolution, the Enhanced art | effects, health bars |
// shroud, factory rally points (| 3D sun shadows) and (Enhanced / 3D only) the panel
// transparency | terrain, time of day | lighting, the classic view key and the touch layout | phone UI size. No graphics-mode row: the mode is chosen in
// the main menu only. A change to a preset value marks the graphics preset Custom.
int inGameOptionRows(Ui& ui, Settings& settings, const std::function<SDL_FRect(int)>& row);
constexpr int kInGameOptionRows = 14;  // the most rows (Enhanced)

// The Graphics page's Custom tab (main menu): every graphics value grouped under headers - Mode,
// Terrain, Art, Effects, Lighting, Quality, Shroud, Interface, 3D, Display, Classic view. `rect(y, h)` maps a
// content y / height (page units) to the full-width screen rect; rows are `rowH` high with `gap`
// between them, headers `headerH`. Draws from `y0`; returns the content height used. A change to
// a preset value marks the preset Custom. `window`: the Fullscreen row switches it.
struct OptionListLayout {
    std::function<SDL_FRect(float y, float h)> rect;
    float rowH = 20, gap = 1, headerH = 16;
    // Options -> Graphics: opens Options -> Art pack. Set: the "Art" header offers the art pack
    // ("Get the art pack") while no redesign pack is installed (the in-game options leave it unset).
    std::function<void()> getArtPack;
};
// An installed redesign art pack (render::ArtPack::isRedesign; cached per ArtPack::generation()).
bool redesignArtPackInstalled();
void overrideRedesignArtPackInstalled(int v);  // screenshots (--menu-page welcome-graphics*): 0 / 1, -1 = the real state
// The Custom tab's rows that change the picture (the previews, platform/preset_previews.hpp).
enum class PictureOption { None, Mode, Terrain2D, Art, Effects, TimeOfDay, Lighting, Quality, Hdr, Msaa, Aniso, Shroud,
                           Panel, HealthBars, Terrain3D, Shadows, Models3D };
// The picture row under the mouse (else the one with the keyboard focus) and the step a click /
// Right key would make there (-1: the left arrow).
struct PictureProbe {
    PictureOption option = PictureOption::None;
    int dir = 1;
};
float graphicsOptionList(Ui& ui, Settings& settings, const OptionListLayout& layout, float y0, SDL_Window* window,
                         PictureProbe* probe = nullptr);
// `s` with option `o` stepped by `dir` like the row's choice / toggle would (false: no such row).
bool stepPictureOption(Settings& s, PictureOption o, int dir);
const char* pictureOptionLabel(PictureOption o);  // the row's label (untranslated)
std::string pictureOptionValue(const Settings& s, PictureOption o);  // its current value, translated
// Its height (for the scroll area before it is drawn): 11 headers, 18 rows.
// Display rows (displayOptionList): resolution, fullscreen / render, touch, vsync / limit, HDR rendering / display; Windows: + graphics API.
#if defined(_WIN32)
inline constexpr int kDisplayOptionRows = 6;
#else
inline constexpr int kDisplayOptionRows = 5;
#endif
inline float graphicsOptionListHeight(const OptionListLayout& L) {
    return 11 * (L.headerH + L.gap) + (13 + kDisplayOptionRows) * (L.rowH + L.gap);
}
// The GPU backend this run uses ("vulkan", "direct3d12", ...; "" = none), for the graphics API row's label.
void setActiveGpuDriver(const char* driver);
// The Display rows alone (resolution / UI scale, fullscreen | render resolution, touch layout |
// phone UI size, VSync | frame limit), also shown under
// the preset tabs: they are not part of a preset. Returns the content height used.
float displayOptionList(Ui& ui, Settings& settings, const OptionListLayout& layout, float y0, SDL_Window* window);

// Whether the running window has full pixel density (App, at window creation): the Render
// resolution row says "Restart to apply" while the setting differs.
void setRenderFullActive(bool full);
bool renderFullActive();
// The HDR output setting the renderer was created with (App): the row says "Restart to apply"
// while the setting differs; active = the display really gets extended range.
void setHdrOutputStart(bool requested, bool active);

// Main menu Options only: Language (one pick for text, voices and cutscenes), then the Advanced
// mix: Text | Voices, Cutscenes | a hint on what the pick lacks (platform/language.hpp).
// Entries without any source are left out. Returns the number of rows (kLanguageOptionRows).
int languageOptionRows(Ui& ui, Settings& settings, const std::function<SDL_FRect(int)>& row);
constexpr int kLanguageOptionRows = 4;

}  // namespace d2k::platform
