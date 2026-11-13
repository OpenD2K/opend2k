// Placement, frame selection and draw order follow the original game's battlefield
// renderer (DUNE2000.DAT 0x427d50 / 0x428e30 / 0x42aa90; see re/notes/rendering.md).

#include "render/map_renderer.hpp"

#include "game/random.hpp"
#include "render/bloom.hpp"
#include "render/buildup_fx.hpp"
#include "render/hdr.hpp"
#include "render/effects.hpp"
#include "render/lighting.hpp"
#include "render/scene_lights.hpp"
#include "render/soft_shroud.hpp"
#include "render/weld_fx.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace d2k::render {

namespace {
constexpr int kWallFrameMapping[16] = {0, 2, 3, 12, 1, 6, 11, 7, 4, 13, 5, 9, 15, 10, 8, 14};
constexpr int kSpecialBehaviorWall = 14;
constexpr int kSpecialBehaviorWallTurret = 16;
constexpr int kSpecialBehaviorSandworm = 5;  // unit behaviour
constexpr int kSpecialBehaviorSilo = 5;      // building behaviour

// DATA.R16 technical frames in Graphics__LoadR16Resource 0x4606d0 order (re/notes/rendering.md §0b).
constexpr int kFrameRepairIcon = 3;          // DAT_006dc468
constexpr int kFrameBracketTL = 5;           // DAT_007951c8
constexpr int kFrameBracketTR = 6;           // DAT_00795338
constexpr int kFrameBracketBL = 7;           // DAT_0078fb28
constexpr int kFrameBracketBR = 8;           // DAT_0078fb2c
constexpr int kFrameHealthBar = 9;           // DAT_00789198[6]
constexpr int kFramePipEmpty = 15;           // DAT_006d8d88
constexpr int kFramePipFull = 16;            // DAT_006d8d8c
constexpr int kFrameGroupDigit = 17;         // DAT_006e2df0[10]; [1] = DAT_006e2df4 (PRIMARY too)
constexpr int kFrameAirliftIcon = 112;       // DAT_00782060 (0x782058[2])
constexpr int kFrameRockCrater = 114;        // DAT_006d7be8[i*2], 32 frames (cell flag 0x8000)
constexpr int kFrameSandCrater = 146;        // DAT_006d7bec[i*2], 32 frames (cell flag 0x10000)
constexpr int kSelectionBoxHeight[6] = {19, 24, 32, 64, 96, 32};  // 0x4e3e48
}  // namespace

MapRenderer::MapRenderer(SDL_Renderer* r, TileAtlas& atlas, SpriteCache& sprites, const formats::Templates& templates)
    : r_(r), atlas_(&atlas), sprites_(sprites), templates_(templates) {}

void MapRenderer::setModernEffects(bool on) {
    if (on == (effects_ != nullptr)) return;
    if (on) {
        effects_ = std::make_unique<Effects>(r_, templates_);
        effects_->setHouseColours(sprites_.colours());
    }
    else effects_.reset();
    lastClock_ = -1;
}

void MapRenderer::resetEffects() {
    if (effects_) effects_->reset();
    lastClock_ = -1;
}

int MapRenderer::effectParticles() const { return effects_ ? effects_->particleCount() : 0; }

void MapRenderer::updateEffects(const game::World& w, const Camera& viewCam, int viewW, int viewH) {
    if (!effects_) return;
    // Effect time: game ticks / 20 (real seconds at the default speed); frozen while paused.
    const double clock = effectClock_ >= 0 ? effectClock_ : double(w.gameTicks());
    float dt = lastClock_ < 0 ? 0.0f : float(std::clamp((clock - lastClock_) / 20.0, 0.0, 0.25));
    if (clock < lastClock_) {  // a load or restart: start over
        effects_->reset();
        dt = 0;
    }
    lastClock_ = clock;
    effects_->update(w, *atlas_, dt, viewCam, viewW, viewH, showShroud);
}

void MapRenderer::draw(const game::World& w, const game::Scenario& s, const Camera& viewCam, int viewW, int viewH) {
    Camera cam = viewCam;
    shadowWarp_ = lighting && !classicSelection ? spriteShadowWarp(*lighting) : SpriteShadowWarp{};
    static const bool noWarp = std::getenv("D2K_NO_SHADOW_WARP") != nullptr;  // check: the drawn shadows as before
    if (noWarp) shadowWarp_.active = false;
    if (effects_) {
        updateEffects(w, viewCam, viewW, viewH);
        cam.x += effects_->shakeX();
        cam.y += effects_->shakeY();
    }
    // Bloom (Modern lighting): the battlefield up to the lights into an offscreen copy of the view,
    // which is then copied to the real target with its bloom added (the haze below nests in it).
    SDL_Texture* bloomTarget = nullptr;
    SDL_Rect bloomVp{}, bloomClip{};
    bool bloomClipOn = false, bloomVpSet = false, bloomOn = false;
    if (bloom && lighting && lighting->bloom > 0.001f) {
        float scX = 1, scY = 1;
        SDL_GetRenderScale(r_, &scX, &scY);
        SDL_GetRenderViewport(r_, &bloomVp);
        if (scX == 1 && scY == 1 && bloomVp.w > 0 && bloomVp.h > 0) {
            float tw = 0, th = 0;
            if (sceneTex_) SDL_GetTextureSize(sceneTex_, &tw, &th);
            const bool wantHdr = hdr && hdr->available();
            if (!sceneTex_ || int(tw) != bloomVp.w || int(th) != bloomVp.h || sceneHdr_ != wantHdr) {
                if (sceneTex_) SDL_DestroyTexture(sceneTex_);
                sceneTex_ = wantHdr ? Hdr::createTarget(r_, bloomVp.w, bloomVp.h) : nullptr;
                sceneHdr_ = sceneTex_ != nullptr;
                if (!sceneTex_)
                    sceneTex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, bloomVp.w, bloomVp.h);
                if (sceneTex_) SDL_SetTextureBlendMode(sceneTex_, SDL_BLENDMODE_NONE);
            }
            if (sceneTex_) {
                bloomTarget = SDL_GetRenderTarget(r_);
                bloomVpSet = SDL_RenderViewportSet(r_);
                bloomClipOn = SDL_RenderClipEnabled(r_);
                if (bloomClipOn) SDL_GetRenderClipRect(r_, &bloomClip);
                SDL_SetRenderTarget(r_, sceneTex_);
                SDL_SetRenderViewport(r_, nullptr);
                SDL_SetRenderClipRect(r_, bloomClipOn ? &bloomClip : nullptr);
                SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
                SDL_RenderClear(r_);
                bloomOn = true;
            }
        }
    }
    // Sonic heat haze: draw the battlefield up to the projectiles into an offscreen copy of the
    // view (same size, same clip), so it can be bent under the beams before they are drawn.
    SDL_Texture* hazeTarget = nullptr;
    SDL_Rect hazeVp{}, hazeClip{};
    bool hazeClipOn = false, hazeVpSet = false;
    if (effects_ && effects_->hasHaze()) {
        float scX = 1, scY = 1;
        SDL_GetRenderScale(r_, &scX, &scY);
        SDL_GetRenderViewport(r_, &hazeVp);
        if (scX == 1 && scY == 1 && hazeVp.w > 0 && hazeVp.h > 0) {
            float tw = 0, th = 0;
            if (hazeTex_) SDL_GetTextureSize(hazeTex_, &tw, &th);
            if (!hazeTex_ || int(tw) != hazeVp.w || int(th) != hazeVp.h) {
                if (hazeTex_) SDL_DestroyTexture(hazeTex_);
                hazeTex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, hazeVp.w, hazeVp.h);
                if (hazeTex_) SDL_SetTextureBlendMode(hazeTex_, SDL_BLENDMODE_NONE);
            }
            if (hazeTex_) {
                hazeTarget = SDL_GetRenderTarget(r_);
                hazeVpSet = SDL_RenderViewportSet(r_);
                hazeClipOn = SDL_RenderClipEnabled(r_);
                if (hazeClipOn) SDL_GetRenderClipRect(r_, &hazeClip);
                SDL_SetRenderTarget(r_, hazeTex_);
                SDL_SetRenderViewport(r_, nullptr);
                SDL_SetRenderClipRect(r_, hazeClipOn ? &hazeClip : nullptr);
                SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
                SDL_RenderClear(r_);
            }
        }
    }
    const bool haze = hazeTex_ && SDL_GetRenderTarget(r_) == hazeTex_;
    drawTerrain(w, cam, viewW, viewH);
    if (effects_) effects_->drawGround(cam);

    // Display__DrawInfantryBehindObjects 0x42af00: infantry on a vehicle's cell (flag 0x1000000,
    // not NOT_DRAWN) is drawn before the building bases and the sorted list, i.e. under the
    // vehicle - this is where a crushed soldier's squash animation plays (CrushInfantry sets
    // animation 13 while the tank is still on the cell).
    for (const auto& side : w.sides())
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || !(u.flags & 0x1000000) || (u.flags & 0x200000)) continue;
            if (!templates_.bin.units[u.type].isInfantry) continue;
            drawUnit(w, u, cam);
            if (u.selected) {
                if (classicSelection) drawClassicUnitSelection(w, u, cam);
                else drawSelection(w, u, cam);
            }
        }

    // Pass A (0x427d50): building base frames under everything else. A destroyed building
    // (state 17) keeps being drawn (with the health it had before the killing hit) under its death explosions until
    // UpdateBuilding frees it 30 ticks later; neither blit pass checks the state.
    for (const auto& side : w.sides())
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i))
            if (side.buildings[i].alive) drawBuildingBase(w, side.buildings[i], cam);
    if (effects_) effects_->drawMarks(cam);  // shockwaves, scorch and rubble of destroyed buildings

    drawParticles(w, cam, viewW, viewH);  // 0x45d550 (sandworm sand spray)

    // Crates and spice blooms (BlitVehicles 0x428e30): crateImg[image] = DATA.R16 frame
    // 102 + image (loaded after the 64 shroud frames by 0x4606d0), at the cell corner.
    constexpr int kCrateFirstFrame = 102;
    for (const auto& c : w.crates()) {
        if (!c.active || !w.inBounds(c.x, c.y)) continue;
        if (effects_ && Effects::replacesCrate(c.type)) continue;  // Modern draws the bloom mound
        if (const Sprite* sp = sprites_.get(kCrateFirstFrame + c.image, -1))
            blit(*sp, float(c.x * 32), float(c.y * 32), cam);
    }

    // Pass B (AddToDisplayList 0x42aa90): buildings and ground units sorted by anchor Y.
    // Insertion order is side 0..7, per side buildings then units; an entry is inserted
    // *before* existing entries with an equal key, i.e. ties resolve latest-first.
    struct Entry { int key; int order; const game::Building* b; const game::Unit* u; };
    std::vector<Entry> list;
    int order = 0;
    for (const auto& side : w.sides()) {
        for (int bi = side.firstBuilding; bi >= 0; bi = side.nextObject(bi)) {
            const auto& b = side.buildings[bi];
            if (!b.alive) continue;
            const auto& t = templates_.bin.buildings[b.type];
            int key = b.tileY * 32 + t.artHeight;
            if (t.flags & 0x200000) key -= 32;  // BF_HAS_SKIRT
            list.push_back({key, order++, &b, nullptr});
        }
        for (int ui = side.firstUnit; ui >= 0; ui = side.nextObject(ui)) {
            const auto& u = side.units[ui];
            // Ground units only: not flying/carried (0x140), not hidden (0x200000/0x1000000).
            if (u.alive && !(u.flags & 0x1000000) && !(u.flags & 0x200140))
                list.push_back({game::fixToPixel(u.posY - u.posZ), order++, nullptr, &u});
        }
    }
    std::sort(list.begin(), list.end(),
              [](const Entry& a, const Entry& b) { return a.key != b.key ? a.key < b.key : a.order > b.order; });
    for (const auto& e : list) {
        if (e.b) {
            const Sprite* main = drawBuilding(w, *e.b, cam);
            if (classicSelection && e.b->selected) drawClassicBuildingSelection(w, *e.b, main, cam);
        } else {
            drawUnit(w, *e.u, cam);
            if (e.u->selected) {
                if (classicSelection) drawClassicUnitSelection(w, *e.u, cam);
                else drawSelection(w, *e.u, cam);
            }
        }
    }
    // Layers above the sorted list (rendering.md §4): low explosions, flyers, high explosions, projectiles.
    drawExplosions(w, cam, false);
    drawFlyers(w, cam, 0);  // shadows, carried units and cargo, low flyers
    drawExplosions(w, cam, true);
    drawFlyers(w, cam, 1);  // high flyers
    drawBullets(w, cam);
    if (haze) {  // back to the real target: the copy, then the copy bent along the beams
        SDL_SetRenderTarget(r_, hazeTarget);
        SDL_SetRenderViewport(r_, hazeVpSet ? &hazeVp : nullptr);
        SDL_SetRenderClipRect(r_, hazeClipOn ? &hazeClip : nullptr);
        SDL_SetTextureScaleMode(hazeTex_, SDL_SCALEMODE_NEAREST);
        const SDL_FRect all{0, 0, float(hazeVp.w), float(hazeVp.h)};
        SDL_RenderTexture(r_, hazeTex_, nullptr, &all);
        const float z = cam.zoom, ox = std::round(cam.x * z), oy = std::round(cam.y * z);
        effects_->buildHaze(
            hazeV_, hazeI_,
            [&](float wx, float wy, float& sx, float& sy) {
                sx = wx * z - ox;
                sy = wy * z - oy;
                return true;
            },
            float(hazeVp.w), float(hazeVp.h), false);
        if (!hazeI_.empty()) {
            SDL_SetTextureScaleMode(hazeTex_, SDL_SCALEMODE_LINEAR);
            SDL_RenderGeometry(r_, hazeTex_, hazeV_.data(), int(hazeV_.size()), hazeI_.data(), int(hazeI_.size()));
        }
    }
    // Time of day / Modern lighting: the grade over everything drawn so far (terrain, buildings,
    // units, flyers, projectiles), then the lamps, then the effects' particles and lights.
    const bool graded = lighting && !lighting->identity &&
                        (lightingGrade[0] != 1 || lightingGrade[1] != 1 || lightingGrade[2] != 1);
    if (graded) {
        SDL_BlendMode prev;
        SDL_GetRenderDrawBlendMode(r_, &prev);
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_MOD);
        auto ch = [](float v) { return Uint8(std::clamp(std::lround(v * 255.0f), 0L, 255L)); };
        SDL_SetRenderDrawColor(r_, ch(lightingGrade[0]), ch(lightingGrade[1]), ch(lightingGrade[2]), 255);
        const SDL_FRect all{0, 0, float(viewW), float(viewH)};
        SDL_RenderFillRect(r_, &all);
        SDL_SetRenderDrawBlendMode(r_, prev);
    }
    if (effects_) effects_->setGrade(graded ? lightingGrade : nullptr);
    // The buildings' emission layers (lit windows, lamps, cyan cores), over the graded picture.
    for (const GlowDraw& g : glows_) {
        // up to x2 in two passes (colour mods stop at 255)
        const float k = std::min(2.0f, g.k);
        for (float left = k; left > 0.01f; left -= 1.0f) {
            const Uint8 m = Uint8(std::lround(std::min(1.0f, left) * 255.0f));
            SDL_SetTextureColorMod(g.tex, m, m, m);
            const SDL_FRect d{g.x, g.y, g.w, g.h};
            SDL_RenderTexture(r_, g.tex, nullptr, &d);
        }
        SDL_SetTextureColorMod(g.tex, 255, 255, 255);
    }
    glows_.clear();
    if (sceneLights) {
        static const float kOne[3] = {1, 1, 1};
        sceneLights->setGrade(graded ? lightingGrade : kOne);
        sceneLights->draw2D(cam, viewW, viewH);
    }
    if (effects_) effects_->drawAir(cam);
    if (bloomOn) {  // back to the real target: the picture, then its bloom
        SDL_SetRenderTarget(r_, bloomTarget);
        SDL_SetRenderViewport(r_, bloomVpSet ? &bloomVp : nullptr);
        SDL_SetRenderClipRect(r_, bloomClipOn ? &bloomClip : nullptr);
        SDL_SetTextureScaleMode(sceneTex_, SDL_SCALEMODE_NEAREST);
        const SDL_FRect all{0, 0, float(bloomVp.w), float(bloomVp.h)};
        // HDR: the bloom of the unclamped picture, both through the tone map (render/hdr.*).
        SDL_Texture* glow = sceneHdr_ ? bloom->compute(sceneTex_, bloomVp.w, bloomVp.h, lighting->bloomThreshold) : nullptr;
        if (!(sceneHdr_ && hdr && hdr->compose(sceneTex_, bloomVp.w, bloomVp.h, glow, lighting->bloom, all, true))) {
            SDL_RenderTexture(r_, sceneTex_, nullptr, &all);
            bloom->apply(sceneTex_, bloomVp.w, bloomVp.h, all, lighting->bloom, lighting->bloomThreshold);
        }
    }
    if (!classicSelection) drawHealthBars(w, cam);
    if (showShroud) {
        if (softShroud) {
            if (!soft_) soft_ = std::make_unique<SoftShroud>(r_, sprites_);
            soft_->draw(w, cam, viewW, viewH);
        } else {
            drawShroud(w, cam, viewW, viewH);
        }
    }
    if (effects_) effects_->drawPost(viewW, viewH);
    if (showMarkers)
        for (const auto& o : s.objects)
            if (o.kind != game::ObjectKind::Building && o.kind != game::ObjectKind::Unit) drawMarker(o, cam);
}

