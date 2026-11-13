#include "platform/preset_previews.hpp"

#include "formats/io.hpp"
#include "formats/json.hpp"
#include "game/scenario.hpp"
#include "game/world.hpp"
#include "platform/app_update.hpp"
#include "platform/game_data.hpp"
#include "platform/session.hpp"
#include "platform/sim_test.hpp"
#include "platform/ui.hpp"
#include "render/art_pack.hpp"
#include "render/hdr.hpp"
#include "render/ttf_font.hpp"
#include "render/ui_text.hpp"
#include "render3d/models.hpp"
#include "render3d/terrain_v2.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace d2k::platform {

namespace {

// Bump when the scene, its framing or anything else that changes every picture changes: the
// cached PNGs are then remade.
constexpr int kSceneVersion = 2;  // 2: close-ups without the empty band (3D), the shipped set
constexpr int kW = 640, kH = 400;   // the preview frame (output pixels, 1 per point)
constexpr int kSceneTicks = 45;     // simulated before the warm-up frames
constexpr int kWarmupFrames = 10;   // drawn (one tick each) before the capture: effects running
constexpr int kFramesPerWork = 4;   // per menu frame
constexpr std::size_t kMaxTextures = 40, kMaxFiles = 200;
constexpr const char* kMap = "A1V1.MAP";
constexpr const char* kMis = "_A1V1.MIS";  // its mission file (tileset, houses)

void drawWatermark(SDL_Renderer* r, const SDL_Rect& area);  // (the shipped set, below)

std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

// What changes in a file / folder: its size and modification time.
std::string stamp(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return "-";
    const auto t = fs::last_write_time(p, ec).time_since_epoch().count();
    const auto n = fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0;
    return std::to_string(n) + "@" + std::to_string(static_cast<long long>(t));
}

std::string artPackStamp(const std::string& name) {
    if (name.empty() || name == "original" || name == "upscaled") return name;
    for (const auto& d : render::ArtPack::searchDirs()) {
        const fs::path dir = fs::path(d) / name;
        std::error_code ec;
        if (fs::exists(dir / "pack.ini", ec)) return name + ":" + stamp(dir / "pack.ini") + ":" + stamp(dir / "DATA");
    }
    return name + ":missing";
}

// The map data of the scene: A1V1 without its objects (spice kept) and without its events, so
// nothing but the curated scene stands on it and no mission message / win / lose fires.
std::unique_ptr<GameData> loadSceneData(const std::string& dir) {
    try {
        GameStart st;
        st.map = kMap;
        st.title = "Preview";
        auto g = std::make_unique<GameData>(loadGameData(dir, st));
        for (auto& c : g->map.cells)
            if (c.special > 2 && c.special != 0xFFFF) c.special = 0;
        g->mission.events.clear();
        g->mission.conditions.clear();
        g->mission.extended = false;
        g->mission.extEvents.clear();
        g->mission.extConditions.clear();
        g->mission.timeLimit = 0;
        g->scenario = game::buildScenario(g->map, g->mission, g->templates, g->tileData);
        g->musicTrack.clear();
        return g;
    } catch (const std::exception& e) {
        SDL_Log("Graphics previews: %s", e.what());
        return nullptr;
    }
}

struct Framing {
    float x, y, zoom, tilt, yaw;  // world px centre, Enhanced / 3D zoom, 3D camera degrees
};
Framing framingOf(PreviewShot s) {
    switch (s) {
    case PreviewShot::Base: return {6.0f * 32, 5.0f * 32, 1.6f, 48, 18};
    case PreviewShot::Battle: return {12.5f * 32, 11.0f * 32, 2.0f, 46, 24};
    case PreviewShot::Terrain: return {16.0f * 32, 15.5f * 32, 1.4f, 52, 30};
    case PreviewShot::Overview: break;
    }
    return {9.0f * 32, 8.0f * 32, 1.0f, 50, 14};
}

}  // namespace

PreviewShot previewShotFor(PictureOption o) {
    using P = PictureOption;
    switch (o) {
    case P::Effects:
    case P::HealthBars:
    case P::Msaa: return PreviewShot::Battle;
    case P::TimeOfDay:
    case P::Lighting:
    case P::Hdr: return PreviewShot::Base;
    case P::Terrain2D:
    case P::Terrain3D:
    case P::Shadows:
    case P::Aniso: return PreviewShot::Terrain;
    default: return PreviewShot::Overview;
    }
}

// The scene (A1V1, 28 x 24 cells; the rock plateau in the north-west, spice east and south, a
// rock ridge in the middle of the south): an Atreides base on the plateau, a harvester on the
// spice, a few defenders, and a Harkonnen raid coming over the sand from the south-east - every
// armed unit fights (orderBattle), the barracks burns.
void presetPreviewScene(game::World& w, int me) {
    const auto& T = w.rules().templates->bin;
    auto building = [&](const std::string& n) {
        for (int i = 0; i < int(T.buildingCount); ++i)
            if (n == T.buildingNames[i]) return i;
        return -1;
    };
    auto unit = [&](const std::string& n) {
        for (int i = 0; i < int(T.unitCount); ++i)
            if (n == T.unitNames[i]) return i;
        return -1;
    };
    int foe = -1;
    for (int s = 0; s < game::kMaxSides && foe < 0; ++s)
        if (s != me && w.diplomacy(me, s) == 1) foe = s;
    if (foe < 0) foe = (me + 1) % game::kMaxSides;
    const std::string A = "ATREIDES ", H = "HARKONNEN ";
    struct B { const char* name; int x, y; int healthPct; };
    static const B kBase[] = {{"CONSTRUCTION YARD", 1, 3, 100}, {"WIND TRAP", 5, 2, 100}, {"WIND TRAP", 7, 2, 100},
                              {"REFINERY", 9, 2, 100},          {"BARRACKS", 1, 7, 30},  {"LIGHT FACTORY", 4, 6, 100},
                              {"MEDIUM GUN TURRET", 11, 7, 100}, {"MEDIUM GUN TURRET", 8, 10, 100}};
    for (const B& b : kBase) {
        const int ty = building(A + b.name);
        if (ty < 0 || !w.canCheatSpawnBuilding(ty, b.x, b.y)) continue;
        const int i = w.addBuilding(me, ty, b.x, b.y, true, true, true);
        if (i >= 0 && b.healthPct < 100) w.sides()[me].buildings[i].health = std::int32_t(T.buildings[ty].hitPoints) * b.healthPct / 100;
    }
    struct U { int side; const char* name; int x, y; bool selected; };
    const U kUnits[] = {{me, "HARVESTER", 20, 6, false},           {me, "ATREIDES COMBAT TANK", 10, 10, true},
                        {me, "ATREIDES COMBAT TANK", 12, 9, true}, {me, "TRIKE", 11, 12, false},
                        {me, "LIGHT INF", 7, 12, false},           {me, "LIGHT INF", 8, 12, false},
                        {me, "TROOPER", 9, 12, false},             {me, "QUAD", 17, 14, false},
                        {me, "TRIKE", 18, 8, false},
                        {foe, "HARKONNEN COMBAT TANK", 15, 11, false}, {foe, "HARKONNEN COMBAT TANK", 16, 9, false},
                        {foe, "QUAD", 14, 13, false},               {foe, "TROOPER", 13, 13, false},
                        {foe, "TROOPER", 14, 14, false},            {foe, "LIGHT INF", 12, 14, false}};
    int harvester = -1;
    for (const U& u : kUnits) {
        const int ty = unit(u.name);
        if (ty < 0) continue;
        const int i = w.addUnit(u.side, ty, u.x, u.y, u.x, u.y);
        if (i < 0) continue;
        if (u.selected) w.sides()[u.side].units[i].selected = true;
        if (std::string(u.name) == "HARVESTER") harvester = i;
    }
    orderBattle(w);
    if (harvester >= 0) w.orderMove(me, harvester, 21, 7);  // onto the spice: harvests
}

