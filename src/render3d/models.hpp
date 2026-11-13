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
#include "render3d/model_gltf.hpp"
#include "render3d/renderer3d.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace d2k::render3d {

class ModelRenderer final : public ModelHook {
public:
    enum class Source { Sprites, Ps1, Pack };
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

    bool active() const override { return source_ != Source::Sprites && !models_.empty(); }
    bool hasUnitModel(int unitType) const override;
    bool hasBuildingModel(int buildingType) const override;
    float unitModelHeight(int unitType) const override;
    float buildingModelHeight(int buildingType) const override;
    float groundOffset(const game::World& w, float wx, float wy) const override;
    float walkableBias(const game::World& w, float wx, float wy) const override;
    bool buildingAnchors(const game::World& w, const Terrain& terr, int side, int building, std::string_view prefix,
                         std::vector<AnchorWorld>& out) const override;
    bool unitAnchors(const game::World& w, const Terrain& terr, int side, int unit, std::string_view prefix,
                     std::vector<AnchorWorld>& out) const override;
    void prepare(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) override;
    int draw(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) override;
    int drawShadowDepth(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd, const ModelFrame& f) override;

private:
    struct Range { std::uint32_t first = 0, count = 0; };
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
        bool turret = false;
        int palette = -1;   // first joint palette entry of the body / house parts (-1 = rest pose)
        int bodyPose = -1;  // GpuModel::bodyPoses index replacing the body (-1 = the body)
        // Walkable front rows (docs/3d-models.md "Walkable cells"): the stencil reference of this
        // draw, 1..255 = a building with units on its walkable cells / such a unit, 0 = none; a
        // building's walk = (x0, z0 of its top-left cell, mask of its walkable cells, min height).
        int walkRef = 0;
        float walk[4] = {};
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
    int buildingPose(const game::World& w, const game::Building& b, int model) const;
    void loadVariants(int model, const std::string& packDir, const std::string& key, const std::string& glbPath,
                      const GltfAdjust& adj);
    // The joint palette of (model, animation, time) in this frame's palette buffer (first entry);
    // poses are quantised to the original frames, so each is computed once and cached.
    int paletteFor(int model, int anim, float time);
    void uploadPalette(SDL_GPUCommandBuffer* cmd);
    void releaseGpu();
    bool ensurePipelines(SDL_GPUDevice* dev, const ModelFrame& f);
    void collect(const ModelFrame& f);
    // The walkable cells of a building (tilesOccupiedAll but not tilesOccupiedSolid: units drive
    // over them), bit y * 4 + x; 0 for a building without a model.
    std::uint32_t walkMask(int buildingType) const;
    void addUnit(const ModelFrame& f, const game::World& w, int side, const game::Unit& u, int type, float height,
                 bool flying);
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
    std::vector<Deck> decks_;  // per model (buildings only)
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
    SDL_GPUGraphicsPipeline* shadowPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* projShadowPipe_ = nullptr;
    SDL_GPUGraphicsPipeline* depthPipe_ = nullptr;  // sun shadow map casters (created on first use)
    SDL_GPUGraphicsPipeline* walkPipe_ = nullptr;   // walkable cells' parts, stencil != the unit's (null: no stencil)
    SDL_GPUTextureFormat depthPipeFmt_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUSampler* nearest_ = nullptr;
    SDL_GPUSampler* linear_ = nullptr;
    SDL_GPUTexture* white_ = nullptr;
    SDL_GPUBuffer* vb_ = nullptr;
    std::vector<GpuModel> gpu_;
    bool uploaded_ = false, pipelineFailed_ = false;
    std::vector<Draw> draws_;
    // Animation.
    std::vector<AnimInfo> anim_;  // per model
    bool smoothAnim_ = false;     // "Modern animation" (future setting; D2K_MODERN_ANIM=1): blend between frames
    bool animOff_ = false;        // D2K_ANIM_OFF=1: rest poses only (measurements)
    std::unordered_map<std::uint64_t, std::vector<float>> poseCache_;  // (model, anim, time) -> palette
    std::unordered_map<std::uint64_t, int> frameSlots_;                // this frame: key -> first entry
    std::vector<float> paletteData_;                                   // this frame's palette (16 floats each)
    SDL_GPUBuffer* palette_ = nullptr;                                 // vertex storage buffer 0
    SDL_GPUTransferBuffer* paletteTb_ = nullptr;
    std::uint32_t paletteCap_ = 0;  // matrices
};

}  // namespace d2k::render3d