MapRenderer::~MapRenderer() {
    if (lensTex_) SDL_DestroyTexture(lensTex_);
    if (weldTex_) SDL_DestroyTexture(weldTex_);
    if (rockTex_) SDL_DestroyTexture(rockTex_);
    if (puffTex_) SDL_DestroyTexture(puffTex_);
    if (hazeTex_) SDL_DestroyTexture(hazeTex_);
    if (sceneTex_) SDL_DestroyTexture(sceneTex_);
    for (auto* t : shroudTex_)
        if (t) SDL_DestroyTexture(t);
}

// Shroud edge piece: 8bpp values 0..31 are subtracted from each 5-bit colour channel by the
// original blitter. Stored as grey (v*8) and drawn with a reverse-subtract blend: dst - src.
SDL_Texture* MapRenderer::shroudTexture(int shroudByte) {
    if (shroudTex_.empty()) shroudTex_.assign(256, nullptr);
    if (shroudTex_[shroudByte]) return shroudTex_[shroudByte];
    const formats::R16Frame* f = sprites_.frame(game::World::kShroudFirstFrame + shroudByte);
    if (!f || f->empty() || f->bpp != 8) return nullptr;
    std::vector<std::uint32_t> px(static_cast<std::size_t>(f->width) * f->height);
    for (std::size_t i = 0; i < px.size(); ++i) {
        const std::uint32_t v = std::min<std::uint32_t>(f->pixels[i], 31) * 255 / 31;
        px[i] = 0xFF000000u | v << 16 | v << 8 | v;
    }
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, f->width, f->height);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), f->width * 4);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    static const SDL_BlendMode kSubtract = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    if (!SDL_SetTextureBlendMode(t, kSubtract)) {
        // Fallback if the backend lacks custom blending: approximate with alpha darkening.
        for (auto& p : px) p = (p & 0xFF) << 24;
        SDL_UpdateTexture(t, nullptr, px.data(), f->width * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    shroudTex_[shroudByte] = t;
    return t;
}

// Shroud pass (after projectiles): low nibble 1 = fully shrouded (black), 2..15 = edge piece
// DATA.R16 frame 38 + byte (re/notes/shroud.md), 0 = clear.
void MapRenderer::drawShroud(const game::World& w, const Camera& cam, int viewW, int viewH) {
    const double ts = 32.0 * cam.zoom;
    const int x0 = std::max(0, int(std::floor(cam.x / 32))), y0 = std::max(0, int(std::floor(cam.y / 32)));
    const int x1 = std::min(w.width(), x0 + int(viewW / ts) + 2), y1 = std::min(w.height(), y0 + int(viewH / ts) + 2);
    const double ox = std::round(cam.x * cam.zoom), oy = std::round(cam.y * cam.zoom);
    auto edge = [&](int t, double o) { return float(std::round(t * ts) - o); };
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const int s = w.cell(x, y).shroud;
            const int low = s & 0xF;
            if (low == 0) continue;
            const SDL_FRect dst{edge(x, ox), edge(y, oy), edge(x + 1, ox) - edge(x, ox), edge(y + 1, oy) - edge(y, oy)};
            if (low == 1) SDL_RenderFillRect(r_, &dst);
            else if (SDL_Texture* t = shroudTexture(s)) SDL_RenderTexture(r_, t, nullptr, &dst);
        }
}

// Blitexplosions 0x4282f0: pass 0 draws height < 20 px, pass 1 the rest.
void MapRenderer::drawExplosions(const game::World& w, const Camera& cam, bool high) {
    using namespace game::explosion_flags;
    for (const auto& side : w.sides())
        for (int i = side.firstExplosion; i >= 0; i = side.nextObject(i)) {
            const auto& e = side.explosions[i];
            if (!e.alive || (e.flags & (kHidden | kBlinkOff)) || e.flags & 0x800) continue;
            if ((game::fixToPixel(e.posZ) >= 20) != high) continue;
            if (e.type < 0 || e.type >= formats::kMaxExplosions) continue;
            // Modern effects draw their own explosions, muzzle flashes and smoke (Effects::replacesExplosion).
            if (effects_ && effects_->replacesExplosion(e.type)) continue;
            const int house = (e.flags & 0x8) ? side.house : -1;
            const int frame = templates_.animationArtFirstFrame[e.type] + e.frame;
            // Blit selection in Blitexplosions order: 2 subtract, 4 brighten, 0x10 50 %, 0x40 red
            // tint, 0x80 green tint, 0x200 additive, else normal. The arithmetic ones (2/4/0x40/0x80)
            // treat the raw index as an amount, not a colour.
            Blend mode = Blend::Normal;
            if (e.flags & 0x2) mode = Blend::Subtract;
            else if (e.flags & 0x4) mode = Blend::Brighten;
            else if (e.flags & 0x10) mode = Blend::Half;
            else if (e.flags & 0x40) mode = Blend::RedTint;
            else if (e.flags & 0x80) mode = Blend::GreenTint;
            else if (e.flags & 0x200) mode = Blend::Additive;
            const bool intensity = mode == Blend::Subtract || mode == Blend::Brighten || mode == Blend::RedTint ||
                                   mode == Blend::GreenTint;
            const Sprite* sp = intensity ? sprites_.getIntensity(frame) : sprites_.get(frame, house);
            if (!sp) continue;
            blit(*sp, float(game::fixToPixel(e.posX) - sp->frame->x),
                 float(game::fixToPixel(e.posY - e.posZ) - sp->frame->y), cam, mode);
        }
}

// BlitBullets 0x428070.
void MapRenderer::drawBullets(const game::World& w, const Camera& cam) {
    using namespace game::weapon_flags;
    for (const auto& side : w.sides())
        for (int i = side.firstBullet; i >= 0; i = side.nextObject(i)) {
            const auto& b = side.bullets[i];
            if (!b.alive || b.delay != 0 || b.type < 0 || b.type >= formats::kMaxWeapons) continue;
            // Modern effects draw their own projectiles (tracers, shells, rockets, the sonic beam
            // instead of the shot's lens; Effects::replacesBullet).
            if (effects_ && ((b.flags & kRangeLimited) || effects_->replacesBullet(b.type))) continue;
            const int art = templates_.bin.weapons[b.type].projectileArt;
            if (art >= formats::kMaxWeapons) continue;
            const Sprite* sp = sprites_.get(templates_.projectileArtFirstFrame[art] + b.animFrame, -1);
            if (!sp) continue;
            const int x = game::fixToPixel(b.posX) - sp->frame->x, y = game::fixToPixel(b.posY) - sp->frame->y;
            const int yz = y - game::fixToPixel(b.posZ);
            // Flag 0x800: SetLensImage first overwrites the projectile image with the frame drawn so
            // far, bent through the lens; the shadow and the image blit follow (in that order).
            bool lens = false;
            SDL_FRect lensDst{};
            if (b.flags & kRangeLimited) {
                lensDst = {std::round(x * cam.zoom - std::round(cam.x * cam.zoom)),
                           std::round(yz * cam.zoom - std::round(cam.y * cam.zoom)), 0, 0};
                const int size = int(std::round(sp->frame->width * cam.zoom));
                lensDst.w = lensDst.h = float(size);
                lens = buildLens(*sp->frame, lensDst.x, lensDst.y, size);
                if (!lens) continue;
            }
            if (b.flags & kShadow) blit(*sp, float(x), float(y), cam, Blend::Silhouette);
            if (lens) {
                SDL_SetTextureBlendMode(lensTex_, (b.flags & kAdditive) ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
                const SDL_FRect src{0, 0, lensDst.w, lensDst.h};
                SDL_RenderTexture(r_, lensTex_, &src, &lensDst);
                continue;
            }
            blit(*sp, float(x), float(yz), cam, (b.flags & kAdditive) ? Blend::Additive : Blend::Normal);
        }
}

// Display__InitLensTable 0x42ace0 (called with 16 by the mission loader 0x469725, and with the SOUND
// projectile image's width when DATA.R16 is loaded 0x467036): for every pixel (x, y) of an N x N
// square, r = N >> 1, dx = x - r, dy = y - r: outside the circle (sqrt(dx^2 + dy^2) > r) the pixel
// samples itself, inside it samples (r - ftol(dx * -2/3), r - ftol(dy * -2/3)) - a 1.5x magnifying
// lens. ftol truncates; with the CRT's 53-bit x87 precision a multiple of 3 gives an exact integer,
// so the value is r + (2 * d) / 3 in C++ integer division (truncation towards zero).
namespace {
struct LensTable {
    std::uint8_t sx[256], sy[256];
    LensTable() {
        constexpr int n = 16, r = n >> 1;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const int dx = x - r, dy = y - r, i = y * n + x;
                const bool outside = dx * dx + dy * dy > r * r;  // sqrt(d2) > r for integers
                sx[i] = static_cast<std::uint8_t>(outside ? x : r + (2 * dx) / 3);
                sy[i] = static_cast<std::uint8_t>(outside ? y : r + (2 * dy) / 3);
            }
    }
};
const LensTable& lensTable() {
    static const LensTable t;
    return t;
}
}  // namespace

