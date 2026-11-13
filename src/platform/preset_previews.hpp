#pragma once
// Options -> Graphics previews (docs/enhanced-graphics.md "Presets"): one curated scene - the first
// Atreides mission's map (A1V1, in every install) with a small Atreides base, a harvester on the
// spice and a Harkonnen raid at the ridge - rendered OFFSCREEN with the settings of a preset tab, or
// of the Custom tab with one row changed, into textures.
//
// Shipped set (owner decision 2026-11-13): `d2k --render-previews assets/previews` renders every
// picture the menus show for the default setups (the three presets = the welcome cards; every value
// of every picture row of the Custom tab from the default Original / Enhanced / 3D setup) and the
// release ships them (assets/previews: PNGs + index.json, tools/release/render_previews.sh). Their
// keys (shippedKeyOf) name only what the picture depends on in a portable way: scene version, the
// A1V1.MAP contents, the text language, the framing, the settings and - for pictures drawn with an
// art / model pack - the pack's fingerprint (packFingerprint: its file names, sizes and .ini
// texts). A shipped picture is shown when its key matches; a changed pack only drops the pictures
// drawn with it (the original-art ones stay valid). Pictures that show the original 1998 art carry
// a small watermark (assets/LICENSES.md "Preview images").
//
// Everything else (unusual Custom combinations, other languages, mods, another pack version) is
// rendered locally from the player's own data and cached as PNGs in <pref>/previews, named by a
// hash of everything they depend on (engine scene version, data folder, language, art / model pack
// stamps, the picture's settings and framing), so they are made once and remade only when
// something changes. Work is spread over the menu frames (work(): the map data loads on a worker
// thread, then one preview game - a Session with its own Settings, drawn into a 640x400 target -
// renders a few warm-up frames per call); the page shows a placeholder until a picture is ready.
// The preview game is dropped a few seconds after the Graphics page closes.

#include "platform/game_options.hpp"
#include "platform/settings.hpp"

#include <SDL3/SDL.h>

#include <deque>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>

namespace d2k::game {
class World;
}
namespace d2k::render {
class TextRenderer;
}

namespace d2k::platform {

struct GameData;
class Session;
class Ui;

// The framing of a preview: the whole frame, the base close up (lamps, buildings), the fight close
// up (effects, health bars), the ridge (terrain relief / cliffs).
enum class PreviewShot { Overview, Base, Battle, Terrain };
PreviewShot previewShotFor(PictureOption o);

// Lays the curated scene out in a freshly created world (Session::createWorld with
// SessionConfig::presetPreview; the objects of the map and its events were stripped).
void presetPreviewScene(game::World& world, int side);

class GraphicsPreviews {
public:
    GraphicsPreviews(SDL_Renderer* r, render::TextRenderer* font);
    ~GraphicsPreviews();
    GraphicsPreviews(const GraphicsPreviews&) = delete;
    GraphicsPreviews& operator=(const GraphicsPreviews&) = delete;

    // A new data folder / language: drops everything. cacheDir "" = no disk cache. textLanguage: the
    // language of the text drawn (the shipped pictures' language, "en" for the shipped set).
    void setData(const std::string& dataDir, const std::string& languageKey, const std::string& cacheDir,
                 const std::string& textLanguage = "en");
    // One-frame runs: pictures are rendered at once when asked for (blocking).
    void setSynchronous(bool on) { sync_ = on; }

    struct Picture {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
        bool pending = false;      // queued / rendering: show a placeholder
        bool unavailable = false;  // cannot be made (no data, 3D without a model pack, render failure)
    };
    // The settings a preset tab stands for (the art pack choice follows `current`; 3D: the model
    // pack's models). False when the preset cannot be previewed (3D without a model pack).
    static bool presetSettings(int preset, const Settings& current, Settings& out);
    Picture preset(int preset, const Settings& current, bool prefetch = false);
    Picture picture(const Settings& s, PreviewShot shot, bool prefetch = false);
    // Whether `a` and `b` draw the same preview (a row that does not change the picture here).
    bool samePicture(const Settings& a, const Settings& b, PreviewShot shot) const;

    // Once per menu frame (outside the page drawing): renders queued pictures; drops the preview
    // game after a while without requests.
    void work();
    void release();  // a game starts: free the preview game now
    // Once the three preset pictures are there: queues the common Custom comparisons for `current`
    // (each picture row's current and next value), once per setup.
    void prefetch(const Settings& current);

