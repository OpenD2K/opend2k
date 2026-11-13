#pragma once
// The "3D" graphics mode, stage 1: a 2.5D battlefield (docs/3d-mode.md).
//
// Terrain: a lit heightfield mesh (render3d/terrain.hpp) textured top-down with the ORIGINAL
// tiles (the session's TileAtlas, i.e. the upscaled tiles or an art pack's, copied into a
// mip-mapped texture array; a per-cell tile-index texture follows spice / concrete / damage
// changes). Objects: the existing DATA.R16 sprite frames (SpriteCache, house colours, HQ / art
// pack) as camera-facing billboards standing on the terrain (units, turrets, flyers, bullets,
// explosions; the facing frame is chosen relative to the camera yaw) and ground-aligned decals
// (buildings on their footprint, crates, craters, unit shadows, flyer / projectile silhouettes,
// selection rings). Shroud: the soft shroud mask darkens terrain and objects.
//
// Rendering: SDL_gpu on the GPU renderer's own device (render3d/gpu.hpp), into an offscreen
// texture (4x MSAA where supported) that is drawn into the session's view with SDL_RenderTexture;
// the Enhanced UI is drawn on top by the session as usual. Render-only: the World is read, never
// changed (the session applies / restores the motion interpolation around render()).

#include "formats/templates.hpp"
#include "render/effect_anchors.hpp"
#include "render3d/camera3d.hpp"
#include "render3d/lights.hpp"
#include "render3d/terrain.hpp"
#include "render3d/terrain_v2.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace d2k::game {
class World;
struct Unit;
struct Building;
}  // namespace d2k::game

namespace d2k::render {
class SpriteCache;
class TileAtlas;
class SoftShroud;
class Effects;
class SceneLights;
class Bloom;
class Hdr;
struct SceneLight;
struct Sprite;
}  // namespace d2k::render