// Image__SetLensImage 0x42ada0 (re/notes/rendering.md §6a). The original copies screen pixels into
// the shared projectile TImage: image[y][x] = screen[py + ty][px + tx] with (tx, ty) = lens table
// [y * N + x], but only when the whole N x N square lies on the screen surface (px, py >= 0,
// px + N <= width, py + N <= height); otherwise the image keeps what the last lens left in it (at
// first the R16 frame itself, the blue square). BlitBullets then blits that image normally
// (16-bit: 0xF81F transparent, 0x001F halves the destination, anything else is copied). Here the
// frame drawn so far is read back (the render target: classicTarget_ in Original), bent on the
// CPU, uploaded into lensTex_ and drawn by the caller. At a size other than 16 screen pixels
// (Enhanced zoom) the same lens is applied continuously: a pixel at lens offset d (in frame
// pixels from the centre) inside the radius samples r + d * 2/3.
bool MapRenderer::buildLens(const formats::R16Frame& f, float sx, float sy, int size) {
    constexpr int n = 16;
    if (f.width != n || f.height != n || f.bpp != 16 || size <= 0) return false;  // 0x42ada0 needs <= 16
    float scaleX = 1, scaleY = 1;
    SDL_GetRenderScale(r_, &scaleX, &scaleY);
    if (scaleX != 1 || scaleY != 1) return false;
    if (lensImage_.empty()) {  // the TImage as loaded: the frame's own pixels (0 = transparent)
        lensImage_.resize(n * n);
        for (int i = 0; i < n * n; ++i) {
            const auto c = static_cast<std::uint16_t>(f.pixels[i * 2] | f.pixels[i * 2 + 1] << 8);
            lensImage_[i] = c == 0 ? 0u : formats::rgb555ToRgba(c);
        }
    }
    if (!lensTex_ || lensTexSize_ < size) {
        if (lensTex_) SDL_DestroyTexture(lensTex_);
        lensTexSize_ = std::max(size, n);
        lensTex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, lensTexSize_,
                                     lensTexSize_);
        if (!lensTex_) { lensTexSize_ = 0; return false; }
        SDL_SetTextureScaleMode(lensTex_, SDL_SCALEMODE_NEAREST);
    }
    // The square on the whole render target (the original's screen surface: options bar,
    // battlefield and sidebar), not just the battlefield viewport.
    SDL_Rect vp{};
    SDL_GetRenderViewport(r_, &vp);
    int outW = 0, outH = 0;
    SDL_GetCurrentRenderOutputSize(r_, &outW, &outH);
    const SDL_Rect abs{vp.x + int(sx), vp.y + int(sy), size, size};
    const bool inside = abs.x >= 0 && abs.y >= 0 && abs.x + size <= outW && abs.y + size <= outH;
    SDL_Surface* got = nullptr;
    if (inside) {
        // Read in absolute target pixels: with the viewport reset, rect == absolute (SDL 3.4).
        SDL_Rect clip{};
        const bool clipped = SDL_RenderClipEnabled(r_);
        if (clipped) SDL_GetRenderClipRect(r_, &clip);
        SDL_SetRenderViewport(r_, nullptr);
        got = SDL_RenderReadPixels(r_, &abs);
        SDL_SetRenderViewport(r_, &vp);
        SDL_SetRenderClipRect(r_, clipped ? &clip : nullptr);
    }
    std::vector<std::uint32_t>& src = lensScratch_;
    if (got) {
        src.resize(static_cast<std::size_t>(size) * size);
        const bool ok = got->w == size && got->h == size &&
                        SDL_ConvertPixels(size, size, got->format, got->pixels, got->pitch, SDL_PIXELFORMAT_ABGR8888,
                                          src.data(), size * 4);
        SDL_DestroySurface(got);
        if (!ok) got = nullptr;
    }
    const LensTable& t = lensTable();
    if (size == n) {
        if (got)
            for (int i = 0; i < n * n; ++i) lensImage_[i] = src[t.sy[i] * n + t.sx[i]] | 0xFF000000u;
        // Off the surface: the image keeps the last lens (or the frame) - drawn as it is.
        const SDL_Rect dst{0, 0, n, n};
        SDL_UpdateTexture(lensTex_, &dst, lensImage_.data(), n * 4);
        return true;
    }
    if (!got) return false;  // Enhanced zoom: no stale image, nothing to show
    std::vector<std::uint32_t> out(static_cast<std::size_t>(size) * size);
    const float k = float(size) / n, r = n / 2.0f;
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const float u = (i + 0.5f) / k - r, v = (j + 0.5f) / k - r;
            int si = i, sj = j;
            if (u * u + v * v <= r * r) {
                si = std::clamp(int(std::floor((r + u * (2.0f / 3.0f)) * k)), 0, size - 1);
                sj = std::clamp(int(std::floor((r + v * (2.0f / 3.0f)) * k)), 0, size - 1);
            }
            out[static_cast<std::size_t>(j) * size + i] = src[static_cast<std::size_t>(sj) * size + si] | 0xFF000000u;
        }
    const SDL_Rect dst{0, 0, size, size};
    SDL_UpdateTexture(lensTex_, &dst, out.data(), size * 4);
    return true;
}

// FUN_0042a5d0: flying units with a ground shadow, drawn above the sorted list.
// Display__DrawFlyingUnits 0x42a5d0 (rendering.md §5). Pass 0: loop 1 over units with
// FLYING or CARRIED (0x140) and not NOT_DRAWN: flyers (not the death hand) leave a shadow at
// ground level, carried units (a harvester under a carryall: dragCargo keeps it at the carryall's
// position, PosZ - 10 px) are drawn raised by PosZ; a delivery carryall bringing one unit (flag
// 0x20000000, e.g. the free harvester) also draws that unit's body - type = its delivery slot's
// first unit (CSide +0x262f4 + slot * 0x38, slot = Unit +0x2c), with the carryall's animation
// frame and facing - 10 px below it. Then loop 2: the flyers below 20 px (pass 0) or from 20 px
// up (pass 1), with the group number.
void MapRenderer::drawFlyers(const game::World& w, const Camera& cam, int pass) {
    auto bodySprite = [&](int type, int animFrame, int facing, int house) -> const Sprite* {
        if (type < 0 || type >= formats::kMaxUnitTypes) return nullptr;
        const auto& t = templates_.bin.units[type];
        if (t.unitArt < 0 || t.unitArt >= formats::kMaxUnitArt) return nullptr;
        const int dirs = templates_.bin.unitArtDirectionFrames[t.unitArt];
        return sprites_.get(templates_.unitArtFirstFrame[t.unitArt] + animFrame * dirs + t.directionFrames[facing & 31], house);
    };
    auto height = [](std::int32_t z) { return (game::fixToPixel(z)) & 0xff; };
    if (pass == 0)
        for (const auto& side : w.sides())
            for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
                const auto& u = side.units[i];
                if (!u.alive || !(u.flags & 0x140) || (u.flags & 0x200000)) continue;
                const Sprite* sp = bodySprite(u.type, u.animFrame, u.facing, side.house);
                if (!sp) continue;
                const float x = unitPx(u.posX), gy = unitPx(u.posY);
                if (!(u.flags & 0x40) || (u.flags & 0x20000)) blitUnit(*sp, x, gy - float(height(u.posZ)), cam);
                else blitUnit(*sp, x, gy, cam, Blend::Silhouette);
                if (u.flags & 0x20000000) {
                    const auto& d = side.deliveries[std::size_t(u.lastMoved % 10)];
                    const int cargo = d.units[0];
                    // After the drop the slot holds 0xFE (the original then has no image).
                    if (cargo >= 0xfe) continue;
                    if (const Sprite* cs = bodySprite(cargo, u.animFrame, u.facing, side.house))
                        blitUnit(*cs, unitPx(u.posX), unitPx(u.posY) - float(height(u.posZ - 0xa0000)), cam);
                }
            }
    for (const auto& side : w.sides())
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || !(u.flags & 0x40) || (u.flags & 0x200000)) continue;
            if ((pass == 0) != (u.posZ < 0x140000)) continue;
            const Sprite* sp = bodySprite(u.type, u.animFrame, u.facing, side.house);
            if (!sp) continue;
            blitUnit(*sp, unitPx(u.posX), unitPx(u.posY) - float(height(u.posZ)), cam);
            if (u.selected && !classicSelection) drawSelection(w, u, cam);  // 0x42a5d0 draws no box
        }
}

void MapRenderer::drawTerrain(const game::World& s, const Camera& cam, int viewW, int viewH) {
    const double ts = TileAtlas::kTileSize * double(cam.zoom);
    const int x0 = std::max(0, static_cast<int>(std::floor(cam.x / TileAtlas::kTileSize)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cam.y / TileAtlas::kTileSize)));
    const int x1 = std::min(s.width(), x0 + static_cast<int>(viewW / ts) + 2);
    const int y1 = std::min(s.height(), y0 + static_cast<int>(viewH / ts) + 2);

    // Snap the camera origin to whole pixels and derive every tile edge from the same
    // rounding so adjacent tiles always share an exact pixel boundary (no seams).
    const double originX = std::round(cam.x * double(cam.zoom));
    const double originY = std::round(cam.y * double(cam.zoom));
    auto edge = [&](int t, double origin) { return float(std::round(t * ts) - origin); };

    if (terrainLayer && terrainLayer->drawTerrain(r_, cam, viewW, viewH)) {
        // Designed terrain + Modern effects: modern craters (same cells and levels) instead of
        // the original crater sprites (render/effects_craters.cpp).
        if (effects_) effects_->drawCraters(s, *atlas_, cam, x0, y0, x1, y1, showShroud);
        else drawDecals(s, cam, x0, y0, x1, y1);
        return;
    }
    for (int ty = y0; ty < y1; ++ty) {
        const float top = edge(ty, originY), bottom = edge(ty + 1, originY);
        for (int tx = x0; tx < x1; ++tx) {
            const int tile = s.cell(tx, ty).tile;
            if (tile >= atlas_->tileCount()) continue;
            const SDL_FRect src = atlas_->sourceRect(tile, cam.zoom);
            const float left = edge(tx, originX), right = edge(tx + 1, originX);
            const SDL_FRect dst{left, top, right - left, bottom - top};
            SDL_RenderTexture(r_, atlas_->texture(cam.zoom), &src, &dst);
        }
    }
    drawDecals(s, cam, x0, y0, x1, y1);
}

// Terrain damage decals (Display__DrawTerrain 0x423d20, right after each cell's tile): when
// cell +9 > 0x10, image (damage >> 4) + 16 * ((x + y) & 1) of the rock crater set (cell flag
// 0x8000 BuildOn) or else the sand crater set (0x10000 Sand); other terrain gets none. Drawn at
// the cell corner plus a per-diagonal jitter (x + y) & 15 from two 16-entry tables filled with
// rand() % 12 - 12 at startup (Setup__LoadGraphicsFile 0x466140), no hotspot. A decal only
// reaches up/left into cells drawn before it, so drawing all decals after all tiles (in the
// same row-major order) gives the same picture as the interleaved original.
void MapRenderer::drawDecals(const game::World& w, const Camera& cam, int x0, int y0, int x1, int y1) {
    if (!decalJitterInit_) {
        // The tables are filled right after gThickSpicePattern (64 x rand() & 3). Our fixed
        // pattern (spice_tables.hpp) matches MSVC rand() from seed 1 after 143137 (mod 2^18)
        // draws; take that state. UNCERTAIN: the real boot-time RNG state (only its low 18 bits
        // are pinned down by the pattern), so the jitter values themselves are a best guess.
        game::Random r(1);
        for (int i = 0; i < 143137 + 64; ++i) r.next();
        for (int i = 0; i < 16; ++i) {
            decalJitterX_[i] = static_cast<int>(static_cast<std::uint32_t>(r.next()) % 12) - 12;
            decalJitterY_[i] = static_cast<int>(static_cast<std::uint32_t>(r.next()) % 12) - 12;
        }
        decalJitterInit_ = true;
    }
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto& c = w.cell(x, y);
            if (c.damage <= 0x10) continue;
            if (showShroud && (c.shroud & 0xF) == 1) continue;  // the original skips shrouded cells
            const int idx = (c.damage >> 4) + 16 * ((x + y) & 1);
            int frame;
            if (c.flags & 0x8000) frame = kFrameRockCrater + idx;
            else if (c.flags & 0x10000) frame = kFrameSandCrater + idx;
            else continue;
            const int j = (x + y) & 15;
            if (const Sprite* sp = sprites_.get(frame, -1))
                blit(*sp, float(x * 32 + decalJitterX_[j]), float(y * 32 + decalJitterY_[j]), cam);
        }
}

