#pragma once
// Draws a Scenario: terrain tiles, then buildings and units from DATA.R16.

#include "formats/templates.hpp"
#include "game/scenario.hpp"
#include "game/world.hpp"
#include "render/effect_anchors.hpp"
#include "render/effects.hpp"
#include "render/lighting.hpp"
#include "render/sprites.hpp"
#include "render/tileset.hpp"

#include <memory>
#include <vector>

struct SDL_Renderer;
struct SDL_FPoint;
struct SDL_Texture;

namespace d2k::render {

class Effects;
class SoftShroud;
class SceneLights;
class Bloom;
class Hdr;
struct SceneLight;

struct Camera {
    float x = 0, y = 0;  // world pixels at the top-left of the view
    float zoom = 1.0f;
};

// Something that draws the terrain instead of the tile atlas (Graphics: Enhanced, "Terrain:
// Designed": render3d/terrain_bake.*). The terrain damage decals and everything else stay
// MapRenderer's.
class TerrainLayer {
public:
    virtual ~TerrainLayer() = default;
    // Draws the view's terrain with the tiles' screen mapping; false = draw the tiles instead.
    virtual bool drawTerrain(SDL_Renderer* r, const Camera& cam, int viewW, int viewH) = 0;
};

class MapRenderer {
public:
    MapRenderer(SDL_Renderer* r, TileAtlas& atlas, SpriteCache& sprites, const formats::Templates& templates);
    void setAtlas(TileAtlas& atlas) { atlas_ = &atlas; }