GraphicsPreviews::GraphicsPreviews(SDL_Renderer* r, render::TextRenderer* font)
    : r_(r), ui_(std::make_unique<Ui>(r)) {
    ui_->setFont(font);
}

GraphicsPreviews::~GraphicsPreviews() {
    if (dataLoad_.valid()) dataLoad_.wait();
    if (envLoad_.valid()) envLoad_.wait();
    session_.reset();
    for (auto& [k, e] : entries_)
        if (e.tex) SDL_DestroyTexture(e.tex);
    if (target_) SDL_DestroyTexture(target_);
}

void GraphicsPreviews::setData(const std::string& dataDir, const std::string& languageKey, const std::string& cacheDir,
                               const std::string& textLanguage) {
    if (dataDir == dataDir_ && languageKey == languageKey_ && cacheDir == cacheDir_ && textLanguage == textLanguage_) return;
    release();
    if (dataLoad_.valid()) dataLoad_.wait();
    dataLoad_ = {};
    for (auto& [k, e] : entries_)
        if (e.tex) SDL_DestroyTexture(e.tex);
    entries_.clear();
    jobs_.clear();
    dataDir_ = dataDir;
    languageKey_ = languageKey;
    cacheDir_ = cacheDir;
    textLanguage_ = textLanguage.empty() ? "en" : textLanguage;
    if (envLoad_.valid()) envLoad_.wait();
    envLoad_ = {};
    envHave_ = false;
    loadShippedIndex();
    if (!dataDir_.empty() && shippedOn_ && !shipped_.empty()) envReady(false);  // starts reading the Env
    dataFailed_ = false;
    prefetched_.clear();
    // The screenshots the game used to take in play (graphics-<preset>.png) are not used any more.
    std::error_code ec;
    if (!cacheDir_.empty())
        for (const char* old : {"graphics-original.png", "graphics-enhanced.png", "graphics-3d.png"}) fs::remove(fs::path(cacheDir_) / old, ec);
}

bool GraphicsPreviews::presetSettings(int preset, const Settings& current, Settings& out) {
    out = current;
    out.applyPreset(static_cast<Settings::GraphicsPreset>(std::clamp(preset, 0, 2)));
    if (preset != 2) return true;
    // 3D: the models of the chosen model pack, else the first installed one.
    out.models3d = Settings::Models3D::New;
    if (render3d::ModelRenderer::packPath(out.modelPack).empty()) out.modelPack.clear();
    return !render3d::ModelRenderer::packPath(out.modelPack).empty();
}

// Only what the picture of this mode depends on: Original ignores every Enhanced / 3D value, 2D
// ignores the 3D ones and 3D the 2D terrain (its fallback without SDL_gpu).
const std::string& GraphicsPreviews::cachedStamp(const std::string& what, bool model) const {
    if (const Uint64 now = SDL_GetTicksNS(); now - stampsAt_ > 2'000'000'000ull) {
        stamps_.clear();
        stampsAt_ = now;
    }
    const std::string id = (model ? "m:" : "a:") + what;
    auto it = stamps_.find(id);
    if (it == stamps_.end()) {
        std::string v;
        if (model) {
            const std::string pack = render3d::ModelRenderer::packPath(what);
            v = pack + ":" + stamp(fs::path(pack) / "models.ini") + ":" + stamp(fs::path(pack) / "models");
        } else {
            v = artPackStamp(what);
        }
        it = stamps_.emplace(id, std::move(v)).first;
    }
    return it->second;
}

std::string GraphicsPreviews::keyOf(const Settings& s, PreviewShot shot) const {
    std::string k = "v" + std::to_string(kSceneVersion) + "|" + dataDir_ + "|" + stamp(fs::path(dataDir_) / "maps" / kMap) +
                    "|" + languageKey_ + "|" + std::to_string(int(shot)) + "|" + Settings::graphicsName(s.graphics);
    if (!s.enhanced()) return k;
    auto add = [&](const std::string& v) { k += "|" + v; };
    add(cachedStamp(s.artPack.empty() ? "upscaled" : s.artPack, false));
    add(std::to_string(int(s.effects)) + std::to_string(int(s.shroud)) + std::to_string(int(s.healthBars)) +
        std::to_string(int(s.panelTransparency)) + std::to_string(int(s.timeOfDay)) + std::to_string(int(s.lighting)) +
        std::to_string(int(s.hdr)) + s.deviceProfile);
    if (s.threeD()) {
        add(std::to_string(int(s.terrain3d)) + std::to_string(int(s.shadows3d)) + "m" + std::to_string(s.msaa) + "a" +
            std::to_string(s.aniso));
        add(std::string(Settings::models3dName(s.models3d)) + ":" +
            (s.models3d == Settings::Models3D::Ps1 ? s.ps1Image
             : s.models3d == Settings::Models3D::New ? cachedStamp(s.modelPack, true) : std::string()));
    } else {
        add("t" + std::to_string(int(s.terrain2d)));
    }
    return k;
}