// Display__DrawParticles 0x45d550 (after the building bases, before crates): every active
// particle is one screen pixel at (ROUND(x), ROUND(y)) (FPU round-to-nearest), skipped under
// full shroud. Its colour is the tile pixel the worm stood on, each RGB555 channel * 0.85
// truncated (FUN_0045d650).
void MapRenderer::drawParticles(const game::World& w, const Camera& cam, int viewW, int viewH) {
    if (w.particleCount() == 0) return;
    if (effects_ && Effects::replacesWormParticles()) return;  // Modern: the worm's own sand (effects_events.cpp)
    const float z = cam.zoom;
    const float ox = std::round(cam.x * z), oy = std::round(cam.y * z);
    const float px = std::max(1.0f, std::round(z));
    const int saved = std::fegetround();
    std::fesetround(FE_TONEAREST);
    for (const auto& p : w.particles()) {
        if (!p.active) continue;
        const int ix = static_cast<int>(std::nearbyint(p.x)), iy = static_cast<int>(std::nearbyint(p.y));
        if (!w.inBounds(ix >> 5, iy >> 5)) continue;
        if (showShroud && (w.cell(ix >> 5, iy >> 5).shroud & 0xF) == 1) continue;
        const float sx = std::round(ix * z) - ox, sy = std::round(iy * z) - oy;
        if (sx < 0 || sy < 0 || sx >= viewW || sy >= viewH) continue;
        const formats::Rgba c = atlas_->pixel(p.tile, p.tilePx, p.tilePy);
        auto scale = [](std::uint32_t v8) {
            const std::uint32_t v5 = static_cast<std::uint32_t>(static_cast<float>(v8 >> 3) * 0.85f);
            return static_cast<Uint8>((v5 << 3) | (v5 >> 2));
        };
        SDL_SetRenderDrawColor(r_, scale(c & 0xFF), scale((c >> 8) & 0xFF), scale((c >> 16) & 0xFF), 255);
        const SDL_FRect rc{sx, sy, px, px};
        SDL_RenderFillRect(r_, &rc);
    }
    std::fesetround(saved);
}

void MapRenderer::blit(const Sprite& sp, float worldX, float worldY, const Camera& cam, Blend mode, float degrees,
                       float pivotX, float pivotY) {
    const float sx = std::round(worldX * cam.zoom - std::round(cam.x * cam.zoom));
    const float sy = std::round(worldY * cam.zoom - std::round(cam.y * cam.zoom));
    blitScreen(sp, sx, sy, cam.zoom, mode, degrees, {(pivotX - worldX) * cam.zoom, (pivotY - worldY) * cam.zoom});
}

// Units: the anchor (the unit position) is rounded onto the screen grid once and the frame
// offset separately, so body, turret and carried cargo keep a fixed whole-pixel layout around
// the anchor. Rounding (anchor - offset) * zoom as one value made the turret hop a pixel
// against the body (and the whole sprite against its anchor) whenever the anchor's parity
// changed at a fractional zoom. Identical to blit() at integer zoom and whole-pixel anchors
// (the Original graphics mode).
void MapRenderer::blitUnit(const Sprite& sp, float anchorX, float anchorY, const Camera& cam, Blend mode,
                           float degrees) {
    const float ox = std::round(float(sp.frame->x) * cam.zoom), oy = std::round(float(sp.frame->y) * cam.zoom);
    const float sx = std::round(anchorX * cam.zoom - std::round(cam.x * cam.zoom)) - ox;
    const float sy = std::round(anchorY * cam.zoom - std::round(cam.y * cam.zoom)) - oy;
    blitScreen(sp, sx, sy, cam.zoom, mode, degrees, {ox, oy});  // rotated about the anchor
}

void MapRenderer::blitScreen(const Sprite& sp, float sx, float sy, float zoom, Blend mode, float degrees,
                             SDL_FPoint centre) {
    const SDL_FRect dst{sx, sy, std::round(sp.drawW * zoom), std::round(sp.drawH * zoom)};
    const Sprite::Level& lv = sp.levels.empty() ? Sprite::Level{sp.texture, sp.shadow, 1} : sp.level(zoom);
    SDL_Texture* tex = lv.texture;
    auto draw = [&](SDL_Texture* t) {
        if (degrees != 0) SDL_RenderTextureRotated(r_, t, nullptr, &dst, degrees, &centre, SDL_FLIP_NONE);
        else SDL_RenderTexture(r_, t, nullptr, &dst);
    };
    switch (mode) {
    case Blend::Normal:
    case Blend::Flash:
        if (lv.shadow) {
            if (shadowPivot_.on && shadowWarp_.active) drawShadowWarped(lv.shadow, dst.x, dst.y, dst.w, dst.h, 0, degrees, centre.x, centre.y, sp.rendered3d);
            else draw(lv.shadow);
        }
        draw(tex);
        if (lv.glow && glowK_ > 0.01f && degrees == 0) glows_.push_back({lv.glow, dst.x, dst.y, dst.w, dst.h, glowK_});
        if (mode == Blend::Flash) {  // grey hit flash (0x4326b0): brighten towards grey
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(tex, 110, 110, 110);
            draw(tex);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        }
        break;
    case Blend::Additive:
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
        draw(tex);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        if (lv.glow && glowK_ > 0.01f && degrees == 0) glows_.push_back({lv.glow, dst.x, dst.y, dst.w, dst.h, glowK_});
        break;
    case Blend::Half:
        SDL_SetTextureAlphaMod(tex, 128);
        draw(tex);
        SDL_SetTextureAlphaMod(tex, 255);
        break;
    case Blend::Subtract:
    case Blend::Brighten:
    case Blend::RedTint:
    case Blend::GreenTint: {  // SpriteCache::getIntensity: dst +/- index per channel, clamped
        static const SDL_BlendMode kSub = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT, SDL_BLENDFACTOR_ZERO,
            SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        static const SDL_BlendMode kAdd = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
            SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        // Channel masks (r, g, b) for the add and subtract passes.
        int add = 0, sub = 0;
        if (mode == Blend::Subtract) sub = 7;
        else if (mode == Blend::Brighten) add = 7;
        else if (mode == Blend::RedTint) { add = 1; sub = 6; }
        else { add = 2; sub = 1; }
        auto pass = [&](int mask, SDL_BlendMode bm) {
            if (!mask || !SDL_SetTextureBlendMode(tex, bm)) return;
            SDL_SetTextureColorMod(tex, mask & 1 ? 255 : 0, mask & 2 ? 255 : 0, mask & 4 ? 255 : 0);
            draw(tex);
        };
        pass(add, kAdd);
        pass(sub, kSub);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        break;
    }
    case Blend::Silhouette:  // ground shadow of flyers / arcing projectiles: halve destination
        SDL_SetTextureColorMod(tex, 0, 0, 0);
        SDL_SetTextureAlphaMod(tex, 128);
        draw(tex);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        break;
    }
}

MapRenderer::ShadowPivotScope::ShadowPivotScope(ShadowPivot& pv, const Camera& cam, float wx, float wy, float hw, float hh)
    : p(pv) {
    const float z = cam.zoom;
    p = {true, wx * z - std::round(cam.x * z), wy * z - std::round(cam.y * z), hw * z, hh * z};
}

// Sun-following sprite shadows (docs/enhanced-graphics.md "Sun-following sprite shadows"): a point p of
// the shadow layer lies s further along the art's shadow direction d0 than the object's footprint
// (a box around the ground contact point; s = 0 on or under it: the shadow stays attached). That cast
// part (s is proportional to the height of what casts it) turns to the sun's shadow direction d and
// is scaled by k = cot(el)/cot(el0): p' = p - s d0 + s k d - a skew / stretch around the object.
// Drawn as a grid (the warp is piecewise linear in s) with the mask lightened towards white by the
// strength:
// dst * lerp(1, mask, a) = mask * a * dst + (1 - a) * dst.
void MapRenderer::drawShadowWarped(SDL_Texture* t, float x, float y, float w, float h, float v0, float degrees,
                                   float cx, float cy, bool rendered3d) {
    if (!t || w <= 0 || h <= 0 || v0 >= 1) return;
    static const SDL_BlendMode kFade = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
        SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    const SpriteShadowWarp& sw = shadowWarp_;
    const ShadowPivot& pv = shadowPivot_;
    const float a = sw.strength, k = rendered3d ? sw.kPack : sw.k;
    const SDL_FColor col{a, a, a, a};
    const float rad = degrees * 3.14159265f / 180.0f, cs = std::cos(rad), sn = std::sin(rad);
    const float px = x + cx, py = y + cy;
    constexpr int N = 12;
    shadowV_.clear();
    shadowI_.clear();
    for (int j = 0; j <= N; ++j) {
        const float v = v0 + (1 - v0) * float(j) / N;
        for (int i = 0; i <= N; ++i) {
            const float u = float(i) / N;
            float qx = x + u * w, qy = y + v * h;
            if (degrees != 0) {  // as SDL_RenderTextureRotated: clockwise about the centre
                const float ox = qx - px, oy = qy - py;
                qx = px + ox * cs - oy * sn;
                qy = py + ox * sn + oy * cs;
            }
            // How far back along d0 the point must go to reach the footprint (the slabs' entry; the
            // same formula beside the band keeps the warp continuous).
            const float vx = qx - pv.x, vy = qy - pv.y;
            float out = 0;
            if (std::fabs(sw.dx0) > 1e-4f) out = std::max(out, (vx * (sw.dx0 > 0 ? 1.0f : -1.0f) - pv.hw) / std::fabs(sw.dx0));
            if (std::fabs(sw.dy0) > 1e-4f) out = std::max(out, (vy * (sw.dy0 > 0 ? 1.0f : -1.0f) - pv.hh) / std::fabs(sw.dy0));
            qx += out * (k * sw.dx - sw.dx0);
            qy += out * (k * sw.dy - sw.dy0);
            shadowV_.push_back({{qx, qy}, col, {u, v}});
        }
    }
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const int b = j * (N + 1) + i;
            shadowI_.insert(shadowI_.end(), {b, b + 1, b + N + 2, b, b + N + 2, b + N + 1});
        }
    SDL_BlendMode old = SDL_BLENDMODE_MOD;
    SDL_GetTextureBlendMode(t, &old);
    SDL_SetTextureBlendMode(t, kFade);
    SDL_RenderGeometry(r_, t, shadowV_.data(), int(shadowV_.size()), shadowI_.data(), int(shadowI_.size()));
    SDL_SetTextureBlendMode(t, old);
}

// The ground contact of a building for its shadows: the centre and half size of its solid cells (the
// structure; the apron cells in front of it receive its shadow), a little inside their edges.
static void buildingFootprint(const formats::BuildingTemplate& t, const game::Building& bld, float& cx, float& cy,
                              float& hw, float& hh) {
    const std::uint32_t mask = t.tilesOccupiedSolid ? t.tilesOccupiedSolid : t.tilesOccupiedAll;
    int x0 = 4, y0 = 4, x1 = -1, y1 = -1;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            if (mask & (1u << (y * 4 + x))) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    if (x1 < 0) x0 = y0 = x1 = y1 = 0;
    cx = float(bld.tileX * 32 + (x0 + x1 + 1) * 16);
    cy = float(bld.tileY * 32 + (y0 + y1 + 1) * 16);
    hw = float((x1 - x0 + 1) * 16) - 4;
    hh = float((y1 - y0 + 1) * 16) - 4;
}

// Building art layout (FUN_004606d0): [base][normal 0..N-1][damaged 0..N-1].
// Anchor = bottom-left of the art box: (tileX*32, tileY*32 + artHeight). See re/notes/rendering.md §3.
void MapRenderer::drawBuildingBase(const game::World& w, const game::Building& bld, const Camera& cam) {
    const auto& t = templates_.bin.buildings[bld.type];
    if (t.buildingArt < 0 || t.buildingArt >= formats::kMaxBuildingArt) return;
    if (bld.flags & 0x100000) return;  // INVISIBLE
    struct GlowScope {  // the base frame's emission layer (base-only arts: star port, repair pad)
        float& k;
        ~GlowScope() { k = 0; }
    } glowScope{glowK_};
    glowK_ = buildingGlowK(w, bld);
    float fcx, fcy, fhw, fhh;
    buildingFootprint(t, bld, fcx, fcy, fhw, fhh);
    const ShadowPivotScope pivot(shadowPivot_, cam, fcx, fcy, fhw, fhh);
    // During build-up / selling the base appears only from frame buildupFramesToShow on, like
    // the main image (0x427d50); before that only the build-up frames show. A redesigned building
    // (Modern build-up) raises its base with the main art from the start.
    const bool modern = packBuildup(w, bld);
    if (!modern && (bld.flags & 0x6) && bld.buildupCounter / 3 < t.buildupFramesToShow) return;
    const int house = w.sides()[bld.side].house;
    const int first = templates_.buildingArtFirstFrame[t.buildingArt];
    const int n = templates_.bin.buildingArtDirections[t.buildingArt];
    int frame = first;
    // Base-only arts show their damaged frame below half health.
    const bool baseOnly = !sprites_.get(first + 1, house);
    if (baseOnly && bld.health * 2 < static_cast<std::int32_t>(t.hitPoints) && n > 0) frame = first + 1 + n;
    if (const Sprite* sp = sprites_.get(frame, house)) {
        const float wx = float(bld.tileX * 32 - sp->frame->x), wy = float(bld.tileY * 32 + t.artHeight - sp->frame->y);
        if (modern) {  // it rises with the main art (base-only arts: the star port, the repair pad)
            const float p = buildupProgress(templates_, bld, buildupTickFrac(w));
            const float cut = buildupCut(*sp, wy, float(bld.tileY * 32 + t.artHeight), cam, p);
            prepareBuildup(w, bld, p);
            const float bx0 = float(bld.tileX * 32), bx1 = float(bld.tileX * 32 + t.artWidth);
            if (baseOnly) drawBuildupRubble(bld, bx0, bx1, cut, cam, p, 0);
            blitBuildup(*sp, wx, wy, cam, cut);
            if (baseOnly) drawBuildupRubble(bld, bx0, bx1, cut, cam, p, 1);
        } else {
            blit(*sp, wx, wy, cam);
        }
    }
}

