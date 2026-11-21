#pragma once
// Real 3D models in Graphics: 3D (docs/3d-models.md): Settings "3D models" = Sprites (billboards
// only), PS1 (the PS1 disc's own models, read from the player's disc image at runtime:
// formats/ps1_models.*) or New (glTF models of a model pack: render3d/model_gltf.*). Units and
// buildings whose type has a model are drawn here as lit, textured meshes oriented by their facing
// (+ turret facing), with the PS1 shadow polygons / house markers or the art pack's house mask;
// Renderer3D skips their billboards (ModelHook). Everything else stays a billboard. Render-only:
// the World is read, never changed.

#include "formats/bintables.hpp"
#include "formats/model3d.hpp"
#include "formats/templates.hpp"
#include "render/building_assemble.hpp"
#include "render/building_dropin.hpp"
#include "render3d/model_assembly.hpp"
#include "render3d/model_drive.hpp"
#include "render3d/model_gltf.hpp"
#include "render3d/renderer3d.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace d2k::render3d {

class ModelRenderer final : public ModelHook, public render::DropInSource, public render::AssemblySource {
public:
    enum class Source { Sprites, Ps1, Pack };
    // Building life (docs/3d-mode.md "Building life"): how a named node moves (models_life.cpp lifeKind).
    enum LifeKind : std::uint8_t { kLifePower, kLifeWind, kLifeVane, kLifeDish };
    ModelRenderer(const formats::Templates& t, const formats::HouseColours& colours);
    ~ModelRenderer() override;
    ModelRenderer(const ModelRenderer&) = delete;
    ModelRenderer& operator=(const ModelRenderer&) = delete;

    // Loads the models of a source (ps1Image: .cue/.bin; packDir: a folder with models/unit and
    // models/building). False (with a log line) when nothing could be loaded: active() is false.
    bool load(Source s, const std::string& ps1Image, const std::string& packDir);
    // A community campaign's Templates.bin (render::ModArt): per type, the name and group the models
    // are looked up by - the base type's it matches - instead of the template's own; both empty = the
    // campaign's own art: no model (Renderer3D draws its billboard). Empty vectors = the templates'
    // names. Call before load().
    void setModelKeys(std::vector<std::string> unitNames, std::vector<std::string> unitGroups,
                      std::vector<std::string> buildingNames, std::vector<std::string> buildingGroups) {
        keyUnitName_ = std::move(unitNames);
        keyUnitGroup_ = std::move(unitGroups);
        keyBldName_ = std::move(buildingNames);
        keyBldGroup_ = std::move(buildingGroups);
    }
    // Graphics: 3D + Reimagined look (docs/3d-models.md "Reimagined models"): a second pack whose models
    // replace the main pack's of the same key (its models.ini section / pose variants when it has them,
    // else the main pack's), with PBR maps (model_pbr_fs) and a simplified KEY_lod1.glb for small views.
    // Empty = none (the default: everything exactly as before). Call before load().
    void setOverlayPack(std::string dir) { overlayDir_ = std::move(dir); }
    // The texture formats the GPU samples (render3d/gpu.hpp modelTextureCaps): glTF textures are
    // block-compressed for it on the loader threads. Default: RGBA8. Call before load().
    void setTextureCaps(const TexCaps& c) { texCaps_ = c; }
    static constexpr const char* kReimaginedPack = "models-reimagined";
    int overlayModels() const { return overlayModels_; }
    Source source() const { return source_; }
    int unitModels() const;
    int buildingModels() const;
    // The model pack folders found (art-pack search dirs with a models/ subfolder): names, or the
    // full path of `name` ("" when none).
    // "new" = the built-in pack (assets/models: our own procedural models, tools/models/), listed
    // first and used when no pack is named.
    static constexpr const char* kBuiltInPack = "new";
    static std::vector<std::string> availablePacks();
    static std::string packPath(const std::string& name);