namespace d2k::render3d {

// Hook for real 3D models (src/render3d/models*, another module): when set, objects whose type
// has a model are left to it (Renderer3D skips their billboards / shadows) and, once the hook
// says models are in use, the camera may turn (yaw). Without it every object is a sprite and the
// camera is locked to the original north-up view (the pre-rendered sprites show one view only).
// What the model pass gets each frame (render3d/models.*): the world, the terrain, the camera,
// lighting / fog / shroud as the terrain uses them, and the render targets' formats.
struct ModelFrame {
    const game::World* world = nullptr;
    const Terrain* terrain = nullptr;
    const Camera3D* camera = nullptr;
    Mat4 viewProj;
    Vec3 eye, sun;
    float ambient = 0.62f, diffuse = 0.55f;
    float fogColor[3] = {}, fogStart = 0, fogEnd = 0;
    SDL_GPUTexture* shroud = nullptr;  // soft shroud mask (alpha = darkness), or a clear 1x1
    SDL_GPUSampler* linearClamp = nullptr;
    bool shroudOn = true;
    float mapW = 0, mapH = 0;           // world pixels
    int playerSide = 0;
    SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_INVALID, depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSampleCount samples = SDL_GPU_SAMPLECOUNT_1;
    int aniso = 1;  // anisotropic filtering of the model textures (1 = off)
    // Sun shadow map (docs/3d-mode.md "Sun shadows"): always bound (a depth texture); shadow[0] says
    // whether it is in use this frame; shadow = (on, texel size in uv, diffuse kept in full
    // shadow, depth bias). Depth pass format: shadowFormat (no colour target, 1 sample).
    Mat4 lightViewProj;
    SDL_GPUTexture* shadowMap = nullptr;
    SDL_GPUSampler* shadowSampler = nullptr;
    SDL_GPUTextureFormat shadowFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    float shadow[4] = {};
    // Dynamic point lights (render3d/lights.hpp, fragment uniform buffer 1 of model_fs); nullptr = none.
    const PointLightsU* lights = nullptr;
    // Emission of the models' glowing surfaces (docs/3d-models.md "Emission"): the time of day's lamp
    // strength (render::SceneLight::lamps; 0 = none, Day + Classic) and the effect clock (s) for pulses.
    float lamps = 0;
    double time = 0;
};

class ModelHook {
public:
    virtual ~ModelHook() = default;
    virtual bool active() const = 0;                  // models are drawn (Settings: not "Sprites")
    virtual bool hasUnitModel(int unitType) const = 0;
    virtual bool hasBuildingModel(int buildingType) const = 0;
    // Height of the model's top above its ground point (world px), < 0 = no model: overlays
    // (health bars) go above it.
    virtual float unitModelHeight(int /*unitType*/) const { return -1; }
    virtual float buildingModelHeight(int /*buildingType*/) const { return -1; }
    // Extra ground height at world point (wx, wy) from the deck of a building model standing there
    // (a refinery's dock pad, a repair pad): units on it stand on top of it, not inside. 0 = none.
    virtual float groundOffset(const game::World& /*w*/, float /*wx*/, float /*wy*/) const { return 0; }
    // Extra pull towards the eye (world px) for a unit billboard at (wx, wy) on a walkable cell of
    // a building model (its front row): drawn over that model's parts there, as the 2D game draws
    // units over the building sprite (docs/3d-models.md "Walkable cells"). 0 = none.
    virtual float walkableBias(const game::World& /*w*/, float /*wx*/, float /*wy*/) const { return 0; }
    // Named anchor points of the object's model in world space (x east, y = absolute height, z
    // south) with their size, the anchors whose name starts with `prefix` in name order
    // (docs/3d-models.md "Anchors"); false = no model / no such anchor.
    struct AnchorWorld {
        Vec3 pos;
        float size = 1;
    };
    virtual bool buildingAnchors(const game::World& /*w*/, const Terrain& /*terr*/, int /*side*/, int /*building*/,
                                 std::string_view /*prefix*/, std::vector<AnchorWorld>& /*out*/) const {
        return false;
    }
    virtual bool unitAnchors(const game::World& /*w*/, const Terrain& /*terr*/, int /*side*/, int /*unit*/,
                             std::string_view /*prefix*/, std::vector<AnchorWorld>& /*out*/) const {
        return false;
    }
    // Before the render pass (uploads, own copy pass on `cmd`), then inside it right after the
    // terrain (opaque, depth-writing meshes; the billboards follow). draw returns its draw calls.
    virtual void prepare(SDL_GPUDevice* /*dev*/, SDL_GPUCommandBuffer* /*cmd*/, const ModelFrame& /*f*/) {}
    virtual int draw(SDL_GPURenderPass* /*pass*/, SDL_GPUCommandBuffer* /*cmd*/, const ModelFrame& /*f*/) { return 0; }
    // Inside the sun shadow map's depth pass (after prepare): the meshes as casters.
    virtual int drawShadowDepth(SDL_GPURenderPass* /*pass*/, SDL_GPUCommandBuffer* /*cmd*/, const ModelFrame& /*f*/) {
        return 0;
    }
};

class Renderer3D final : public render::EffectAnchors {
public:
    // Quality (Settings::msaa / aniso / hdr; docs/enhanced-graphics.md "Quality"), fixed for the
    // renderer's life (the pipelines depend on it: the session recreates the renderer on a change).
    // msaa 1/2/4 (lowered to what the device supports), aniso 1 = off .. 16 (terrain tiles, the
    // designed materials, model textures), hdr = a half-float scene target (values above 1 kept;
    // drawn through render::Hdr's tone map, set with setHdr).
    struct Quality {
        int msaa = 4;
        int aniso = 16;
        bool hdr = false;
        bool operator==(const Quality&) const = default;
    };
    // nullptr (with a log line) when `r` is not the SDL GPU renderer or the pipelines fail.
    static std::unique_ptr<Renderer3D> create(SDL_Renderer* r, render::SpriteCache& sprites,
                                              const formats::Templates& templates, const Quality& quality);
    const Quality& quality() const { return quality_; }
    bool hdrScene() const { return colorFormat_ == SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; }
    ~Renderer3D() override;
    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    void setAtlas(render::TileAtlas& atlas);

    struct Options {
        bool showShroud = true;
        int playerSide = 0;
        int healthBars = 2;  // Settings::healthBars: 0 selected, 1 damaged buildings, 2 always
        bool snapCamera = false;  // jump to the wanted pose (first frame, screenshots, loads)
        // Modern effects (Settings::effects = Modern; nullptr = Classic): particles, lights,
        // tread marks in world space; shake is added to the look-at point.
        const render::Effects* effects = nullptr;
        float shakeX = 0, shakeY = 0;
        // Settings "3D terrain": false = the original tiles on the heightfield, true = the designed
        // terrain (render3d/terrain_v2.*; falls back to the tiles when its assets are missing).
        bool designedTerrain = false;
        // With designedTerrain: "Designed (faithful)" - laid out after the original tiles
        // (docs/3d-mode.md "Terrain v3").
        bool faithfulTerrain = false;
        // Settings "Sun shadows": a real shadow map (terrain, rocks and models cast; everything
        // receives); false = the baked heightfield shadows of stage 1.
        bool shadowMap = true;
        // Time of day / Modern lighting (render/lighting.hpp; nullptr = the old light): the sun's
        // direction and colour, the sky, the haze; lamps (render/scene_lights.*: spot / point
        // lights + flare billboards); bloom over the picture (render/bloom.*).
        const render::SceneLight* light = nullptr;
        const render::SceneLights* lamps = nullptr;
        // Building sprites from a rendered art pack (no models): its fx_weld anchors in 2D art
        // coordinates (render/weld_fx.hpp PackAnchors) - weld flashes instead of the original overlay.
        const render::EffectAnchors* packAnchors = nullptr;
        render::Bloom* bloom = nullptr;
        render::Hdr* hdr = nullptr;  // the tone map of an HDR scene (Quality::hdr; render/hdr.*)
    };
    // Draws the battlefield into (0, 0, viewW, viewH) of the current render target.
    void render(const game::World& w, const CameraPose& want, int viewW, int viewH, float dt, const Options& o);
    // Health bars of the frame just rendered (SDL_Renderer, over the 3D picture).
    void drawOverlays(const game::World& w, const Options& o);