// Modern build-up (render/buildup_fx.hpp): the building's main art is redesigned (an art-pack frame)
// and the pack has no build-up frames of its own, so the original's (1998 art) are not shown.
bool MapRenderer::packBuildup(const game::World& w, const game::Building& bld) {
    if (!(bld.flags & 0x6) || classicSelection) return false;
    const auto& t = templates_.bin.buildings[bld.type];
    if (t.buildingArt < 0 || t.buildingArt >= formats::kMaxBuildingArt) return false;
    const int house = w.sides()[bld.side].house;
    const int first = templates_.buildingArtFirstFrame[t.buildingArt];
    const Sprite* main = sprites_.get(first + 1, house);
    if (!main) main = sprites_.get(first, house);  // base-only arts
    if (!main || !main->fromPack) return false;
    if (t.buildupArt >= formats::kMaxBuildingTypes) return true;
    const Sprite* bsp = sprites_.get(templates_.buildupArtFirstFrame[t.buildupArt] + bld.buildupCounter / 3, house);
    return !(bsp && bsp->fromPack);
}

float MapRenderer::buildupTickFrac(const game::World& w) const {
    return effectClock_ < 0 ? 0.0f : float(std::clamp(effectClock_ - double(w.gameTicks()), 0.0, 1.0));
}

// The cut line on the screen: from the building's base line (the front edge of its cells, world y
// baseY) at reveal 0 up to the top of the art at reveal 1 (buildupReveal(p)); whatever lies in front
// of the base (an apron, the cast shadow) stands from the start.
float MapRenderer::buildupCut(const Sprite& sp, float worldY, float baseY, const Camera& cam, float p) const {
    const float z = cam.zoom, oy = std::round(cam.y * z);
    const float sy = std::round(worldY * z - oy), h = std::round(sp.drawH * z);
    const float base = std::clamp(std::round(baseY * z - oy), sy, sy + h);
    return base - buildupReveal(p) * (base - sy);
}

void MapRenderer::prepareBuildup(const game::World& w, const game::Building& bld, float p) {
    const auto& t = templates_.bin.buildings[bld.type];
    BuildupLook& L = buildup_;
    L.seed = std::uint32_t(bld.side * 1009 + bld.index * 7919 + bld.type * 31 + bld.tileX * 131 + bld.tileY * 337);
    L.reveal = buildupReveal(p);
    L.artHeight = float(t.artHeight);
    const BuildupFootprint f = buildupFootprint(t, bld.tileX, bld.tileY);
    L.fx0 = f.x0;
    L.fy0 = f.y0;
    L.fx1 = f.x1;
    L.fy1 = f.y1;
    // The rocks and the dust take the colour of the ground around the footprint (the cells in front
    // of it and behind it).
    float r = 0, g = 0, b = 0;
    int n = 0;
    for (int ty : {int(f.y1) / 32, int(f.y0) / 32 - 1})
        for (int tx = int(f.x0) / 32 - 1; tx <= int(f.x1) / 32; ++tx) {
            if (!w.inBounds(tx, ty) || !atlas_) continue;
            const auto& c = w.cell(tx, ty);
            if (c.buildingSide >= 0) continue;
            for (int i = 0; i < 4; ++i) {
                const formats::Rgba px = atlas_->pixel(c.tile, 6 + (i & 1) * 18, 6 + (i >> 1) * 18);
                r += float(px & 0xFF);
                g += float((px >> 8) & 0xFF);
                b += float((px >> 16) & 0xFF);
                ++n;
            }
        }
    if (n > 0) {
        L.sand[0] = r / float(n * 255);
        L.sand[1] = g / float(n * 255);
        L.sand[2] = b / float(n * 255);
    } else {
        L.sand[0] = 0.78f;
        L.sand[1] = 0.63f;
        L.sand[2] = 0.44f;
    }
}

// The sprite below the cut drawn as usual (shadow + art) under a broken, rubble-shaped top edge
// (buildupJag, in world x so the main art, its barrel and its base break alike); the fresh edge
// carries a band of the ground's dust fading downwards. Nothing above the cut.
void MapRenderer::blitBuildup(const Sprite& sp, float worldX, float worldY, const Camera& cam, float cutS) {
    const float z = cam.zoom;
    const float sx = std::round(worldX * z - std::round(cam.x * z));
    const float sy = std::round(worldY * z - std::round(cam.y * z));
    const float w = std::round(sp.drawW * z), h = std::round(sp.drawH * z);
    const Sprite::Level& lv = sp.levels.empty() ? Sprite::Level{sp.texture, sp.shadow, 1} : sp.level(z);
    if (w <= 0 || h <= 0 || !lv.texture) return;
    const BuildupLook& L = buildup_;
    const float cut = std::clamp(cutS, sy, sy + h);
    const int cols = std::clamp(int(std::ceil(sp.drawW / 2.0f)), 1, 256);
    collapseV_.clear();
    collapseI_.clear();
    // Columns from the broken top edge down by bandH screen px (< 0: to the bottom of the sprite).
    auto strip = [&](float bandH, SDL_FColor top, SDL_FColor bottom) {
        const int b0 = int(collapseV_.size());
        for (int i = 0; i <= cols; ++i) {
            const float fu = float(i) / float(cols);
            const float x = sx + w * fu;
            const float yt = std::clamp(cut + buildupJag(L.seed, worldX + sp.drawW * fu, L.reveal) * z, sy, sy + h);
            const float yb = bandH < 0 ? sy + h : std::clamp(yt + bandH, sy, sy + h);
            collapseV_.push_back({{x, yt}, top, {fu, (yt - sy) / h}});
            collapseV_.push_back({{x, yb}, bottom, {fu, (yb - sy) / h}});
            if (i > 0) {
                const int a = b0 + 2 * (i - 1);
                collapseI_.insert(collapseI_.end(), {a, a + 2, a + 3, a, a + 3, a + 1});
            }
        }
    };
    auto flush = [&](SDL_Texture* t, SDL_BlendMode bm) {
        if (t && !collapseV_.empty()) {
            SDL_BlendMode old = SDL_BLENDMODE_BLEND;
            SDL_GetTextureBlendMode(t, &old);
            SDL_SetTextureBlendMode(t, bm);
            SDL_RenderGeometry(r_, t, collapseV_.data(), int(collapseV_.size()), collapseI_.data(), int(collapseI_.size()));
            SDL_SetTextureBlendMode(t, old);
        }
        collapseV_.clear();
        collapseI_.clear();
    };
    const SDL_FColor white{1, 1, 1, 1};
    if (lv.shadow && shadowPivot_.on && shadowWarp_.active) {  // the sun-following shadow, cut at the same height
        if (cut < sy + h) drawShadowWarped(lv.shadow, sx, sy, w, h, (cut - sy) / h, 0, 0, 0, sp.rendered3d);
    } else if (lv.shadow) {
        strip(-1, white, white);
        flush(lv.shadow, SDL_BLENDMODE_MOD);
    }
    strip(-1, white, white);
    flush(lv.texture, SDL_BLENDMODE_BLEND);
    if (L.reveal < 1) {
        // The fresh edge: the art's own pixels in a dusty tint of the ground, fading downwards.
        const float k = std::clamp((1.0f - L.reveal) * 8.0f, 0.0f, 1.0f);
        const SDL_FColor dust{L.sand[0] * 0.8f, L.sand[1] * 0.78f, L.sand[2] * 0.74f, 0.85f * k};
        strip(std::max(3.0f, 7.0f * z), dust, SDL_FColor{dust.r, dust.g, dust.b, 0});
        flush(lv.texture, SDL_BLENDMODE_BLEND);
    }
}