    bool active() const override { return source_ != Source::Sprites && (!models_.empty() || anyPending_); }
    // glTF packs (docs/memory.md): only the types of this game are read - those on the map and those the
    // houses in play can build (Templates ownerHouse); any other type is read the first frame it appears
    // (prepare). Call before load() (without it every type is read).
    struct Wanted {
        std::vector<std::uint8_t> units, buildings;  // per type: read at load
    };
    static Wanted wantedFor(const formats::Templates& t, const game::World& w);
    void setWanted(Wanted w) {
        wantSet_ = true;
        wantUnit_ = std::move(w.units);
        wantBld_ = std::move(w.buildings);
    }
    bool hasUnitModel(int unitType) const override;
    bool hasBuildingModel(int buildingType) const override;
    int buildingGlowBlink(int buildingType) const override;
    bool unitTrackContact(int unitType, TrackContact& out) const override;
    void setVehicleMotion(const render::VehicleMotion* vm) override { vm_ = vm; }
    void setEffects(const render::Effects* fx) override { fx_ = fx; }
    void setSmoothDrive(const render::SmoothDrive* d) override { drive_ = d; }
    void setTickPhase(float phase) override { tickPhase_ = std::clamp(phase, 0.0f, 1.0f); }
    void setWrecks(Wrecks* w) override { wrecks_ = w; }
    float unitModelHeight(int unitType) const override;
    float buildingModelHeight(int buildingType) const override;
    float groundOffset(const game::World& w, float wx, float wy) const override;
    float walkableBias(const game::World& w, float wx, float wy) const override;
    bool buildingAnchors(const game::World& w, const Terrain& terr, int side, int building, std::string_view prefix,
                         std::vector<AnchorWorld>& out) const override;
    bool unitAnchors(const game::World& w, const Terrain& terr, int side, int unit, std::string_view prefix,
                     std::vector<AnchorWorld>& out) const override;
    bool buildingSurface(const game::World& w, const Terrain& terr, int side, int building, float& gx, float& gy,
                         float& top) const override;
    float rayBuilding(const game::World& w, const Terrain& terr, int side, int building, const Vec3& o,
                      const Vec3& d) const override;
    float rayUnit(const game::World& w, const Terrain& terr, int side, int unit, const Vec3& o, const Vec3& d) const override;
    // Building placement: Drop-in (render/building_dropin.hpp; Settings::buildingPlacement): building
    // models in build-up come down folded, land and unfold instead of rising behind the cut plane.
    void setDropIn(bool on) { dropIn_ = on; }
    bool dropIn() const { return dropIn_; }
    // Also the lamp fade of an assembling building (the same curve) and 1 for a settled one.
    float buildingDropIn(const game::World& w, int side, int building) const override;
    // Building placement: Assemble (render/building_assemble.hpp): building models in build-up are
    // built up from the ground part by part (render3d/model_assembly.*). Buildings that stand when the
    // renderer first sees a world, or after settle(), are drawn finished (mission start, a loaded save).
    void setAssemble(bool on) { assemble_ = on; }
    bool assemble() const { return assemble_; }
    // Every building in build-up at the next frame stands finished (call after a new world / a load).
    void settle() {
        settleNext_ = true;
        if (wrecks_) wrecks_->reset();  // (a loaded game: the old battle's hulks are not its own)
    }
    float buildingAssembly(const game::World& w, int side, int building,
                           std::vector<render::AssemblyPartWorld>* parts) const override;
    // Puts the models read so far on the GPU (once; prepare() does it too): right after load() it keeps
    // their full CPU copies from living alongside the 3D terrain's build at the first frame (docs/memory.md).
    void upload(SDL_GPUDevice* dev);
    void prepare(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) override;
    int draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) override;
    int drawShadowDepth(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) override;