    // Picking with the camera of the last render(): view pixel -> world pixel on the terrain.
    // pick() snaps to a unit drawn under the cursor (its position), else the terrain point.
    bool screenToGround(float sx, float sy, float& wx, float& wy) const;
    bool pick(const game::World& w, float sx, float sy, float& wx, float& wy) const;
    // World pixel (+ height above the terrain) -> view pixel.
    bool worldToScreen(float wx, float wy, float above, float& sx, float& sy) const;
    // A tinted cell-aligned rectangle lying on the terrain (placement footprint), SDL_Renderer.
    void fillGroundRect(float x0, float y0, float x1, float y1, SDL_FColor fill, SDL_FColor outline) const;
    const Camera3D& camera() const { return cam_; }
    // Camera limits of the current object mode: sprites = yaw locked to 0 (north up), tilt
    // kSpriteMinTilt..kSpriteMaxTilt; models = free yaw, the full tilt range. Clamps in place.
    void constrainPose(float& yaw, float& tilt) const;
    bool yawUnlocked() const { return models_ && models_->active(); }
    // render::EffectAnchors: the models' anchors as effect points (ground point + height above the
    // terrain there), from the placement of the last frame's terrain.
    bool buildingAnchors(const game::World& w, int side, int building, std::string_view prefix,
                         std::vector<render::EffectAnchor>& out) const override;
    bool unitAnchors(const game::World& w, int side, int unit, std::string_view prefix,
                     std::vector<render::EffectAnchor>& out) const override;
    void setModelHook(ModelHook* hook) { models_ = hook; }
    // A screen-space line / flag helper for overlays: view pixels per world pixel at a ground point.
    float pixelsPerWorld(float wx, float wy) const { return ppwAt(wx, wy, 0); }
    bool ready() const { return terrainBuilt_; }
    // The designed terrain is drawn (asked for and its assets / pipelines are there).
    bool designedTerrainActive() const { return terrainBuilt_ && terrain_.designed(); }
    double lastCpuMs() const { return cpuMs_; }
    double lastShadowMs() const { return shadowMs_; }  // CPU time of the shadow pass setup + recording
    int lastDrawCalls() const { return drawCalls_; }
    int lastSprites() const { return int(insts_.size()); }

private:
    Renderer3D(SDL_Renderer* r, SDL_GPUDevice* dev, render::SpriteCache& sprites, const formats::Templates& t);
    bool init();
    bool ensureTargets(int w, int h);
    void releaseTargets();
    void buildTileArray();
    void collect(const game::World& w, const Options& o);
    void addUnit(const game::World& w, const game::Unit& u, const Options& o);
    void addBuilding(const game::World& w, const game::Building& b);
    void addFlyers(const game::World& w);