bool GraphicsPreviews::samePicture(const Settings& a, const Settings& b, PreviewShot shot) const {
    return keyOf(a, shot) == keyOf(b, shot);
}

std::string GraphicsPreviews::fileOf(const std::string& key) const {
    if (cacheDir_.empty()) return {};
    char name[40];
    std::snprintf(name, sizeof name, "preview-%016llx.png", static_cast<unsigned long long>(fnv1a(key)));
    return (fs::path(cacheDir_) / name).string();
}

GraphicsPreviews::Picture GraphicsPreviews::result(Entry& e) {
    e.used = SDL_GetTicksNS();
    Picture p;
    p.tex = e.tex;
    p.w = e.w;
    p.h = e.h;
    p.unavailable = e.failed || (dataFailed_ && !e.tex);
    p.pending = !e.tex && !p.unavailable;
    return p;
}

GraphicsPreviews::Picture GraphicsPreviews::preset(int preset, const Settings& current, bool prefetch) {
    Settings s;
    if (!presetSettings(preset, current, s)) {
        Picture p;
        p.unavailable = true;
        return p;
    }
    return picture(s, PreviewShot::Overview, prefetch);
}

GraphicsPreviews::Picture GraphicsPreviews::picture(const Settings& s, PreviewShot shot, bool prefetch) {
    if (dataDir_.empty()) {
        Picture p;
        p.unavailable = true;
        return p;
    }
    if (!prefetch) lastRequest_ = SDL_GetTicksNS();
    const std::string key = keyOf(s, shot);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        // A shipped picture? (Its key needs the Env - pack fingerprints, the map's hash - read on a
        // worker thread when the data folder is set: a moment of "pending" at most.)
        std::string shippedFile;
        if (shippedOn_ && !shipped_.empty()) {
            const std::string sk = shippedKeyOf(s, shot);
            if (sk.empty()) {
                Picture p;
                p.pending = !prefetch;
                return p;
            }
            if (auto f = shipped_.find(sk); f != shipped_.end()) shippedFile = f->second;
        }
        trimMemory();
        it = entries_.emplace(key, Entry{}).first;
        Entry& e = it->second;
        e.shipped = shippedFile;
        // The disk cache first (a PNG load takes a few milliseconds).
        if (const std::string f = fileOf(key); !f.empty() && e.shipped.empty()) {
            std::error_code ec;
            if (fs::exists(f, ec))
                if (SDL_Surface* img = SDL_LoadPNG(f.c_str())) {
                    e.tex = SDL_CreateTextureFromSurface(r_, img);
                    e.w = img->w;
                    e.h = img->h;
                    SDL_DestroySurface(img);
                    if (e.tex) SDL_SetTextureScaleMode(e.tex, SDL_SCALEMODE_LINEAR);
                }
        }
    }
    Entry& e = it->second;
    if (!e.tex && !e.shipped.empty()) {
        if (prefetch) return Picture{};  // there when asked for: nothing to render
        if (SDL_Surface* img = SDL_LoadPNG(e.shipped.c_str())) {
            e.tex = SDL_CreateTextureFromSurface(r_, img);
            e.w = img->w;
            e.h = img->h;
            SDL_DestroySurface(img);
            if (e.tex) SDL_SetTextureScaleMode(e.tex, SDL_SCALEMODE_LINEAR);
        }
        if (!e.tex) {
            SDL_Log("Graphics previews: %s: %s (rendering it instead)", e.shipped.c_str(), SDL_GetError());
            e.shipped.clear();
        }
    }
    if (!e.tex && !e.failed && !dataFailed_) {
        if (sync_) {
            Job job{key, s, shot};
            if (ensureSession(s, true)) {
                runJob(job, 1 << 20);
            } else {
                e.failed = true;
            }
        } else if (!e.queued) {
            e.queued = true;
            Job job{key, s, shot};
            job.prefetch = prefetch;
            job.asked = SDL_GetTicks();
            if (prefetch) {
                jobs_.push_back(std::move(job));
            } else {
                lastQueued_ = job.asked;
                jobs_.push_front(std::move(job));  // what is pointed at now comes first
            }
        } else if (!prefetch) {
            // Already queued (maybe as a prefetch): still wanted; to the front unless it is running.
            for (std::size_t i = 0; i < jobs_.size(); ++i)
                if (jobs_[i].key == key) {
                    jobs_[i].asked = SDL_GetTicks();
                    jobs_[i].prefetch = false;
                    if (i > 0 && jobs_[i].frame < 0) {
                        Job j = std::move(jobs_[i]);
                        jobs_.erase(jobs_.begin() + std::ptrdiff_t(i));
                        jobs_.push_front(std::move(j));
                    }
                    break;
                }
        }
    }
    return result(e);
}

bool GraphicsPreviews::ensureSession(const Settings& first, bool block) {
    if (session_) return true;
    if (dataFailed_) return false;
    if (!dataLoad_.valid()) {
        if (block) {
            std::promise<std::unique_ptr<GameData>> p;
            p.set_value(loadSceneData(dataDir_));
            dataLoad_ = p.get_future();
        } else {
            const std::string dir = dataDir_;
            dataLoad_ = std::async(std::launch::async, [dir] { return loadSceneData(dir); });
            return false;
        }
    }
    if (!block && dataLoad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    std::unique_ptr<GameData> g = dataLoad_.get();
    dataLoad_ = {};
    if (!g) {
        dataFailed_ = true;
        return false;
    }
    if (!target_) {
        target_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kW, kH);
        if (!target_) {
            SDL_Log("Graphics previews: no render target (%s)", SDL_GetError());
            dataFailed_ = true;
            return false;
        }
    }
    const Uint64 t0 = SDL_GetTicksNS();
    settings_ = first;
    SessionConfig cfg;
    cfg.presetPreview = true;
    cfg.outputW = kW;
    cfg.outputH = kH;
    cfg.ticks = kSceneTicks;
    cfg.asyncModels = !sync_;  // the menu keeps running while 3D models load
    try {
        session_ = std::make_unique<Session>(r_, std::move(g), settings_, nullptr, nullptr, cfg);
    } catch (const std::exception& e) {
        SDL_Log("Graphics previews: %s", e.what());
        dataFailed_ = true;
        return false;
    }
    SDL_Log("Graphics previews: preview game ready in %.0f ms", double(SDL_GetTicksNS() - t0) / 1e6);
    return true;
}