    // Terrain + live objects from the world; misc markers from the scenario.
    void draw(const game::World& w, const game::Scenario& s, const Camera& cam, int viewW, int viewH);
    bool showMarkers = false;  // player starts / worm spawners / blooms as debug markers
    bool showShroud = true;
    // Enhanced "Terrain: Designed": drawn instead of the tiles when set (nullptr = the tiles).
    TerrainLayer* terrainLayer = nullptr;
    // Enhanced "Soft" shroud (render/soft_shroud.hpp) instead of the original edge pieces.
    // Always false in the Original graphics mode.
    bool softShroud = false;
    int playerSide = 0;  // own cloaked units shimmer, enemy cloaked units are invisible
    // Selection overlay: false = modern brackets + bar (default), true = the original
    // bar/bracket/pip images from DATA.R16 (BlitVehicles 0x428e30), also for buildings.
    bool classicSelection = false;
    // Enhanced (classicSelection off): health bars. 0 Selected (selected units and buildings
    // only), 1 Damaged (also buildings below full health), 2 Always (every building and unit).
    // A selected building always shows corner brackets and its bar.
    int healthBars = 2;
    // Enhanced "Modern" effects (render/effects.hpp): particles, glows, treads, shake, smooth
    // turning. Off = the original presentation (always off in the Original graphics mode).
    void setModernEffects(bool on);
    bool modernEffects() const { return effects_ != nullptr; }
    // 3D mode: advance the effects without drawing the 2D map (viewCam: the ground rectangle the
    // 3D view covers, for the emitters' culling); the 3D renderer draws effects() in world space.
    void updateEffects(const game::World& w, const Camera& viewCam, int viewW, int viewH);
    const Effects* effects() const { return effects_.get(); }
    Effects* effects() { return effects_.get(); }
    // Game clock for the effects (gameTicks + fraction of the next tick), set every frame before
    // draw(); effects advance by its difference (paused = no change = frozen).
    void setEffectClock(double ticks) { effectClock_ = ticks; }
    void resetEffects();
    int effectParticles() const;
    // Time of day / Modern lighting (render/lighting.hpp; Enhanced only, nullptr = none - always
    // in the Original mode): the grade over the battlefield before the lights (the baked terrain
    // was lit for it, render3d/terrain_bake.*), the lamps (render/scene_lights.*, updated by the
    // session), and bloom (render/bloom.*; the map is then drawn into an offscreen copy first).
    const SceneLight* lighting = nullptr;
    float lightingGrade[3] = {1, 1, 1};
    SceneLights* sceneLights = nullptr;
    // A rendered art pack's model anchors (render/weld_fx.hpp, fx_weld): a working building whose
    // pack art has them shows weld flashes there instead of the original overlay. nullptr = none.
    const EffectAnchors* packAnchors = nullptr;
    Bloom* bloom = nullptr;
    // HDR rendering (Settings::hdr, render/hdr.*; with the bloom path only): the offscreen copy is
    // half-float and goes back through the tone map. nullptr = the 8-bit path.
    Hdr* hdr = nullptr;
    ~MapRenderer();

private:
    // A unit coordinate (16.16) in world pixels for drawing. Original graphics: whole pixels
    // (fixToPixel, like the original blitter; classicSelection is set exactly in that mode).
    // Enhanced: the exact sub-pixel position, so blit() rounds once on the *screen* grid, as the
    // terrain does (a world-pixel floor first made a zoomed unit advance 2,2,2,4 px per frame -
    // a visible stutter - and its overlays round differently from the sprite).
    float unitPx(std::int32_t fix) const {
        return classicSelection ? float(game::fixToPixel(fix)) : float(fix) / 65536.0f;
    }
    void drawTerrain(const game::World& w, const Camera& cam, int viewW, int viewH);
    void drawBuildingBase(const game::World& w, const game::Building& b, const Camera& cam);
    // Returns the main image drawn (for the selection box), or nullptr.
    const Sprite* drawBuilding(const game::World& w, const game::Building& b, const Camera& cam);
    // Modern effects: a dying building's art in sinking, leaning, charred pieces (whole: one piece
    // at the average sink, for turret barrels).
    void drawCollapsed(const Sprite& sp, float worldX, float worldY, const Camera& cam, const Effects::CollapseLook& cl,
                       bool whole);
    // Modern build-up of redesigned buildings (render/buildup_fx.hpp).
    bool packBuildup(const game::World& w, const game::Building& b);
    float buildupTickFrac(const game::World& w) const;
    float buildupCut(const Sprite& sp, float worldY, float baseY, const Camera& cam, float p) const;
    // Sets the building's look for the calls below (seed, reveal, footprint, terrain colour).
    void prepareBuildup(const game::World& w, const game::Building& b, float p);
    void blitBuildup(const Sprite& sp, float worldX, float worldY, const Camera& cam, float cutS);
    // pass 0: the ground stain (before the art); pass 1: the rock heap, dust puffs and sparks (after it).
    void drawBuildupRubble(const game::Building& b, float x0, float x1, float cutS, const Camera& cam, float p, int pass);
    struct BuildupLook {
        std::uint32_t seed = 0;
        float reveal = 0, artHeight = 0;
        float sand[3] = {0.78f, 0.63f, 0.44f};
        float fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;  // footprint (world px)
    } buildup_;
    SDL_Texture* rockTex_ = nullptr;
    SDL_Texture* puffTex_ = nullptr;
    void drawUnit(const game::World& w, const game::Unit& u, const Camera& cam);
    void drawSelection(const game::World& w, const game::Unit& u, const Camera& cam);
    void drawHealthBars(const game::World& w, const Camera& cam);
    void drawPipRow(float x, float y, float w, float h, int filled, int n, float z);
    void drawBar(float x, float y, float w, float h, float frac);
    void drawClassicUnitSelection(const game::World& w, const game::Unit& u, const Camera& cam);
    void drawClassicBuildingSelection(const game::World& w, const game::Building& b, const Sprite* main,
                                      const Camera& cam);
    void drawClassicBox(int x0, int y0, int hbSize, std::int32_t hp, std::int32_t maxHp, const Camera& cam);
    void drawPips(int x0, int y, int filled, int total, const Camera& cam);
    void drawParticles(const game::World& w, const Camera& cam, int viewW, int viewH);
    void drawDecals(const game::World& w, const Camera& cam, int x0, int y0, int x1, int y1);
    void drawMarker(const game::MapObject& o, const Camera& cam);
    // Subtract / Brighten / RedTint / GreenTint draw SpriteCache::getIntensity sprites.
    enum class Blend { Normal, Additive, Half, Flash, Silhouette, Subtract, Brighten, RedTint, GreenTint };
    // degrees != 0: rotated clockwise about the world point (pivotX, pivotY) (Modern effects).
    void blit(const Sprite& sp, float worldX, float worldY, const Camera& cam, Blend mode = Blend::Normal,
              float degrees = 0, float pivotX = 0, float pivotY = 0);
    // A unit sprite whose frame offset is relative to the unit position (anchorX, anchorY);
    // rotated (Modern effects) about the anchor.
    void blitUnit(const Sprite& sp, float anchorX, float anchorY, const Camera& cam, Blend mode = Blend::Normal,
                  float degrees = 0);
    void blitScreen(const Sprite& sp, float sx, float sy, float zoom, Blend mode, float degrees, SDL_FPoint centre);
    void drawExplosions(const game::World& w, const Camera& cam, bool high);
    void drawBullets(const game::World& w, const Camera& cam);
    void drawFlyers(const game::World& w, const Camera& cam, int pass);  // 0x42a5d0 pass 0 / 1
    // The sonic shot's lens (Image__SetLensImage 0x42ada0, bullet flag 0x800): fills lensTex_ with
    // the already drawn frame under the frame's square (top-left at screen point sx, sy, size
    // `size` screen pixels) bent through the lens table. Returns false if nothing is to be drawn.
    bool buildLens(const formats::R16Frame& f, float sx, float sy, int size);
    std::vector<std::uint32_t> lensImage_;  // the projectile TImage SetLensImage writes into (16x16)
    std::vector<std::uint32_t> lensScratch_;
    SDL_Texture* lensTex_ = nullptr;
    int lensTexSize_ = 0;
    // Weld flashes at a redesigned building's fx_weld anchors (render/weld_fx.hpp); true if drawn.
    bool drawWeldFlashes(const game::World& w, const game::Building& b, const Camera& cam);
    SDL_Texture* weldTex_ = nullptr;
    std::vector<EffectAnchor> weldTmp_;
    void drawShroud(const game::World& w, const Camera& cam, int viewW, int viewH);
    SDL_Texture* shroudTexture(int frame);
    std::vector<SDL_Texture*> shroudTex_;  // subtractive edge pieces, index = shroud byte
    int decalJitterX_[16] = {}, decalJitterY_[16] = {};  // 0x503f88 / 0x502ed8
    bool decalJitterInit_ = false;