    // --render-previews OUTDIR: renders the shipped set (see the top) for `current` (the defaults of a
    // one-frame run) into OUTDIR: PNGs + index.json. Returns the process exit code.
    int renderShippedSet(const std::string& outDir, const Settings& current);
    // The folder of the shipped set (assets/previews next to the program / in the bundle; "" = none).
    static std::string shippedDir();
    // A pack's fingerprint: FNV-1a 64 over its DATA/, models/, variants/ file names + sizes and the
    // texts of pack.ini / models.ini (tools/release/check_previews.py computes the same).
    static std::string packFingerprint(const std::string& packDir);
    // Whether the picture of `s` shows original 1998 art (Graphics: Original, original / upscaled
    // sprites, original tiles terrain, PS1 models): the shipped copy carries the watermark.
    static bool showsOriginalArt(const Settings& s);

private:
    struct Entry {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
        Uint64 used = 0;
        bool failed = false, queued = false;
        std::string shipped;  // the shipped PNG of this picture (loaded when first shown)
    };
    // What the shipped keys depend on beyond the settings (read on a worker thread: ~0.1 s).
    struct Env {
        std::string mapHash;
        std::unordered_map<std::string, std::string> packs;  // pack name -> fingerprint
        bool terrainAssets = false;
        int generation = -1;  // render::ArtPack::generation() it was read at
    };
    std::string shippedKeyOf(const Settings& s, PreviewShot shot);  // "" while the Env is read
    bool envReady(bool block);
    void loadShippedIndex();
    struct Job {
        std::string key;
        Settings s;
        PreviewShot shot = PreviewShot::Overview;
        int frame = -1;  // -1 not started
        bool prefetch = false;
        Uint64 asked = 0;  // SDL_GetTicks of the last request (stale interactive jobs are dropped)
    };
    Uint64 lastQueued_ = 0;  // a new picture the player asked for (clicking through values: wait)
    float lastMx_ = -1, lastMy_ = -1;  // the pointer: heavy steps start only while it rests
    Uint64 lastMove_ = 0;
    std::string keyOf(const Settings& s, PreviewShot shot) const;
    std::string fileOf(const std::string& key) const;
    Picture result(Entry& e);
    void runJob(Job& job, int frames);  // renders up to `frames` warm-up frames of the job
    bool ensureSession(const Settings& first, bool block);
    void renderFrame();
    void finish(Job& job);
    void trimMemory();
    void trimDisk();

    SDL_Renderer* r_;
    std::unique_ptr<Ui> ui_;
    std::string dataDir_, languageKey_, cacheDir_, textLanguage_ = "en";
    // The shipped set: key -> file, on when the index matches this engine's scene version.
    bool shippedOn_ = true;
    std::unordered_map<std::string, std::string> shipped_;
    std::future<Env> envLoad_;
    Env env_;
    bool envHave_ = false;
    // --render-previews: the picture being captured (finish() hands it over) and its watermark.
    SDL_Surface** capture_ = nullptr;
    bool watermark_ = false;
    bool sync_ = false;
    std::unordered_map<std::string, Entry> entries_;
    std::deque<Job> jobs_;
    // The preview game.
    std::future<std::unique_ptr<GameData>> dataLoad_;
    bool dataFailed_ = false;
    Settings settings_;  // what the preview session draws with (Session keeps a reference)
    std::unique_ptr<Session> session_;
    SDL_Texture* target_ = nullptr;
    Uint64 lastRequest_ = 0;
    std::string prefetched_;
    // Pack stamps (file times) are read at most every two seconds.
    mutable std::unordered_map<std::string, std::string> stamps_;
    mutable Uint64 stampsAt_ = 0;
    const std::string& cachedStamp(const std::string& what, bool model) const;
};

// The Graphics page's picture card: the picture fitted into `r`, else a placeholder with the
// preset badge and "Rendering preview..." (pending) or `note` / "Preview unavailable".
void drawPreviewCard(Ui& ui, const SDL_FRect& r, const GraphicsPreviews::Picture& p, const char* badge, const char* note,
                     float textScale);
// The Custom tab's column inside `area`: the row pointed at (probe) compared - the picture with the
// current settings and with that row stepped as a click would - or the current setup alone.
void customPreviewColumn(Ui& ui, GraphicsPreviews& previews, const Settings& s, const PictureProbe& probe, const SDL_FRect& area,
                         float textScale);
inline void prefetchPreviews(GraphicsPreviews& p, const Settings& s) { p.prefetch(s); }

}  // namespace d2k::platform