// One frame of the preview game into the target; the App's frame (target, viewport, clip, the EDR
// canvas as the "screen") is restored afterwards.
void GraphicsPreviews::renderFrame() {
    SDL_Texture* prevTarget = SDL_GetRenderTarget(r_);
    SDL_Texture* prevScreen = render::screenTarget();
    SDL_Rect vp{}, clip{};
    const bool hadVp = SDL_RenderViewportSet(r_), hadClip = SDL_RenderClipEnabled(r_);
    SDL_GetRenderViewport(r_, &vp);
    SDL_GetRenderClipRect(r_, &clip);
    render::setScreenTarget(target_);
    SDL_SetRenderTarget(r_, target_);
    SDL_SetRenderViewport(r_, nullptr);
    SDL_SetRenderClipRect(r_, nullptr);
    ui_->beginFrame();
    session_->render(*ui_);
    ui_->endFrame();
    render::setScreenTarget(prevScreen);
    SDL_SetRenderTarget(r_, prevTarget);
    SDL_SetRenderViewport(r_, hadVp ? &vp : nullptr);
    SDL_SetRenderClipRect(r_, hadClip ? &clip : nullptr);
}

void GraphicsPreviews::runJob(Job& job, int frames) {
    if (job.frame < 0) {  // start: the job's settings, the scene afresh, its framing
        settings_ = job.s;
        settings_.touchLayout = Settings::TouchLayout::Tablet;  // the desktop panel at any size
        settings_.classicView = Settings::ClassicView::Off;
        settings_.resolution = "auto";
        settings_.uiScale = "auto";
        settings_.fullscreen = false;
        session_->restart();
        const Framing f = framingOf(job.shot);
        renderFrame();  // applies the settings (mode / art / terrain may rebuild here)
        session_->previewCamera(f.x, f.y, f.zoom, f.tilt, f.yaw);
        job.frame = 0;
    }
    for (int n = 0; n < frames && job.frame < kWarmupFrames; ++n, ++job.frame) {
        if (session_->modelsLoading()) {  // 3D models still reading in the background: wait (no ticks)
            renderFrame();                // (polls the load)
            return;
        }
        session_->previewTick();
        renderFrame();
    }
    if (job.frame >= kWarmupFrames) finish(job);
}

void GraphicsPreviews::finish(Job& job) {
    auto it = entries_.find(job.key);
    if (it == entries_.end()) it = entries_.emplace(job.key, Entry{}).first;
    Entry& e = it->second;
    e.queued = false;
    SDL_Texture* prev = SDL_GetRenderTarget(r_);
    SDL_SetRenderTarget(r_, target_);
    // Close-ups (the Custom comparisons) show the battlefield alone, the overview the whole frame.
    SDL_Rect area{0, 0, kW, kH};
    if (job.shot != PreviewShot::Overview && session_) {
        SDL_Rect bf = session_->battlefieldRect();
        if (SDL_GetRectIntersection(&bf, &area, &bf) && bf.w > 64 && bf.h > 64) area = bf;
    }
    auto readBack = [&]() -> SDL_Surface* {
        if (capture_) SDL_FlushRenderer(r_);
        SDL_Surface* s = SDL_RenderReadPixels(r_, &area);
        if (s)
            if (SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888)) {
                SDL_DestroySurface(s);
                s = c;
            }
        return s;
    };
    SDL_Surface* s = readBack();
    if (s && job.shot != PreviewShot::Overview) {
        // Close-ups: no empty black band at the right (3D draws its battlefield narrower than the frame).
        const auto* px = static_cast<const Uint32*>(s->pixels);
        const int pitch = s->pitch / 4;
        int right = s->w;
        while (right > s->w / 2) {
            bool empty = true;
            for (int y = 0; y < s->h && empty; y += 2) empty = (px[y * pitch + right - 1] & 0x00FFFFFFu) == 0;
            if (!empty) break;
            --right;
        }
        if (right < s->w - 8) {
            area.w = right;
            SDL_DestroySurface(s);
            s = readBack();
        }
    }
    if (s && capture_ && watermark_) {  // --render-previews: the watermark on pictures of the original art
        SDL_DestroySurface(s);
        drawWatermark(r_, area);
        s = readBack();
    }
    SDL_SetRenderTarget(r_, prev);
    if (!s) {
        SDL_Log("Graphics previews: read back failed (%s)", SDL_GetError());
        e.failed = true;
        return;
    }
    if (capture_) {  // --render-previews: the picture goes to the caller
        *capture_ = s;
        return;
    }
    if (e.tex) SDL_DestroyTexture(e.tex);
    e.tex = SDL_CreateTextureFromSurface(r_, s);
    e.w = s->w;
    e.h = s->h;
    e.used = SDL_GetTicksNS();
    if (e.tex) SDL_SetTextureScaleMode(e.tex, SDL_SCALEMODE_LINEAR);
    if (const std::string f = fileOf(job.key); !f.empty()) {
        std::error_code ec;
        fs::create_directories(cacheDir_, ec);
        if (!SDL_SavePNG(s, f.c_str())) SDL_Log("Graphics previews: %s: %s", f.c_str(), SDL_GetError());
        trimDisk();
    }
    SDL_DestroySurface(s);
    trimMemory();
}

