#pragma once
// Top level of the executable: window/renderer, settings, data folder, shared audio and music,
// and the state machine main menu <-> game session. Also the headless/one-frame modes
// (--simtest, --screenshot/--smoke, --menu-screenshot).

#include "formats/font.hpp"
#include "platform/game_data.hpp"
#include "platform/music.hpp"
#include "platform/options.hpp"
#include "platform/settings.hpp"
#include "game/sound_system.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <memory>
#include <string>

namespace d2k::render {
class TextRenderer;
class CursorRenderer;
class Hdr;
}

namespace d2k::platform {

class Audio;
class SoundBank;
class Ui;
class Menu;
class Movies;
class Session;
class TouchControls;
class GraphicsPreviews;

class App {
public:
    explicit App(Options opt);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int run();  // returns the process exit code

private:
    int runSimTest();
    int runSoundTest();
    int runInstallFrom();
    bool initVideo();
    void shutdownVideo();
    bool loadData(const std::string& dir);
    bool startGame(const GameStart& start);
    bool loadSavedGame(int slot, std::string& error);  // platform/saves.hpp slot -> new session
    void drawLoading(const std::string& what);
    bool saveScreenshot(const std::string& path);
    // The Graphics page's previews, rendered offscreen from the data (platform/preset_previews.hpp).
    std::unique_ptr<GraphicsPreviews> previews_;
    std::string previewStage_;  // --render-previews: the temporary art pack folder (removed at exit)
    void persistSettings();
    void drawCursor();
    // Frame-time overlay (Settings::perfOverlay; iPad: four-finger tap): the last second's fps,
    // frame and CPU (update + render, before present) times, the output size and the device.
    void drawPerfOverlay();
    std::vector<float> perfFrameMs_, perfCpuMs_;  // ring buffers (last 120 frames)
    std::size_t perfAt_ = 0;
    Uint64 perfLast_ = 0, perfCpuEnd_ = 0;

    Options opt_;
    Settings settings_;
    bool persist_ = true;  // one-frame/screenshot runs never write the settings file
    int recordFrame_ = 0;  // --record: next frame number
    std::string dataDir_;
    std::string languageKey_;  // platform/language.hpp languageKey() of the loaded front end
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool vsync_ = false;       // presents wait for the display; else frames are paced by hand
    // Display settings in effect (Settings::vsync / fpsLimit): applySync() when they change.
    Settings::VSync appliedVsync_ = Settings::VSync::On;
    bool syncApplied_ = false;
    void applySync();
    // EDR / HDR display output (Settings::hdrOutput, render/hdr.hpp): the frame is drawn into
    // canvas_ and presentFrame() decodes it into the half-float backbuffer.
    bool hdrExtended_ = false;
    float hdrHeadroom_ = 1.0f;
    int hdrCheckFrames_ = 0;  // frames until the backbuffer format is checked
    std::unique_ptr<render::Hdr> hdrOut_;
    SDL_Texture* canvas_ = nullptr;
    bool setHdrSwapchain(bool hdr);
    void updateHdrHeadroom(bool log);
    void beginFrame();
    void presentFrame();
    bool renderFull_ = false;  // window created with SDL_WINDOW_HIGH_PIXEL_DENSITY (Settings::fullPixelDensity)
    Uint64 nextFrameNs_ = 0;   // manual pacing deadline (no vsync)
    std::unique_ptr<Ui> ui_;
    formats::Font font_;
    std::unique_ptr<render::TextRenderer> text_;
    std::unique_ptr<render::CursorRenderer> cursor_;  // MOUSE.R16 (null: the system cursor stays)
    std::unique_ptr<Audio> audio_;
    std::unique_ptr<SoundBank> sounds_;
    MusicPlayer music_;
    // Which screen the music was picked for (re/notes/audio.md): a session, else a menu page.
    const Session* musicSession_ = nullptr;
    int musicMenuPage_ = -1;
    void updateSound();  // music per screen + the UI's "BUTTON" clicks
    game::SoundSystem menuSpeech_;  // QueueAudioToPlay in the front end (campaign pages)
    std::unique_ptr<Menu> menu_;
    std::unique_ptr<Session> session_;
    // Cutscenes (platform/movies.hpp): while one plays it owns the screen and the input.
    std::unique_ptr<Movies> movies_;
    bool swallowMouseUp_ = false;  // a click skipped a movie: its release is not a UI click
    void startupMovie();  // --movie, else the intro on startup (Settings::introOnStartup)
    // iPad (docs/ios.md): touch gestures during a game (platform/touch_controls.hpp); the game
    // cursor is only drawn for a real mouse / trackpad.
    std::unique_ptr<TouchControls> touch_;
    bool realMouse_ = !kTouchPlatform;
    // iPad: set from an SDL event watch when the app goes to the background (no drawing, audio
    // paused, the game paused) and back.
    std::atomic<bool> background_{false};
    bool inBackground_ = false;
    static bool SDLCALL appEventWatch(void* self, SDL_Event* e);
    void enterBackground(bool bg);
};

}  // namespace d2k::platform