// The rubble around a rising building (world x from x0 to x1 = its art box): pass 0 the disturbed,
// darker sand under the heap (drawn before the art); pass 1 the heap of rocks over the footprint,
// and with Modern effects dust puffs off the heap and the edge plus a few faint warm sparks.
// Render-only; the puffs and sparks run on the effect clock (frozen while paused).
void MapRenderer::drawBuildupRubble(const game::Building& bld, float x0, float x1, float cutS, const Camera& cam, float p,
                                    int pass) {
    const BuildupLook& L = buildup_;
    const float z = cam.zoom, ox = std::round(cam.x * z), oy = std::round(cam.y * z);
    const float* sand = L.sand;
    auto sx = [&](float wx) { return wx * z - ox; };
    auto sy = [&](float wy) { return wy * z - oy; };
    if (!puffTex_) puffTex_ = makeBuildupPuffTexture(r_);
    auto puff = [&](float cx, float cy, float pw, float ph, float r, float g, float b, float a) {
        if (!puffTex_ || a <= 0.004f) return;
        const SDL_FRect dst{cx - pw * 0.5f, cy - ph * 0.5f, pw, ph};
        SDL_SetTextureColorModFloat(puffTex_, std::min(r, 1.0f), std::min(g, 1.0f), std::min(b, 1.0f));
        SDL_SetTextureAlphaModFloat(puffTex_, std::clamp(a, 0.0f, 1.0f));
        SDL_RenderTexture(r_, puffTex_, nullptr, &dst);
    };
    const float fw = L.fx1 - L.fx0, fh = L.fy1 - L.fy0;
    const float fcx = sx((L.fx0 + L.fx1) * 0.5f), fcy = sy((L.fy0 + L.fy1) * 0.5f + 3);
    if (pass == 0) {
        const float s = buildupStain(p);
        if (s > 0) {
            puff(fcx, fcy, fw * 1.25f * z, fh * 1.05f * z, sand[0] * 0.55f, sand[1] * 0.5f, sand[2] * 0.45f, 0.55f * s);
            // The first dark splash at the centre (the original's frame 0), spreading as the rocks come.
            const float g = 0.35f + 0.65f * std::clamp(p / 0.2f, 0.0f, 1.0f);
            puff(fcx, fcy + 2 * z, fw * 0.7f * g * z, fh * 0.55f * g * z, sand[0] * 0.38f, sand[1] * 0.33f, sand[2] * 0.3f,
                 0.5f * s);
        }
        if (puffTex_) {
            SDL_SetTextureColorModFloat(puffTex_, 1, 1, 1);
            SDL_SetTextureAlphaModFloat(puffTex_, 1);
        }
        return;
    }
    // The heap: each rock's contact shadow, then the rock (one batch, back to front).
    static thread_local std::vector<BuildupRock> rocks;
    buildupRocks(L.seed, BuildupFootprint{L.fx0, L.fy0, L.fx1, L.fy1}, L.artHeight, p, (cutS + oy) / z, rocks);
    if (!rockTex_) rockTex_ = makeBuildupRockTexture(r_);
    if (rockTex_ && !rocks.empty()) {
        collapseV_.clear();
        collapseI_.clear();
        auto quad = [&](float cx, float by, float s, int shape, SDL_FColor c) {
            const float u0 = float(shape % 4) * 0.25f, v0 = float(shape / 4) * 0.5f;
            const int b = int(collapseV_.size());
            collapseV_.push_back({{cx - s * 0.5f, by - s}, c, {u0, v0}});
            collapseV_.push_back({{cx + s * 0.5f, by - s}, c, {u0 + 0.25f, v0}});
            collapseV_.push_back({{cx + s * 0.5f, by}, c, {u0 + 0.25f, v0 + 0.5f}});
            collapseV_.push_back({{cx - s * 0.5f, by}, c, {u0, v0 + 0.5f}});
            collapseI_.insert(collapseI_.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        };
        for (const BuildupRock& r : rocks) {
            const float cx = sx(r.x), by = sy(r.y), s = r.size * z;
            quad(cx + 0.9f * z, by + 0.6f * z, s * 1.08f, r.shape, SDL_FColor{0, 0, 0, 0.38f * r.alpha});
            const float k = r.shade * 1.12f;
            quad(cx, by, s, r.shape,
                 SDL_FColor{std::min(1.0f, sand[0] * k), std::min(1.0f, sand[1] * k * 0.97f), std::min(1.0f, sand[2] * k * 0.92f),
                            r.alpha});
        }
        SDL_RenderGeometry(r_, rockTex_, collapseV_.data(), int(collapseV_.size()), collapseI_.data(), int(collapseI_.size()));
        collapseV_.clear();
        collapseI_.clear();
    }
    if (!effects_) return;  // Effects Classic: the reveal and the rocks, no particles
    const float act = std::clamp(std::min(p, 1.0f - p) * 9.0f, 0.0f, 1.0f);
    if (act <= 0.01f) return;
    const double secs = std::max(0.0, effectClock_) / 20.0;
    auto rnd = [&](std::uint32_t slot, int i, int k) {
        std::uint32_t hsh = slot * 2654435761u ^ std::uint32_t(i * 40503 + k * 7919) ^ L.seed;
        hsh ^= hsh >> 15;
        hsh *= 2246822519u;
        hsh ^= hsh >> 13;
        hsh *= 3266489917u;
        hsh ^= hsh >> 16;
        return float(hsh & 0xFFFF) / 65535.0f;
    };
    // Dust puffs: rolling off the heap and the fresh edge, drifting up and widening.
    const float span = x1 - x0;
    const int nPuffs = std::clamp(int(span / 14.0f), 4, 9);
    constexpr float kLife = 1.6f;
    for (int i = 0; i < nPuffs; ++i) {
        const double ph = secs / kLife + i * 0.618 + (L.seed % 97) * 0.01;
        const auto slot = static_cast<std::uint32_t>(std::floor(ph));
        const float f = float(ph - std::floor(ph));
        const float u = rnd(slot, i, 1), v = rnd(slot, i, 2), q = rnd(slot, i, 3);
        float wx = 0, wy = 0;
        if (v < 0.45f && L.reveal > 0.02f && L.reveal < 0.98f) {  // at the edge
            wx = x0 + span * (0.12f + 0.76f * u);
            wy = (cutS + oy) / z + 2;
        } else {  // off the heap
            wx = L.fx0 + fw * (0.08f + 0.84f * u);
            wy = L.fy0 + fh * (0.45f + 0.5f * q);
        }
        const float rise = 14.0f * f + 4.0f * f * f, drift = (q - 0.5f) * 10.0f * f;
        const float s = (7.0f + 11.0f * f) * (0.8f + 0.5f * q) * z;
        const float a = 0.3f * std::sin(3.14159f * std::min(1.0f, f * 1.15f)) * act;
        puff(sx(wx + drift), sy(wy - rise), s * 1.3f, s, sand[0] * 1.08f, sand[1] * 1.05f, sand[2] * 1.0f, a);
    }
    if (puffTex_) {
        SDL_SetTextureColorModFloat(puffTex_, 1, 1, 1);
        SDL_SetTextureAlphaModFloat(puffTex_, 1);
    }
    // A few faint warm sparks where the structure is being fitted (only while it rises).
    if (L.reveal <= 0.05f || L.reveal >= 0.95f) return;
    if (!weldTex_) weldTex_ = makeWeldGlowTexture(r_);
    if (!weldTex_) return;
    static constexpr float kSpark[3] = {1.0f, 0.78f, 0.45f};
    constexpr int kSparks = 3;
    constexpr float kSparkLife = 0.45f;
    const float sx0 = sx(x0);
    for (int i = 0; i < kSparks; ++i) {
        const double ph = secs / kSparkLife + i * 0.43 + bld.index * 0.13;
        const auto slot = static_cast<std::uint32_t>(std::floor(ph));
        if (rnd(slot, i, 9) < 0.45f) continue;  // intermittent
        const float f = float(ph - std::floor(ph)), t = f * kSparkLife;
        const float u = rnd(slot, i, 4), v = rnd(slot, i, 5);
        const float x = sx0 + span * z * (0.15f + 0.7f * u) + (v - 0.5f) * 30.0f * t * z;
        const float y = cutS + (-(12.0f + 14.0f * v) * t + 60.0f * t * t) * z;
        const float s = (1.6f + 1.2f * v) * z * (1.0f - 0.5f * f);
        const SDL_FRect dst{x - s, y - s, 2 * s, 2 * s};
        SDL_SetTextureColorModFloat(weldTex_, kSpark[0], kSpark[1], kSpark[2]);
        SDL_SetTextureAlphaModFloat(weldTex_, 0.45f * (1.0f - f) * act);
        SDL_RenderTexture(r_, weldTex_, nullptr, &dst);
    }
    SDL_SetTextureColorModFloat(weldTex_, 1, 1, 1);
    SDL_SetTextureAlphaModFloat(weldTex_, 1);
}

// Emission of a building's art-pack glow layer this frame: the time of day's lamps (Enhanced lighting only;
// never with the original sprites, which have no .glow.png) x render::buildingGlow (power, damage, pulse).
float MapRenderer::buildingGlowK(const game::World& w, const game::Building& b) const {
    if (!lighting || lighting->identity || lighting->lamps <= 0.001f || classicSelection) return 0;
    return buildingGlow(w, templates_, b.side, b.index, lighting->lamps, std::max(0.0, effectClock_) / 20.0);
}

const Sprite* MapRenderer::drawBuilding(const game::World& w, const game::Building& bld, const Camera& cam) {
    const auto& b = templates_.bin.buildings[bld.type];
    if (b.buildingArt < 0 || b.buildingArt >= formats::kMaxBuildingArt) return nullptr;
    const int house = w.sides()[bld.side].house;

    auto linksWithWall = [&](int x, int y) {
        if (!w.inBounds(x, y)) return false;
        const auto& c = w.cell(x, y);
        if (c.buildingSide < 0) return false;
        const auto& other = w.sides()[c.buildingSide].buildings[c.buildingIndex];
        if (other.tileX != x || other.tileY != y) return false;
        const int sb = templates_.bin.buildings[other.type].specialBehavior;
        return sb == kSpecialBehaviorWall || sb == kSpecialBehaviorWallTurret;
    };

    // Main frame index (BlitVehicles 0x428e30): Building.__DirectionFrame +0x39 (only ever 0
    // in the vanilla model), the wall-link frame (+0x20) for walls / wall-linking turrets, and
    // for silos the fill level min(spice*4500/capacity/1500, N-1). The damaged frame is
    // indexed by +0x20 (wallFrame, 0 for non-walls), not by f.
    const int first = templates_.buildingArtFirstFrame[b.buildingArt];
    const int n = templates_.bin.buildingArtDirections[b.buildingArt];
    int f = 0, wallFrame = 0;
    if (b.specialBehavior == kSpecialBehaviorWall || b.specialBehavior == kSpecialBehaviorWallTurret) {
        int m = 0;
        if (linksWithWall(bld.tileX - 1, bld.tileY)) m |= 1;
        if (linksWithWall(bld.tileX, bld.tileY - 1)) m |= 2;
        if (linksWithWall(bld.tileX + 1, bld.tileY)) m |= 4;
        if (linksWithWall(bld.tileX, bld.tileY + 1)) m |= 8;
        f = wallFrame = kWallFrameMapping[m];
    } else if (b.specialBehavior == kSpecialBehaviorSilo) {
        const auto& sd = w.sides()[bld.side];
        if (sd.maxStorage > 0) {
            const auto lvl = static_cast<std::uint64_t>(static_cast<std::uint32_t>(sd.spice)) * 4500u /
                             static_cast<std::uint32_t>(sd.maxStorage) / 1500u;
            f = static_cast<int>(std::min<std::uint64_t>(lvl, static_cast<std::uint64_t>(std::max(1, n) - 1)));
        }
    }
    int mainFrame = first + 1 + f;
    if (bld.health * 2 < static_cast<std::int32_t>(b.hitPoints) && sprites_.get(first + 1 + n + wallFrame, house))
        mainFrame = first + 1 + n + wallFrame;  // damaged

    const bool hasBarrel = b.barrelArt >= 0 && b.barrelArt < formats::kMaxBuildingArt;
    const int anchorX = bld.tileX * 32, anchorY = bld.tileY * 32 + b.artHeight;
    const int shiftX = hasBarrel ? b.exitPoint1X : 0, shiftY = hasBarrel ? b.exitPoint1Y : 0;
    const Blend flash = (bld.flicker & 2) ? Blend::Flash : Blend::Normal;
    float fcx, fcy, fhw, fhh;
    buildingFootprint(b, bld, fcx, fcy, fhw, fhh);
    const ShadowPivotScope pivot(shadowPivot_, cam, fcx, fcy, fhw, fhh);

    const Sprite* mainSp = sprites_.get(mainFrame, house);
    const Sprite* barrelSp = nullptr;
    if (hasBarrel)
        barrelSp = sprites_.get(templates_.buildingArtFirstFrame[b.barrelArt] + 1 + b.directionFrames[bld.facing & 31], house);
    const Sprite* animSp = nullptr;
    bool animAdditive = false;

    constexpr std::uint32_t kBuildupOrSelling = 0x6, kAnimPermanentOrOnce = 0x30, kHasAnimation = 0x40,
                            kAnimAlpha = 0x800000;
    // Modern build-up of a redesigned building: its finished art rises (buildup_fx.hpp); < 0 = none.
    const float buildP = packBuildup(w, bld) ? buildupProgress(templates_, bld, buildupTickFrac(w)) : -1.0f;
    if (buildP >= 0) {
        // no original build-up frames (1998 art): mainSp / barrelSp stay the finished art
    } else if (bld.flags & kBuildupOrSelling) {
        // Build-up / selling: buildupImg[template.buildupArt][counter/3] (rendering.md §3 pass B).
        const int k = bld.buildupCounter / 3;
        const Sprite* bsp = b.buildupArt < formats::kMaxBuildingTypes
                                ? sprites_.get(templates_.buildupArtFirstFrame[b.buildupArt] + k, house)
                                : nullptr;
        if (!mainSp || k < b.buildupFramesToShow) {
            mainSp = bsp;
            barrelSp = nullptr;
        } else {
            animSp = bsp;
        }
    } else if ((bld.flags & kAnimPermanentOrOnce) && (bld.flags & kHasAnimation) &&
               bld.health * 2 >= static_cast<std::int32_t>(b.hitPoints)) {
        // The draw divides by the animationSpeed of template *index* buildingAnimation
        // (vanilla quirk: differs from the update for the H/O repair pads).
        const int a = b.buildingAnimation;
        if (a >= 0 && a < formats::kMaxBuildingTypes) {
            const int k = bld.animCounter / std::max(1, int(templates_.bin.buildings[a].animationSpeed));
            if (k < templates_.bin.buildingAnimationFrames[a])
                animSp = sprites_.get(templates_.buildingAnimationFirstFrame[a] + k, house);
            animAdditive = (b.flags & kAnimAlpha) != 0;
        }
    }

    // Modern effects: a destroyed building collapses during its 30 death ticks (render only).
    Effects::CollapseLook cl;
    if (effects_ && bld.state == 17 && effects_->collapseLook(bld.side, bld.index, cl)) {
        if (mainSp)
            drawCollapsed(*mainSp, float(anchorX + shiftX - mainSp->frame->x), float(anchorY + shiftY - mainSp->frame->y),
                          cam, cl, false);
        if (barrelSp)
            drawCollapsed(*barrelSp, float(anchorX + shiftX - barrelSp->frame->x),
                          float(anchorY + shiftY - barrelSp->frame->y), cam, cl, true);
        return mainSp;
    }
    if (buildP >= 0) {
        if (!mainSp) return nullptr;  // base-only art: drawBuildingBase raises it
        const float my = float(anchorY + shiftY - mainSp->frame->y);
        const float cut = buildupCut(*mainSp, my, float(anchorY), cam, buildP);
        prepareBuildup(w, bld, buildP);
        drawBuildupRubble(bld, float(anchorX), float(anchorX + b.artWidth), cut, cam, buildP, 0);
        blitBuildup(*mainSp, float(anchorX + shiftX - mainSp->frame->x), my, cam, cut);
        if (barrelSp)
            blitBuildup(*barrelSp, float(anchorX + shiftX - barrelSp->frame->x),
                        float(anchorY + shiftY - barrelSp->frame->y), cam, cut);
        drawBuildupRubble(bld, float(anchorX), float(anchorX + b.artWidth), cut, cam, buildP, 1);
        return mainSp;
    }
    glowK_ = buildingGlowK(w, bld);
    if (mainSp) blit(*mainSp, float(anchorX + shiftX - mainSp->frame->x), float(anchorY + shiftY - mainSp->frame->y), cam, flash);
    glowK_ = 0;
    if (barrelSp) blit(*barrelSp, float(anchorX + shiftX - barrelSp->frame->x), float(anchorY + shiftY - barrelSp->frame->y), cam, flash);
    // A redesigned building with fx_weld anchors (rendered pack): weld flashes in its new works
    // instead of the original overlay, which was painted for the original art.
    if (animSp && !(bld.flags & kBuildupOrSelling) && packAnchors && drawWeldFlashes(w, bld, cam)) animSp = nullptr;
    if (animSp) {
        glowK_ = buildingGlowK(w, bld);  // overlay frames with a glow layer (the star port's landing lights)
        blit(*animSp, float(anchorX - animSp->frame->x), float(anchorY - animSp->frame->y), cam,
             animAdditive ? Blend::Additive : flash);
        glowK_ = 0;
    }
    // Repairing (flag 1): wrench icon DAT_006dc468 centred on the main image (or the base
    // image), blinking with (MyIndex + gGameTicks) & 0x20 (BlitVehicles, before the box).
    if ((bld.flags & 0x1) && ((static_cast<std::uint32_t>(bld.index) + w.gameTicks()) & 0x20)) {
        const Sprite* ref = mainSp ? mainSp : sprites_.get(first, house);
        const Sprite* icon = sprites_.get(kFrameRepairIcon, -1);
        if (ref && icon)
            blit(*icon, float(anchorX + ref->frame->width / 2 - ref->frame->x - icon->frame->width / 2),
                 float(anchorY + ref->frame->height / 2 - ref->frame->y - icon->frame->height / 2), cam);
    }
    return mainSp;
}

// Weld flashes (render/weld_fx.hpp): a blue-white halo and a white core at this tick's anchor.
// Returns whether the building has fx_weld anchors (then the original overlay is not drawn, also
// between two arcs).
bool MapRenderer::drawWeldFlashes(const game::World& w, const game::Building& b, const Camera& cam) {
    if (!packAnchors->buildingAnchors(w, b.side, b.index, "fx_weld", weldTmp_) || weldTmp_.empty()) return false;
    WeldFlash fl;
    static const bool all = std::getenv("D2K_WELD_ALL") != nullptr;  // check: every anchor lit, steady
    if (all) fl = WeldFlash{0, 1, 1};
    else if (!weldFlash(w, b, templates_, int(weldTmp_.size()), fl)) return true;
    if (!weldTex_) weldTex_ = makeWeldGlowTexture(r_);
    if (!weldTex_) return true;
    for (std::size_t ai = 0; ai < weldTmp_.size(); ++ai) {
        if (!all && int(ai) != fl.anchor) continue;
        const EffectAnchor& an = weldTmp_[ai];
        const float z = cam.zoom;
        const float sx = (an.x - cam.x) * z, sy = (an.y - an.z - cam.y) * z;
        auto glow = [&](float sizePx, const float* c, float a) {
            const float s = sizePx * an.size * fl.size * z;
            const SDL_FRect dst{sx - s * 0.5f, sy - s * 0.5f, s, s};
            SDL_SetTextureColorModFloat(weldTex_, c[0], c[1], c[2]);
            SDL_SetTextureAlphaModFloat(weldTex_, std::min(1.0f, a));
            SDL_RenderTexture(r_, weldTex_, nullptr, &dst);
        };
        glow(26, kWeldHalo, 0.75f * fl.a);
        glow(9, kWeldCore, fl.a);
    }
    return true;
}

// The art is cut into vertical pieces; each shows the top (1 - sink) of its strip, moved down by
// sink so it disappears below the base line, its top shifted by lean (crumbling outwards), the
// colour charred by shade. The base-line dust (Effects) hides the cut.
void MapRenderer::drawCollapsed(const Sprite& sp, float worldX, float worldY, const Camera& cam,
                                const Effects::CollapseLook& cl, bool whole) {
    const float z = cam.zoom;
    const float sx = std::round((worldX + cl.dx) * z - std::round(cam.x * z));
    const float sy = std::round((worldY + cl.dy) * z - std::round(cam.y * z));
    const float w = std::round(sp.drawW * z), h = std::round(sp.drawH * z);
    const Sprite::Level& lv = sp.levels.empty() ? Sprite::Level{sp.texture, sp.shadow, 1} : sp.level(z);
    Effects::CollapseLook one;
    if (whole) {
        one.pieces = 1;
        one.piece[0] = {0, 1, cl.sink, 0};
    }
    const Effects::CollapseLook& look = whole ? one : cl;
    for (int pass = 0; pass < 2; ++pass) {
        SDL_Texture* t = pass == 0 ? lv.shadow : lv.texture;
        if (!t || (pass == 0 && cl.sink > 0.45f)) continue;
        const float k = pass == 0 ? 1.0f : cl.shade;
        const SDL_FColor col{k, k * 0.93f, k * 0.86f, 1};
        collapseV_.clear();
        collapseI_.clear();
        for (int i = 0; i < look.pieces; ++i) {
            const auto& p = look.piece[i];
            const float x0 = sx + p.u0 * w, x1 = sx + p.u1 * w, top = sy + p.sink * h, bot = sy + h, lean = p.lean * z;
            const float v1 = 1 - p.sink;
            const int b = int(collapseV_.size());
            collapseV_.push_back({{x0 + lean, top}, col, {p.u0, 0}});
            collapseV_.push_back({{x1 + lean, top}, col, {p.u1, 0}});
            collapseV_.push_back({{x1, bot}, col, {p.u1, v1}});
            collapseV_.push_back({{x0, bot}, col, {p.u0, v1}});
            collapseI_.insert(collapseI_.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        }
        SDL_RenderGeometry(r_, t, collapseV_.data(), int(collapseV_.size()), collapseI_.data(), int(collapseI_.size()));
    }
}

void MapRenderer::drawUnit(const game::World& w, const game::Unit& unit, const Camera& cam) {
    const auto& u = templates_.bin.units[unit.type];
    // Sandworms never get a sprite: unitArt is -1 and they keep NOT_DRAWN (0x200000) from
    // spawn. The original shows them only through the sand spray particles (drawParticles),
    // the wormsign explosions groupIds[73..78] and the swallow explosions deathExplosion/+1.
    if (u.specialBehavior == kSpecialBehaviorSandworm) return;
    const int house = w.sides()[unit.side].house;
    const float ax = unitPx(unit.posX), ay = unitPx(unit.posY - unit.posZ);
    // Modern effects: eased body / turret facing (the closest of the 32 frames, rotated by the
    // residual angle). Render only. (No driving bob: a sub-pixel bob can only be drawn as whole
    // pixel hops, which read as the unit shaking.)
    const Effects::UnitVisual* vis = effects_ ? effects_->unitVisual(unit.side, unit.index) : nullptr;
    float bodyFacing = float(unit.facing & 31), turretFacing = float(unit.facingTurret & 31);
    if (vis) {
        bodyFacing = vis->facing;
        turretFacing = vis->turret;
    }
    auto frameAndResidual = [](float f, int& dir32, float& degrees) {
        const float r = std::round(f);
        dir32 = int(r) & 31;
        degrees = (f - r) * 11.25f;
    };
    Blend flash = (unit.flicker & 2) ? Blend::Flash : Blend::Normal;
    // Cloaked (0x4) or fading (stealth timer): own units shimmer, enemies are not drawn.
    if (unit.flags & 0x4) {
        if (unit.side != playerSide) return;
        flash = Blend::Half;
    } else if (unit.stealthTimer != 0) {
        flash = Blend::Half;
    }

    // The shadows' ground contact: the unit position; the body's half size (infantry / vehicles).
    const float bodyHalf = u.isInfantry ? 2.5f : 7.0f;
    const ShadowPivotScope pivot(shadowPivot_, cam, ax, ay, bodyHalf, bodyHalf);
    if (u.unitArt >= 0 && u.unitArt < formats::kMaxUnitArt) {
        const int dirs = templates_.bin.unitArtDirectionFrames[u.unitArt];
        int f32 = unit.facing & 31;
        float rot = 0;
        if (vis) frameAndResidual(bodyFacing, f32, rot);
        if (dirs != 32) rot = 0;
        int dir = u.directionFrames[f32];
        if (unit.animType > 8) dir = unit.animType - 9;  // direction-less infantry animations
        int frame = templates_.unitArtFirstFrame[u.unitArt] + unit.animFrame * dirs + dir;
        // Harvester exception (BlitVehicles 0x428e30): a harvester with animFrame != 0 (docking /
        // unloading at a refinery, states 13-15) shows animImg[groupIds[64] (0x6d6118)][animFrame]
        // (0x6e2e20, 40 frames per animation): the direction-less "unloading" art with the pipe.
        if (u.specialBehavior == 1 && unit.animFrame != 0) {
            const int a = static_cast<std::uint8_t>(templates_.bin.groupIds[64]);
            if (a < formats::kMaxExplosions) frame = templates_.animationArtFirstFrame[a] + unit.animFrame;
            rot = 0;
        }
        if (const Sprite* sp = sprites_.get(frame, house))
            blitUnit(*sp, ax, ay, cam, flash, rot);
    }
    if (u.barrelArt >= 0 && u.barrelArt < formats::kMaxUnitArt) {
        int f32 = unit.facingTurret & 31;
        float rot = 0;
        if (vis) frameAndResidual(turretFacing, f32, rot);
        if (templates_.bin.unitArtDirectionFrames[u.barrelArt] != 32) rot = 0;
        const int frame = templates_.unitArtFirstFrame[u.barrelArt] + u.directionFrames[f32];
        if (const Sprite* sp = sprites_.get(frame, house))
            blitUnit(*sp, ax, ay, cam, flash, rot);
    }
}

// Enhanced pips (harvester load, refinery / silo storage): n segments in a dark frame, the first
// `filled` green (the original's pip colour), the rest black.
void MapRenderer::drawPipRow(float x, float y, float w, float h, int filled, int n, float z) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    const float gap = std::max(1.0f, std::round(z * 0.75f));
    const SDL_FRect back{std::round(x) - 1, y - 1, std::round(w) + 2, h + 2};
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 200);
    SDL_RenderFillRect(r_, &back);
    const float segW = (w - (n - 1) * gap) / float(n);
    for (int i = 0; i < n; ++i) {
        const SDL_FRect seg{std::round(x + i * (segW + gap)), y, std::max(1.0f, std::round(segW)), h};
        if (i < filled) {
            SDL_SetRenderDrawColor(r_, 40, 210, 60, 255);
            SDL_RenderFillRect(r_, &seg);
            const SDL_FRect hi{seg.x, seg.y, seg.w, std::max(1.0f, std::round(h / 3))};
            SDL_SetRenderDrawColor(r_, 170, 255, 160, 255);
            SDL_RenderFillRect(r_, &hi);
        } else {  // empty: black with a faint rim so the segments still read
            SDL_SetRenderDrawColor(r_, 26, 26, 26, 255);
            SDL_RenderFillRect(r_, &seg);
            const SDL_FRect in{seg.x + 1, seg.y + 1, std::max(0.0f, seg.w - 2), std::max(0.0f, seg.h - 2)};
            SDL_SetRenderDrawColor(r_, 4, 4, 4, 255);
            SDL_RenderFillRect(r_, &in);
        }
    }
}