void GraphicsPreviews::work() {
    if (sync_) return;
    // Each picture costs the menu a frame of up to ~1 s (the preview game on the main thread), so:
    // pictures nobody asked for in 1.5 s (values clicked past) are dropped, nothing starts while the
    // player is still clicking, and the prefetched comparisons wait until the page is idle.
    const Uint64 now = SDL_GetTicks();
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->frame < 0 && !it->prefetch && now - it->asked > 1500) {
            if (auto e = entries_.find(it->key); e != entries_.end()) e->second.queued = false;
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
    // The heavy steps (making the preview game, a scene's start and first frame: up to ~0.8 s each) wait
    // until the pointer has rested 400 ms with no button down, so the short stall falls while the player
    // reads the page rather than while the cursor moves.
    float mx = 0, my = 0;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);
    if (mx != lastMx_ || my != lastMy_) {
        lastMx_ = mx;
        lastMy_ = my;
        lastMove_ = now;
    }
    if (!jobs_.empty() && jobs_.front().frame <= 0 && (now - lastMove_ < 400 || buttons != 0)) return;
    if (!jobs_.empty() && jobs_.front().frame < 0) {
        if (now - lastQueued_ < 350) return;
        if (jobs_.front().prefetch && now - lastQueued_ < 1500) return;
    }
    if (jobs_.empty()) {
        // Nothing asked for a while (the page closed): the preview game's memory goes back.
        if (session_ && SDL_GetTicksNS() - lastRequest_ > 8'000'000'000ull) release();
        return;
    }
    const bool hadSession = session_ != nullptr;
    if (!ensureSession(jobs_.front().s, false)) {
        if (dataFailed_) {
            for (Job& j : jobs_)
                if (auto it = entries_.find(j.key); it != entries_.end()) it->second.queued = false;
            jobs_.clear();
        }
        return;
    }
    if (!hadSession) {  // the preview game was just made: its first scene in the next menu frame (no long hitch)
        if (const Uint64 t = SDL_GetTicks() - now; t > 100) SDL_Log("Graphics previews: slow step %llu ms (preview game)", (unsigned long long)t);
        return;
    }
    Job& job = jobs_.front();
    const std::string key = job.key;
    const int startFrame = job.frame;
    runJob(job, kFramesPerWork);
    if (const Uint64 t = SDL_GetTicks() - now; t > 100)
        SDL_Log("Graphics previews: slow step %llu ms (%s, frame %d)", (unsigned long long)t, startFrame < 0 ? "scene start" : "warm-up",
                startFrame);
    if (!jobs_.empty() && jobs_.front().key == key && job.frame >= kWarmupFrames) jobs_.pop_front();
}

void GraphicsPreviews::release() {
    if (!session_) return;
    session_.reset();
    for (Job& j : jobs_) j.frame = -1;
    SDL_Log("Graphics previews: preview game released");
}

void GraphicsPreviews::trimMemory() {
    // The least recently shown pictures go (they come back from the disk cache when asked for).
    std::size_t n = 0;
    for (auto& [k, e] : entries_) n += e.tex != nullptr;
    while (n > kMaxTextures) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.tex && !it->second.queued && (oldest == entries_.end() || it->second.used < oldest->second.used)) oldest = it;
        if (oldest == entries_.end()) break;
        SDL_DestroyTexture(oldest->second.tex);
        entries_.erase(oldest);
        --n;
    }
}

void GraphicsPreviews::trimDisk() {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (const auto& f : fs::directory_iterator(cacheDir_, ec))
        if (f.path().filename().string().rfind("preview-", 0) == 0) files.emplace_back(f.last_write_time(ec), f.path());
    if (files.size() <= kMaxFiles) return;
    std::sort(files.begin(), files.end());
    for (std::size_t i = 0; i + kMaxFiles < files.size(); ++i) fs::remove(files[i].second, ec);
}

void GraphicsPreviews::prefetch(const Settings& current) {
    if (sync_ || dataDir_.empty() || dataFailed_) return;
    if (shippedOn_ && !shipped_.empty() && !envReady(false)) return;  // (the shipped keys need the Env)
    for (int i = 0; i < 3; ++i)
        if (preset(i, current, true).pending) return;  // the presets first (after the one shown)
    const std::string id = keyOf(current, PreviewShot::Overview);
    if (id == prefetched_) return;
    prefetched_ = id;
    using P = PictureOption;
    std::vector<P> rows;
    if (current.threeD()) rows = {P::Terrain3D, P::Effects, P::TimeOfDay, P::Lighting, P::Shadows, P::Models3D, P::HealthBars};
    else if (current.enhanced()) rows = {P::Terrain2D, P::Art, P::Effects, P::TimeOfDay, P::Lighting, P::Shroud, P::HealthBars};
    else rows = {P::Mode};
    for (P o : rows) {
        const PreviewShot shot = previewShotFor(o);
        Settings next = current;
        stepPictureOption(next, o, 1);
        picture(current, shot, true);
        if (!samePicture(current, next, shot)) picture(next, shot, true);
    }
}

// ---- The shipped set (assets/previews; the top of preset_previews.hpp) ------------------------

namespace {

constexpr int kIndexFormat = 1;
constexpr const char* kWatermark = "Dune 2000 \xC2\xA9 Westwood Studios / EA";

std::string hex64(std::uint64_t h) {
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

std::string readFile(const std::string& path) {
    std::size_t n = 0;
    void* p = SDL_LoadFile(path.c_str(), &n);  // SDL_IOFromFile: also an Android APK asset
    if (!p) return {};
    std::string s(static_cast<const char*>(p), n);
    SDL_free(p);
    return s;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += char(c);
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o;
}

// The small watermark on pictures that show the original art: bottom right of `area`, light text
// with a soft dark edge, semi-transparent.
void drawWatermark(SDL_Renderer* r, const SDL_Rect& area) {
    static std::unique_ptr<render::TtfFont> font;
    static SDL_Renderer* fontFor = nullptr;
    if (!font || fontFor != r) {
        std::size_t n = 0;
        const unsigned char* d = render::embeddedFont("play_regular", n);
        if (!d) return;
        font = std::make_unique<render::TtfFont>(r, d, n);
        fontFor = r;
    }
    const float px = 12.0f;
    const float w = font->width(kWatermark, px), h = font->lineHeight(px);
    const float x = float(area.x + area.w) - w - 6, y = float(area.y + area.h) - h - 4;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 90);
    const SDL_FRect bg{x - 4, y - 1, w + 8, h + 2};
    SDL_RenderFillRect(r, &bg);
    font->draw(kWatermark, x + 1, y + 1, px, Colour{0, 0, 0, 150});
    font->draw(kWatermark, x, y, px, Colour{235, 225, 205, 190});
}

const char* shotName(PreviewShot s) {
    switch (s) {
    case PreviewShot::Base: return "base";
    case PreviewShot::Battle: return "battle";
    case PreviewShot::Terrain: return "terrain";
    case PreviewShot::Overview: break;
    }
    return "overview";
}

}  // namespace