    enum Pipe : std::uint8_t { kAlpha, kAdd, kSub, kMul, kLight, kPipeCount };
    struct Inst {
        SDL_GPUTexture* tex = nullptr;
        Pipe pipe = kAlpha;
        bool billboard = false;  // camera-facing: the whole quad at the depth of `anchor`
        Vec3 c[4];               // corners: top-left, top-right, bottom-left, bottom-right
        Vec3 anchor;             // billboards: depth anchor
        Vec3 s[4];               // per corner: the world point whose sun shadow applies
        float gx[4] = {}, gy[4] = {};  // per corner: the map point whose shroud darkness applies
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        float r = 1, g = 1, b = 1, a = 1, flash = 0;
        float depth = 0;         // billboards: sort key (view depth)
        int seq = 0;
    };
    // Shroud / map visibility of a cell (2D rule: fully shrouded cells hide what is on them,
    // nothing outside the map is drawn).
    bool cellHidden(int cx, int cy) const;
    bool pointHidden(float wx, float wy) const { return cellHidden(int(std::floor(wx / 32)), int(std::floor(wy / 32))); }
    bool buildingHidden(const game::Building& b) const;
    void addEffects(const render::Effects& fx);
    // The frame's dynamic point lights from the Modern effects' light list (strongest visible first).
    void buildPointLights(const Options& o, int viewW, int viewH);
    PointLightsU lightsU_{};
    std::vector<std::pair<float, int>> lightPick_;
    // Sprite helpers: `level` picks the mip for the on-screen size; nullptr textures are skipped.
    const render::Sprite* sprite(int frame, int house);
    // An object's billboard frame (a type the model pack has no model for, docs/3d-mode.md "Objects"):
    // the redesigned art pack's frame. Logs each (kind, type) once - "3D fallback: ..." - with whether the
    // pack has the frame; a frame the pack lacks is not drawn (never the original sprite) unless
    // D2K_3D_ORIGINAL_FALLBACK=1. kind: "unit", "building", "crate", ...
    const render::Sprite* objectSprite(int frame, int house, const char* kind, int type, const char* name);
    std::unordered_set<long long> fallbackLogged_;
    // Non-object art drawn from the original frames (Classic explosions / projectiles the Modern effects do not
    // replace, crates): logged once per (kind, type) as "3D original art: ..." when the art pack lacks the frame.
    void noteOriginalArt(const char* kind, int type, int frame);
    SDL_GPUTexture* tex(SDL_Texture* t);
    float ppwAt(float wx, float wy, float z) const;
    // A sprite standing at ground point (gx, gy) + height z, image top-left at (ox, oy) world px
    // from it (image axes = screen axes); rot: clockwise degrees about the anchor (eased facing).
    // g2x/g2y: where the image's top-left lies in the 2D view (shroud darkness per pixel exactly
    // as the 2D shroud overlay); NAN = (gx + ox, gy - z + oy).
    void billboard(const render::Sprite* sp, float gx, float gy, float z, float ox, float oy, Pipe pipe,
                   float alpha = 1, float flash = 0, float bias = 0, float r = 1, float g = 1, float b = 1,
                   float rot = 0, float g2x = NAN, float g2y = NAN);
    // Generic camera-facing quad (also effects); returns false if culled. rayDepth: find the
    // depth that keeps the part below the anchor in front of the terrain with ray casts (sprites)
    // instead of the flat-ground estimate (small particles).
    bool pushBillboard(SDL_GPUTexture* t, const Vec3& base, float ox, float oy, float w, float h, float rot, Pipe pipe,
                       float r, float g, float b, float a, float flash, float bias, float g2x, float g2y, bool rayDepth,
                       float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
    void decal(const render::Sprite* sp, bool shadowMask, float x0, float y0, Pipe pipe, float flatY = -1, float alpha = 1,
               float flash = 0, float r = 1, float g = 1, float b = 1);
    void decalTex(SDL_GPUTexture* t, float x0, float y0, float w, float h, Pipe pipe, float flatY, float r, float g,
                  float b, float a);
    // A rotated rectangle lying on the terrain (centre, half sizes, angle in radians).
    void groundQuad(SDL_GPUTexture* t, float cx, float cy, float hw, float hh, float angle, Pipe pipe, float r, float g,
                    float b, float a, float u0, float v0, float u1, float v1);
    // Depth along the eye ray through `p` of the first terrain hit, searching heights
    // [p.y - below, p.y + above]; false if none.
    bool terrainDepthThrough(const Vec3& p, float& depth) const;

    SDL_Renderer* r_;
    SDL_GPUDevice* dev_;
    render::SpriteCache& sprites_;
    const formats::Templates& t_;
    render::TileAtlas* atlas_ = nullptr;
    bool atlasDirty_ = true;
    std::unique_ptr<render::SoftShroud> shroud_;

    Camera3D cam_;
    Terrain terrain_;
    std::unique_ptr<DesignedTerrain> designed_;  // created on first use ("3D terrain: Designed")
    bool designedFailed_ = false;
    bool ensureDesigned();
    // Sun shadow map: an orthographic light frustum fitted to the visible ground every frame.
    static constexpr int kShadowSize = 4096;
    bool initShadows();
    Mat4 fitLight(float mapW, float mapH, float& depthRange) const;
    SDL_GPUTexture* shadowTex_ = nullptr;
    SDL_GPUTextureFormat shadowFmt_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    SDL_GPUSampler* shadowSampler_ = nullptr;
    SDL_GPUGraphicsPipeline* terrainDepthPipe_ = nullptr;
    float terrainTop_ = 0;  // highest terrain vertex
    double shadowMs_ = 0;
    const game::World* world_ = nullptr;
    bool terrainBuilt_ = false, terrainDirty_ = false;
    Vec3 sun_ = Terrain::sunDirection();  // this frame's sun (the faithful terrain: from the south-west)
    Vec3 baseSun_ = Terrain::sunDirection();  // the terrain style's own sun (time of day turns it)
    float fogRgb_[3] = {};                    // this frame's haze colour
    const render::SceneLights* lamps_ = nullptr;
    void addLampFlares(const render::SceneLights& l);
    // Weld flashes at the building models' fx_weld anchors while they work (render/weld_fx.hpp).
    void addWeldFlashes(const game::World& w);
    SDL_Texture* weldTex_ = nullptr;
    const render::EffectAnchors* packAnchors_ = nullptr;  // this frame's Options::packAnchors
    std::vector<render::EffectAnchor> weldTmp_;
    Uint64 lastSpiceCheck_ = 0;

    // GPU objects.
    SDL_GPUGraphicsPipeline* terrainPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* spritePipe_[kPipeCount] = {};
    SDL_GPUSampler* linearClamp_ = nullptr;
    SDL_GPUSampler* tileSampler_ = nullptr;
    SDL_GPUSampler* pointClamp_ = nullptr;
    SDL_GPUTexture* tileArray_ = nullptr;
    int tileArrayCount_ = 0;
    SDL_GPUTexture* tileMap_ = nullptr;
    std::vector<std::uint16_t> tileMapCpu_;
    bool tileMapDirty_ = true;
    SDL_GPUTexture* white_ = nullptr;
    SDL_GPUTexture* clearMask_ = nullptr;  // shroud off: alpha 0
    SDL_GPUTexture* ring_ = nullptr;
    bool smallTexturesUploaded_ = false;
    SDL_GPUBuffer* terrainVB_ = nullptr;
    SDL_GPUBuffer* terrainIB_ = nullptr;
    std::uint32_t terrainIndexCount_ = 0;
    SDL_GPUBuffer* spriteVB_ = nullptr;
    SDL_GPUBuffer* quadIB_ = nullptr;
    std::uint32_t spriteCapacity_ = 0;  // quads
    std::uint32_t quadIndexCap_ = 0;    // quads the index buffer holds
    SDL_GPUTransferBuffer* spriteTB_ = nullptr;
    // Targets.
    SDL_GPUTextureFormat colorFormat_ = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    SDL_GPUSampleCount samples_ = SDL_GPU_SAMPLECOUNT_1;
    Quality quality_;
    SDL_GPUTexture* sceneTex_ = nullptr;   // resolved colour, wrapped by sceneSdl_
    SDL_GPUTexture* msaaTex_ = nullptr;
    SDL_GPUTexture* depthTex_ = nullptr;
    SDL_Texture* sceneSdl_ = nullptr;
    int tw_ = 0, th_ = 0;

    ModelHook* models_ = nullptr;
    const game::World* frameWorld_ = nullptr;  // the world collect() is drawing
    bool frameShroud_ = true;
    std::vector<Inst> insts_;
    std::vector<float> verts_;
    std::unordered_set<SDL_Texture*> seen_;
    bool newTextures_ = false;
    int yawSteps_ = 0;  // camera yaw in 1/32 turns, for the facing frames
    int decalJitterX_[16] = {}, decalJitterY_[16] = {};
    // Modern effects of this frame (collect): destroyed buildings collapse in addBuilding.
    const render::Effects* fx_ = nullptr;
    // Classic effects: the sonic shot's lens (Image__SetLensImage 0x42ada0), drawn after the scene
    // from the scene texture (screen centre, radius in view px); Modern: the beams' heat haze.
    struct Lens { float sx, sy, r; };
    std::vector<Lens> lenses_;
    std::vector<SDL_Vertex> postV_;
    std::vector<int> postI_;
    void drawPostRefraction(int viewW, int viewH);
    // Sun glare (Modern lighting, a low sun: dawn / dusk; docs/3d-mode.md "Sun glare"): when the
    // camera faces towards the sun, a soft glow at the screen edge towards it and faint flare ghosts
    // along the axis through the centre, added over the finished picture. Returns the strength (0 =
    // nothing drawn; for the checks).
    float drawSunGlare(int viewW, int viewH, const Options& o);
    SDL_Texture* glareGlowTex_ = nullptr;
    SDL_Texture* glareGhostTex_ = nullptr;
    double cpuMs_ = 0;
    int drawCalls_ = 0;
};

}  // namespace d2k::render3d