// A thin health bar: dark back, green -> yellow -> red fill (as the unit selection bar).
void MapRenderer::drawBar(float x, float y, float bw, float bh, float frac) {
    const SDL_FRect back{x, y, bw, bh};
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 180);
    SDL_RenderFillRect(r_, &back);
    if (frac > 0.5f) SDL_SetRenderDrawColor(r_, 40, 220, 60, 255);
    else if (frac > 0.25f) SDL_SetRenderDrawColor(r_, 240, 200, 40, 255);
    else SDL_SetRenderDrawColor(r_, 230, 40, 30, 255);
    const SDL_FRect bar{x, y, std::round(bw * frac), bh};
    SDL_RenderFillRect(r_, &bar);
}

// Enhanced: building health bars (selected ones always, others per Damaged / Always) with
// corner brackets around selected buildings, and unselected unit bars (Always), drawn above the
// objects and under the shroud.
void MapRenderer::drawHealthBars(const game::World& w, const Camera& cam) {
    const float zoom = cam.zoom;
    const float z = std::round(std::clamp(std::sqrt(zoom), 1.0f, 2.0f) * 2) / 2;
    const float ox = std::round(cam.x * zoom), oy = std::round(cam.y * zoom);
    for (const auto& side : w.sides()) {
        for (int i = side.firstBuilding; i >= 0; i = side.nextObject(i)) {
            const auto& b = side.buildings[i];
            if (!b.alive || b.state == 17 || (b.flags & 0x100000)) continue;
            const auto& t = templates_.bin.buildings[b.type];
            if (t.specialBehavior == game::building_behaviour::kConcrete || t.hitPoints == 0) continue;
            const float frac = std::clamp(float(b.health) / float(t.hitPoints), 0.f, 1.f);
            if (!b.selected && (healthBars == 0 || (healthBars == 1 && frac >= 1.0f))) continue;
            // The whole footprint (tilesOccupiedAll: the art including its skirt rows), the same
            // box for every building type; the bar centred over it at the top of the drawn art
            // (the original anchors its box there too: Y - main.yoff, rendering.md §3).
            const std::uint32_t m = t.tilesOccupiedAll ? t.tilesOccupiedAll : t.tilesOccupiedSolid;
            int c0 = 4, c1 = -1, r0 = 4, r1 = -1;
            for (int k = 0; k < 16; ++k)
                if ((m >> k) & 1) {
                    c0 = std::min(c0, k % 4);
                    c1 = std::max(c1, k % 4);
                    r0 = std::min(r0, k / 4);
                    r1 = std::max(r1, k / 4);
                }
            if (c1 < 0) { c0 = r0 = 0; c1 = r1 = 0; }
            const float x0 = std::round((b.tileX + c0) * 32 * zoom - ox) + 1;
            const float x1 = std::round((b.tileX + c1 + 1) * 32 * zoom - ox) - 1;
            const float fy0 = std::round((b.tileY + r0) * 32 * zoom - oy) + 1;
            const float y1 = std::round((b.tileY + r1 + 1) * 32 * zoom - oy) - 1;
            int artTop = (b.tileY + r0) * 32;
            if (t.buildingArt >= 0 && t.buildingArt < formats::kMaxBuildingArt)
                if (const Sprite* main = sprites_.get(templates_.buildingArtFirstFrame[t.buildingArt], w.sides()[b.side].house))
                    artTop = std::min(artTop, b.tileY * 32 + t.artHeight - main->frame->y);
            const float barH = std::round(3 * z);
            const float width = std::max(8.0f, x1 - x0 - std::round(6 * zoom));
            const float left = std::round((x0 + x1 - width) / 2);
            const float top = std::round(artTop * zoom - oy) - barH - std::round(3 * z);
            if (b.selected) {
                // Corner brackets around the footprint (as the unit brackets), the bar on top.
                const float y0 = std::min(fy0, std::round(artTop * zoom - oy) + 1);
                const float len = std::round(std::min(8 * z, (x1 - x0) / 4));
                const float th = std::max(1.0f, std::round(z));  // arm thickness, with a dark outline
                SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
                for (int pass = 0; pass < 2; ++pass) {
                    const float o = pass == 0 ? 1 : 0;
                    if (pass == 0) SDL_SetRenderDrawColor(r_, 0, 0, 0, 150);
                    else SDL_SetRenderDrawColor(r_, 255, 255, 255, 235);
                    for (float x : {x0, x1})
                        for (float y : {y0, y1}) {
                            const bool l = x < x1, t = y < y1;
                            const float ax = l ? x : x - len, ay = t ? y : y - th;
                            const float bx = l ? x : x - th, by = t ? y : y - len;
                            const SDL_FRect h{ax - o, ay - o, len + 2 * o, th + 2 * o}, v{bx - o, by - o, th + 2 * o, len + 2 * o};
                            SDL_RenderFillRect(r_, &h);
                            SDL_RenderFillRect(r_, &v);
                        }
                }
                // PRIMARY: the original's "1" marker (DAT_006e2df4) inside the top-left bracket.
                if ((b.flags & 0x80) && b.side == playerSide)
                    if (const Sprite* sp = sprites_.get(kFrameGroupDigit + 1, -1)) {
                        const float sc = std::round(std::max(1.0f, zoom));
                        const SDL_FRect d{x0 + std::round(2 * z), y0 + std::round(2 * z), sp->width * sc, sp->height * sc};
                        SDL_RenderTexture(r_, sp->texture, nullptr, &d);
                    }
            }
            drawBar(left, top, width, std::round(3 * z), frac);
            // Own refinery (16) / silo (8): the side's spice storage as pips under the bar, the
            // original's selected-building pips (N - 1 slots, filled = min(spice * N / capacity, N - 1)).
            if (b.selected && b.side == playerSide && side.maxStorage > 0 &&
                (t.specialBehavior == game::building_behaviour::kRefinery || t.specialBehavior == game::building_behaviour::kSilo)) {
                const int n = t.specialBehavior == game::building_behaviour::kRefinery ? 16 : 8;
                const int filled = int(std::min<std::int64_t>(std::int64_t(side.spice) * n / side.maxStorage, n - 1));
                drawPipRow(left, top + barH + std::round(2 * z), width, std::round(3 * z), filled, n - 1, z);
            }
        }
        if (healthBars != 2) continue;
        for (int i = side.firstUnit; i >= 0; i = side.nextObject(i)) {
            const auto& u = side.units[i];
            if (!u.alive || u.selected || (u.flags & 0x1200100) || u.state == game::UnitState::Dead) continue;
            if ((u.flags & 0x4) && u.side != playerSide) continue;  // cloaked enemies
            const auto& t = templates_.bin.units[u.type];
            if (t.hitPoints == 0) continue;
            const float cx = std::round(unitPx(u.posX) * zoom - ox);
            const float cy = std::round(unitPx(u.posY - u.posZ) * zoom - oy);
            const float h = std::round((t.isInfantry ? 9.0f : 14.0f) * zoom);
            drawBar(cx - h, cy - h - std::round(5 * z), 2 * h, std::round(3 * z),
                    std::clamp(float(u.health) / float(t.hitPoints), 0.f, 1.f));
        }
    }
}