std::string GraphicsPreviews::shippedDir() {
    auto ok = [](const fs::path& p) {
        std::error_code ec;
        return fs::is_regular_file(p / "index.json", ec);
    };
    if (const char* env = std::getenv("D2K_SHIPPED_PREVIEWS"); env && *env)  // tests: "-" = none
        return std::string(env) == "-" ? std::string() : std::string(env);
    if (const char* base = SDL_GetBasePath()) {
        fs::path p = fs::path(base).lexically_normal();
        if (p.filename().empty()) p = p.parent_path();
        for (int up = 0; up < 6 && !p.empty(); ++up) {
            if (ok(p / "assets" / "previews")) return (p / "assets" / "previews").string();
            if (p == p.parent_path()) break;
            p = p.parent_path();
        }
    }
    std::error_code ec;
    if (const fs::path c = fs::current_path(ec) / "assets" / "previews"; ok(c)) return c.string();
#ifdef SDL_PLATFORM_ANDROID
    // The APK's assets/ (android/app/build.gradle copies assets/previews there): relative paths
    // open through SDL_IOFromFile.
    if (SDL_IOStream* io = SDL_IOFromFile("previews/index.json", "rb")) {
        SDL_CloseIO(io);
        return "previews";
    }
#endif
    return {};
}

std::string GraphicsPreviews::packFingerprint(const std::string& packDir) {
    std::error_code ec;
    const fs::path root(packDir);
    std::vector<std::pair<std::string, std::uintmax_t>> files;
    for (const char* sub : {"DATA", "models", "variants"}) {
        if (!fs::is_directory(root / sub, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(root / sub, fs::directory_options::follow_directory_symlink, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string name = it->path().filename().string();
            if (name == ".DS_Store") continue;
            files.emplace_back(it->path().lexically_relative(root).generic_string(), it->file_size(ec));
        }
    }
    std::sort(files.begin(), files.end());
    std::string all;
    all.reserve(files.size() * 40);
    for (const char* ini : {"pack.ini", "models.ini"}) {
        all += ini;
        all += '\n';
        all += readFile((root / ini).string());
        all += '\n';
    }
    for (const auto& [n, sz] : files) all += n + '\t' + std::to_string(sz) + '\n';
    return hex64(fnv1a(all));
}

bool GraphicsPreviews::showsOriginalArt(const Settings& s) {
    if (!s.enhanced()) return true;  // Graphics: Original
    if (s.threeD()) {
        // 3D draws objects from the 3D art pack (never the original sprites): only the original tiles
        // terrain and the PS1 disc's models are original art.
        return s.terrain3d == Settings::Terrain3D::Tiles || s.models3d == Settings::Models3D::Ps1;
    }
    if (s.terrain2d == Settings::Terrain2D::Original || s.terrain2d == Settings::Terrain2D::Upscaled) return true;
    return !render::ArtPack::isRedesign(s.artPack);  // the original / upscaled sprites
}

void GraphicsPreviews::loadShippedIndex() {
    shipped_.clear();
    if (!shippedOn_) return;
    const std::string dir = shippedDir();
    if (dir.empty()) return;
    const std::string text = readFile(dir + "/index.json");
    formats::json::Value v;
    std::string err;
    if (text.empty() || !formats::json::parse(text, v, err)) {
        SDL_Log("Graphics previews: %s/index.json: %s", dir.c_str(), text.empty() ? "unreadable" : err.c_str());
        return;
    }
    auto num = [&](const char* k) {
        const auto* f = formats::json::field(v, k);
        return f && f->kind == formats::json::Value::Number ? int(f->num) : -1;
    };
    if (num("format") != kIndexFormat || num("scene_version") != kSceneVersion) {
        SDL_Log("Graphics previews: the shipped set is for another engine (scene version %d, this one %d): rendering locally",
                num("scene_version"), kSceneVersion);
        return;
    }
    if (const auto* pics = formats::json::field(v, "pictures"); pics && pics->kind == formats::json::Value::Array)
        for (const auto& p : pics->arr) {
            const std::string key = formats::json::str(p, "key"), file = formats::json::str(p, "file");
            if (!key.empty() && !file.empty() && file.find('/') == std::string::npos) shipped_[key] = dir + "/" + file;
        }
    SDL_Log("Graphics previews: %zu shipped pictures (engine %s, %s)", shipped_.size(), formats::json::str(v, "engine_version").c_str(),
            dir.c_str());
}

bool GraphicsPreviews::envReady(bool block) {
    const int gen = render::ArtPack::generation();
    if (envHave_ && env_.generation == gen) return true;
    if (!envLoad_.valid()) {
        const std::string dataDir = dataDir_;
        envLoad_ = std::async(std::launch::async, [dataDir, gen] {
            Env e;
            e.generation = gen;
            // The scene's map and its mission file (tileset, houses), found as loadGameData finds them.
            const std::string maps = dataDir + "/maps", missions = dataDir + "/Missions";
            std::string map = formats::findFileNoCase(maps, kMap);
            if (map.empty()) map = formats::findFileNoCase(missions, kMap);
            const std::string mis = formats::findFileNoCase(maps, kMis);
            e.mapHash = map.empty() ? "none" : hex64(fnv1a(readFile(map) + "|" + (mis.empty() ? std::string() : readFile(mis))));
            // Every installed pack, from the folder the game uses for that name (the first search dir).
            for (const auto& d : render::ArtPack::searchDirs()) {
                std::error_code ec;
                for (const auto& f : fs::directory_iterator(d, ec)) {
                    const std::string name = f.path().filename().string();
                    if (!f.is_directory(ec) || e.packs.count(name)) continue;
                    if (!fs::exists(f.path() / "pack.ini", ec) && !fs::is_directory(f.path() / "models", ec)) continue;
                    e.packs[name] = packFingerprint(f.path().string());
                }
            }
            e.terrainAssets = !render3d::DesignedTerrain::assetsDir().empty();
            return e;
        });
    }
    if (!block && envLoad_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    env_ = envLoad_.get();
    envLoad_ = {};
    envHave_ = true;
    return env_.generation == gen || envReady(block);
}

std::string GraphicsPreviews::shippedKeyOf(const Settings& s, PreviewShot shot) {
    if (!envReady(sync_)) return {};
    std::string k = "s" + std::to_string(kSceneVersion) + "|map:" + env_.mapHash + "|" + shotName(shot) +  // any text language: the shipped set is English (owner 2026-11-13)
                   
                    "|" + Settings::graphicsName(s.graphics);
    if (!s.enhanced()) return k;
    auto add = [&](const std::string& v) { k += "|" + v; };
    auto pack = [&](const std::string& name) {
        auto it = env_.packs.find(name);
        return "pack:" + name + "@" + (it == env_.packs.end() ? std::string("missing") : it->second);
    };
    const std::string art = s.artPack.empty() ? "upscaled" : s.artPack;
    add(art == "original" || art == "upscaled" ? art : pack(art));
    add("fx" + std::to_string(int(s.effects)) + "sh" + std::to_string(int(s.shroud)) + "hb" + std::to_string(int(s.healthBars)) + "pt" +
        std::to_string(int(s.panelTransparency)) + "tod" + std::to_string(int(s.timeOfDay)) + "li" + std::to_string(int(s.lighting)) + "hdr" +
        std::to_string(int(s.hdr)) + (s.deviceProfile == "lite" ? "lite" : ""));
    const std::string assets = env_.terrainAssets ? "" : "-noassets";
    if (s.threeD()) {
        add("t3d" + std::to_string(int(s.terrain3d)) + (s.terrain3d != Settings::Terrain3D::Tiles ? assets : "") + "sh" +
            std::to_string(int(s.shadows3d)) + "m" + std::to_string(s.msaa) + "a" + std::to_string(s.aniso));
        if (s.models3d == Settings::Models3D::Ps1) {
            std::error_code ec;
            add(std::string("ps1:") + (!s.ps1Image.empty() && fs::exists(s.ps1Image, ec) ? "disc" : "none"));
        } else {
            const std::string dir = render3d::ModelRenderer::packPath(s.modelPack);
            const std::string name = dir.empty() ? std::string() : fs::path(dir).filename().string();
            add(dir.empty() ? std::string("models:none")
                : s.modelPack == render3d::ModelRenderer::kBuiltInPack ? std::string("models:builtin")
                                                                        : "models:" + pack(name));
        }
    } else {
        const bool designed = s.terrain2d == Settings::Terrain2D::Designed || s.terrain2d == Settings::Terrain2D::Faithful;
        add("t2d" + std::to_string(int(s.terrain2d)) + (designed ? assets : ""));
    }
    return k;
}

int GraphicsPreviews::renderShippedSet(const std::string& outDir, const Settings& current) {
    if (dataDir_.empty()) {
        SDL_Log("--render-previews: no game data");
        return 1;
    }
    shippedOn_ = false;  // render everything (never load the old set)
    shipped_.clear();
    cacheDir_.clear();
    sync_ = true;
    const std::string savedLang = textLanguage_;
    envHave_ = false;
    if (!envReady(true)) return 1;
    std::error_code ec;
    fs::create_directories(outDir, ec);

    // What the menus can show for the default setups: each preset's overview (Graphics tabs, welcome
    // cards, the Custom tab's "Your settings"), and for every picture row of the Custom tab every
    // value it can take from that setup, in the row's framing (now vs. pointed at: both are in the set).
    struct Pic {
        Settings s;
        PreviewShot shot;
        std::string label;
    };
    std::vector<Pic> pics;
    std::set<std::string> seen;
    auto want = [&](const Settings& s, PreviewShot shot, const std::string& label) {
        if (seen.insert(shippedKeyOf(s, shot)).second) pics.push_back({s, shot, label});
    };
    // The setups: a new player starts on Enhanced (the installed redesign pack); the Original / 3D tabs
    // apply their preset on top of it (an Original preset keeps the Enhanced values it does not set).
    // Plus Enhanced without the art pack (the welcome page and the Graphics page before the download:
    // the original sprites).
    using P = PictureOption;
    struct Base {
        std::string name;
        Settings s;
        bool modeRow = true;
    };
    std::vector<Base> bases;
    Settings enhanced;
    presetSettings(1, current, enhanced);
    for (int preset = 0; preset < 3; ++preset) {
        static const char* const kPresetNames[] = {"Original", "Enhanced", "3D"};
        Settings s;
        if (!presetSettings(preset, enhanced, s)) {
            SDL_Log("--render-previews: the %s preset cannot be previewed here (no 3D art pack?)", kPresetNames[preset]);
            continue;
        }
        bases.push_back({kPresetNames[preset], s});
    }
    if (render::ArtPack::isRedesign(enhanced.artPack)) {
        Settings s = enhanced;
        s.artPack = "original";
        bases.push_back({"Enhanced without the art pack", s, false});
    }
    for (const Base& b : bases) {
        const Settings& base = b.s;
        const char* const name = b.name.c_str();
        want(base, PreviewShot::Overview, name);
        for (int o = int(P::Mode); o <= int(P::Models3D); ++o) {
            const P opt = static_cast<P>(o);
            if (opt == P::Mode && !b.modeRow) continue;
            const PreviewShot shot = previewShotFor(opt);
            want(base, shot, std::string(name) + " (" + shotName(shot) + ")");
            Settings s = base;
            const std::string start = shippedKeyOf(base, shot);
            for (int step = 0; step < 8; ++step) {
                if (!stepPictureOption(s, opt, 1)) break;
                if (pictureOptionValue(s, opt) == pictureOptionValue(base, opt)) break;  // back at the start
                want(s, shot, std::string(name) + ": " + pictureOptionLabel(opt) + " = " + pictureOptionValue(s, opt));
            }
            (void)start;
        }
    }
    SDL_Log("--render-previews: %zu pictures", pics.size());

    std::ostringstream idx;
    std::map<std::string, std::string> fps;
    for (const auto& [n, fp] : env_.packs) fps[n] = fp;
    idx << "{\n  \"format\": " << kIndexFormat << ",\n  \"scene_version\": " << kSceneVersion << ",\n  \"engine_version\": \""
        << jsonEscape(appVersion()) << "\",\n  \"language\": \"" << jsonEscape(textLanguage_) << "\",\n  \"map_hash\": \"" << env_.mapHash
        << "\",\n  \"terrain_assets\": " << (env_.terrainAssets ? "true" : "false") << ",\n  \"packs\": {";
    {
        bool first = true;
        for (const auto& [n, fp] : fps) {
            std::string version;
            for (const auto& d : render::ArtPack::searchDirs()) {
                std::ifstream in(fs::path(d) / n / "pack.ini");
                for (std::string line; std::getline(in, line);)
                    if (line.rfind("version=", 0) == 0) version = line.substr(8);
                if (in.is_open() || !version.empty()) break;
            }
            idx << (first ? "\n" : ",\n") << "    \"" << jsonEscape(n) << "\": {\"fingerprint\": \"" << fp << "\", \"version\": \"" << jsonEscape(version)
                << "\"}";
            first = false;
        }
    }
    idx << "\n  },\n  \"pictures\": [";
    std::set<std::string> written;
    int failures = 0, marked = 0;
    std::uintmax_t bytes = 0;
    for (std::size_t i = 0; i < pics.size(); ++i) {
        const Pic& p = pics[i];
        const std::string key = shippedKeyOf(p.s, p.shot);
        const bool wm = showsOriginalArt(p.s);
        const std::string file = "preview-" + hex64(fnv1a(key)) + ".png";
        SDL_Surface* surf = nullptr;
        capture_ = &surf;
        watermark_ = wm;
        if (ensureSession(p.s, true)) {
            // Frame by frame with a present in between: the 3D passes (SDL_gpu) and the renderer's queue
            // are submitted as in the menu's frames (without it, some 3D pictures came out black).
            Job job{keyOf(p.s, p.shot), p.s, p.shot};
            for (int guard = 0; !surf && guard < 4000; ++guard) {
                runJob(job, 1);
                SDL_SetRenderTarget(r_, nullptr);
                SDL_RenderPresent(r_);
            }
        }
        capture_ = nullptr;
        if (!surf) {
            SDL_Log("--render-previews: FAILED %s", p.label.c_str());
            ++failures;
            continue;
        }
        const std::string path = (fs::path(outDir) / file).string();
        if (!SDL_SavePNG(surf, path.c_str())) {
            SDL_Log("--render-previews: %s: %s", path.c_str(), SDL_GetError());
            ++failures;
        }
        idx << (written.empty() ? "\n" : ",\n") << "    {\"file\": \"" << file << "\", \"watermark\": " << (wm ? "true" : "false") << ", \"shot\": \""
            << shotName(p.shot) << "\", \"w\": " << surf->w << ", \"h\": " << surf->h << ", \"label\": \"" << jsonEscape(p.label)
            << "\",\n     \"key\": \"" << jsonEscape(key) << "\"}";
        SDL_DestroySurface(surf);
        written.insert(file);
        marked += wm;
        bytes += fs::file_size(path, ec);
        SDL_Log("--render-previews: [%zu/%zu] %s%s", i + 1, pics.size(), p.label.c_str(), wm ? " (watermark)" : "");
    }
    idx << "\n  ]\n}\n";
    // Pictures of an older set that this one does not have.
    for (const auto& f : fs::directory_iterator(outDir, ec)) {
        const std::string n = f.path().filename().string();
        if (n.rfind("preview-", 0) == 0 && f.path().extension() == ".png" && !written.count(n)) fs::remove(f.path(), ec);
    }
    std::ofstream(fs::path(outDir) / "index.json", std::ios::binary) << idx.str();
    textLanguage_ = savedLang;
    SDL_Log("--render-previews: %zu pictures (%d with the watermark), %.1f MB, %d failed -> %s", written.size(), marked,
            double(bytes) / 1048576.0, failures, outDir.c_str());
    return failures == 0 ? 0 : 1;
}

void drawPreviewCard(Ui& ui, const SDL_FRect& r, const GraphicsPreviews::Picture& p, const char* badge, const char* note,
                     float ts) {
    ui.fill(r, Colour{12, 8, 5, 255});
    if (p.tex) {
        const float k = std::min(r.w / float(std::max(1, p.w)), r.h / float(std::max(1, p.h)));
        const float iw = p.w * k, ih = p.h * k;
        ui.image(p.tex, SDL_FRect{r.x + (r.w - iw) / 2, r.y + (r.h - ih) / 2, iw, ih});
    } else {
        ui.gradient(r, Colour{58, 42, 26, 255}, Colour{22, 15, 9, 255});
        if (badge && *badge) {
            Ui::BoldScope bold(ui);
            const float bs = ts * 2.6f;
            ui.text(badge, r.x + r.w / 2, r.y + r.h * 0.40f - ui.textHeight(bs) / 2, Colour{150, 112, 70, 255}, bs, Align::Centre);
        }
        std::string line;
        if (p.pending) {
            // A slow pulse of dots: the menu keeps running while the picture is rendered.
            const int dots = int(SDL_GetTicks() / 400 % 4);
            line = std::string(render::tr("Rendering preview")) + std::string(std::size_t(dots), '.');
        } else {
            line = note ? note : render::tr("Preview unavailable");
        }
        const float pad = r.w * 0.05f;
        ui.wrapped(line, SDL_FRect{r.x + pad, r.y + r.h * 0.66f, r.w - 2 * pad, r.h * 0.32f}, theme::kDim, ts);
    }
    ui.outline(r, theme::kEdge);
}

void customPreviewColumn(Ui& ui, GraphicsPreviews& previews, const Settings& s, const PictureProbe& probe, const SDL_FRect& area,
                         float ts) {
    using render::tr;
    const float lineH = ui.textHeight(ts) + 4;
    const float cardW = area.w, cardH = std::min(cardW * 10 / 16, (area.h - 3 * lineH - 6) / 2);
    const float cw = cardH * 16 / 10, cx = area.x + (area.w - cw) / 2;
    float y = area.y;
    auto caption = [&](const std::string& t, Colour c) {
        ui.text(ui.ellipsize(t, area.w, ts), area.x, y, c, ts);
        y += lineH;
    };
    if (probe.option == PictureOption::None) {
        caption(tr("Your settings"), theme::kAccent);
        drawPreviewCard(ui, SDL_FRect{cx, y, cw, cardH}, previews.picture(s, PreviewShot::Overview), "", nullptr, ts);
        y += cardH + 6;
        ui.wrapped(tr("Point at an option to compare its values here."), SDL_FRect{area.x, y, area.w, area.h - (y - area.y)},
                   theme::kDim, ts);
        return;
    }
    const PreviewShot shot = previewShotFor(probe.option);
    Settings next = s;
    stepPictureOption(next, probe.option, probe.dir);
    caption(tr(pictureOptionLabel(probe.option)), theme::kAccent);
    caption(std::string(tr("Now")) + ": " + pictureOptionValue(s, probe.option), theme::kText);
    drawPreviewCard(ui, SDL_FRect{cx, y, cw, cardH}, previews.picture(s, shot), "", nullptr, ts);
    y += cardH + 4;
    if (previews.samePicture(s, next, shot)) {
        ui.wrapped(tr("No visible change with the current graphics mode."), SDL_FRect{area.x, y, area.w, area.h - (y - area.y)},
                   theme::kDim, ts);
        return;
    }
    caption(std::string(probe.dir < 0 ? "< " : "> ") + pictureOptionValue(next, probe.option), theme::kTitle);
    drawPreviewCard(ui, SDL_FRect{cx, y, cw, cardH}, previews.picture(next, shot), "", nullptr, ts);
}

}  // namespace d2k::platform