    // Sonic heat haze (Modern effects): while a beam is on, the map up to the projectiles is drawn
    // into hazeTex_, copied to the target and bent along the beams (Effects::buildHaze).
    SDL_Texture* hazeTex_ = nullptr;
    std::vector<SDL_Vertex> hazeV_;
    std::vector<int> hazeI_;
    // Bloom: the battlefield up to the lights is drawn into sceneTex_ (view size), copied to the
    // target, and its bloom added.
    SDL_Texture* sceneTex_ = nullptr;
    bool sceneHdr_ = false;  // sceneTex_ is the half-float HDR target
    std::vector<SDL_Vertex> collapseV_;
    std::vector<int> collapseI_;

    std::unique_ptr<Effects> effects_;
    std::unique_ptr<SoftShroud> soft_;
    double effectClock_ = -1, lastClock_ = -1;
    // Emission layers of the buildings drawn this frame (art pack .glow.png; docs/enhanced-graphics.md
    // "Emission"): queued by blitScreen while glowK_ > 0, added after the time of day's grade.
    // Sprite shadows that follow the sun (Modern lighting, render::spriteShadowWarp; set per frame in
    // draw()). The object being drawn sets its ground contact point and half footprint (screen px)
    // with ShadowPivotScope; blitScreen / blitBuildup then warp its shadow layers around it.
    SpriteShadowWarp shadowWarp_;
    struct ShadowPivot { bool on = false; float x = 0, y = 0, hw = 0, hh = 0; } shadowPivot_;
    struct ShadowPivotScope {
        ShadowPivot& p;
        ShadowPivotScope(ShadowPivot& pv, const Camera& cam, float wx, float wy, float hw, float hh);
        ~ShadowPivotScope() { p.on = false; }
    };
    // The shadow mask t (drawn at dst; rows from v0 of the texture down; rotated by degrees about
    // dst + centre like the sprite) warped by shadowWarp_ around shadowPivot_.
    void drawShadowWarped(SDL_Texture* t, float x, float y, float w, float h, float v0, float degrees, float cx, float cy,
                          bool rendered3d);
    std::vector<SDL_Vertex> shadowV_;
    std::vector<int> shadowI_;
    struct GlowDraw { SDL_Texture* tex; float x, y, w, h; float k; };
    std::vector<GlowDraw> glows_;
    float glowK_ = 0;
    float buildingGlowK(const game::World& w, const game::Building& b) const;

    SDL_Renderer* r_;
    TileAtlas* atlas_;
    SpriteCache& sprites_;
    const formats::Templates& templates_;
};

}  // namespace d2k::render