void MapRenderer::drawSelection(const game::World& w, const game::Unit& unit, const Camera& cam) {
    // Corner brackets around the unit cell plus a health bar (modern-style overlay).
    // The box follows the unit's size on screen (h ~ the zoom); the bracket arms, bar heights
    // and gaps grow only gently (sqrt, at most 2x) so zoomed-in overlays stay thin.
    const float zoom = cam.zoom;
    const float cx = std::round(unitPx(unit.posX) * zoom - std::round(cam.x * zoom));
    const float cy = std::round(unitPx(unit.posY) * zoom - std::round(cam.y * zoom));
    const float z = std::round(std::clamp(std::sqrt(zoom), 1.0f, 2.0f) * 2) / 2;  // 1, 1.5 or 2
    const auto& tmpl = templates_.bin.units[unit.type];
    const float h = std::round((tmpl.isInfantry ? 9.0f : 14.0f) * zoom), len = std::round(4 * z);
    SDL_SetRenderDrawColor(r_, 255, 255, 255, 230);
    const float xs[2] = {cx - h, cx + h}, ys[2] = {cy - h, cy + h};
    for (float x : xs)
        for (float y : ys) {
            const float sx = x < cx ? 1.f : -1.f, sy = y < cy ? 1.f : -1.f;
            SDL_RenderLine(r_, x, y, x + sx * len, y);
            SDL_RenderLine(r_, x, y, x, y + sy * len);
        }
    const auto& t = templates_.bin.units[unit.type];
    const float frac = t.hitPoints ? std::clamp(float(unit.health) / float(t.hitPoints), 0.f, 1.f) : 1.f;
    const SDL_FRect back{cx - h, cy - h - std::round(5 * z), 2 * h, std::round(3 * z)};
    const SDL_FRect bar{back.x, back.y, back.w * frac, back.h};
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 180);
    SDL_RenderFillRect(r_, &back);
    if (frac > 0.5f) SDL_SetRenderDrawColor(r_, 40, 220, 60, 255);
    else if (frac > 0.25f) SDL_SetRenderDrawColor(r_, 240, 200, 40, 255);
    else SDL_SetRenderDrawColor(r_, 230, 40, 30, 255);
    SDL_RenderFillRect(r_, &bar);
    // Own harvester: spice load (Unit +0x29, 0..7 blobs) as 7 pips under the box, green when
    // loaded and black when empty like the 7 classic pips (BlitVehicles 0x428e30, behaviour 1).
    if (t.specialBehavior == 1 && unit.side == playerSide)
        drawPipRow(cx - h, std::round(cy + h + 2 * z), 2 * h, std::round(3 * z), std::min<int>(unit.byte29, 7), 7, z);
    (void)w;
}

// Classic selection overlay (BlitVehicles 0x428e30, per display-list entry right after its
// sprites): bar background DAT_00789198[hbSize] at (x0, y0), corner brackets, and the HP fill
// as two lines (y0+1, y0+2) from x0+1, (bar.w - 3) * HP / maxHP + 1 pixels long; green
// (0,200,0), yellow (220,220,0) below 1/2, red (200,0,0) below 1/3 (0x516690/538/53c).
void MapRenderer::drawClassicBox(int x0, int y0, int s, std::int32_t hp, std::int32_t maxHp, const Camera& cam) {
    s = std::clamp(s, 0, 5);
    const int boxH = kSelectionBoxHeight[s];
    const Sprite* bar = sprites_.get(kFrameHealthBar + s, -1);
    const Sprite* tl = sprites_.get(kFrameBracketTL, -1);
    const Sprite* tr = sprites_.get(kFrameBracketTR, -1);
    const Sprite* bl = sprites_.get(kFrameBracketBL, -1);
    const Sprite* br = sprites_.get(kFrameBracketBR, -1);
    if (!bar || !tr) return;
    const int barW = bar->frame->width, trW = tr->frame->width, trH = tr->frame->height;
    blit(*bar, float(x0), float(y0), cam);
    if (tl) blit(*tl, float(x0), float(y0 + 4), cam);
    blit(*tr, float(x0 - trW + barW), float(y0 + 4), cam);
    if (bl) blit(*bl, float(x0), float(y0 - trH + 4 + boxH), cam);
    if (br) blit(*br, float(x0 - trW + barW), float(y0 - trH + 4 + boxH), cam);
    if (maxHp <= 0) return;
    const int fill = static_cast<std::int8_t>(((barW - 3) * hp) / maxHp);  // stored as a char
    if (hp < maxHp / 3) SDL_SetRenderDrawColor(r_, 200, 0, 0, 255);
    else if (hp < maxHp / 2) SDL_SetRenderDrawColor(r_, 220, 220, 0, 255);
    else SDL_SetRenderDrawColor(r_, 0, 200, 0, 255);
    const float z = cam.zoom;
    const float l = std::round((x0 + 1) * z - std::round(cam.x * z)), t = std::round((y0 + 1) * z - std::round(cam.y * z));
    const float rr = std::round((x0 + 2 + fill) * z - std::round(cam.x * z)), b = std::round((y0 + 3) * z - std::round(cam.y * z));
    if (rr > l) {
        const SDL_FRect rc{l, t, rr - l, b - t};
        SDL_RenderFillRect(r_, &rc);
    }
}

// Horizontal pips: `total` slots from x0 - 1 in steps of 3 (the step comes first), filled first.
void MapRenderer::drawPips(int x0, int y, int filled, int total, const Camera& cam) {
    int x = x0 - 1;
    for (int i = 0; i < total; ++i) {
        x += 3;
        if (const Sprite* sp = sprites_.get(i < filled ? kFramePipFull : kFramePipEmpty, -1)) blit(*sp, float(x), float(y), cam);
    }
}

void MapRenderer::drawClassicUnitSelection(const game::World& w, const game::Unit& unit, const Camera& cam) {
    const auto& t = templates_.bin.units[unit.type];
    const int s = std::clamp(int(t.healthBarSize), 0, 5);
    const Sprite* bar = sprites_.get(kFrameHealthBar + s, -1);
    if (!bar) return;
    const int boxH = kSelectionBoxHeight[s];
    const int x = game::fixToPixel(unit.posX), y = game::fixToPixel(unit.posY - unit.posZ);
    const int x0 = x - bar->frame->width / 2;
    int y0 = y - ((boxH * 5) >> 3);
    if (t.isInfantry) y0 -= 8;
    drawClassicBox(x0, y0, s, unit.health, static_cast<std::int32_t>(t.hitPoints), cam);
    if (unit.side != playerSide) return;
    int groupUp = 6;
    if (t.specialBehavior == 1) {  // harvester: 7 spice pips (Unit +0x29)
        drawPips(x0, y0 - 1 + boxH, unit.byte29, 7, cam);
        groupUp = 10;
    } else if (t.specialBehavior == 4) {  // saboteur: 5 vertical charge pips (+0x29 >> 5)
        const int filled = unit.byte29 >> 5;
        int py = y0 + 1 + boxH;
        for (int i = 0; i < 5; ++i) {
            py -= 3;
            if (const Sprite* sp = sprites_.get(i < filled ? kFramePipFull : kFramePipEmpty, -1))
                blit(*sp, float(x0), float(py), cam);
        }
        groupUp = 10;
    }
    if (unit.groupId < 10)
        if (const Sprite* sp = sprites_.get(kFrameGroupDigit + unit.groupId, -1))
            blit(*sp, float(x0 + 2), float(y0 - groupUp + boxH), cam);
    if (static_cast<int>(unit.state) == 0x12)  // waiting for a carryall
        if (const Sprite* sp = sprites_.get(kFrameAirliftIcon, -1)) blit(*sp, float(x0 + 2), float(y0 + 6), cam);
    (void)w;
}

void MapRenderer::drawClassicBuildingSelection(const game::World& w, const game::Building& bld, const Sprite* main,
                                               const Camera& cam) {
    const auto& t = templates_.bin.buildings[bld.type];
    const int s = std::clamp(int(t.healthBarSize), 0, 5);
    const int boxH = kSelectionBoxHeight[s];
    if (!main && t.buildingArt >= 0 && t.buildingArt < formats::kMaxBuildingArt)
        main = sprites_.get(templates_.buildingArtFirstFrame[t.buildingArt], w.sides()[bld.side].house);
    const int x0 = bld.tileX * 32;
    int y0 = bld.tileY * 32 + t.artHeight - (main ? main->frame->y : 0);
    if (t.specialBehavior == 5) y0 -= 6;   // silo
    if (t.barrelArt >= 0 && t.barrelArt < formats::kMaxBuildingArt) y0 -= 12;
    drawClassicBox(x0, y0, s, bld.health, static_cast<std::int32_t>(t.hitPoints), cam);
    if (bld.side != playerSide) return;
    const Sprite* tr = sprites_.get(kFrameBracketTR, -1);
    const int trH = tr ? tr->frame->height : 0;
    if (bld.flags & 0x80)  // PRIMARY
        if (const Sprite* sp = sprites_.get(kFrameGroupDigit + 1, -1))
            blit(*sp, float(x0 + 2), float(y0 - trH - 4 + boxH), cam);
    const auto& side = w.sides()[bld.side];
    int n = 0;
    if (t.specialBehavior == 4) n = 16;       // refinery
    else if (t.specialBehavior == 5) n = 8;   // silo
    if (n && side.maxStorage > 0) {
        const int filled = std::min<std::int64_t>(std::int64_t(side.spice) * n / side.maxStorage, n - 1);
        drawPips(x0, y0 - 1 + boxH, filled, n - 1, cam);
    }
    // UNCERTAIN/not drawn: side +0x24544[template +0x88] icon DAT_00782058 (upgrade?).
}

void MapRenderer::drawMarker(const game::MapObject& o, const Camera& cam) {
    Uint8 r = 255, g = 255, b = 255;
    switch (o.kind) {
    case game::ObjectKind::PlayerStart: r = 0; g = 255; b = 255; break;
    case game::ObjectKind::WormSpawner: r = 255; g = 0; b = 255; break;
    case game::ObjectKind::SpiceBloom: r = 255; g = 60; b = 0; break;
    case game::ObjectKind::Crate: r = 0; g = 200; b = 0; break;
    default: break;
    }
    const float z = cam.zoom;
    const float sx = std::round((o.x * 32 + 8) * z - std::round(cam.x * z));
    const float sy = std::round((o.y * 32 + 8) * z - std::round(cam.y * z));
    const SDL_FRect rc{sx, sy, 16 * z, 16 * z};
    SDL_SetRenderDrawColor(r_, r, g, b, 200);
    SDL_RenderRect(r_, &rc);
    const SDL_FRect inner{rc.x + 2, rc.y + 2, rc.w - 4, rc.h - 4};
    SDL_RenderRect(r_, &inner);
}

}  // namespace d2k::render