private:
    struct Range { std::uint32_t first = 0, count = 0; };  // indices into ib_ (first index, index count)
    struct GpuModel {
        formats::Model3D::Anchor anchor = formats::Model3D::Anchor::Centre;
        Range body, turret, shadow, house;
        std::vector<Range> bodyPoses;  // Model3D::bodyPoses
        float pivot[3] = {};
        bool point = false;
        bool houseOnTurret = false;
        bool projectShadow = false;  // no shadow polygons (glTF): the mesh is flattened along the sun
        bool emissive = false;       // the texture alpha carries emission (Model3D::emissiveInAlpha)
        float top = 0;               // the highest vertex (model space y, px above the origin)
        SDL_GPUTexture* tex = nullptr;  // null = white
        SDL_GPUBuffer* vb = nullptr;    // the page (vertex / index buffers) its ranges are in
        SDL_GPUBuffer* ib = nullptr;
        // Reimagined models: PBR maps (both set = drawn with model_pbr_fs) and the simplified parts.
        SDL_GPUTexture* pbrTex = nullptr;
        SDL_GPUTexture* nrmTex = nullptr;
        Range lodBody, lodTurret, lodHouse;
        bool hasLod = false;
    };
    struct Draw {
        int model = -1;
        Mat4 bodyM, turretM, shadowM;
        float plane[4] = {0, 1, 0, 0};  // ground plane for projected shadows (normal, dot(normal, point))
        float house[3] = {1, 1, 1};
        float flash = 0, alpha = 1;
        float emit = 0;     // emission strength (render::buildingGlow), 0 = the texture alpha is not emission
        // Build-up / selling (render/buildup_fx.hpp): cut height (world y), glow band (px; 0 = complete),
        // scan-line drift; buildK = how much stands (its shadow-map caster is squashed by it).
        float build[3] = {};
        float buildK = 1;
        // Drop-in (render/building_dropin.hpp): model_vs skin.y = 1 + unfold (0 = off), skin.z = model height;
        // Assemble (render/building_assemble.hpp): skin.w = 1 + progress (0 = off).
        float unfold[3] = {0, 0, 0};
        bool turret = false;
        int palette = -1;   // first joint palette entry of the body / house parts (-1 = rest pose)
        int bodyPose = -1;  // GpuModel::bodyPoses index replacing the body (-1 = the body)
        // Walkable front rows (docs/3d-models.md "Walkable cells"): the stencil reference of this
        // draw, 1..255 = a building with units on its walkable cells / such a unit, 0 = none; a
        // building's walk = (x0, z0 of its top-left cell, mask of its walkable cells, min height).
        int walkRef = 0;
        float walk[4] = {};
        bool lod = false;  // GpuModel::lod* instead of the full parts (small on screen)
        // Running gear (render3d/model_drive.hpp): model_vs drive = (1 = on, odometer px, spin radians, 0).
        float drive[3] = {0, 0, 0};
        // Realistic grade (docs/3d-mode.md): the height (px) of the sand dust band over the model's foot, above
        // its ground plane (0 = none: flyers, cargo).
        float dustBand = 0;
        // Vehicle wrecks (render3d/wrecks.hpp): a hulk's draw - model_fs dust.zw = (how charred 0..1; the ember
        // glow 0..1, or -(the share crumbled away)); 0, 0 = a live model. parts: 1 = body (+ house), 2 = turret
        // (a hulk's blown-off turret is a draw of its own).
        float wreck[2] = {0, 0};
        std::uint8_t parts = 3;
    };
    // Animation (docs/3d-models.md "Animation"): per model, how the simulation's animation state
    // picks a pose.
    struct AnimInfo {
        FrameMap frameMap;          // infantry: original art frame -> glTF animation / time / turn
        std::vector<int> frameAnim; // frameMap entries' animation indices (-1 = missing)
        int poseAnim = -1;          // the "pose" animation (pose k at time k / 24 s), -1 none
        int poseCount = 0;          // poses of poseAnim / bodyPoses
        std::vector<int> animFrames, levels;  // pose spec: building overlay frame / silo level -> pose
        // Unit art animation rows (the ornithopter's wing beat): pose = the row the 2D frame is
        // taken from (Unit::animFrame), from KEY_rowNN.glb variants, or through the spec's "rows"
        // (row -> pose of KEY_poseNN.glb; -1 = rest).
        bool rows = false;
        std::vector<int> rowPoses;
    };
    // How a unit's model is posed this frame (from the same state that picks its 2D frame).
    struct UnitPose {
        int anim = -1;      // skeleton animation, -1 = rest
        float time = 0;     // s
        int bodyPose = -1;  // body mesh variant
        bool framed = false;  // a frame-map entry: turn / shift / scale below replace the facing
        float yaw = 0, shiftE = 0, shiftN = 0, scale = 1, alpha = 1;
    };
    UnitPose unitPose(const game::Unit& u, int type, int model) const;
    void easeInfantryYaw(const ModelFrame& f, const game::Unit& u, int type, int model);
    int buildingPose(const game::World& w, const game::Building& b, int model) const;
    // False when the pack has no variants for the key (the overlay pack then falls back to the main pack's).
    bool loadVariants(int model, const std::string& packDir, const std::string& key, const std::string& glbPath,
                      const GltfAdjust& adj);
    // The joint palette of (model, animation, time) in this frame's palette buffer (first entry);
    // poses are quantised to the original frames, so each is computed once and cached.
    int paletteFor(int model, int anim, float time);
    // Smooth driving: the palette of a unit whose wheel nodes (wheel_* / sprocket_* / idler_*) are
    // turned by the distance it drove (on top of its animation pose, anim < 0 = rest); -1 = no wheels.
    // spin = the drawn heading turned so far (radians, + clockwise): wheels left / right of the
    // centre line turn by odometer -/+ spin * their lateral offset (they counter-turn on the spot).
    void buildWheels(int model);
    int wheelPalette(int model, int anim, float time, float odometer, float spin = 0);
    struct Wheels {
        bool built = false;
        std::vector<int> node;
        std::vector<float> rest, restInv;  // 16 floats per wheel: node space -> model space (rest) and back
        std::vector<float> radius;         // px
    };
    std::vector<Wheels> wheels_;  // per model (built on first use)
    // Building life (docs/3d-mode.md "Building life"; models_life.cpp): building nodes named turbine* / fan* /
    // rotor* / propeller* (spun by the building's power and health), anemometer* (spun by the wind), vane* (turned
    // into the wind, swinging gently) and dish* / radar* (turning while the radar works) turn about their own axis
    // (node local +Y by convention). Prepared at the upload (reads the body's vertices: the axis check, the vane's
    // tail); posed per frame through the joint palette, as the wheels.
    struct Spinner {
        int node = -1;
        int outer = -1;  // the spinner this one sits on (index into Life::spin), -1 none
        LifeKind kind = kLifePower;
        int axis = 1;       // node local axis 0 x, 1 y, 2 z
        float rate = 0;     // rad/s at full speed
        float phase = 0;    // rad (spinners of one building do not line up)
        float rest[16] = {}, restInv[16] = {};  // node space -> model space (rest) and back
    };
    struct Life {
        bool built = false;
        bool dish = false;  // has a dish* / radar* spinner (replaces the original animation's stepped pose)
        std::vector<Spinner> spin;  // outer spinners first
        std::vector<int> owner;     // per node: the spinner moving it (itself or an ancestor), -1 none
    };
    std::vector<Life> life_;  // per model
    void buildLife(int model);
    // Per building (side * kLifePool + index): the integrated turn of its spinners and its turret's scan.
    static constexpr int kLifePool = 1000;
    struct LifeState {
        int type = -1;
        std::uint32_t timeAdded = 0;
        double phi = 0, phiW = 0, phiD = 0;  // integrated speed (s at full speed): power / wind spinners, dish
        float speed = 0, speedW = 0, speedD = 0;  // 0..1
        float turret = 0, idle = 0;   // turret scan offset (rad), seconds its game facing has not changed
        int facing = -1;
    };
    std::vector<LifeState> lifeState_;
    bool lifeOff_ = false;    // D2K_BUILDING_LIFE_OFF=1
    double lifeClock_ = 0;    // real seconds of building life (frozen while paused)
    float lifeDt_ = 0;        // this frame's step
    std::uint32_t lifeTick_ = 0;
    float lifeStall_ = 0;
    void lifeFrame(const ModelFrame& f);
    // A building's life this frame: its palette (spinners; -1 none / not animated) and its turret's scan
    // offset (rad, added to the game facing). building = in build-up / selling / dropping in / assembling.
    int buildingLife(const game::World& w, int side, int index, const game::Building& b, int model, bool building,
                     bool turret, float& turretOff);
    std::vector<std::vector<DriveContact>> contacts_;  // per model: ground wheels / track loops (track marks)
    void uploadPalette(SDL_GPUCommandBuffer* cmd);
    // Lazy reading (glTF packs): file / adjustment of a key, the parallel pre-read, one model, one type.
    std::string fileFor(const char* kind, const std::string& key) const;
    bool isOverlay(const std::string& file) const;
    GltfAdjust adjustFor(const std::string& file, const std::string& key) const;
    void readAhead(const std::vector<std::pair<std::string, std::string>>& want);
    int getModel(const char* kind, const std::string& key);
    void resolveUnit(int type);
    void resolveBuilding(int type);
    void finishModels(std::size_t first);
    // Reads the pending types the world shows now; true when models were added (uploaded next).
    bool loadPending(const game::World& w);
    bool wantedUnit(int i) const { return !wantSet_ || (std::size_t(i) < wantUnit_.size() && wantUnit_[std::size_t(i)]); }
    bool wantedBuilding(int i) const { return !wantSet_ || (std::size_t(i) < wantBld_.size() && wantBld_[std::size_t(i)]); }
    struct PreRead {
        bool ok = false, done = false;
        formats::Model3D m;
        std::string err;
    };
    std::map<std::string, PreRead> pre_;
    std::map<std::string, int> loadedFiles_;  // file -> model index (-1 = failed)
    std::string packDir_, root_, overlayRoot_;
    std::map<std::string, GltfAdjust> baseAdjust_, overlayAdjust_;
    std::vector<std::string> rUnitName_, rUnitKey_, rBldName_, rBldGroup_;  // the keys types are looked up by
    std::vector<std::uint8_t> unitPending_, bldPending_;  // in the pack, not read yet
    bool anyPending_ = false;
    std::vector<bool> fitted_;
    bool wantSet_ = false;
    std::vector<std::uint8_t> wantUnit_, wantBld_;
    std::size_t uploadedModels_ = 0;  // models_ [0, this) are on the GPU
    struct Page {
        SDL_GPUBuffer* vb = nullptr;
        SDL_GPUBuffer* ib = nullptr;
    };
    std::vector<Page> pages_;  // one per upload (the first frame, then each lazily read batch)
    void releaseGpu();
    // Memory accounting (render/mem_stats.hpp): re-counts the CPU copies (models_) and the GPU
    // textures / buffers into the global counters (the difference to what was counted before).
    void account();
    std::int64_t countedCpuGeom_ = 0, countedCpuImg_ = 0, countedGpuTex_ = 0, countedGpuBuf_ = 0;
    std::int64_t gpuTexBytes_ = 0, gpuBufBytes_ = 0;
    bool ensurePipelines(SDL_GPUDevice* dev, const ModelFrame& f);
    void collect(const ModelFrame& f);
    // The walkable cells of a building (tilesOccupiedAll but not tilesOccupiedSolid: units drive
    // over them), bit y * 4 + x; 0 for a building without a model.
    std::uint32_t walkMask(int buildingType) const;
    void addUnit(const ModelFrame& f, const game::World& w, int side, const game::Unit& u, int type, float height,
                 bool flying);
    // Vehicle wrecks: the hulks' draws (collect), and the pose a dying unit hands its hulk.
    void addWrecks(const ModelFrame& f);
    void poseWreck(const ModelFrame& f, const game::World& w, Wrecks::Hulk& h, const game::Unit* u);
    bool smallOnScreen(const ModelFrame& f, const GpuModel& g, const Vec3& at) const;
    // Placement of an object's model this frame (also for the anchors): body / turret matrices.
    bool unitPlacement(const Terrain& terr, const game::World& w, const game::Unit& u, int type, float height,
                       bool flying, Mat4& body, Mat4& turret, Mat4& tilt, Vec3& at) const;
    bool buildingPlacement(const Terrain& terr, const game::Building& b, Mat4& body, Mat4& turret, float& groundY) const;
    // Per building model: its low surfaces seen from above (groundOffset), model space.
    struct Deck {
        static constexpr float kRes = 2.0f;   // px per cell
        static constexpr float kCap = 8.0f;   // px: surfaces up to this high are walked / driven on
        float x0 = 0, z0 = 0;
        int w = 0, h = 0;
        std::vector<float> y;  // highest surface <= kCap per cell, -1 none
    };
    // Per model: model-space boxes of the parts drawn with the body matrix (body + house) and with
    // the turret matrix (picking; built lazily). Empty box: lo > hi.
    struct PickBox {
        bool built = false;
        Vec3 bodyLo{1e30f, 1e30f, 1e30f}, bodyHi{-1e30f, -1e30f, -1e30f};
        Vec3 turretLo{1e30f, 1e30f, 1e30f}, turretHi{-1e30f, -1e30f, -1e30f};
        // Runs of consecutive triangles with their boxes (meshes are spatially coherent in file
        // order): the ray tests only the triangles of the runs whose box it crosses.
        struct Run {
            std::uint8_t part = 0;  // 0 body, 1 house, 2 turret
            std::uint32_t first = 0, count = 0;  // triangle (not vertex) indices
            Vec3 lo, hi;
        };
        std::vector<Run> runs;
    };
    mutable std::vector<PickBox> pickBoxes_;
    const PickBox& pickBox(int model) const;
    std::vector<Deck> decks_;  // per model (buildings only)
    // Per building model: its top surface seen from above (buildingSurface), model space; built lazily
    // (Deck layout, every surface, no cap; maxY = the tallest).
    struct Roof {
        static constexpr float kRes = 3.0f;
        float x0 = 0, z0 = 0, maxY = 0;
        int w = 0, h = 0;
        bool built = false;
        std::vector<float> y;  // highest surface per cell, -1 none
    };
    mutable std::vector<Roof> roofs_;
    const Roof& roof(int model) const;
    void buildDeck(int model);
    // The deck under (wx, wy) of the building on cell (cx, cy), only on the cells where the original
    // puts units on a building: a refinery's dock, a repair pad, a factory's exit cell (0 elsewhere).
    float deckAt(const game::World& w, float wx, float wy, int cx, int cy) const;
    // Docking harvester (docs/3d-models.md "Docking"): the plan shift (px) that brings its unloading
    // pipe's nozzle (unit model anchor fx_nozzle, else the rear-most point of its last pose) onto the
    // refinery model's inlet (anchor fx_inlet*), eased in over the docking frames. False = none.
    bool dockOffset(const game::World& w, const game::Unit& u, int model, float gx, float gy, float& ox, float& oz) const;
    struct DockTip {
        bool valid = false;
        float x = 0, y = 0, z = 0;  // model px (facing 0)
    };
    std::vector<DockTip> dockTip_;  // per model (units with unloading poses)

    const formats::Templates& t_;
    const render::VehicleMotion* vm_ = nullptr;  // setVehicleMotion
    const render::Effects* fx_ = nullptr;         // setEffects
    const render::SmoothDrive* drive_ = nullptr;  // setSmoothDrive
    Wrecks* wrecks_ = nullptr;                    // setWrecks
    // The drawn body / turret facing of a unit in 32nds of a turn: eased between the simulation's
    // steps (Modern effects' Effects::unitVisual; ground vehicles), else the step itself.
    void drawnFacing(const game::Unit& u, int type, float& body, float& turret) const;
    const formats::HouseColours& colours_;
    Source source_ = Source::Sprites;
    std::vector<formats::Model3D> models_;   // CPU copies until uploaded
    std::vector<float> modelTop_;            // per model: highest vertex (world px above its ground)
    std::vector<int> unitModel_, buildingModel_;  // template -> model index, -1 none
    std::vector<std::string> keyUnitName_, keyUnitGroup_, keyBldName_, keyBldGroup_;  // setModelKeys
    // GPU.
    SDL_GPUDevice* dev_ = nullptr;
    SDL_GPUTextureFormat colorFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID, depthFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSampleCount samples_ = SDL_GPU_SAMPLECOUNT_1;
    int aniso_ = 1;
    SDL_GPUGraphicsPipeline* meshPipe_ = nullptr;
    // The same without blending (opaque draws: no stealth, no build-up hologram) - blended
    // fragments are never hidden-surface-removed on tile GPUs (Apple): what lies under an opaque
    // model would be shaded too.
    SDL_GPUGraphicsPipeline* meshOpaquePipe_ = nullptr;
    SDL_GPUGraphicsPipeline* shadowPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* projShadowPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* depthPipe_ = nullptr;  // sun shadow map casters (created on first use)
    SDL_GPUGraphicsPipeline* viewDepthPipe_ = nullptr;  // the same without the depth bias (camera depth)
    SDL_GPUGraphicsPipeline* walkPipe_ = nullptr;   // walkable cells' parts, stencil != the unit's (null: no stencil)
    // The same three with model_pbr_fs (Reimagined models with PBR maps; created with the others).
    SDL_GPUGraphicsPipeline* pbrPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* pbrOpaquePipe_ = nullptr;
    SDL_GPUGraphicsPipeline* pbrWalkPipe_ = nullptr;
    bool pbrBuilt_ = false;   // the pipelines were made for models with PBR maps
    std::string overlayDir_;  // setOverlayPack
    TexCaps texCaps_;         // setTextureCaps
    int overlayModels_ = 0;   // models taken from the overlay pack by the last load()
    float lodPxPerTile_ = 44.0f;  // a model with a simplified version uses it below this many screen px per tile
    // model_pbr_fs tuning (D2K_PBR="normal green spec env ao"): see models.cpp ModelPbrU.
    float pbrK_[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0, 0, 0};
    SDL_GPUTextureFormat depthPipeFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSampler* nearest_ = nullptr;
    SDL_GPUSampler* linear_ = nullptr;
    SDL_GPUTexture* white_ = nullptr;
    // Indexed meshes (pages_): the glTF loader gives triangle lists with every corner its own vertex; the
    // upload shares equal corners (a ~13k-triangle tank: ~3x fewer vertices to shade, twice a frame).
    std::vector<GpuModel> gpu_;
    bool pipelineFailed_ = false;
    std::vector<Draw> draws_;
    bool dropIn_ = false;
    // The time into the next tick of the last collected frame (the drop-in pose between ticks).
    mutable float tickFrac_ = 0;
    // Drop-in progress of a building (buildupProgress at tickFrac_) when its model drops in, else < 0.
    float dropInProgress(const game::Building& b) const;
    // Assemble: the parts per model (buildings), the setting, the buildings drawn finished although in
    // build-up (they stood when the renderer first saw the world / after settle()).
    std::vector<AssemblyPlan> assembly_;
    bool assemble_ = false;
    bool settleNext_ = true;
    struct Settled {
        int side = 0, index = 0, type = 0, tileX = 0, tileY = 0;
    };
    std::vector<Settled> settled_;
    bool isSettled(int side, int index, const game::Building& b) const;
    void updateSettled(const game::World& w);
    // Assemble progress of building (side, index) when its model assembles this frame, -2 when it is
    // settled (drawn finished), else -1.
    float assembleProgress(int side, int index, const game::Building& b) const;
    // Animation.
    std::vector<AnimInfo> anim_;  // per model
    std::vector<std::uint8_t> glowBlink_;  // per model: models.ini glow = blink (1) | pulse (2)
    // Infantry frame-map poses blend towards the next frame of their strip by the 3-tick frame counter and the
    // tick phase (setTickPhase): the walk cycle runs continuously at any GameSpeed. D2K_MODERN_ANIM=0: the
    // original's steps (one pose per 2D frame).
    bool smoothAnim_ = true;
    float tickPhase_ = 0;         // setTickPhase
    // Infantry: the drawn yaw per unit (side << 16 | index), eased towards the frame map's (8 directions: a
    // turn was a 45 degree snap). Updated by addUnit, read by unitPlacement.
    struct InfantryYaw {
        float yaw = 0;
        double time = 0;
        int type = -1;
    };
    std::unordered_map<std::uint32_t, InfantryYaw> infantryYaw_;
    bool animOff_ = false;        // D2K_ANIM_OFF=1: rest poses only (measurements)
    std::unordered_map<std::uint64_t, std::vector<float>> poseCache_;  // (model, anim, time) -> palette
    std::unordered_map<std::uint64_t, int> frameSlots_;                // this frame: key -> first entry
    std::vector<float> paletteData_;                                   // this frame's palette (16 floats each)
    SDL_GPUBuffer* palette_ = nullptr;                                 // vertex storage buffer 0
    LightGridBuffer lightFallback_;  // fragment storage buffer 0 when the frame has no light grid
    SDL_GPUTransferBuffer* paletteTb_ = nullptr;
    std::uint32_t paletteCap_ = 0;  // matrices
};

}  // namespace d2k::render3d
