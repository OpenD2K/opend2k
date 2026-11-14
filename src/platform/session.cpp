#include "platform/session.hpp"

#include "platform/audio.hpp"
#include "platform/cheat_menu.hpp"
#include "platform/campaign_screens.hpp"
#include "platform/game_options.hpp"
#include "platform/pointer.hpp"
#include "platform/preset_previews.hpp"
#include "platform/rally_ui.hpp"
#include "platform/ingame_pages.hpp"
#include "platform/settings.hpp"
#include "platform/sim_test.hpp"
#include "platform/sound_bank.hpp"
#include "platform/ui.hpp"
#include "render/art_pack.hpp"
#include "render/bloom.hpp"
#include "render/hdr.hpp"
#include "render/build_icons.hpp"
#include "render/classic_sidebar.hpp"
#include "render/effects.hpp"
#include "render/command_panel.hpp"
#include "render/ui_text.hpp"
#include "render/minimap.hpp"
#include "render/phone_hud.hpp"
#include "render/scene_lights.hpp"
#include "render/sidebar.hpp"
#include "render/ui_skin.hpp"
#include "render/sprites.hpp"
#include "render/text.hpp"
#include "render/tileset.hpp"
#include "render3d/models.hpp"
#include "render3d/renderer3d.hpp"
#include "render3d/terrain_bake.hpp"

#include <algorithm>
#include <filesystem>
#include <array>
#include <cmath>
#include "render/lighting.hpp"
#include <cstdio>
#include <ctime>
#include <sstream>

namespace d2k::platform {

namespace {

void clampCamera(render::Camera& cam, const game::Scenario& s, int viewW, int viewH) {
    const float worldW = s.width * 32.0f, worldH = s.height * 32.0f;
    cam.x = std::clamp(cam.x, 0.0f, std::max(0.0f, worldW - viewW / cam.zoom));
    cam.y = std::clamp(cam.y, 0.0f, std::max(0.0f, worldH - viewH / cam.zoom));
}

// Screenshots of the production UI (--sidebar-page demo-base; debug, uses the cheat orders):
// tech level 9, 20000 credits, every buildable structure once (walls / concrete / turrets / yards
// left out) plus extra power, spawned around the player's first building, then a few units queued.
void demoBase(game::World& w, int side) {
    w.orderCheatTechLevel(side, 9);
    w.orderCheatSetCredits(side, 20000);
    {  // the house's palace weapons / air strike on the sidebar (the specials slot)
        const auto& U = w.rules().templates->bin.units;
        const int bit = 1 << w.sides()[side].house;
        for (int t = 0; t < int(std::size(U)); ++t)
            if (render::specialBuildUnit(U[t].specialBehavior) && (U[t].ownerHouse & bit))
                w.orderCheatUnitOverride(side, t, game::BuildOverride::Allowed);
    }
    w.tick();
    const auto& me = w.sides()[side];
    if (me.firstBuilding < 0) return;
    const auto& T = w.rules().templates->bin;
    const int cx = me.buildings[me.firstBuilding].tileX, cy = me.buildings[me.firstBuilding].tileY;
    std::vector<int> done;
    int power = -1;
    auto spawn = [&](int type) {
        bool placed = false;
        for (int r = 3; r < 30 && !placed; ++r)
            for (int dy = -r; dy <= r && !placed; dy += 1)
                for (int dx = -r; dx <= r && !placed; dx += 1) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int tx = cx + dx, ty = cy + dy;
                    // One free tile around the footprint so the base stays walkable.
                    if (!w.canCheatSpawnBuilding(type, tx, ty) || !w.canCheatSpawnBuilding(type, tx - 1, ty - 1) ||
                        !w.canCheatSpawnBuilding(type, tx + 1, ty + 1))
                        continue;
                    placed = w.orderCheatSpawnBuilding(side, type, tx, ty, true);
                }
        w.tick();
    };
    // Rounds: each new structure may unlock more (prerequisites).
    for (int round = 0; round < 8; ++round) {
        bool any = false;
        for (const auto& o : w.buildableBuildings(side)) {
            const int beh = T.buildings[o.type].specialBehavior;
            if (beh == game::building_behaviour::kWall || beh == game::building_behaviour::kConcrete ||
                beh == game::building_behaviour::kTurret || beh == game::building_behaviour::kConstructionYard ||
                std::find(done.begin(), done.end(), o.type) != done.end())
                continue;
            if (power < 0) {  // the first in sidebar order: the windtrap, three more of it
                power = o.type;
                for (int i = 0; i < 3; ++i) spawn(o.type);
            }
            done.push_back(o.type);
            spawn(o.type);
            any = true;
        }
        if (!any) break;
    }
    for (int i = 0; i < 10 && power >= 0 && w.economy(side).powerPercent < 130; ++i) spawn(power);
    SDL_Log("demo-base: %d structures, power %u%%, %zu unit icons", int(done.size()), w.economy(side).powerPercent,
            w.buildableUnits(side).size());
    int queued = 0;
    for (const auto& o : w.buildableUnits(side)) {
        if (render::specialBuildUnit(T.units[o.type].specialBehavior)) continue;
        if (queued == 0) w.orderAutoAdd(side, o.type, 3);
        else if (queued == 2) w.orderAutoInfinite(side, o.type);
        else if (queued < 4) w.orderAutoAdd(side, o.type, 1);
        ++queued;
    }
    w.tick();
}

}  // namespace

Session::Session(SDL_Renderer* r, std::unique_ptr<GameData> data, Settings& settings, Audio* audio, SoundBank* sounds,
                 const SessionConfig& cfg)
    : r_(r), data_(std::move(data)), settings_(settings), audio_(audio), sounds_(sounds), cfg_(cfg) {
    asyncModels_ = cfg.asyncModels;
    const GameData& g = *data_;
    out_ = std::make_unique<ChannelOutput>(audio_ && audio_->ok() ? audio_ : nullptr, sounds_);
    if (sounds_) snd_.setMoneyTick(sounds_->idOf("S_MONEYTICK"));
    // The session plays as the mission's side (Missions/<map>.ini SideId); skirmish: side 0.
    playerSide_ = g.start.skirmish ? 0 : g.playerSide;
    SDL_Log("Playing as side %d (house %d)", playerSide_,
            g.start.skirmish ? g.start.sk.playerHouse : int(g.mission.houseId[playerSide_]));

    appliedGraphics_ = settings_.graphics;
    const bool enhanced = settings_.enhanced();
    atlas_ = std::make_unique<render::TileAtlas>(r_, g.tiles, enhanced);
    sprites_ = std::make_unique<render::SpriteCache>(r_, g.sprites, g.colours);
    if (enhanced) sprites_->setHq(true);
    mapRenderer_ = std::make_unique<render::MapRenderer>(r_, *atlas_, *sprites_, g.templates);
    mapRenderer_->playerSide = playerSide_;
    if (cfg.reveal) mapRenderer_->showShroud = false;
    mapRenderer_->classicSelection = !enhanced;
    mapRenderer_->healthBars = static_cast<int>(settings_.healthBars);
    appliedArtPack_ = enhanced ? "upscaled" : "";
    appliedTerrainKey_ = enhanced ? "hq|" : "classic";
    syncVisuals();
    minimap_ = std::make_unique<render::Minimap>(r_, g.tiles, g.colours);
    text_ = std::make_unique<render::TextRenderer>(r_, g.font);
    skin_ = std::make_unique<render::UiSkin>(r_, *text_, g.colours);
    command_ = std::make_unique<render::CommandPanel>(*skin_);
    iconSprites_ = std::make_unique<render::SpriteCache>(r_, g.sprites, g.colours);
    iconSprites_->setHq(true);
    sidebar_ = std::make_unique<render::Sidebar>(*skin_, *iconSprites_);
    sidebar_->setStrings(&g.text);
    hud_ = std::make_unique<render::PhoneHud>(*skin_, *iconSprites_);
    {  // the phone layout's quick-build pins and order counts (Settings::quickBuildPins / Counts)
        std::stringstream pins(settings_.quickBuildPins), counts(settings_.quickBuildCounts);
        for (std::string t; std::getline(pins, t, ',');)
            if (!t.empty()) quickPins_.push_back(std::atoi(t.c_str()));
        for (std::string t; std::getline(counts, t, ',');)
            if (const auto c = t.find(':'); c != std::string::npos) quickCounts_[std::atoi(t.c_str())] = std::atoi(t.c_str() + c + 1);
    }
    hudActiveNs_ = SDL_GetTicksNS();
    syncUiArt();
    uiSprites_ = std::make_unique<render::SpriteCache>(r_, g.ui, g.colours);
    if (g.ui.frames.size() >= 55) {
        render::ClassicSidebar::Assets a{g.ui, g.uibb, g.haveFontCol ? &g.fontCol : nullptr, g.fontColPalette,
                                         g.sprites, g.colours, g.templates};
        if (const std::string* t = g.text.find("Ready")) a.readyText = *t;
        if (const std::string* t = g.text.find("Hold")) a.holdText = *t;
        a.text = &g.text;
        a.tileAtr = &g.tileAtr;
        classic_ = std::make_unique<render::ClassicSidebar>(r_, std::move(a));
    } else {
        SDL_Log("UI_ENG.R16 missing or incomplete: the Original graphics mode uses the modern panel");
    }

    cam_.zoom = cfg_.zoom;
    camTilt_ = cfg_.cameraTilt ? render3d::radians(std::clamp(*cfg_.cameraTilt, 0.0f, 66.0f)) : render3d::Camera3D::kDefaultTilt;
    camYaw_ = cfg_.cameraYaw ? render3d::radians(*cfg_.cameraYaw) : 0.0f;
    sync3d();
    if (classicPeekAvailable()) ensureClassicPeek();  // the classic view key: instant from the first press
    last_ = SDL_GetTicksNS();
    createWorld();
    cursorMode_ = cfg_.cursorMode;
    // Phone layout screenshots: "drawer", "hide-ui", "radar-large", "radar-off", "idle", "awake", "place"
    // (the first buildable building follows --cursor-at), comma-separated.
    auto page = [&](const char* name) { return ("," + cfg_.sidebarPage + ",").find("," + std::string(name) + ",") != std::string::npos; };
    phoneDrawer_ = page("drawer") || page("starport") || page("upgrade");
    phoneHidden_ = page("hide-ui");
    phoneRadar_ = page("radar-large") ? 1 : page("radar-off") ? 2 : 0;
    hudIdleForced_ = page("idle");
    hudAwakeForced_ = page("awake");  // never fades (Simulator screenshots taken a while after the start)
    if (page("place")) {
        const auto b = world_->buildableBuildings(playerSide_);
        if (!b.empty()) placingType_ = b.size() > 2 ? b[2].type : b.front().type;
        phoneReopen_ = phoneDrawer_;
        phoneDrawer_ = false;
    }
    if (cfg_.sidebarPage == "starport") {
        if (classic_) classic_->showStarportPage();
        sidebar_->showStarport(true);
    } else if (cfg_.sidebarPage == "upgrade") {
        if (classic_) classic_->showUpgradePage();
        sidebar_->showUpgrades(true);
    } else if (cfg_.sidebarPage == "select-building" || cfg_.sidebarPage == "select-buildings") {
        // Screenshots of the selected-building overlay: the first / every selectable own building.
        auto& me = world_->sides()[playerSide_];
        for (int i = me.firstBuilding; i != -1; i = me.nextObject(i))
            if (g.templates.bin.buildings[me.buildings[i].type].flags & game::building_flags::kSelectable) {
                me.buildings[i].selected = true;
                if (cfg_.sidebarPage == "select-building") break;
            }
    } else if (cfg_.sidebarPage == "select-upgradable") {  // a building the UPGRADE page can upgrade
        auto& me = world_->sides()[playerSide_];
        const auto ups = world_->upgradableBuildings(playerSide_);
        for (int i = me.firstBuilding; i != -1 && !ups.empty(); i = me.nextObject(i))
            if (g.templates.bin.buildings[me.buildings[i].type].buildingGroup == ups.front().group) {
                me.buildings[i].selected = true;
                break;
            }
    }
    if (cfg_.at) {
        int vw = 0, vh = 0;
        SDL_GetCurrentRenderOutputSize(r_, &vw, &vh);
        cam_.x = cfg_.at->first * 32.0f - vw / (2 * cam_.zoom);
        cam_.y = cfg_.at->second * 32.0f - vh / (2 * cam_.zoom);
    }
}

Session::~Session() {
    // The day / night cycle continues in the next game from here (Settings::cyclePhase); previews and
    // one-frame runs never write settings (App::persist_), so only played games move it.
    if (!cfg_.presetPreview && settings_.timeOfDay == Settings::TimeOfDay::Cycle && lastCycleSeconds_ > 0)
        settings_.cyclePhase = std::fmod(lastCycleSeconds_, render::cycleLength());
    if (classicTarget_) SDL_DestroyTexture(classicTarget_);
    if (peekTarget_) SDL_DestroyTexture(peekTarget_);
}

void Session::toGame(float& x, float& y) const {
    if (present_.w <= 0 || present_.h <= 0) return;
    x = (x - present_.x) * float(gameW_) / present_.w;
    y = (y - present_.y) * float(gameH_) / present_.h;
}

void Session::toGameEvent(SDL_Event& e) const {
    const float sx = present_.w > 0 ? float(gameW_) / present_.w : 1.0f, sy = present_.h > 0 ? float(gameH_) / present_.h : 1.0f;
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: toGame(e.button.x, e.button.y); break;
    case SDL_EVENT_MOUSE_MOTION:
        toGame(e.motion.x, e.motion.y);
        e.motion.xrel *= sx;
        e.motion.yrel *= sy;
        break;
    case SDL_EVENT_MOUSE_WHEEL: toGame(e.wheel.mouse_x, e.wheel.mouse_y); break;
    default: break;
    }
}

void Session::createWorld() {
    const GameData& g = *data_;
    if (g.start.skirmish)
        world_ = std::make_unique<game::World>(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), skirmishSetupOf(g));
    else
        world_ = std::make_unique<game::World>(g.map, g.mission, g.tileAtr, g.tileData, rulesOf(g), playerSide_,
                                               g.playAs() ? g.missionSide : -1);
    game::World& world = *world_;
    if (!g.start.skirmish) world.difficulty = std::clamp(g.start.sk.difficulty, 0, 2);  // gDifficultyLevel
    if (cfg_.battle) orderBattle(world);
    if (cfg_.selectAll) {
        auto& me = world.sides()[playerSide_];
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) me.units[i].selected = true;
        for (int i = me.firstBuilding; i != -1; i = me.nextObject(i)) me.buildings[i].selected = true;
    }
    if (cfg_.killX >= 0) {
        int ks = -1, ki = -1;
        if (world.buildingAt(cfg_.killX, cfg_.killY, ks, ki)) world.destroyBuilding(world.sides()[ks].buildings[ki], false);
        SDL_Log("--kill-building %d %d: %s", cfg_.killX, cfg_.killY, ks >= 0 ? "destroyed" : "no building there");
    }
    if (("," + cfg_.sidebarPage + ",").find(",demo-base,") != std::string::npos) demoBase(world, playerSide_);
    // D2K_PREVIEW_SCENE (check): the Graphics previews' scene (a base, tanks, infantry) on this map too.
    if (cfg_.presetPreview || std::getenv("D2K_PREVIEW_SCENE")) presetPreviewScene(world, playerSide_);
    for (int t = 0; t < cfg_.ticks; ++t) world.tick();
    // Check of the build-up look (render/buildup_fx.hpp): D2K_BUILDUP=<0..1>[s] puts every building
    // at that point of its build-up (s: of its sale). Debug, before the first frame.
    if (const char* bu = std::getenv("D2K_BUILDUP")) {
        const float p = std::clamp(float(std::atof(bu)), 0.0f, 1.0f);
        const bool sell = std::strchr(bu, 's') != nullptr;
        const auto& T = world.rules().templates->bin;
        if (std::strchr(bu, 'g')) {  // g: a gallery from cell (2, 2): a row of buildings per house (sides 0..2)
            static const char* kHouse[] = {"ATREIDES ", "HARKONNEN ", "ORDOS "};
            static const char* kRow[] = {"CONSTRUCTION YARD", "REFINERY", "BARRACKS", "WIND TRAP", "HEAVY FACTORY", "PALACE"};
            for (int h = 0; h < 3; ++h) {
                world.sides()[std::size_t(h)].house = h;
                int x = 2;
                for (const char* n : kRow)
                    for (int ty = 0; ty < int(T.buildingCount); ++ty)
                        if (std::string(kHouse[h]) + n == T.buildingNames[ty]) {
                            world.addBuilding(h, ty, x, 2 + h * 5, true, true, true);
                            x += std::max(1, int(T.buildings[ty].artWidth) / 32) + 1;
                            break;
                        }
            }
        }
        for (auto& sd : world.sides())
            for (int i = sd.firstBuilding; i >= 0; i = sd.nextObject(i)) {
                auto& b = sd.buildings[std::size_t(i)];
                const auto& bt = T.buildings[b.type];
                const int n = bt.buildupArt < formats::kMaxBuildingTypes ? T.buildupArtFrames[bt.buildupArt] : 0;
                if (n <= 0 || bt.specialBehavior == game::building_behaviour::kWall ||
                    bt.specialBehavior == game::building_behaviour::kConcrete)
                    continue;
                b.flags |= sell ? game::building_flags::kSelling : game::building_flags::kBuildup;
                b.buildupCounter = std::uint8_t(std::clamp(int(std::lround(p * 3 * n)), sell ? 1 : 0, 3 * n - 1));
                SDL_Log("D2K_BUILDUP: %s at %d %d (side %d)", T.buildingNames[b.type], b.tileX, b.tileY, b.side);
            }
    }
    snd_.reset();  // FUN_0046f980 at mission start
    motion_.reset();
    minimap_->rebuild(world, playerSide_);
    // Open the view on the player's first building or unit (unless --at is given).
    if (!cfg_.at) {
        const auto& me = world.sides()[playerSide_];
        int tx = -1, ty = -1;
        if (me.firstBuilding != -1) { tx = me.buildings[me.firstBuilding].tileX + 1; ty = me.buildings[me.firstBuilding].tileY + 1; }
        else if (me.firstUnit != -1) { tx = me.units[me.firstUnit].tileX(); ty = me.units[me.firstUnit].tileY(); }
        if (tx >= 0) {
            layout();
            cam_.x = tx * 32.0f - viewW_ / (2 * cam_.zoom);
            cam_.y = ty * 32.0f - viewH_ / (2 * cam_.zoom);
        }
    }
    tickAccumulator_ = 0;
    messageLog_.clear();
    placingType_ = -1;
    endSeenAt_ = 0;
    leftDown_ = dragging_ = minimapDrag_ = false;
    cursorMode_ = 0;
    snapCamera_ = true;
}

void Session::restart() {
    SDL_Log("Restarting %s", data_->start.map.c_str());
    createWorld();
    paused_ = false;
    endSeenAt_ = 0;
    replayShown_ = false;
    last_ = SDL_GetTicksNS();
}

void Session::showPausePage(const std::string& name) {
    setPaused(true);
    if (name.rfind("pause-cheats", 0) == 0 && settings_.graphics == Settings::Graphics::Original) {
        openCheats(false);
        if (name.size() > 13) cheatMenu_->setTab(name.substr(13));
        return;
    }
    if (settings_.graphics == Settings::Graphics::Original) {
        pendingPage_ = name;
        return;
    }
    if (name == "pause-options") showPauseOptions();
    else if (name == "pause-save") showPauseSlots(true);
    else if (name == "pause-load") showPauseSlots(false);
    else if (name.rfind("pause-cheats", 0) == 0) {  // pause-cheats[-money|-tech|-build|-deliver|-spawn|-toggles]
        openCheats(true);
        if (name.size() > 13) cheatMenu_->setTab(name.substr(13));
    }
}

void Session::ensurePages(Ui& ui) {
    if (pages_) return;
    pages_ = std::make_unique<InGamePages>(r_, ui, settings_);
    const GameStart& st = data_->start;
    std::string recap;
    int house = 0, mission = 0;
    // (campaignHouse 3 = custom missions, 4 = community campaigns: platform/community_menu.hpp)
    if (!st.skirmish && st.campaignHouse < 3 && CampaignScreens::parseCampaignMap(st.map, house, mission)) {
        recap = std::string("IG") + "AHO"[house] + std::to_string(mission) + "MIS";
    } else if (!st.skirmish && st.campaignHouse >= 3) {
        // Custom missions: the patch's IG<house>0MIS page with the mission's own briefing.
        const int h = std::clamp(world_->sides()[std::size_t(std::clamp(playerSide_, 0, 7))].house, 0, 2);
        recap = std::string("IG") + "AHO"[h] + "0MIS";
    }
    pages_->setup(data_->dataDir, &data_->text, st.skirmish, recap, st.briefing);
    pages_->saveGame = [this](int slot, const std::string& name, std::string& err) { return saveToSlot(slot, err, name); };
}

// HandleBattlefieldClick 0x4433a0: INGAME / INGAME_SKIRMISH over the battlefield.
void Session::drawOriginalMenu(Ui& ui) {
    ensurePages(ui);
    if (pauseOpened_ || pages_->empty()) {  // never an empty stack while paused (no way out)
        pauseOpened_ = false;
        if (pendingPage_.empty() || !pages_->openByName(pendingPage_)) pages_->openMenu();
        pendingPage_.clear();
    }
    pages_->canSave = world_->gameEndState() == game::GameEnd::None && !savesDir().empty();
    pages_->gameRunning = world_->gameEndState() == game::GameEnd::None;
    const InGameAction a = pages_->frame();
    for (const MenuSound& m : pages_->takeSounds()) uiSound(m.name.c_str());
    switch (a.kind) {
    case InGameAction::None: break;
    case InGameAction::Cheats: if (settings_.cheats) openCheats(true); break;
    case InGameAction::Resume: setPaused(false); break;
    case InGameAction::Load:
        loadSlot_ = a.slot;
        result_ = SessionResult::LoadGame;
        break;
    case InGameAction::Abort: result_ = SessionResult::QuitToMenu; break;
    case InGameAction::Restart: restart(); break;
    }
}

void Session::setPaused(bool p) {
    if (p && !paused_) { pauseOpened_ = true; pauseOptions_ = false; pauseSlots_ = 0; }
    paused_ = p;
    leftDown_ = dragging_ = minimapDrag_ = false;
    last_ = SDL_GetTicksNS();
}

game::GameEnd Session::endState() const { return world_->gameEndState(); }

game::ScoreInput Session::scoreInput() const {
    game::ScoreInput in;
    for (int i = 0; i < game::kMaxSides; ++i) {
        const game::Side& s = world_->sides()[static_cast<std::size_t>(i)];
        in.sides[i] = {s.participates, s.house, s.unitsLost, s.buildingsLost, s.spiceHarvested,
                       s.unitsKilled, s.buildingsKilled, std::max(0, world_->playerColor(i))};
    }
    in.mySide = playerSide_;
    in.myHouse = std::clamp(world_->sides()[static_cast<std::size_t>(std::clamp(playerSide_, 0, game::kMaxSides - 1))].house, 0, 2);
    in.lost = endState() == game::GameEnd::Lost;
    return in;
}

bool Session::saveToSlot(int slot, std::string& error, const std::string& name) {
    game::SaveMeta m = saveMetaOf(data_->start, playerSide_);
    if (!name.empty()) m.name = name;
    m.savedAt = static_cast<std::int64_t>(std::time(nullptr));
    m.cameraX = cam_.x;
    m.cameraY = cam_.y;
    m.cameraZoom = cam_.zoom / ppp_;  // in points: the same view at either render resolution
    m.gameTicks = world_->gameTicks();
    for (const LogLine& l : messageLog_) m.messages.push_back({l.text, l.until});
    if (!writeSaveSlot(slot, game::writeSaveFile(m, *world_), error)) return false;
    mentatSpeech("GAMESAVED", true);  // FUN_00474e20
    SDL_Log("Saved %s (tick %u) to %s", m.name.c_str(), m.gameTicks, slotPath(slot).c_str());
    return true;
}

void Session::applySave(const game::SaveMeta& meta, std::span<const std::uint8_t> worldSection) {
    game::loadWorldState(*world_, worldSection);
    playerSide_ = world_->mySide();
    mapRenderer_->playerSide = playerSide_;
    cam_.x = meta.cameraX;
    cam_.y = meta.cameraY;
    if (meta.cameraZoom > 0.1f && settings_.enhanced()) cam_.zoom = meta.cameraZoom * ppp_;
    motion_.reset();
    snapCamera_ = true;
    minimap_->rebuild(*world_, playerSide_);
    tickAccumulator_ = 0;
    messageLog_.clear();
    placingType_ = -1;
    cursorMode_ = 0;
    endSeenAt_ = 0;
    leftDown_ = dragging_ = minimapDrag_ = false;
    last_ = SDL_GetTicksNS();
    snd_.reset();                       // FUN_0046f980 (LoadGame 0x4418b0)
    mentatSpeech("GAMELOADED", true);   // Game__MainLoopFrame 0x448bfa after a load
    SDL_Log("Loaded %s at tick %u (side %d)", meta.name.c_str(), world_->gameTicks(), playerSide_);
}

void Session::previewCamera(float x, float y, float zoom, float tiltDeg, float yawDeg) {
    layout();
    if (!classicUi()) cam_.zoom = zoom;
    camTilt_ = render3d::radians(std::clamp(tiltDeg, 0.0f, 66.0f));
    camYaw_ = render3d::radians(yawDeg);
    cam_.x = x - viewW_ / (2 * cam_.zoom);
    cam_.y = y - viewH_ / (2 * cam_.zoom);
    if (!r3d_) clampCamera(cam_, data_->scenario, viewW_, viewH_);
    snapCamera_ = true;
}

void Session::previewTick() {
    motion_.beforeTick(*world_);
    world_->tick();
    world_->clearMessages();
    tickAccumulator_ = 0;
}

SDL_Rect Session::battlefieldRect() const {
    if (classicUi()) {
        const SDL_Rect bf = classic_->battlefield();
        const float k = gameW_ > 0 ? present_.w / float(gameW_) : 1.0f;
        return SDL_Rect{int(present_.x + bf.x * k), int(present_.y + bf.y * k), int(bf.w * k), int(bf.h * k)};
    }
    return SDL_Rect{0, mapTop_, viewW_, viewH_};
}

void Session::runTicks(int n) {
    for (int t = 0; t < n; ++t) world_->tick();
    motion_.reset();
    minimap_->rebuild(*world_, playerSide_);
}

void Session::syncGraphics() {
    if (appliedGraphics_ == settings_.graphics) {
        syncVisuals();
        sync3d();
        return;
    }
    // Enhanced: Scale2x-upscaled sprites and tiles with smooth filtering and the modern panel.
    // Original: pixel-exact sprites, the original selection overlay and sidebar.
    appliedGraphics_ = settings_.graphics;
    const bool enhanced = settings_.enhanced();
    sprites_->setHq(enhanced);
    atlas_ = std::make_unique<render::TileAtlas>(r_, data_->tiles, enhanced);
    mapRenderer_->setAtlas(*atlas_);
    mapRenderer_->classicSelection = !enhanced;
    mapRenderer_->healthBars = static_cast<int>(settings_.healthBars);
    cursorMode_ = 0;
    placingType_ = -1;
    leftDown_ = dragging_ = minimapDrag_ = false;
    if (enhanced) cam_.zoom = cfg_.zoom * ppp_;
    // The two modes have separate pause menus (modern Ui pages / the original page stack). The
    // pause frame that consumed pauseOpened_ was the other mode's, so the original page stack
    // would stay empty: nothing drawn, no Resume, the game stuck paused (the reported hang after
    // switching Enhanced -> Original in the pause options). Open the new mode's menu afresh.
    pauseOpened_ = paused_;
    pauseOptions_ = false;
    pauseSlots_ = 0;
    SDL_Log("Graphics: %s", Settings::graphicsName(settings_.graphics));
    appliedArtPack_ = enhanced ? "upscaled" : "";  // the textures just built
    appliedTerrainKey_ = enhanced ? "hq|" : "classic";
    syncVisuals();
    sync3d();
    snapCamera_ = true;
    // The classic view's original-graphics resources live only while Enhanced / 3D is active.
    peekHeld_ = peekToggled_ = false;
    if (!enhanced) {
        peekMap_.reset();
        peekSprites_.reset();
        peekAtlas_.reset();
    } else if (classicPeekAvailable()) {
        ensureClassicPeek();
    }
}

render::Hdr* Session::hdrIfOn() {
    if (!settings_.hdr || appliedGraphics_ == Settings::Graphics::Original) return nullptr;
    if (!hdr_) hdr_ = std::make_unique<render::Hdr>(r_);
    return hdr_->available() ? hdr_.get() : nullptr;
}

void Session::sync3d() {
    const bool want = appliedGraphics_ == Settings::Graphics::ThreeD;
    if (!want) {
        r3d_.reset();
        return;
    }
    // Quality (MSAA, anisotropy, HDR scene): the pipelines depend on it - a change rebuilds the 3D renderer.
    render3d::Renderer3D::Quality q;
    q.msaa = settings_.msaa;
    q.aniso = settings_.aniso;
    q.hdr = hdrIfOn() != nullptr;
    if (r3d_ && !(r3d_->quality() == q)) {
        SDL_Log("Graphics: 3D quality changed - rebuilding the 3D renderer");
        r3d_.reset();
    }
    if (!r3d_ && !threeDFailed_) {
        r3d_ = render3d::Renderer3D::create(r_, *sprites_, data_->templates, q);
        if (!r3d_) {
            threeDFailed_ = true;
            SDL_Log("Graphics: 3D is not available here - showing the Enhanced battlefield instead");
        }
        snapCamera_ = true;
    }
    if (r3d_) r3d_->setAtlas(*atlas_);
    syncModels();
}

// Graphics: 3D "3D models" (Settings::models3d, docs/3d-models.md): (re)load the PS1 disc's
// models or the 3D art pack's glTF files when the choice changes; types without a model (and failures)
// are billboards of the 3D art pack's redesigned frames (syncVisuals), never the original sprites.
void Session::syncModels() {
    if (!r3d_) return;
    using M = Settings::Models3D;
    const M src = settings_.models3d;
    const std::string pack = src == M::New ? render3d::ModelRenderer::packPath(settings_.modelPack) : std::string();
    const std::string key = std::string(Settings::models3dName(src)) + "|" + (src == M::Ps1 ? settings_.ps1Image : pack);
    if (key == appliedModels_ && models3d_) {
        r3d_->setModelHook(models3d_.get());  // a re-created Renderer3D
        return;
    }
    const auto rs = src == M::Ps1 ? render3d::ModelRenderer::Source::Ps1 : render3d::ModelRenderer::Source::Pack;
    if (asyncModels_) {
        if (modelsJob_.valid() && modelsJobKey_ != key) modelsJob_.wait(), modelsJob_ = {};  // a stale choice: dropped
        if (!modelsJob_.valid()) {
            if (src == M::New && pack.empty()) SDL_Log("3D models: no model pack found (artpacks/<name>/models)");
            modelsJobKey_ = key;
            r3d_->setModelHook(nullptr);  // billboards until the models are in
            modelsJob_ = std::async(std::launch::async, [t = &data_->templates, c = &data_->colours, ma = &data_->modArt, rs,
                                                         ps1 = settings_.ps1Image, pack] {
                const Uint64 t0 = SDL_GetTicks();
                auto m = std::make_unique<render3d::ModelRenderer>(*t, *c);
                m->setModelKeys(ma->unitKeyName, ma->unitKeyGroup, ma->buildingKeyName, ma->buildingKeyGroup);  // (as below)
                m->load(rs, ps1, pack);
                SDL_Log("3D models: %d unit + %d building models read in %llu ms (background)", m->unitModels(), m->buildingModels(),
                        (unsigned long long)(SDL_GetTicks() - t0));
                return std::make_shared<std::unique_ptr<render3d::ModelRenderer>>(std::move(m));
            });
            return;
        }
        if (modelsJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        models3d_ = std::move(*modelsJob_.get());  // valid() is false from here
        appliedModels_ = key;
        r3d_->setModelHook(models3d_.get());
        snapCamera_ = true;
        logModelCoverage();
        return;
    }
    appliedModels_ = key;
    if (!models3d_) {
        models3d_ = std::make_unique<render3d::ModelRenderer>(data_->templates, data_->colours);
        // A community campaign's templates: models by the matching base type; its own art: no model
        // (billboards of its sprites; render/mod_art.hpp).
        const render::ModArt& ma = data_->modArt;
        models3d_->setModelKeys(ma.unitKeyName, ma.unitKeyGroup, ma.buildingKeyName, ma.buildingKeyGroup);
    }
    if (src == M::New && pack.empty()) SDL_Log("3D models: no model pack found (artpacks/<name>/models)");
    const Uint64 t0 = SDL_GetTicks();
    models3d_->load(rs, settings_.ps1Image, pack);
    SDL_Log("3D models: %d unit + %d building models read in %llu ms", models3d_->unitModels(), models3d_->buildingModels(),
            (unsigned long long)(SDL_GetTicks() - t0));
    r3d_->setModelHook(models3d_.get());
    snapCamera_ = true;
    logModelCoverage();
}

// Which unit / building types have no model and so stand as billboards of the 3D art pack's frames
// (docs/3d-mode.md "Objects"): a summary line, and with D2K_3D_COVERAGE=1 one line per such type.
void Session::logModelCoverage() const {
    const auto& T = data_->templates;
    const render::ArtPack* art = sprites_->artPack();
    const char* e = std::getenv("D2K_3D_COVERAGE");
    const bool each = e && e[0] == '1';
    int noModel = 0, noArt = 0, campaign = 0;
    const render::ModArt& ma = data_->modArt;
    auto note = [&](const char* kind, int type, const char* name, int frame) {
        ++noModel;
        const bool has = art && frame >= 0 && art->hasFrame(frame);
        // (Renderer3D::objectSprite: a type / frame of a campaign's own art is its sprite's billboard)
        const bool own = !has && ma.active && (ma.ownFrame(frame) || (kind[0] == 'b' ? ma.touchedBuilding(type) : ma.touchedUnit(type)));
        if (own) ++campaign;
        else if (!has) ++noArt;
        if (each)
            SDL_Log("3D coverage: %s %d %s: no model, %s", kind, type, name,
                    has ? "billboard of the redesigned frame" : own ? "billboard of the campaign's own sprite"
                                                                : "no redesigned frame either (not drawn)");
    };
    for (int i = 0; i < int(T.bin.unitCount) && i < formats::kMaxUnitTypes; ++i) {
        const auto& u = T.bin.units[i];
        if (models3d_->hasUnitModel(i) || u.specialBehavior == 5) continue;  // (sandworms draw no sprite)
        const int a = u.unitArt;
        note("unit", i, T.bin.unitNames[i], a >= 0 && a < formats::kMaxUnitArt ? T.unitArtFirstFrame[std::size_t(a)] : -1);
    }
    for (int i = 0; i < int(T.bin.buildingCount) && i < formats::kMaxBuildingTypes; ++i) {
        if (models3d_->hasBuildingModel(i) || T.bin.buildings[i].specialBehavior == game::building_behaviour::kConcrete)
            continue;  // (concrete becomes cells, never an object)
        const int a = T.bin.buildings[i].buildingArt;
        note("building", i, T.bin.buildingNames[i],
             a >= 0 && a < formats::kMaxBuildingArt ? T.buildingArtFirstFrame[std::size_t(a)] + 1 : -1);
    }
    SDL_Log("3D coverage: %d unit / building types without a model, %d of them without a redesigned frame (art: %s)", noModel,
            noArt, art ? art->folder().c_str() : "none");
    if (campaign) SDL_Log("3D coverage: %d types drawn with the campaign's own sprites (render/mod_art.hpp)", campaign);
}

void Session::screenToWorld(float sx, float sy, float& wx, float& wy, bool units) const {
    if (view3d() && view3d()->ready()) {
        const bool hit = units ? view3d()->pick(*world_, sx, sy, wx, wy) : view3d()->screenToGround(sx, sy, wx, wy);
        if (hit) return;
    }
    wx = cam_.x + sx / cam_.zoom;
    wy = cam_.y + sy / cam_.zoom;
}

void Session::panView(float dx, float dy) {
    if (view3d()) {
        const float yaw = view3d()->camera().pose().yaw, c = std::cos(yaw), s = std::sin(yaw);
        const float x = c * dx - s * dy, y = s * dx + c * dy;
        dx = x;
        dy = y;
    }
    cam_.x += dx;
    cam_.y += dy;
}

bool Session::unitInScreenBox(const game::Unit& u, float x0, float y0, float x1, float y1) const {
    float sx = 0, sy = 0;
    if (!view3d() || !view3d()->worldToScreen(float(game::fixToPixel(u.posX)), float(game::fixToPixel(u.posY)),
                                      float(game::fixToPixel(u.posZ) & 0xff), sx, sy))
        return false;
    return std::min(x0, x1) < sx && sx < std::max(x0, x1) && std::min(y0, y1) < sy && sy < std::max(y0, y1);
}

// Enhanced: the art pack ("original" = unfiltered original pixels, "upscaled" = the built-in
// Scale2x/4x, else artpacks/<name>, falling back to "upscaled" frames) and the Modern effects.
// Original graphics: neither (pixel-exact).
void Session::syncVisuals() {
    const bool enhanced = appliedGraphics_ != Settings::Graphics::Original;
    mapRenderer_->setModernEffects(enhanced && settings_.effects == Settings::Effects::Modern);
    mapRenderer_->softShroud = enhanced && settings_.shroud == Settings::Shroud::Soft;
    mapRenderer_->healthBars = static_cast<int>(settings_.healthBars);
    std::string want = enhanced ? (settings_.artPack.empty() ? std::string("upscaled") : settings_.artPack) : "";
    // Graphics: 3D: the objects' art is the 3D art pack's redesigned frames (whatever the 2D "Art" row says;
    // App only enters 3D with one installed), so a type without a model never shows an original sprite.
    if (appliedGraphics_ == Settings::Graphics::ThreeD)
        if (const std::string p3 = render::ArtPack::threeDPack(settings_.modelPack); !p3.empty()) want = p3;
    if (skin_) syncUiArt();
    if (want != appliedArtPack_) {
        appliedArtPack_ = want;
        sprites_->setArtPack(nullptr);
        artPack_.reset();
        if (enhanced) {  // (Original: syncGraphics built the Original textures)
            if (want != "original" && want != "upscaled") artPack_ = render::ArtPack::open(want);
            // A community mission's own art never takes the pack's frames / tiles (render/mod_art.hpp).
            if (artPack_ && data_->modArt.active) artPack_->setModArt(&data_->modArt);
            const bool hq = want != "original";
            sprites_->setHq(hq);
            sprites_->setArtPack(artPack_.get());
            mapRenderer_->resetEffects();
            SDL_Log("Art: %s", want.c_str());
        }
        packAnchors_.load(artPack_.get(), data_->templates);
    }
    syncTerrain();
}

// The terrain's look, independent of the unit / building art and the effects. Enhanced (2D):
// Settings::terrain2d - Original = the original tiles (unfiltered), Upscaled = the built-in
// upscale or the chosen art pack's tiles, Designed = the designed terrain baked from above
// (render3d/terrain_bake.*; the upscaled atlas stays for its colour lock and as the fallback),
// Designed (faithful) = the same bake laid out after the original tiles.
// 3D keeps its tiles following the art setting (its own "3D terrain" row picks the style).
// Original graphics: the original tiles (syncGraphics).
void Session::syncTerrain() {
    const bool enhanced = appliedGraphics_ != Settings::Graphics::Original;
    const bool flat = appliedGraphics_ == Settings::Graphics::Enhanced;
    bool hq = enhanced;
    const render::ArtPack* pack = artPack_.get();
    if (flat) {
        hq = settings_.terrain2d != Settings::Terrain2D::Original;
        if (!hq) pack = nullptr;
    } else if (enhanced) {  // 3D: the tiles follow the "Art" row (the objects' 3D art pack has no say)
        hq = settings_.artPack != "original";
        if (!hq || settings_.artPack != appliedArtPack_) pack = nullptr;
    }
    const std::string key = !enhanced ? std::string("classic") : std::string(hq ? "hq|" : "orig|") + (pack ? appliedArtPack_ : "");
    if (key != appliedTerrainKey_) {
        appliedTerrainKey_ = key;
        if (enhanced) {
            const std::string tileset = std::filesystem::path(data_->tilesetName).stem().string();
            atlas_ = std::make_unique<render::TileAtlas>(r_, data_->tiles, hq, pack, tileset);
            mapRenderer_->setAtlas(*atlas_);
            if (r3d_) r3d_->setAtlas(*atlas_);
        }
    }
    const bool designed = flat && settings_.terrain2dBaked();
    if (!designed) {
        mapRenderer_->terrainLayer = nullptr;
        baker_.reset();
        return;
    }
    if (!baker_ && !bakerFailed_) {
        baker_ = render3d::TerrainBaker::create(r_);
        if (!baker_) bakerFailed_ = true;  // logged; the upscaled tiles stay
    }
    if (baker_)
        baker_->setStyle(settings_.terrain2d == Settings::Terrain2D::Faithful ? render3d::TerrainStyle::Faithful
                                                                               : render3d::TerrainStyle::Designed);
}

// Enhanced production icons: the chosen art pack, or - with the built-in "upscaled" art - the
// first installed pack with DATA frames, so the unit / building icons use AI-upscaled pictures
// whenever a pack exists. "original" keeps the HQ upscale of the original icons. (The UI chrome
// itself is procedural, UiSkin.)
void Session::syncUiArt() {
    std::string want;
    if (appliedGraphics_ != Settings::Graphics::Original && settings_.artPack != "original")
        want = settings_.artPack.empty() || settings_.artPack == "upscaled" ? std::string("*") : settings_.artPack;
    if (want == appliedUiPack_) return;
    appliedUiPack_ = want;
    iconSprites_->setArtPack(nullptr);
    uiArtPack_.reset();
    if (want == "*") {
        for (const auto& name : render::ArtPack::available()) {
            auto p = render::ArtPack::open(name);
            if (p && p->frameCount() > 0) {
                uiArtPack_ = std::move(p);
                break;
            }
        }
    } else if (!want.empty()) {
        uiArtPack_ = render::ArtPack::open(want);
    }
    if (uiArtPack_ && data_->modArt.active) uiArtPack_->setModArt(&data_->modArt);  // (campaign icons stay theirs)
    iconSprites_->setArtPack(uiArtPack_.get());
    if (uiArtPack_) SDL_Log("Icon art: %s", uiArtPack_->title().c_str());
}

// ---- Classic view (Settings::classicView; docs/enhanced-graphics.md "Classic view") ----

void Session::classicPeekPress(bool down) {
    if (!classicPeekAvailable()) {
        peekHeld_ = false;
        return;
    }
    if (settings_.classicView == Settings::ClassicView::Toggle) {
        if (down) peekToggled_ = !peekToggled_;
        peekHeld_ = false;
    } else {
        peekHeld_ = down;
    }
}

bool Session::classicPeek() const {
    if (!classicPeekAvailable()) return false;
    if (cfg_.classicPeek) return true;
    return settings_.classicView == Settings::ClassicView::Toggle ? peekToggled_ : peekHeld_;
}

// The original-graphics battlefield resources, made once per session (Enhanced / 3D with the key
// enabled: at the start, so the first press is instant) and kept: the original tile atlas (the
// only sizeable part, tiles x 34 x 34 RGBA texels + its 32 x 32 RGBA CPU copy) and a 1x sprite
// cache whose (small) textures are built on the first frame that shows each sprite.
void Session::ensureClassicPeek() {
    if (peekMap_) return;
    const Uint64 t0 = SDL_GetTicksNS();
    const GameData& g = *data_;
    peekAtlas_ = std::make_unique<render::TileAtlas>(r_, g.tiles, false);
    peekSprites_ = std::make_unique<render::SpriteCache>(r_, g.sprites, g.colours);
    peekMap_ = std::make_unique<render::MapRenderer>(r_, *peekAtlas_, *peekSprites_, g.templates);
    // As in Graphics: Original (Session(), syncGraphics): the original selection overlay,
    // whole-pixel unit positions, no Modern effects, the original shroud edge pieces.
    peekMap_->classicSelection = true;
    peekMap_->softShroud = false;
    peekMap_->healthBars = 0;
    const int n = peekAtlas_->tileCount();
    const double mb = n * (double(render::TileAtlas::kStride) * render::TileAtlas::kStride + 32.0 * 32.0) * 4 / (1024.0 * 1024.0);
    double all = 0;  // every DATA.R16 frame once at 1x RGBA (the sprite cache's lazy upper bound, per house)
    for (const auto& f : g.sprites.frames) all += double(f.width) * f.height * 4;
    SDL_Log("Classic view: original tiles ready (%d tiles, ~%.1f MB texture + CPU copy) in %.1f ms; 1x sprites lazy "
            "(all frames once: %.1f MB)", n, mb, double(SDL_GetTicksNS() - t0) / 1e6, all / (1024.0 * 1024.0));
}

// The classic view: the battlefield drawn like Graphics: Original - one world pixel per texel at a
// whole-pixel camera into peekTarget_ - then scaled onto the Enhanced view at its zoom (whole
// factors nearest, others pixel-art filtering) and shifted by the camera's sub-pixel part, so it
// lines up with the Enhanced overlays (placement, rally flag, selection box) and the input mapping.
void Session::drawClassicPeek(const game::World& world, const game::Scenario& s, int viewW, int viewH) {
    ensureClassicPeek();
    const float z = std::max(0.05f, cam_.zoom);
    const float fx = std::floor(cam_.x), fy = std::floor(cam_.y);
    const int tw = int(std::ceil(viewW / z)) + 2, th = int(std::ceil(viewH / z)) + 2;
    float cw = 0, ch = 0;
    if (peekTarget_) SDL_GetTextureSize(peekTarget_, &cw, &ch);
    if (!peekTarget_ || int(cw) != tw || int(ch) != th) {
        if (peekTarget_) SDL_DestroyTexture(peekTarget_);
        peekTarget_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
        if (!peekTarget_) return;
        SDL_SetTextureBlendMode(peekTarget_, SDL_BLENDMODE_NONE);
    }
    peekMap_->playerSide = playerSide_;
    peekMap_->showShroud = mapRenderer_->showShroud;
    peekMap_->showMarkers = mapRenderer_->showMarkers;
    SDL_Texture* prev = SDL_GetRenderTarget(r_);
    SDL_Rect vp{}, clip{};
    const bool vpSet = SDL_RenderViewportSet(r_);
    if (vpSet) SDL_GetRenderViewport(r_, &vp);
    const bool clipOn = SDL_RenderClipEnabled(r_);
    if (clipOn) SDL_GetRenderClipRect(r_, &clip);
    SDL_SetRenderTarget(r_, peekTarget_);
    SDL_SetRenderViewport(r_, nullptr);
    SDL_SetRenderClipRect(r_, nullptr);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    peekMap_->draw(world, s, render::Camera{fx, fy, 1.0f}, tw, th);
    SDL_SetRenderTarget(r_, prev);
    SDL_SetRenderViewport(r_, vpSet ? &vp : nullptr);
    SDL_SetRenderClipRect(r_, clipOn ? &clip : nullptr);
    SDL_SetTextureScaleMode(peekTarget_, z < 1.0f ? SDL_SCALEMODE_LINEAR
                                         : std::abs(z - std::round(z)) < 1e-3f ? SDL_SCALEMODE_NEAREST
                                                                               : SDL_SCALEMODE_PIXELART);
    // The original's screen shake (gScreenShakeOffset: the frame shifted by -2..2 pixels).
    const float shake = float(world.screenShakeOffset());
    const SDL_FRect dst{(fx - cam_.x) * z, (fy - cam_.y + shake) * z, tw * z, th * z};
    SDL_RenderTexture(r_, peekTarget_, nullptr, &dst);
}

// "Original 1998 graphics": a small caption at the top centre of the battlefield while the
// classic view is on.
void Session::drawClassicPeekLabel() {
    const std::string t = render::tr("Original 1998 graphics");
    const float ts = skin_->ts();
    const float w = skin_->textW(t, ts), h = skin_->textH(ts);
    const float padX = skin_->px(6), padY = skin_->px(2);
    const float x = std::round(viewW_ / 2.0f - w / 2), y = std::round(command_->topBarH() + skin_->px(6));
    skin_->fill(SDL_FRect{x - padX, y - padY, w + 2 * padX, h + 2 * padY}, render::Colour{0, 0, 0, 150});
    skin_->label(t, x, y, render::Colour{240, 225, 190, 255}, ts);
}

// PlayUnitResponse 0x49f240: the selected unit with the highest voice priority speaks
// voices[(order ? 9 : 0) + house*3 + gameTicks%3] (house clamped to the three playable),
// queued on the speech channel only while nothing else waits (QueueAudioToPlay(id, 0, 0, 0)).
void Session::unitResponse(bool order) {
    const GameData& g = *data_;
    const auto& me = world_->sides()[playerSide_];
    int best = -1;
    std::uint32_t prio = 0;
    for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
        const auto& u = me.units[i];
        if (!u.selected) continue;
        const std::uint32_t p = g.templates.bin.units[u.type].voicePriority;
        if (prio <= p) { prio = p; best = u.type; }
    }
    if (best >= 0) unitVoice(best, order);
}

void Session::unitVoice(int type, bool order) {
    const auto& me = world_->sides()[playerSide_];
    const int house = std::min(2, std::max(0, int(me.house)));
    queueSpeech(int(data_->templates.bin.units[type].voices[(order ? 9 : 0) + house * 3 + world_->gameTicks() % 3]), false);
}

// ---------------------------------------------------------------------------------------
// Sound (re/notes/audio.md)

int Session::sfxVolume() const { return int(std::lround(std::clamp(settings_.soundVolume, 0.0f, 1.0f) * 100)); }

// The battlefield view in map pixels (BattleFieldPosX/Y, BattleFieldWidth/Height).
game::SoundView Session::soundView() const {
    return {int(cam_.x), int(cam_.y), int(viewW_ / cam_.zoom), int(viewH_ / cam_.zoom)};
}

void Session::queueSpeech(int sample, bool queue, int throttle, int priority) {
    if (!out_->ok() || sample < 0) return;
    snd_.queue(*out_, sample, queue, throttle, priority, world_->gameTicks());
}

void Session::mentatSpeech(const char* name, bool queue) {
    if (!sounds_) return;
    static const char* const kPrefix[3] = {"S_", "H_", "O_"};  // PlayMentatSound: Atreides, Harkonnen, Ordos
    const int house = world_->sides()[playerSide_].house;
    if (house < 0 || house > 2) return;
    queueSpeech(sounds_->idOf(std::string(kPrefix[house]) + name), queue);
}

void Session::uiSound(const char* name) {
    if (out_->ok()) game::SoundSystem::playUi(*out_, sounds_->idOf(name), sfxVolume());
}

// Sample id of a world sound event (GetSoundTableID of its name; "%s%d" for numbered ones;
// PlayMentatSound's house prefix).
int Session::sampleOf(const game::SoundEvent& ev) const {
    if (!ev.name) return ev.id;
    if (!sounds_) return -1;
    if (ev.house) {
        static const char* const kPrefix[3] = {"S_", "H_", "O_"};
        const int house = world_->sides()[playerSide_].house;
        if (house < 0 || house > 2) return -1;
        return sounds_->idOf(std::string(kPrefix[house]) + ev.name);
    }
    if (ev.variant != 0) return sounds_->idOf(std::string(ev.name) + std::to_string(ev.variant));
    return sounds_->idOf(ev.name);
}

void Session::playSoundEvents(const game::World& world) {
    if (!out_->ok()) return;
    const game::SoundView view = soundView();
    const int sfx = sfxVolume();
    for (const auto& ev : world.soundEvents()) {
        const int id = sampleOf(ev);
        if (id < 0) continue;
        if (ev.onScreen && !view.contains(ev.spotX, ev.spotY)) continue;
        if (ev.tileX >= 0) {
            if (ev.unique && game::SoundSystem::playing(*out_, id)) continue;
            game::SoundSystem::playAt(*out_, id, ev.tileX, ev.tileY, view, world.width(), sfx);
        } else {
            snd_.queue(*out_, id, ev.queue, ev.throttle, ev.priority, world.gameTicks());
        }
    }
}

// Top-left tile of the placement footprint, centred on the cursor. Concrete keeps the
// original anchor (Display__DrawConcretePlacementMarkers 0x4288d0: the cursor cell is the
// top-left of the 4x4 tilesOccupiedSolid mask).
std::pair<int, int> Session::placementTile(float sx, float sy) const {
    float wx = 0, wy = 0;
    screenToWorld(sx, sy, wx, wy, false);
    const auto& t = data_->templates.bin.buildings[std::max(0, placingType_)];
    if (t.specialBehavior == game::building_behaviour::kConcrete) return {int(wx) / 32, int(wy) / 32};
    const int fw = std::max(1, t.artWidth / 32), fh = std::max(1, t.artHeight / 32);
    return {int(wx) / 32 - (fw - 1) / 2, int(wy) / 32 - (fh - 1) / 2};
}

void Session::selectInRect(float x0, float y0, float x1, float y1, bool additive) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const bool click = (x1 - x0) < 4 && (y1 - y0) < 4;
    if (view3d() && !click) {  // 3D: the box is a screen rectangle, not a world one
        if (!additive) deselectAll();
        auto& me3 = world_->sides()[playerSide_];
        for (int i = me3.firstUnit; i != -1; i = me3.nextObject(i))
            if (me3.units[i].alive && unitInScreenBox(me3.units[i], x0, y0, x1, y1)) me3.units[i].selected = true;
        return;
    }
    if (view3d()) {  // 3D click: the picked world point as a tiny box
        float px = 0, py = 0;
        screenToWorld((x0 + x1) / 2, (y0 + y1) / 2, px, py);
        x0 = x1 = (px - cam_.x) * cam_.zoom;
        y0 = y1 = (py - cam_.y) * cam_.zoom;
    }
    float wx0 = cam_.x + x0 / cam_.zoom, wy0 = cam_.y + y0 / cam_.zoom;
    float wx1 = cam_.x + x1 / cam_.zoom, wy1 = cam_.y + y1 / cam_.zoom;
    if (click) { wx0 -= 12; wy0 -= 12; wx1 += 12; wy1 += 12; }
    auto& me = world_->sides()[playerSide_];
    auto& units = me.units;
    if (!additive) deselectAll();
    bool any = false;
    for (auto& u : units) {
        if (!u.alive) continue;
        const float px = float(game::fixToPixel(u.posX)), py = float(game::fixToPixel(u.posY));
        if (px >= wx0 && px <= wx1 && py >= wy0 && py <= wy1) {
            u.selected = true;
            any = true;
            if (click) break;
        }
    }
    if (click && !any) {
        // Clicked no unit: select an own building on that tile (GetBuildingOnTile_0).
        int bs = -1, bi = -1;
        const float cx = cam_.x + (x0 + x1) / 2 / cam_.zoom, cy = cam_.y + (y0 + y1) / 2 / cam_.zoom;
        int us = -1, ui = -1;
        if (world_->buildingAt(int(cx) / 32, int(cy) / 32, bs, bi) && bs == playerSide_) {
            me.buildings[bi].selected = true;
        } else if (!additive) {
            // Another side's unit or building: select it for viewing only (as in the original).
            const game::Unit* u = world_->unitAt(int(cx), int(cy), us, ui);
            if (u && !(u->flags & game::unit_flags::kExcluded)) world_->sides()[us].units[ui].selected = true;
            else if (bs >= 0) world_->sides()[bs].buildings[bi].selected = true;
        }
    }
}

// DeselectAllForAllSides: other sides' objects can be selected for viewing too.
void Session::deselectAll() {
    for (auto& sd : world_->sides()) {
        for (auto& u : sd.units) u.selected = false;
        for (auto& b : sd.buildings) b.selected = false;
    }
}

bool Session::anyUnitSelected() const {
    const auto& me = world_->sides()[playerSide_];
    for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
        if (me.units[i].selected) return true;
    return false;
}

// SelectAllUnitsInArea 0x4a5ae0: own units whose pixel position lies strictly inside the box
// (not carried/flying 0x140, not still leaving a factory, not state 0x11). Only when there is
// at least one does it replace the selection. Shift adds instead (not original).
bool Session::originalBoxSelect(float x0, float y0, float x1, float y1, bool additive) {
    const int wx0 = int(cam_.x + std::min(x0, x1) / cam_.zoom), wx1 = int(cam_.x + std::max(x0, x1) / cam_.zoom);
    const int wy0 = int(cam_.y + std::min(y0, y1) / cam_.zoom), wy1 = int(cam_.y + std::max(y0, y1) / cam_.zoom);
    auto& me = world_->sides()[playerSide_];
    auto inBox = [&](const game::Unit& u) {
        if ((u.state == game::UnitState::LeavingBuilding && u.posSteps == 0) || int(u.state) == 0x11) return false;
        if (u.flags & 0x140) return false;
        if (view3d()) return unitInScreenBox(u, x0, y0, x1, y1);
        const int px = game::fixToPixel(u.posX), py = game::fixToPixel(u.posY);
        return wx0 < px && px < wx1 && wy0 < py && py < wy1;
    };
    bool any = false;
    for (int i = me.firstUnit; i != -1 && !any; i = me.nextObject(i)) any = inBox(me.units[i]);
    if (!any) return false;
    if (!additive) deselectAll();
    for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
        if (inBox(me.units[i])) me.units[i].selected = true;
    return true;
}

// Left click on the battlefield in the original scheme (HandleBattlefieldClick 0x4433a0, mode 0,
// left button released; re/notes/timing.md §Input). Ctrl = force attack, Shift = toggle an own
// unit in the selection.
void Session::originalLeftClick(float sx, float sy) {
    game::World& world = *world_;
    const int side = playerSide_;
    auto& me = world.sides()[side];
    float fx = 0, fy = 0;
    screenToWorld(sx, sy, fx, fy);
    const int wx = int(fx), wy = int(fy);
    const int tx = wx >> 5, ty = wy >> 5;
    if (tx < 0 || ty < 0 || tx >= data_->scenario.width || ty >= data_->scenario.height) return;
    const SDL_Keymod mod = SDL_GetModState();
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0, shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool alt = (mod & SDL_KMOD_ALT) != 0;  // force move (crushers onto infantry)
    const bool anySel = anyUnitSelected();
    // World::smartOrder = the order part of 0x4433a0 (units, or a selected own turret when no
    // units are selected). Unit orders play the unit response; turret orders are silent.
    auto order = [&] {
        const game::SmartOrder r = world.smartOrder(side, wx, wy, ctrl, alt);
        if (r == game::SmartOrder::Move || r == game::SmartOrder::AttackUnit || r == game::SmartOrder::AttackBuilding ||
            r == game::SmartOrder::AttackTile)
            unitResponse(true);
        return r;
    };
    // Shrouded cell: move there (GenerateUnitMoveOrder) if anything is selected.
    if (mapRenderer_->showShroud && world.shrouded(tx, ty)) {
        if (anySel) {
            for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
                if (me.units[i].selected) world.orderMove(side, i, tx, ty);
            unitResponse(true);
        }
        return;
    }
    int us = -1, ui = -1;
    if (const game::Unit* u = world.unitAt(wx, wy, us, ui); u && !(u->flags & game::unit_flags::kExcluded)) {
        if (us == side && !ctrl) {
            game::Unit& own = me.units[ui];
            if (shift) {  // SelectUnit(.., toggle)
                own.selected = !own.selected;
            } else if (own.selected && world.canDeploy(side, ui)) {  // second click on a selected deployer (MCV, Devastator, thumper, saboteur)
                world.orderDeploy(side, ui);
                return;
            } else {  // SelectUnit(.., 0): deselect everything, select this one
                deselectAll();
                own.selected = true;
            }
            unitResponse(false);
            return;
        }
        // Another side's unit (or Ctrl): allied without Ctrl -> select it; with own units selected
        // attack it (Alt + enemy infantry + a crusher: drive over it); with none, a selected own
        // turret in reach fires at it, else select it for viewing only (0x4433a0).
        const game::SmartOrder r = order();
        const bool hidden = (u->flags & game::unit_flags::kCloaked) && world.diplomacy(side, us) != 0;
        if (r == game::SmartOrder::Friendly || (r == game::SmartOrder::None && !anySel && !hidden)) {
            deselectAll();
            world.sides()[us].units[ui].selected = true;
        }
        return;
    }
    int bs = -1, bi = -1;
    if (const game::Building* b = world.buildingAt(tx, ty, bs, bi)) {
        const auto& bt = data_->templates.bin.buildings[b->type];
        if (bs == side && !ctrl) {
            auto allHarvesters = [&] {
                bool any = false;
                for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
                    if (!me.units[i].selected) continue;
                    if (data_->templates.bin.units[me.units[i].type].specialBehavior != game::behaviour::kHarvester) return false;
                    any = true;
                }
                return any;
            };
            if (bt.specialBehavior == game::building_behaviour::kRefinery && allHarvesters()) {
                for (int i = me.firstUnit; i != -1; i = me.nextObject(i))  // GenerateDockWithRefineryOrder
                    if (me.units[i].selected) world.orderDockAtRefinery(side, i, bi);
                me.buildings[bi].flicker = 7;
                unitResponse(true);
                return;
            }
            if (bt.specialBehavior == game::building_behaviour::kRepairPad && anySel) {
                bool sent = false;  // GenerateRepairSelectedUnitsOrder: vehicles only
                for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
                    if (me.units[i].selected && !data_->templates.bin.units[me.units[i].type].isInfantry)
                        sent = world.orderRepairUnit(side, i, bi) || sent;
                if (sent) {
                    me.buildings[bi].flicker = 7;
                    unitResponse(true);
                    return;
                }
            }
            if (bt.flags & game::building_flags::kSelectable) {
                // A second click on a selected production building makes it primary.
                if (b->selected) {
                    if (world.orderSetPrimary(side, bi)) mentatSpeech("PRIMARY", false);
                }
                else if (!(b->flags & (game::building_flags::kBuildup | game::building_flags::kSelling))) {
                    deselectAll();
                    me.buildings[bi].selected = true;
                }
            }
            return;
        }
        // Another side's building (or Ctrl): allied without Ctrl -> select it; a selected own turret
        // fires at it when in range (checked before the units), else units attack it; when nothing
        // is ordered and no units (or a turret) are selected, select it (template flag 0x200).
        const game::SmartOrder r = order();
        const bool selectIt = r == game::SmartOrder::Friendly ||
                              (r == game::SmartOrder::None && (!anySel || world.selectedBarrelBuilding(side)));
        if (selectIt && (bt.flags & game::building_flags::kSelectable) &&
            !(b->flags & (game::building_flags::kBuildup | game::building_flags::kSelling))) {
            deselectAll();
            world.sides()[bs].buildings[bi].selected = true;
        }
        return;
    }
    order();  // open ground: move (Ctrl: attack the ground)
}

bool Session::centreFromMinimap(float sx, float sy) {
    float wx = 0, wy = 0;
    if (!world_->radarActive(playerSide_)) return false;  // no radar: the logo takes no clicks
    const SDL_FPoint p{sx, sy};
    if (!SDL_PointInRectFloat(&p, &minimapArea_)) return false;  // (phone: the radar may be hidden)
    if (!minimap_->screenToWorld(sx, sy, wx, wy)) return false;
    centreViewOn(wx, wy);
    return true;
}

void Session::centreViewOn(float wx, float wy) {
    cam_.x = wx - viewW_ / (2 * cam_.zoom);
    cam_.y = wy - viewH_ / (2 * cam_.zoom);
    const render3d::Renderer3D* v = view3d();
    if (!v || !v->ready()) return;
    // 3D: the camera looks at the centre of the drawn area (drawW_: the whole window under a
    // translucent panel), so the visible centre's ground point sits off the look-at point. That
    // offset depends only on zoom / yaw / tilt (ground plane), so subtract it.
    const render3d::Camera3D& c = v->camera();
    float gx = 0, gy = 0;
    c.groundPoint(viewW_ * 0.5f, viewH_ * 0.5f, gx, gy);
    const float lookX = wx - (gx - c.pose().cx), lookY = wy - (gy - c.pose().cy);
    cam_.x = lookX - drawW_ / (2 * cam_.zoom);
    cam_.y = lookY - viewH_ / (2 * cam_.zoom);
}

bool Session::viewFootprint3D(std::vector<SDL_FPoint>& poly) const {
    poly.clear();
    const render3d::Renderer3D* v = view3d();
    if (!v || !v->ready() || !world_) return false;
    const render3d::Camera3D& c = v->camera();
    // The four screen edges, sampled (a ray clamped at the far plane bends an edge).
    constexpr int kSteps = 8;
    const float w = float(viewW_), h = float(viewH_);
    const SDL_FPoint corner[5] = {{0, 0}, {w, 0}, {w, h}, {0, h}, {0, 0}};
    for (int e = 0; e < 4; ++e)
        for (int i = 0; i < kSteps; ++i) {
            const float t = float(i) / kSteps;
            float gx = 0, gy = 0;
            c.groundPoint(corner[e].x + (corner[e + 1].x - corner[e].x) * t,
                          corner[e].y + (corner[e + 1].y - corner[e].y) * t, gx, gy);
            poly.push_back({gx, gy});
        }
    // Sutherland-Hodgman against the map rectangle.
    const float mapW = world_->width() * 32.0f, mapH = world_->height() * 32.0f;
    auto clip = [&](auto inside, auto cut) {
        std::vector<SDL_FPoint> out;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const SDL_FPoint a = poly[i], b = poly[(i + 1) % poly.size()];
            const bool ia = inside(a), ib = inside(b);
            if (ia) out.push_back(a);
            if (ia != ib) out.push_back(cut(a, b));
        }
        poly.swap(out);
    };
    auto atX = [](SDL_FPoint a, SDL_FPoint b, float x) {
        const float t = (x - a.x) / (b.x - a.x);
        return SDL_FPoint{x, a.y + (b.y - a.y) * t};
    };
    auto atY = [](SDL_FPoint a, SDL_FPoint b, float y) {
        const float t = (y - a.y) / (b.y - a.y);
        return SDL_FPoint{a.x + (b.x - a.x) * t, y};
    };
    clip([](SDL_FPoint p) { return p.x >= 0; }, [&](SDL_FPoint a, SDL_FPoint b) { return atX(a, b, 0); });
    clip([&](SDL_FPoint p) { return p.x <= mapW; }, [&](SDL_FPoint a, SDL_FPoint b) { return atX(a, b, mapW); });
    clip([](SDL_FPoint p) { return p.y >= 0; }, [&](SDL_FPoint a, SDL_FPoint b) { return atY(a, b, 0); });
    clip([&](SDL_FPoint p) { return p.y <= mapH; }, [&](SDL_FPoint a, SDL_FPoint b) { return atY(a, b, mapH); });
    return true;
}

void Session::layout() {
    SDL_GetRenderOutputSize(r_, &outW_, &outH_);  // the window, also while classicTarget_ is bound
    if (cfg_.outputW > 0 && cfg_.outputH > 0) outW_ = cfg_.outputW, outH_ = cfg_.outputH;  // offscreen (previews)
    // Output pixels per window point (Render resolution: Full on a Retina display; also changes
    // when the window moves to another display). Sizes below are chosen in points and drawn in
    // pixels, so Full looks the same size as Half, only sharper. The map zoom is in pixels.
    if (const float ppp = cfg_.outputW > 0 ? 1.0f : pixelsPerPoint(r_); ppp != ppp_) {
        if (!classicUi()) {  // keep the view centre
            const float oz = cam_.zoom, nz = oz * ppp / ppp_;
            cam_.x += outW_ / (2 * oz) - outW_ / (2 * nz);
            cam_.y += outH_ / (2 * oz) - outH_ / (2 * nz);
            cam_.zoom = nz;
        }
        ppp_ = ppp;
    }
    const int ptW = std::max(1, int(std::lround(outW_ / ppp_))), ptH = std::max(1, int(std::lround(outH_ / ppp_)));
    phone_ = false;
    if (classicUi()) {
        // Original frame at the game resolution (scale 1), scaled to the window: an integer
        // factor when that fills most of it, else the exact fit (letterboxed either way).
        Settings::gameResolution(settings_.resolution, ptW, ptH, gameW_, gameH_);  // the same game size at either density
        const float fit = std::min(float(outW_) / float(gameW_), float(outH_) / float(gameH_));
        float s = fit;
        if (fit >= 1.0f && std::floor(fit) / fit >= 0.85f) s = std::floor(fit);
        present_ = SDL_FRect{std::floor((outW_ - gameW_ * s) / 2), std::floor((outH_ - gameH_ * s) / 2), gameW_ * s, gameH_ * s};
        classic_->layout(gameW_, gameH_);
        const SDL_Rect bf = classic_->battlefield();
        viewW_ = bf.w;
        viewH_ = bf.h;
        drawW_ = viewW_;
        mapTop_ = bf.y;
        cam_.zoom = 1.0f;
        return;
    }
    mapTop_ = 0;
    phone_ = world_ && phoneWanted(ptW, ptH);
    if (phone_) {
        layoutPhone(ptW, ptH);
        return;
    }
    // Settings::uiScale (Enhanced / 3D): auto = UiSkin::scaleFor, else a fixed percentage; read
    // every frame, so a change in the pause options applies at once.
    skin_->setScale(Settings::uiScaleFor(settings_.uiScale, render::UiSkin::scaleFor(ptW, ptH), ptW) * ppp_);
    viewW_ = std::max(64, outW_ - int(skin_->px(160)));
    viewH_ = outH_;
    // Settings "Panel transparency" (Enhanced, not original): the column is translucent and the
    // battlefield is drawn under it as well (drawW_); viewW_ stays the visible / clickable part
    // (camera limits, edge of the clickable map, view centre).
    const auto pt = settings_.panelTransparency;
    const float opacity = pt == Settings::PanelTransparency::Medium ? 0.4f : pt == Settings::PanelTransparency::Light ? 0.65f : 1.0f;
    skin_->setPanelOpacity(opacity, opacity >= 1.0f ? 1.0f : opacity + 0.05f);
    drawW_ = opacity < 1.0f ? outW_ : viewW_;
    command_->layout(float(viewW_), outW_, outH_);
    minimapArea_ = command_->radarRect();
}

bool Session::overUi(float x, float y) const {
    if (classicUi()) return false;
    if (phone_) {
        const SDL_FPoint p{x, y};
        return hud_->contains(x, y) || hud_->inDrawer(x, y) || SDL_PointInRectFloat(&p, &minimapArea_);
    }
    return x >= viewW_ || command_->contains(x, y) || sidebar_->contains(x, y);
}

void Session::controlGroup(int grp, bool assign) {
    auto& me = world_->sides()[playerSide_];
    if (!assign) deselectAll();  // also another side's object being viewed
    for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
        auto& u = me.units[i];
        if (!u.alive) continue;
        if (assign) {
            if (u.selected) u.groupId = static_cast<std::uint8_t>(grp);
            else if (u.groupId == grp) u.groupId = 0xFF;
        } else {
            u.selected = u.groupId == grp;
        }
    }
}

// Original UI input (HandleBattlefieldClick 0x4433a0): clicks outside the battlefield go to the
// sidebar / options bar; battlefield events are moved into map coordinates and handled by the
// common code (repair/sell/target modes first).
bool Session::handleClassicEvent(SDL_Event& e) {
    game::World& world = *world_;
    const bool ended = world.gameEndState() != game::GameEnd::None;
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const float x = e.button.x, y = e.button.y;
        if (e.button.button == SDL_BUTTON_RIGHT) cursorMode_ = 0;  // right press clears the click mode
        if (!classic_->inBattlefield(x, y)) {
            if (ended) return true;
            if (e.button.button == SDL_BUTTON_RIGHT) placingType_ = -1;
            const auto act = classic_->mouseDown(x, y, e.button.button, world, playerSide_, anyUnitSelected());
            classicAction(act);
            for (const auto& name : classic_->takeSounds())  // QueueAudioToPlay(id, 0, 0, 0)
                if (sounds_) queueSpeech(sounds_->idOf(name), false);
            return true;
        }
        e.button.y -= float(mapTop_);
        if (!ended && e.button.button == SDL_BUTTON_LEFT && cursorMode_ != 0 && placingType_ < 0) {
            classicMapClick(e.button.x, e.button.y);
            return true;
        }
        return false;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        classic_->mouseUp(e.button.button);
        if (e.button.button == SDL_BUTTON_LEFT) minimapDrag_ = false;
        e.button.y -= float(mapTop_);
        return false;
    case SDL_EVENT_MOUSE_MOTION:
        rawMouseX_ = e.motion.x;
        rawMouseY_ = e.motion.y;
        if (minimapDrag_) {
            int wx = 0, wy = 0;
            if (classic_->radarToWorld(e.motion.x, e.motion.y, world, playerSide_, wx, wy)) {
                cam_.x = wx - viewW_ / (2 * cam_.zoom);
                cam_.y = wy - viewH_ / (2 * cam_.zoom);
            }
        }
        e.motion.y -= float(mapTop_);
        return false;
    case SDL_EVENT_MOUSE_WHEEL: {
        classic_->wheel(e.wheel.mouse_x, e.wheel.mouse_y, e.wheel.y, world, playerSide_);
        return true;  // the original view has a fixed scale
    }
    default: return false;
    }
}

// Extended queue: left +1, Shift+left +10, right = infinite (again: cancel the type), Ctrl+right
// (or Cmd+right) = one fewer. Special weapons never get here as BuildUnit/CancelUnit orders that
// the extended queue accepts, so they keep the original orders.
void Session::unitIconClick(int type, bool cancel) {
    game::World& world = *world_;
    const int side = playerSide_;
    if (!settings_.extendedQueue()) {
        if (cancel) world.orderCancelUnit(side, type);
        else if (world.orderBuildUnit(side, type)) {
            ++quickCounts_[type];
            saveQuickBuild();
        }
        return;
    }
    const SDL_Keymod mod = SDL_GetModState();
    if (!cancel) {  // the phone layout's quick-build strip puts the most ordered types first
        quickCounts_[type] += (mod & SDL_KMOD_SHIFT) ? 10 : 1;
        saveQuickBuild();
    }
    bool queued = false;
    if (!cancel) queued = world.orderAutoAdd(side, type, (mod & SDL_KMOD_SHIFT) ? 10 : 1);
    else if (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) queued = world.orderAutoRemove(side, type);
    else queued = world.orderAutoInfinite(side, type);
    if (!queued) {  // not a queueable type (special weapons): the original orders
        if (cancel) world.orderCancelUnit(side, type);
        else world.orderBuildUnit(side, type);
    }
}

void Session::classicAction(const render::ClassicAction& a) {
    game::World& world = *world_;
    const int side = playerSide_;
    using A = render::ClassicAction;
    switch (a.kind) {
    case A::BuildBuilding: world.orderBuildBuilding(side, a.type); break;
    case A::CancelBuilding: world.orderCancelBuilding(side, a.type); placingType_ = -1; break;
    case A::BeginPlacement: placingType_ = a.type; cursorMode_ = 0; break;
    case A::BuildUnit: unitIconClick(a.type, false); break;
    case A::CancelUnit: unitIconClick(a.type, true); break;
    case A::SpecialWeapon: world.orderSpecialWeapon(side, a.type); break;
    case A::TargetWeapon: cursorMode_ = a.type == game::behaviour::kOrnithopter ? 4 : 5; weaponBehaviour_ = a.type; break;
    case A::Upgrade: world.orderUpgrade(side, a.type); break;
    case A::CancelUpgrade: world.orderCancelUpgrade(side, a.type); break;
    case A::RepairMode: cursorMode_ = cursorMode_ == 2 ? 0 : 2; placingType_ = -1; break;
    case A::SellMode: cursorMode_ = cursorMode_ == 3 ? 0 : 3; placingType_ = -1; break;
    // 0x4433a0: the mentat confirms guard / retreat (QueueAudioToPlay flag 0), no unit voice.
    case A::Guard: if (world.orderGuardSelected(side)) mentatSpeech("GUARDING", false); break;
    case A::Retreat: if (world.orderRetreatSelected(side)) mentatSpeech("RETREAT", false); break;
    case A::Options: setPaused(true); break;
    case A::RadarCentre:
        cam_.x = a.x - viewW_ / (2 * cam_.zoom);
        cam_.y = a.y - viewH_ / (2 * cam_.zoom);
        minimapDrag_ = true;
        break;
    case A::RadarMove: {
        auto& me = world.sides()[side];
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
            if (me.units[i].selected) world.orderMove(side, i, a.x, a.y);
        unitResponse(true);
        break;
    }
    case A::Deselect: deselectAll(); break;
    case A::StarportPick: world.orderStarportPick(side, a.type); break;
    case A::StarportUnpick: world.orderStarportUnpick(side, a.type); break;
    case A::StarportPurchase: world.orderStarportPurchase(side); break;
    case A::StarportCancel: world.orderStarportCancel(side); break;
    case A::None: break;
    }
}

// Left click on the battlefield in a click mode (0x4433a0 modes 2..5).
void Session::classicMapClick(float mx, float my) {
    game::World& world = *world_;
    const int side = playerSide_;
    float fx = 0, fy = 0;
    screenToWorld(mx, my, fx, fy);
    const int wx = int(fx), wy = int(fy);
    const int tx = wx >> 5, ty = wy >> 5;
    if (!world.inBounds(tx, ty)) return;
    if (cursorMode_ == 4 || cursorMode_ == 5) {
        world.orderSpecialWeapon(side, weaponBehaviour_, tx, ty);
        cursorMode_ = 0;
        return;
    }
    int bs = -1, bi = -1;
    if (world.buildingAt(tx, ty, bs, bi) && bs == side) {
        if (cursorMode_ == 2) world.orderRepairBuilding(side, bi);
        else world.orderSellBuilding(side, bi);
        return;
    }
    if (cursorMode_ == 2) {  // own damaged vehicle with a repair pad: send it there
        int us = -1, ui = -1;
        if (const game::Unit* u = world.unitAt(wx, wy, us, ui); u && us == side) {
            const auto& t = world.unitTemplate(*u);
            if (u->health < std::int32_t(t.hitPoints) && world.orderRepairUnit(side, ui)) {
                world.sides()[side].units[ui].flicker = 7;
                unitVoice(u->type, true);  // that unit's order voice (0x4433a0 repair mode)
            }
        }
    }
}

void Session::handleEvent(const SDL_Event& ev) {
    // Ctrl+Shift+C: the cheats page (also while paused; again closes it). Off with Settings::cheats.
    if (settings_.cheats && ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_C && !ev.key.repeat && (ev.key.mod & SDL_KMOD_CTRL) &&
        (ev.key.mod & SDL_KMOD_SHIFT) && result_ == SessionResult::None) {
        if (cheatsOpen_) {
            cheatsOpen_ = false;
            if (!cheatsFromPause_) setPaused(false);
        } else if (world_->gameEndState() == game::GameEnd::None) {
            openCheats(paused_);
        }
        return;
    }
    // Tab: the classic view (Enhanced / 3D; Settings::classicView Hold / Toggle). Also while paused
    // (the pause menu shows the battlefield behind it). The original binds no action to Tab; its
    // Space opens the in-game menu like Esc (Game__MainLoopFrame 0x448550, KeyIsDown1[VK_SPACE]).
    if ((ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) && ev.key.key == SDLK_TAB &&
        result_ == SessionResult::None && (classicPeekAvailable() || peekHeld_)) {
        if (ev.type == SDL_EVENT_KEY_UP) classicPeekPress(false);
        else if (!ev.key.repeat) classicPeekPress(true);
        if (!paused_) return;  // paused: the menu's Ui moves its focus with Tab too
    }
    if (paused_ || result_ != SessionResult::None) return;  // the pause menu handles input via Ui
    syncGraphics();
    layout();
    SDL_Event e = ev;
    if (classicUi()) toGameEvent(e);
    if (classicUi() && handleClassicEvent(e)) return;
    game::World& world = *world_;
    const int side = playerSide_;
    const bool ended = world.gameEndState() != game::GameEnd::None;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
        if (e.key.key == SDLK_ESCAPE) {
            if (cheatMenu_ && cheatMenu_->placing()) cheatMenu_->cancelPlacing();
            else if (placingType_ >= 0) placingType_ = -1;
            else if (!ended) setPaused(true);
            break;
        }
        if (e.key.key == SDLK_P && !ended) { setPaused(true); break; }
        if (view3d() && e.key.key == SDLK_HOME) {  // 3D: camera back to north-up, default tilt
            camYaw_ = std::round(camYaw_ / (2 * render3d::kPi)) * 2 * render3d::kPi;
            camTilt_ = render3d::Camera3D::kDefaultTilt;
            break;
        }
        if (e.key.key == SDLK_M) mapRenderer_->showMarkers = !mapRenderer_->showMarkers;
        if (e.key.key == SDLK_F) mapRenderer_->showShroud = !mapRenderer_->showShroud;
        {
            auto& me = world.sides()[side];
            if (e.key.key == SDLK_S)
                for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
                    if (me.units[i].selected) world.orderStop(side, i);
            if (e.key.key == SDLK_D)
                for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
                    if (me.units[i].selected && world.canDeploy(side, i)) world.orderDeploy(side, i);
            if (e.key.key >= SDLK_0 && e.key.key <= SDLK_9) {
                // Control groups (UI-side): Ctrl+N assigns, N selects; 0 is group 10.
                const int grp = e.key.key == SDLK_0 ? 10 : int(e.key.key - SDLK_1) + 1;
                controlGroup(grp, (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0);
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (view3d() && e.button.button == SDL_BUTTON_MIDDLE && e.button.x < viewW_) { middleDrag_ = true; break; }
        if (ended) break;  // the end prompt takes clicks
        if (!classicUi()) {
            const float x = e.button.x, y = e.button.y;
            const int button = e.button.button;
            // Every right press clears the repair / sell click mode (0x4433a0).
            if (button == SDL_BUTTON_RIGHT && (cursorMode_ == 2 || cursorMode_ == 3)) {
                cursorMode_ = 0;
                break;
            }
            hudActiveNs_ = SDL_GetTicksNS();
            if (phone_ && phoneMouseDown(x, y, button)) break;
            using C = render::CommandAction;
            const auto ca = phone_ ? render::CommandAction{} : command_->mouseDown(x, y, button, world, side);
            switch (ca.kind) {
            case C::Options: setPaused(true); break;
            case C::RepairMode: cursorMode_ = cursorMode_ == 2 ? 0 : 2; placingType_ = -1; break;
            case C::SellMode: cursorMode_ = cursorMode_ == 3 ? 0 : 3; placingType_ = -1; break;
            // 0x4433a0: the mentat confirms guard / retreat (QueueAudioToPlay flag 0), no unit voice.
            case C::Guard: if (world.orderGuardSelected(side)) mentatSpeech("GUARDING", false); break;
            case C::Retreat: if (world.orderRetreatSelected(side)) mentatSpeech("RETREAT", false); break;
            case C::Upgrade: sidebar_->showUpgrades(true); break;
            case C::Starport: sidebar_->showStarport(true); break;
            case C::None: break;
            }
            if (ca.kind != C::None) break;
            if (button == SDL_BUTTON_LEFT && centreFromMinimap(x, y)) {
                minimapDrag_ = true;
                break;
            }
            if (sidebarLive(x, y)) {
                const auto act = sidebar_->click(x, y, button, world, side);
                if (phone_) phoneSidebarAction(act.kind);
                using A = render::SidebarAction;
                switch (act.kind) {
                case A::BuildBuilding: world.orderBuildBuilding(side, act.type); break;
                case A::CancelBuilding: world.orderCancelBuilding(side, act.type); placingType_ = -1; break;
                case A::BeginPlacement: placingType_ = act.type; cursorMode_ = 0; break;
                case A::BuildUnit: unitIconClick(act.type, false); break;
                case A::CancelUnit: unitIconClick(act.type, true); break;
                case A::SpecialWeapon: world.orderSpecialWeapon(side, act.type); break;
                case A::TargetWeapon:
                    cursorMode_ = act.type == game::behaviour::kOrnithopter ? 4 : 5;
                    weaponBehaviour_ = act.type;
                    placingType_ = -1;
                    break;
                case A::StarportPick: world.orderStarportPick(side, act.type); break;
                case A::StarportUnpick: world.orderStarportUnpick(side, act.type); break;
                case A::StarportPurchase: world.orderStarportPurchase(side); break;
                case A::StarportCancel: world.orderStarportCancel(side); break;
                case A::Upgrade: world.orderUpgrade(side, act.type); break;
                case A::CancelUpgrade: world.orderCancelUpgrade(side, act.type); break;
                default: break;
                }
                break;
            }
            if (overUi(x, y)) break;  // the panels' frames take the click
            if ((cursorMode_ == 2 || cursorMode_ == 3) && button == SDL_BUTTON_LEFT && placingType_ < 0) {
                classicMapClick(x, y);  // repair / sell a building
                break;
            }
        }
        if ((cursorMode_ == 4 || cursorMode_ == 5) && e.button.x < viewW_) {  // air strike / Death Hand target
            if (e.button.button == SDL_BUTTON_LEFT) classicMapClick(e.button.x, e.button.y);
            else cursorMode_ = 0;
            break;
        }
        if (cheatMenu_ && cheatMenu_->placing() && e.button.x < viewW_) {  // cheat spawn placement
            if (e.button.button == SDL_BUTTON_LEFT) {
                const bool keep = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                float cwx = 0, cwy = 0;
                screenToWorld(e.button.x, e.button.y, cwx, cwy, false);
                if (!cheatMenu_->click(world, cwx, cwy, keep))
                    mentatSpeech("CANTBUILD", true);
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                cheatMenu_->cancelPlacing();
            }
            break;
        }
        if (placingType_ >= 0 && e.button.x < viewW_) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                const auto [px, py] = placementTile(e.button.x, e.button.y);
                if (world.canPlaceBuilding(side, placingType_, px, py) && world.orderPlaceBuilding(side, placingType_, px, py))
                    placingType_ = -1;
                else
                    mentatSpeech("CANTBUILD", true);  // 0x4433a0 placement mode
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                placingType_ = -1;
            }
            break;
        }
        if (e.button.button == SDL_BUTTON_RIGHT) { dragging_ = true; rightMoved_ = false; }
        if (e.button.button == SDL_BUTTON_LEFT) {
            if (!classicUi() && centreFromMinimap(e.button.x, e.button.y)) minimapDrag_ = true;
            else if (e.button.x < viewW_) { leftDown_ = true; leftX_ = e.button.x; leftY_ = e.button.y; }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_MIDDLE) middleDrag_ = false;
        if (!classicUi()) {
            command_->mouseUp();
            sidebar_->mouseUp();
        }
        if (e.button.button == SDL_BUTTON_RIGHT) {
            dragging_ = false;
            if (!rightMoved_ && e.button.x < viewW_ && !ended) {
                if (originalControls()) {
                    // Original: right click on the battlefield deselects everything
                    // (HandleBattlefieldClick 0x4433a0 -> DeselectAllForAllSides). It acts on
                    // release here so that a right drag can still pan the view.
                    deselectAll();
                } else {
                    // Modern: smart order for the selected units (or a selected own turret):
                    // attack the enemy unit/building under the cursor, else move there. Ctrl =
                    // force attack (any unit/building, else the tile), Alt = force move (crushers
                    // drive over enemy infantry). Own/allied objects: move onto them. A selected
                    // own factory (no units): set its rally point (platform/rally_ui.hpp).
                    float wx = 0, wy = 0;
                    screenToWorld(e.button.x, e.button.y, wx, wy);
                    const SDL_Keymod mod = SDL_GetModState();
                    if (settings_.rallyPoints && rallyClick(world, side, int(wx), int(wy))) {
                        uiSound("BUTTON");
                    } else {
                        game::SmartOrder r = world.smartOrder(side, int(wx), int(wy), (mod & SDL_KMOD_CTRL) != 0,
                                                              (mod & SDL_KMOD_ALT) != 0);
                        if (r == game::SmartOrder::Friendly && anyUnitSelected()) {
                            const int tx = int(wx) >> 5, ty = int(wy) >> 5;
                            const auto& mine = world.sides()[side];
                            for (int i = mine.firstUnit; i != -1; i = mine.nextObject(i))
                                if (mine.units[i].selected && mine.units[i].state != game::UnitState::Dead)
                                    world.orderMove(side, i, tx, ty);
                            r = game::SmartOrder::Move;
                        }
                        if (r == game::SmartOrder::Move || r == game::SmartOrder::AttackUnit ||
                            r == game::SmartOrder::AttackBuilding || r == game::SmartOrder::AttackTile)
                            unitResponse(true);
                    }
                }
            }
        }
        if (e.button.button == SDL_BUTTON_LEFT) {
            minimapDrag_ = false;
            if (leftDown_) {
                const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (originalControls()) {
                    // A box needs >= 4 px in both directions (0x4433a0), else it is a click.
                    const bool box = std::abs(e.button.x - leftX_) >= 4 && std::abs(e.button.y - leftY_) >= 4;
                    const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;  // Alt: factory rally point (not original)
                    float rwx = 0, rwy = 0;
                    if (!box && alt) screenToWorld(e.button.x, e.button.y, rwx, rwy, false);
                    if (!box && alt && settings_.rallyPoints && rallyClick(world, side, int(rwx), int(rwy)))
                        uiSound("BUTTON");
                    else if (!box) originalLeftClick(e.button.x, e.button.y);
                    else if (originalBoxSelect(leftX_, leftY_, e.button.x, e.button.y, shift)) unitResponse(false);
                } else {
                    selectInRect(leftX_, leftY_, e.button.x, e.button.y, shift);
                    unitResponse(false);
                }
            }
            leftDown_ = false;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        mouseX_ = e.motion.x;
        mouseY_ = e.motion.y;
        if (minimapDrag_ && !classicUi()) centreFromMinimap(e.motion.x, e.motion.y);
        if (dragging_) {
            if (std::abs(e.motion.xrel) + std::abs(e.motion.yrel) > 0) rightMoved_ = true;
            panView(-e.motion.xrel / cam_.zoom, -e.motion.yrel / cam_.zoom);
        }
        if (middleDrag_ && view3d()) {  // 3D: middle drag turns (x) and tilts (y) the camera
            camYaw_ += e.motion.xrel * 0.006f;
            camTilt_ = std::clamp(camTilt_ - e.motion.yrel * 0.005f, render3d::Camera3D::kMinTilt, render3d::Camera3D::kMaxTilt);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        // Zoom around the mouse cursor (or scroll the sidebar).
        float mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);  // window points -> output pixels
        const float ppp = windowToPixels(r_);
        mx *= ppp;
        my *= ppp;
        if (sidebarLive(mx, my)) { sidebar_->scroll(mx, my, e.wheel.y); break; }
        if (overUi(mx, my)) break;
        if (view3d() && (SDL_GetModState() & SDL_KMOD_CTRL)) {  // 3D: Ctrl+wheel tilts
            camTilt_ = std::clamp(camTilt_ - e.wheel.y * 0.06f, render3d::Camera3D::kMinTilt, render3d::Camera3D::kMaxTilt);
            break;
        }
        zoomAround(e.wheel.y > 0 ? 1.1f : 1 / 1.1f, mx, my);
        break;
    }
    default: break;
    }
}

// Zoom by `factor` keeping the point under (mx, my) (view pixels) in place.
void Session::zoomAround(float factor, float mx, float my) {
    if (view3d()) {
        // Zoom towards the terrain point under the cursor.
        const float oz = cam_.zoom, nz = std::clamp(oz * factor, 0.3f * ppp_, 4.0f * ppp_);
        float cx = cam_.x + viewW_ / (2 * oz), cy = cam_.y + viewH_ / (2 * oz), px = 0, py = 0;
        if (view3d()->screenToGround(mx, my, px, py)) {
            cx = px + (cx - px) * (oz / nz);
            cy = py + (cy - py) * (oz / nz);
        }
        cam_.zoom = nz;
        cam_.x = cx - viewW_ / (2 * nz);
        cam_.y = cy - viewH_ / (2 * nz);
        return;
    }
    const float wx = cam_.x + mx / cam_.zoom, wy = cam_.y + my / cam_.zoom;
    cam_.zoom = std::clamp(cam_.zoom * factor, 0.25f * ppp_, 4.0f * ppp_);
    cam_.x = wx - mx / cam_.zoom;
    cam_.y = wy - my / cam_.zoom;
}

// ---- Touch (platform/touch_controls.hpp) ----

bool Session::toBattlefield(float& x, float& y) const {
    if (classicUi()) {
        toGame(x, y);
        if (!classic_->inBattlefield(x, y)) return false;
        y -= float(mapTop_);
        return true;
    }
    return x >= 0 && y >= 0 && x < viewW_ && y < viewH_ && !overUi(x, y);
}

Session::TouchZone Session::touchZone(float x, float y) const {
    if (classicUi()) {
        float gx = x, gy = y;
        toGame(gx, gy);
        if (classic_->inBattlefield(gx, gy)) return TouchZone::Battlefield;
        int wx = 0, wy = 0;
        if (classic_->radarToWorld(gx, gy, *world_, playerSide_, wx, wy)) return TouchZone::Radar;
        return TouchZone::Panel;
    }
    const SDL_FPoint p{x, y};
    if (SDL_PointInRectFloat(&p, &minimapArea_)) return TouchZone::Radar;
    if (phone_) {
        if (hud_->inDrawer(x, y) && !hud_->contains(x, y)) return TouchZone::Panel;
        if (hud_->contains(x, y)) return TouchZone::Hud;
        return TouchZone::Battlefield;
    }
    if (overUi(x, y) || x >= viewW_) return TouchZone::Panel;
    return TouchZone::Battlefield;
}

SDL_FRect Session::touchBattlefield() const {
    if (classicUi() && gameW_ > 0 && gameH_ > 0) {
        const SDL_Rect bf = classic_->battlefield();
        const float kx = present_.w / float(gameW_), ky = present_.h / float(gameH_);
        return {present_.x + bf.x * kx, present_.y + bf.y * ky, bf.w * kx, bf.h * ky};
    }
    return {0, 0, float(viewW_), float(viewH_)};
}

bool Session::touchPlacing() const { return placingType_ >= 0 || (cheatMenu_ && cheatMenu_->placing()); }

void Session::touchPan(float dx, float dy) {
    if (classicUi()) {  // window pixels -> game pixels (the original view is 1:1 with the world)
        if (present_.w > 0) dx *= float(gameW_) / present_.w;
        if (present_.h > 0) dy *= float(gameH_) / present_.h;
        panView(-dx, -dy);
        return;
    }
    panView(-dx / cam_.zoom, -dy / cam_.zoom);
}

void Session::touchZoom(float factor, float x, float y) {
    if (classicUi() || !(factor > 0)) return;
    zoomAround(factor, x, y);
}

void Session::touchRotate(float radians) {
    if (view3d()) camYaw_ += radians;
}

bool Session::touchTapNothing(float x, float y) {
    if (world_->gameEndState() != game::GameEnd::None || cursorMode_ != 0 || touchPlacing() || anyUnitSelected()) return false;
    if (!toBattlefield(x, y)) return false;
    float fx = 0, fy = 0;
    screenToWorld(x, y, fx, fy);
    const int wx = int(fx), wy = int(fy);
    int us = -1, ui = -1, bs = -1, bi = -1;
    if (world_->inBounds(wx >> 5, wy >> 5) &&
        (world_->unitAt(wx, wy, us, ui) || world_->buildingAt(wx >> 5, wy >> 5, bs, bi)))
        return false;
    deselectAll();
    return true;
}

bool Session::touchSelectType(float x, float y) {
    if (world_->gameEndState() != game::GameEnd::None || cursorMode_ != 0 || touchPlacing()) return false;
    if (!toBattlefield(x, y)) return false;
    float fx = 0, fy = 0;
    screenToWorld(x, y, fx, fy);
    int us = -1, ui = -1;
    const game::Unit* hit = world_->unitAt(int(fx), int(fy), us, ui);
    if (!hit || us != playerSide_ || (hit->flags & game::unit_flags::kExcluded)) return false;
    const int type = hit->type;
    deselectAll();
    auto& me = world_->sides()[playerSide_];
    const float x0 = cam_.x, y0 = cam_.y, x1 = cam_.x + viewW_ / cam_.zoom, y1 = cam_.y + viewH_ / cam_.zoom;
    for (int i = me.firstUnit; i != -1; i = me.nextObject(i)) {
        game::Unit& u = me.units[i];
        if (!u.alive || u.type != type || (u.flags & 0x140)) continue;
        bool on = false;
        if (view3d()) on = unitInScreenBox(u, 0, 0, float(viewW_), float(viewH_));
        else {
            const float px = float(game::fixToPixel(u.posX)), py = float(game::fixToPixel(u.posY));
            on = px >= x0 && px < x1 && py >= y0 && py < y1;
        }
        if (on || i == ui) u.selected = true;
    }
    unitResponse(false);
    return true;
}

void Session::touchCommand(TouchCommand c) {
    if (result_ != SessionResult::None) return;
    game::World& world = *world_;
    const int side = playerSide_;
    const bool ended = world.gameEndState() != game::GameEnd::None;
    auto& me = world.sides()[side];
    switch (c) {
    case TouchCommand::Stop:
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
            if (me.units[i].selected) world.orderStop(side, i);
        break;
    case TouchCommand::Deploy:
        for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
            if (me.units[i].selected && world.canDeploy(side, i)) world.orderDeploy(side, i);
        break;
    case TouchCommand::Guard:
        if (world.orderGuardSelected(side)) mentatSpeech("GUARDING", false);
        break;
    case TouchCommand::Deselect:  // three-finger tap / the X button: the original's right click
        if (!ended) {
            cursorMode_ = 0;
            deselectAll();
        }
        break;
    case TouchCommand::Menu:
        if (cheatMenu_ && cheatMenu_->placing()) cheatMenu_->cancelPlacing();
        else if (placingType_ >= 0) placingType_ = -1;
        else if (cursorMode_ != 0) cursorMode_ = 0;
        else if (!ended) setPaused(true);
        break;
    }
}

bool Session::touchGroupUsed(int grp) const {
    const auto& me = world_->sides()[playerSide_];
    for (int i = me.firstUnit; i != -1; i = me.nextObject(i))
        if (me.units[i].alive && me.units[i].groupId == grp) return true;
    return false;
}

// ---- Phone layout (render/phone_hud.hpp, docs/ios.md "Phone layout") ----

bool Session::phoneWanted(int ptW, int ptH) const {
    if (classicUi()) return false;
    switch (settings_.touchLayout) {
    case Settings::TouchLayout::Phone: return true;
    case Settings::TouchLayout::Tablet: return false;
    case Settings::TouchLayout::Auto: break;
    }
    return kTouchPlatform && (isPhoneDevice() || std::min(ptW, ptH) < 500);
}

float Session::phoneIconPx() const {
    const auto z = settings_.phoneUiSize;
    return std::round((z == Settings::PhoneUiSize::Tiny ? 22.0f : z == Settings::PhoneUiSize::Normal ? 30.0f : 26.0f) * ppp_);
}

void Session::layoutPhone(int ptW, int ptH) {
    // The drawer is the Sidebar's 160-pixel column at a scale that follows the UI size.
    const auto z = settings_.phoneUiSize;
    const float k = z == Settings::PhoneUiSize::Tiny ? 1.15f : z == Settings::PhoneUiSize::Normal ? 1.45f : 1.3f;
    skin_->setScale(k * ppp_);
    viewW_ = outW_;  // the battlefield is the whole screen
    viewH_ = outH_;
    drawW_ = outW_;
    const auto pt = settings_.panelTransparency;
    const float opacity = pt == Settings::PanelTransparency::Medium ? 0.4f : pt == Settings::PanelTransparency::Light ? 0.65f : 1.0f;
    skin_->setPanelOpacity(opacity, opacity >= 1.0f ? 1.0f : opacity + 0.05f);
    // Safe area (notch / Dynamic Island / home indicator), window points -> output pixels.
    // SDL's safe area is in window coordinates (windowToPixels: = points except on Android, where they are pixels).
    SDL_Rect sa{0, 0, ptW, ptH};
    float saScale = ppp_;
    if (SDL_Window* win = SDL_GetRenderWindow(r_); win && SDL_GetWindowSafeArea(win, &sa) && sa.w > 0 && sa.h > 0)
        saScale = windowToPixels(win);
    else
        sa = SDL_Rect{0, 0, ptW, ptH};
    if (cfg_.safeInsets) {
        const auto& in = *cfg_.safeInsets;
        sa = SDL_Rect{int(in[0]), int(in[1]), std::max(1, ptW - int(in[0] + in[2])), std::max(1, ptH - int(in[1] + in[3]))};
        saScale = ppp_;
    }
    safe_ = SDL_FRect{sa.x * saScale, sa.y * saScale, sa.w * saScale, sa.h * saScale};
    render::PhoneHud::Params p;
    p.outW = outW_;
    p.outH = outH_;
    p.ppp = ppp_;
    p.safe = safe_;
    p.iconPt = phoneIconPx() / ppp_;
    p.maxQuick = z == Settings::PhoneUiSize::Normal ? 5 : 6;
    p.hidden = phoneHidden_;
    p.drawerOpen = phoneDrawer_ && !phoneHidden_;
    p.radarMode = phoneRadar_;
    p.radarActive = world_->radarActive(playerSide_);
    p.mapW = data_->scenario.width;
    p.mapH = data_->scenario.height;
    p.drawerW = skin_->px(160);
    p.pinned = quickPins_;
    p.counts = &quickCounts_;
    p.cursorMode = cursorMode_;
    p.placing = touchPlacing();
    p.freezeOrder = !quickResort_ && SDL_GetTicksNS() - hudActiveNs_ < 4'000'000'000ull;
    quickResort_ = false;
    hud_->layout(p, *world_, playerSide_);
    minimapArea_ = hud_->radarRect();
    if (minimapArea_.w > 0) minimapArea_ = minimap_->placement(minimapArea_);  // the frame hugs the map
}

float Session::phoneAlpha() const {
    constexpr float kIdle = 0.5f;  // semi-transparent, fading further after 4 s without input
    if (hudIdleForced_) return kIdle;
    if (hudAwakeForced_) return 1.0f;
    const double idle = double(SDL_GetTicksNS() - hudActiveNs_) / 1e9;
    if (idle < 4) return 1.0f;
    if (idle > 5) return kIdle;
    return 1.0f - (1.0f - kIdle) * float(idle - 4);
}

render::UiSkin* Session::phoneSkin() const { return phone_ ? skin_.get() : nullptr; }

void Session::touchActivity() {
    hudActiveNs_ = SDL_GetTicksNS();
    hudIdleForced_ = false;
}

int Session::touchQuickType(float x, float y) const { return phone_ && !phoneHidden_ ? hud_->quickTypeAt(x, y) : -1; }

bool Session::touchQuickPinned(int type) const { return std::find(quickPins_.begin(), quickPins_.end(), type) != quickPins_.end(); }

void Session::touchTogglePin(int type) {
    if (type < 0) return;
    if (const auto f = std::find(quickPins_.begin(), quickPins_.end(), type); f != quickPins_.end()) quickPins_.erase(f);
    else quickPins_.push_back(type);
    quickResort_ = true;  // pins apply at once
    saveQuickBuild();
}

void Session::saveQuickBuild() {
    std::string pins, counts;
    for (int t : quickPins_) pins += (pins.empty() ? "" : ",") + std::to_string(t);
    for (const auto& [t, n] : quickCounts_) counts += (counts.empty() ? "" : ",") + std::to_string(t) + ":" + std::to_string(n);
    settings_.quickBuildPins = pins;
    settings_.quickBuildCounts = counts;
}

bool Session::touchSwipe(float x0, float y0, float dx, float dy) {
    if (!phone_ || phoneHidden_ || std::abs(dx) < 24 * ppp_ || std::abs(dx) < std::abs(dy)) return false;
    if (!phoneDrawer_ && dx < 0 && hud_->onDrawerTab(x0, y0)) {
        phoneDrawer_ = true;
        phoneReopen_ = false;
        return true;
    }
    if (phoneDrawer_ && dx > 0 && (hud_->inDrawer(x0, y0) || hud_->onDrawerTab(x0, y0))) {
        phoneDrawer_ = false;
        phoneReopen_ = false;
        return true;
    }
    return false;
}

bool Session::sidebarLive(float x, float y) const { return (!phone_ || hud_->inDrawer(x, y)) && sidebar_->contains(x, y); }

void Session::phoneSidebarAction(int kind) {
    // The drawer covers the right part of the map: it closes while a building is being placed
    // (and comes back afterwards) and when a palace weapon / air strike wants its target.
    if (kind == render::SidebarAction::BeginPlacement && phoneDrawer_) {
        phoneDrawer_ = false;
        phoneReopen_ = true;
    } else if (kind == render::SidebarAction::TargetWeapon) {
        phoneDrawer_ = false;
    }
}

bool Session::phoneMouseDown(float x, float y, int button) {
    using A = render::PhoneHudAction;
    const A a = hud_->hit(x, y, button);
    game::World& world = *world_;
    const int side = playerSide_;
    const bool ended = world.gameEndState() != game::GameEnd::None;
    switch (a.kind) {
    case A::None: return false;
    case A::Absorb: break;
    case A::Menu:
        if (!ended) setPaused(true);
        break;
    case A::CancelMode:
        if (cheatMenu_ && cheatMenu_->placing()) cheatMenu_->cancelPlacing();
        else if (placingType_ >= 0) placingType_ = -1;
        else cursorMode_ = 0;
        break;
    case A::HideUi: phoneHidden_ = true; break;
    case A::ShowUi: phoneHidden_ = false; break;
    case A::Radar: phoneRadar_ = (phoneRadar_ + 1) % 3; break;
    case A::Drawer:
        phoneDrawer_ = !phoneDrawer_;
        phoneReopen_ = false;
        break;
    case A::BuildUnit: unitIconClick(a.type, false); break;
    case A::CancelUnit: unitIconClick(a.type, true); break;
    case A::Special: {
        // As the sidebar's palace icons: launched when charged; the air strike / Death Hand pick a target.
        const int beh = world.rules().templates->bin.units[a.type].specialBehavior;
        if (!world.unitQueueFor(side, a.type).ready) break;
        if (beh == game::behaviour::kOrnithopter || beh == game::behaviour::kDeathHand) {
            cursorMode_ = beh == game::behaviour::kOrnithopter ? 4 : 5;
            weaponBehaviour_ = beh;
            placingType_ = -1;
            phoneDrawer_ = false;
        } else if (beh == game::behaviour::kSaboteur || beh == game::behaviour::kFremen) {
            world.orderSpecialWeapon(side, beh);
        }
        break;
    }
    case A::Repair:
    case A::Sell: {
        const int mode = a.kind == A::Repair ? 2 : 3;
        cursorMode_ = cursorMode_ == mode ? 0 : mode;
        placingType_ = -1;
        if (cursorMode_) phoneDrawer_ = false;  // the buildings are on the map (the chip cancels the mode)
        break;
    }
    case A::Guard:
        if (world.orderGuardSelected(side)) mentatSpeech("GUARDING", false);
        break;
    case A::Retreat:
        if (world.orderRetreatSelected(side)) mentatSpeech("RETREAT", false);
        break;
    case A::Upgrades:
        if (!world.upgradableBuildings(side).empty() || world.upgradeQueue(side).active) sidebar_->showUpgrades(true);
        break;
    case A::Starport:
        if (world.starportOpen(side)) sidebar_->showStarport(true);
        break;
    }
    return true;
}

void Session::renderPhoneUi(game::World& world) {
    const int side = playerSide_;
    if (phoneReopen_ && placingType_ < 0) {  // placed or cancelled: the drawer comes back
        phoneDrawer_ = true;
        phoneReopen_ = false;
        layout();
    }
    const float a = phoneAlpha();
    // The radar: small, see-through, always on with a working radar (World::radarActive).
    if (minimapArea_.w > 0) {
        minimap_->rebuild(world, side);
        const float ra = (phoneRadar_ == 1 ? 0.72f : 0.52f) * (0.6f + 0.4f * a);
        skin_->fill(minimapArea_, render::Colour{0, 0, 0, Uint8(80 * ra)});
        std::vector<SDL_FPoint> footprint;
        minimap_->draw(minimapArea_, cam_, viewW_, viewH_, ra, false, viewFootprint3D(footprint) ? &footprint : nullptr);
        skin_->outline(minimapArea_, render::Colour{255, 236, 200, Uint8(110 * ra)}, std::max(1.0f, std::round(ppp_)));
    }
    if (hud_->drawerRect().w > 0) {
        hud_->drawDrawer(world, side, sidebar_->upgradesShown(), sidebar_->starportShown(), mouseX_, mouseY_);
        sidebar_->extendedQueue = settings_.extendedQueue();
        sidebar_->draw(hud_->sidebarArea(), world, side, mouseX_, mouseY_);
    }
    hud_->draw(world, side, a, settings_.extendedQueue());
    if (!paused_ && world.gameEndState() == game::GameEnd::None && sidebarLive(mouseX_, mouseY_))
        sidebar_->drawTooltip(mouseX_, mouseY_, world, side, float(outW_), float(outH_));
}

// D2K_CAMERA_MOVE (recordings, debug): a scripted camera move over the first seconds of the game,
// e.g. "secs=8,yaw=-30:30,tilt=40:55,zoom=2:2.6,at=10:9:14:12" - each key optional, from:to eased
// (smoothstep); at = view centre in cells (x0:y0:x1:y1); zoom is ignored by Graphics: Original.
void Session::applyCameraMove(double dt) {
    static const struct Move {
        bool on = false;
        double secs = 10;
        bool yaw = false, tilt = false, zoom = false, at = false;
        float yaw0 = 0, yaw1 = 0, tilt0 = 0, tilt1 = 0, zoom0 = 1, zoom1 = 1, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    } mv = [] {
        Move m;
        const char* e = std::getenv("D2K_CAMERA_MOVE");
        if (!e) return m;
        m.on = true;
        std::string s = e;
        std::size_t p = 0;
        while (p < s.size()) {
            std::size_t q = s.find(',', p);
            if (q == std::string::npos) q = s.size();
            const std::string kv = s.substr(p, q - p);
            p = q + 1;
            const std::size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
            float a = 0, b = 0, c = 0, d = 0;
            if (k == "secs") m.secs = std::max(0.01, std::atof(v.c_str()));
            else if (k == "yaw" && std::sscanf(v.c_str(), "%f:%f", &a, &b) == 2) { m.yaw = true; m.yaw0 = a; m.yaw1 = b; }
            else if (k == "tilt" && std::sscanf(v.c_str(), "%f:%f", &a, &b) == 2) { m.tilt = true; m.tilt0 = a; m.tilt1 = b; }
            else if (k == "zoom" && std::sscanf(v.c_str(), "%f:%f", &a, &b) == 2) { m.zoom = true; m.zoom0 = a; m.zoom1 = b; }
            else if (k == "at" && std::sscanf(v.c_str(), "%f:%f:%f:%f", &a, &b, &c, &d) == 4) {
                m.at = true; m.x0 = a; m.y0 = b; m.x1 = c; m.y1 = d;
            }
        }
        SDL_Log("D2K_CAMERA_MOVE: %s", e);
        return m;
    }();
    if (!mv.on) return;
    cameraMoveT_ += dt;
    const float t = float(std::clamp(cameraMoveT_ / mv.secs, 0.0, 1.0)), u = t * t * (3 - 2 * t);
    auto lerp = [u](float a, float b) { return a + (b - a) * u; };
    if (mv.yaw) camYaw_ = render3d::radians(lerp(mv.yaw0, mv.yaw1));
    if (mv.tilt) camTilt_ = render3d::radians(std::clamp(lerp(mv.tilt0, mv.tilt1), 0.0f, 66.0f));
    const float cx = cam_.x + viewW_ / (2 * cam_.zoom), cy = cam_.y + viewH_ / (2 * cam_.zoom);
    if (mv.zoom && !classicUi()) cam_.zoom = lerp(mv.zoom0, mv.zoom1) * ppp_;
    const float nx = mv.at ? lerp(mv.x0, mv.x1) * 32.0f : cx, ny = mv.at ? lerp(mv.y0, mv.y1) * 32.0f : cy;
    cam_.x = nx - viewW_ / (2 * cam_.zoom);
    cam_.y = ny - viewH_ / (2 * cam_.zoom);
}

void Session::update() {
    syncGraphics();
    layout();
    if (world_) world_->rallyPointsEnabled = settings_.rallyPoints;  // "Factory rally points" (not original)
    const Uint64 now = SDL_GetTicksNS();
    // Long stalls (window drag, breakpoints) are not caught up.
    const double dt = fixedStep_ > 0 ? fixedStep_ : std::min(0.25, double(now - last_) / 1e9);
    last_ = now;
    if (out_->ok()) snd_.update(*out_, sfxVolume());  // FUN_0046faa0: next queued speech
    if (paused_) return;
    game::World& world = *world_;
    const GameData& g = *data_;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    // ScrollRate px per original frame, scaled to real time (0x4433a0 scrolls by ScrollRate per
    // frame when the cursor touches a screen edge (fullscreen) or a scroll key is held).
    // Original UI: ScrollRate is in game pixels (= world pixels at its 1:1 scale), so the integer
    // display scale must not slow the scroll down; Enhanced keeps screen pixels at any zoom.
    const float speed = settings_.scrollPixelsPerSecond() * float(dt) / (classicUi() ? 1.0f : cam_.zoom / ppp_);
    // Fullscreen edge scrolling against the display edges (platform/pointer.hpp): also works
    // with the pointer in the notch / menu-bar band above the content area.
    ScreenEdges se;
    const bool edges = settings_.fullscreen && screenEdges(SDL_GetRenderWindow(r_), se);
    const bool kl = keys[SDL_SCANCODE_LEFT], kr = keys[SDL_SCANCODE_RIGHT];
    const bool ku = keys[SDL_SCANCODE_UP], kd = keys[SDL_SCANCODE_DOWN];
    const bool el = edges && se.left, er = edges && se.right, eu = edges && se.up, ed = edges && se.down;
    panView(((kr || er) ? speed : 0.0f) - ((kl || el) ? speed : 0.0f), ((kd || ed) ? speed : 0.0f) - ((ku || eu) ? speed : 0.0f));
    applyCameraMove(dt);  // D2K_CAMERA_MOVE (recordings)
    if (r3d_) {  // 3D: Q / E turn, Page Up / Down tilt
        const float fdt = float(dt);
        if (keys[SDL_SCANCODE_Q]) camYaw_ -= 1.6f * fdt;
        if (keys[SDL_SCANCODE_E]) camYaw_ += 1.6f * fdt;
        if (keys[SDL_SCANCODE_PAGEUP]) camTilt_ = std::max(render3d::Camera3D::kMinTilt, camTilt_ - 0.8f * fdt);
        if (keys[SDL_SCANCODE_PAGEDOWN]) camTilt_ = std::min(render3d::Camera3D::kMaxTilt, camTilt_ + 0.8f * fdt);
        r3d_->constrainPose(camYaw_, camTilt_);  // sprites: north up, modest tilt (docs/3d-mode.md)
    }
    if (view3d()) {
        // 3D: keep the view centre inside the map by the view's half extent turned with the yaw
        // (the screen width lies along world x at yaw 0, along world y at 90), so every edge stays
        // reachable (minimap clicks land) whichever way the camera faces. Yaw 0 on a map larger
        // than the view is the 2D clamp; a smaller map is centred.
        const float c = std::cos(camYaw_), sn = std::sin(camYaw_), hw = viewW_ / (2 * cam_.zoom), hh = viewH_ / (2 * cam_.zoom);
        const float ex = hw * c * c + hh * sn * sn, ey = hh * c * c + hw * sn * sn;
        const float mapW = g.scenario.width * 32.0f, mapH = g.scenario.height * 32.0f;
        const float offX = hw, offY = hh;
        const float lx = ex * 2 >= mapW ? mapW / 2 : std::clamp(cam_.x + offX, ex, mapW - ex);
        const float ly = ey * 2 >= mapH ? mapH / 2 : std::clamp(cam_.y + offY, ey, mapH - ey);
        cam_.x = lx - offX;
        cam_.y = ly - offY;
    } else {
        clampCamera(cam_, g.scenario, viewW_, viewH_);
    }
    // Scroll arrows (0x4433a0): the screen edge in fullscreen, the scroll keys in a window;
    // the crossed-out arrow when the view is already at that edge.
    {
        const bool fs = settings_.fullscreen;
        scrollDy_ = (fs ? ed : kd) ? 1 : (fs ? eu : ku) ? -1 : 0;
        scrollDx_ = (fs ? er : kr) ? 1 : (fs ? el : kl) ? -1 : 0;
        const float maxX = std::max(0.0f, g.scenario.width * 32.0f - viewW_ / cam_.zoom);
        const float maxY = std::max(0.0f, g.scenario.height * 32.0f - viewH_ / cam_.zoom);
        scrollBlockedX_ = scrollDx_ > 0 ? cam_.x >= maxX : cam_.x <= 0;
        scrollBlockedY_ = scrollDy_ > 0 ? cam_.y >= maxY : cam_.y <= 0;
    }

    // Fixed-timestep simulation at the GameSpeed tick rate (RecalcModelRate 0x45b610 spreads
    // that many ticks evenly over each second of frames).
    tickAccumulator_ += dt * settings_.ticksPerSecond();
    for (int n = 0; tickAccumulator_ >= 1.0 && n < 10; ++n) {
        motion_.beforeTick(world);
        if (cfg_.vfxDemo)
            vfxDemoTick(world, cfg_.at ? cfg_.at->first * 32 : world.width() * 16, cfg_.at ? cfg_.at->second * 32 : world.height() * 16);
        world.tick();
        if (world.gameTicks() % 10 == 0) minimap_->rebuild(world, playerSide_);
        for (const auto& m : world.messages()) {
            // A mission .ini [Text] entry replaces the string (show-message-event.c).
            const auto custom = g.missionText.find(m.textId);
            const std::string txt = custom != g.missionText.end() ? custom->second
                                    : m.textId >= 0 && m.textId < int(g.text.entries.size())
                                        ? g.text.entries[m.textId].second
                                        : "(message " + std::to_string(m.textId) + ")";
            messageLog_.push_back({txt, world.gameTicks() + 400});
        }
        world.clearMessages();
        // Community extended events: Change Viewport / Play Music requests.
        if (auto& req = world.extUiRequests(); req.viewport || !req.music.empty()) {
            if (req.viewport) {
                const float vw = viewW_ / cam_.zoom, vh = viewH_ / cam_.zoom;
                cam_.x = req.viewportCentred ? req.viewportX - vw / 2 : float(req.viewportX);
                cam_.y = req.viewportCentred ? req.viewportY - vh / 2 : float(req.viewportY);
                cam_.x = std::clamp(cam_.x, 0.0f, std::max(0.0f, g.scenario.width * 32.0f - vw));
                cam_.y = std::clamp(cam_.y, 0.0f, std::max(0.0f, g.scenario.height * 32.0f - vh));
            }
            if (!req.music.empty()) musicRequest_ = req.music;
            req = {};
        }
        playSoundEvents(world);
        tickAccumulator_ -= 1.0;
    }
    if (tickAccumulator_ >= 1.0) tickAccumulator_ -= std::floor(tickAccumulator_);  // fell behind: drop
}

// Placement cursor: per footprint cell, the original DATA.R16 markers (Display__DrawPlacementMarkers
// 0x4285c0: buildable / non-buildable stripes) in Original mode, tinted squares in Enhanced.
void Session::drawPlacement(const game::World& world) {
    if (placingType_ < 0 || mouseX_ >= viewW_ || mouseY_ < 0) return;
    const GameData& g = *data_;
    const auto [px, py] = placementTile(mouseX_, mouseY_);
    std::array<bool, 16> ok{};
    world.canPlaceBuilding(playerSide_, placingType_, px, py, &ok);
    // Concrete slabs lay their tilesOccupiedSolid cells (2x2 / 3x3); the generic
    // footprint mask is a single tile for them (0x428ca0 passes template +0x594 for both).
    const auto& pt = g.templates.bin.buildings[placingType_];
    const std::uint32_t mask =
        pt.specialBehavior == game::building_behaviour::kConcrete ? pt.tilesOccupiedSolid : pt.tilesOccupiedAll;
    const float camX = classicUi() ? std::floor(cam_.x) : cam_.x, camY = classicUi() ? std::floor(cam_.y) : cam_.y;
    for (int i = 0; i < 16; ++i) {
        if (!((mask >> i) & 1)) continue;
        const float tx = (px + i % 4) * 32.0f, ty = (py + i / 4) * 32.0f;
        if (view3d()) {  // 3D: the cell lying on the terrain
            const SDL_FColor fill = ok[i] ? SDL_FColor{0.24f, 0.86f, 0.35f, 0.35f} : SDL_FColor{0.9f, 0.2f, 0.16f, 0.43f};
            view3d()->fillGroundRect(tx, ty, tx + 32, ty + 32, fill, SDL_FColor{1, 1, 1, 0.47f});
            continue;
        }
        const SDL_FRect rc{std::round((tx - camX) * cam_.zoom), std::round((ty - camY) * cam_.zoom), 32 * cam_.zoom,
                           32 * cam_.zoom};
        if (classicUi()) {
            // _image_placement_marker_buildable (frame 0) / _nonbuildable (frame 1).
            if (const render::Sprite* sp = sprites_->get(ok[i] ? 0 : 1, -1)) SDL_RenderTexture(r_, sp->texture, nullptr, &rc);
            continue;
        }
        if (ok[i]) SDL_SetRenderDrawColor(r_, 60, 220, 90, 90);
        else SDL_SetRenderDrawColor(r_, 230, 50, 40, 110);
        SDL_RenderFillRect(r_, &rc);
        SDL_SetRenderDrawColor(r_, 255, 255, 255, 120);
        SDL_RenderRect(r_, &rc);
    }
}

// Time of day / Modern lighting (docs/enhanced-graphics.md "Lighting"): this frame's light from the
// settings (Cycle: from the game clock, so it is reproducible), the lamps for the view, bloom.
// Enhanced / 3D only; Original graphics and the classic view keep the old light.
bool Session::prepareLighting(const game::World& w, bool threeD, const float view[4]) {
    light_ = render::SceneLight{};
    if (appliedGraphics_ == Settings::Graphics::Original) return false;
    // D2K_CYCLE_START (recordings, debug): game seconds added to the clock of the day / night cycle.
    static const double cycleStart = std::getenv("D2K_CYCLE_START") ? std::atof(std::getenv("D2K_CYCLE_START")) : 0.0;
    const double seconds =
        (cfg_.presetPreview ? 0.0 : settings_.cyclePhase) + cycleStart + (double(w.gameTicks()) + tickAccumulator_) / 20.0;
    lastCycleSeconds_ = seconds;
    const bool lite = settings_.deviceProfile == "lite";
    light_ = render::sceneLight(static_cast<render::TimeOfDay>(settings_.timeOfDay),
                                static_cast<render::LightingStyle>(settings_.lighting), seconds, lite);
    if (light_.identity) return false;
    if (light_.bloom > 0 && !bloom_) bloom_ = std::make_unique<render::Bloom>(r_);
    if (light_.lamps > 0) {
        if (!sceneLights_) sceneLights_ = std::make_unique<render::SceneLights>(r_, data_->templates);
        // Building types drawn from a redesigned art: a 3D model, or an art-pack frame rendered from one (its own
        // hotspot, docs/enhanced-graphics.md "Alignment"): their lamps come from the model (scene_lights.hpp).
        const auto& T = data_->templates;
        redesignedTypes_.assign(formats::kMaxBuildingTypes, 0);
        const bool models = threeD && models3d_ && models3d_->active();
        for (int ty = 0; ty < int(T.bin.buildingCount) && ty < formats::kMaxBuildingTypes; ++ty) {
            if (models && models3d_->hasBuildingModel(ty)) {
                redesignedTypes_[std::size_t(ty)] = 2;  // 2 = a 3D model (its build-up band lights the ground at night)
                continue;
            }
            const int art = T.bin.buildings[ty].buildingArt;
            if (!artPack_ || art < 0 || art >= formats::kMaxBuildingArt) continue;
            const int first = T.buildingArtFirstFrame[art];
            int hx = 0, hy = 0;
            if (artPack_->hotspot(first + 1, hx, hy) || artPack_->hotspot(first, hx, hy)) redesignedTypes_[std::size_t(ty)] = 1;
        }
        // Units the same (a 3D model, or pack frames with their own hotspot): lamps only at the model's fx_light anchors.
        redesignedUnitTypes_.assign(formats::kMaxUnitTypes, 0);
        for (int ty = 0; ty < int(T.bin.unitCount) && ty < formats::kMaxUnitTypes; ++ty) {
            if (models && models3d_->hasUnitModel(ty)) {
                redesignedUnitTypes_[std::size_t(ty)] = 1;
                continue;
            }
            const int art = T.bin.units[ty].unitArt;
            if (!artPack_ || art < 0 || std::size_t(art) >= T.unitArtFirstFrame.size()) continue;
            int hx = 0, hy = 0;
            if (artPack_->hotspot(T.unitArtFirstFrame[std::size_t(art)], hx, hy)) redesignedUnitTypes_[std::size_t(ty)] = 1;
        }
        packAnchors_.setEffects(mapRenderer_->effects());
        sceneLights_->update(w, light_, seconds, view, mapRenderer_->showShroud, mapRenderer_->effects(),
                             threeD ? static_cast<const render::EffectAnchors*>(view3d()) : packAnchors2D(), &redesignedTypes_,
                             &redesignedUnitTypes_);
    } else if (sceneLights_) {
        sceneLights_->update(w, light_, seconds, view, mapRenderer_->showShroud, nullptr, nullptr);  // clears
    }
    return true;
}

void Session::render(Ui& ui) {
    syncGraphics();
    if (modelsJob_.valid()) syncModels();  // setAsyncModels: swap the models in once they are read
    layout();
    game::World& world = *world_;
    const GameData& g = *data_;
    const int viewW = drawW_, viewH = viewH_;  // the drawn battlefield (under a translucent panel too)
    const bool classic = classicUi();
    const bool peek = classicPeek();
    if (peekWasOn_ && !peek) snapCamera_ = true;  // 3D: back to the 3D camera without a glide
    peekWasOn_ = peek;
    if (cfg_.mouseAt) {  // screenshot: a resting mouse at the requested point
        float x = cfg_.mouseAt->first, y = cfg_.mouseAt->second;
        if (classic) {
            toGame(x, y);
            rawMouseX_ = x;
            rawMouseY_ = y;
            if (classic_) classic_->forceTooltipRest();
        }
        mouseX_ = x;
        mouseY_ = y - float(mapTop_);
    }
    if (classic) {  // draw the original frame into the game-resolution target
        float tw = 0, th = 0;
        if (classicTarget_) SDL_GetTextureSize(classicTarget_, &tw, &th);
        if (!classicTarget_ || int(tw) != gameW_ || int(th) != gameH_) {
            if (classicTarget_) SDL_DestroyTexture(classicTarget_);
            classicTarget_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, gameW_, gameH_);
        }
        if (classicTarget_) SDL_SetRenderTarget(r_, classicTarget_);
    }
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    if (classic) {
        const SDL_Rect bf = classic_->battlefield();
        SDL_SetRenderViewport(r_, &bf);
    }
    // Original: whole world pixels at an integer scale, like BattleFieldPosX/Y.
    const render::Camera cam = classic ? render::Camera{std::floor(cam_.x), std::floor(cam_.y), cam_.zoom} : cam_;
    // 2D: clip to the map itself as well - a view larger than a small map (zoomed out) must not
    // show what lies beyond its right / bottom edge (arriving carryalls and frigates, effects,
    // shadows hanging off the edge), which no shroud covers. 3D hides those itself.
    SDL_Rect mapClip{0, 0, viewW, viewH};
    if (!view3d()) {
        mapClip.w = std::clamp(int(std::ceil((g.scenario.width * 32.0f - cam.x) * cam.zoom)), 0, viewW);
        mapClip.h = std::clamp(int(std::ceil((g.scenario.height * 32.0f - cam.y) * cam.zoom)), 0, viewH);
    }
    SDL_SetRenderClipRect(r_, &mapClip);
    // Smooth motion: units/projectiles drawn between the last two ticks (render only; restored
    // right after the draw so the simulation keeps its exact positions).
    const bool smooth = settings_.smoothMotion;
    if (smooth) motion_.apply(world, float(tickAccumulator_));
    const Uint64 nowNs = SDL_GetTicksNS();
    const float frameDt = fixedStep_ > 0 ? float(fixedStep_)
                          : lastRenderNs_ ? std::min(0.1f, float(double(nowNs - lastRenderNs_) / 1e9)) : 0.0f;
    lastRenderNs_ = nowNs;
    render3d::Renderer3D::Options o3;
    if (view3d()) {  // 3D mode (docs/3d-mode.md): the 2.5D battlefield instead of the 2D map
        o3.showShroud = mapRenderer_->showShroud;
        o3.playerSide = playerSide_;
        o3.healthBars = static_cast<int>(settings_.healthBars);
        o3.designedTerrain = settings_.terrain3dDesigned();
        o3.faithfulTerrain = settings_.terrain3d == Settings::Terrain3D::Faithful;
        o3.shadowMap = settings_.shadows3d;
        o3.packAnchors = packAnchors_.empty() ? nullptr : &packAnchors_;  // building sprites from the pack
        o3.snapCamera = snapCamera_;
        snapCamera_ = false;
        view3d()->constrainPose(camYaw_, camTilt_);
        const render3d::CameraPose pose{cam_.x + viewW / (2 * cam_.zoom), cam_.y + viewH / (2 * cam_.zoom), cam_.zoom, camYaw_,
                                        camTilt_};
        {  // time of day / lighting: the ground rectangle the 3D view sees (about twice the 2D one)
            const float view[4] = {pose.cx - viewW / cam_.zoom, pose.cy - viewH / cam_.zoom, pose.cx + viewW / cam_.zoom,
                                   pose.cy + viewH / cam_.zoom};
            if (prepareLighting(world, true, view)) {
                o3.light = &light_;
                o3.lamps = sceneLights_ && light_.lamps > 0 ? sceneLights_.get() : nullptr;
                o3.bloom = bloom_.get();
            }
            o3.hdr = hdrIfOn();
        }
        if (render::Effects* fx = mapRenderer_->effects()) {  // Modern effects, drawn in world space
            // Emitters cull to a ground rectangle: the 3D view sees about twice the 2D one.
            const render::Camera wide{pose.cx - viewW / cam_.zoom, pose.cy - viewH / cam_.zoom, cam_.zoom * 0.5f};
            mapRenderer_->setEffectClock(double(world.gameTicks()) + tickAccumulator_);
            fx->setCardTilt(camTilt_);  // effects on building art stand on the cards (render3d)
            fx->setAnchors(view3d());   // ... or leave the models' anchors (refinery flues)
            mapRenderer_->updateEffects(world, wide, viewW, viewH);
            fx->setCardTilt(-1);
            fx->setAnchors(nullptr);
            o3.effects = fx;
            o3.shakeX = fx->shakeX();
            o3.shakeY = fx->shakeY();
        }
        view3d()->render(world, pose, viewW, viewH, frameDt, o3);
        view3d()->drawOverlays(world, o3);
        if (render::Effects* fx = mapRenderer_->effects()) fx->drawPost(viewW, viewH);
    } else if (peek) {
        // Classic view: the original-graphics battlefield. The Modern effects keep running unseen
        // (so they do not jump when the view returns); the designed terrain chunks stay baked.
        if (mapRenderer_->effects()) {
            mapRenderer_->setEffectClock(double(world.gameTicks()) + tickAccumulator_);
            mapRenderer_->updateEffects(world, cam, viewW, viewH);
        }
        drawClassicPeek(world, g.scenario, viewW, viewH);
    } else {
        mapRenderer_->setEffectClock(double(world.gameTicks()) + tickAccumulator_);
        // Time of day / lighting (Enhanced only): the grade over the map, the lamps, bloom, and the
        // light the terrain is baked under.
        const float view[4] = {cam.x, cam.y, cam.x + viewW / cam.zoom, cam.y + viewH / cam.zoom};
        const bool lit = prepareLighting(world, false, view);
        mapRenderer_->lighting = lit ? &light_ : nullptr;
        mapRenderer_->sceneLights = lit && light_.lamps > 0 ? sceneLights_.get() : nullptr;
        mapRenderer_->bloom = lit ? bloom_.get() : nullptr;
        mapRenderer_->hdr = lit ? hdrIfOn() : nullptr;
        {
            // The sun of this view (the faithful bake: west-south-west; else north-west).
            const bool faithful = baker_ && baker_->style() == render3d::TerrainStyle::Faithful;
            const render3d::Vec3 b = faithful ? render3d::DesignedTerrain::faithfulBakeSun() : render3d::Terrain::sunDirection();
            const float base[3] = {b.x, b.y, b.z};
            float sunNow[3];
            light_.sunDirection(base, sunNow);
            light_.grade(0.62f, 0.55f, sunNow[1], mapRenderer_->lightingGrade);
            if (baker_) {
                // Baked under the light divided by the grade (Cycle: the Day light; the grade then
                // follows the clock over the baked picture).
                const render::SceneLight bl =
                    settings_.timeOfDay == Settings::TimeOfDay::Cycle && lit
                        ? render::sceneLight(render::TimeOfDay::Day, static_cast<render::LightingStyle>(settings_.lighting))
                        : light_;
                render3d::TerrainBaker::Light L;
                if (lit && !bl.identity) {
                    float sunB[3], g[3];
                    bl.sunDirection(base, sunB);
                    bl.grade(0.46f, 0.78f, sunB[1], g);  // the bake's own ambient / diffuse
                    L.dAz = bl.dAz;
                    L.elK = bl.elK;
                    L.sat = bl.sat;
                    for (int i = 0; i < 3; ++i) {
                        L.sun[i] = bl.sun[i] / std::max(0.05f, g[i]);
                        L.amb[i] = bl.amb[i] / std::max(0.05f, g[i]);
                    }
                }
                baker_->setLight(L);
            }
        }
        // Enhanced "Terrain: Designed": the baked chunks instead of the tiles (built on the first
        // frame, then only the chunks whose spice / concrete changed).
        mapRenderer_->terrainLayer = baker_ && baker_->update(world, atlas_.get()) ? baker_.get() : nullptr;
        mapRenderer_->packAnchors = packAnchors2D();  // weld flashes / sparks in the redesigned works
        if (render::Effects* fx = mapRenderer_->effects()) fx->setAnchors(mapRenderer_->packAnchors);
        mapRenderer_->draw(world, g.scenario, cam, viewW, viewH);
        if (render::Effects* fx = mapRenderer_->effects()) fx->setAnchors(nullptr);
        mapRenderer_->packAnchors = nullptr;
    }
    if (smooth) motion_.restore(world);
    drawPlacement(world);
    if (settings_.rallyPoints && view3d()) {  // 3D: the same flag, projected onto the terrain
        drawRallyPoint(
            r_, world, playerSide_,
            [&](float x, float y, float& sx, float& sy) { return view3d()->worldToScreen(x, y, 0, sx, sy); },
            std::max(1.0f, view3d()->pixelsPerWorld(cam_.x + viewW / (2 * cam_.zoom), cam_.y + viewH / (2 * cam_.zoom))));
    }
    if (cheatMenu_ && cheatMenu_->placing() && mouseX_ < viewW_ && mouseY_ >= 0 && view3d()) {
        float wx = 0, wy = 0;
        screenToWorld(mouseX_, mouseY_, wx, wy, false);
        cheatMenu_->drawPlacement(
            ui, world, wx, wy,
            [&](int tx, int ty, SDL_FColor fill, SDL_FColor outline) {
                view3d()->fillGroundRect(tx * 32.0f, ty * 32.0f, tx * 32.0f + 32, ty * 32.0f + 32, fill, outline);
            },
            [&](float x, float y, float& sx, float& sy) { return view3d()->worldToScreen(x, y, 0, sx, sy); });
    }
    if (settings_.rallyPoints && !view3d())  // selected factory's rally point (not original)
        drawRallyPoint(r_, world, playerSide_, classic ? std::floor(cam_.x) : cam_.x, classic ? std::floor(cam_.y) : cam_.y,
                       cam_.zoom);
    if (cheatMenu_ && cheatMenu_->placing() && mouseX_ < viewW_ && mouseY_ >= 0 && !view3d()) {
        const float camX = classic ? std::floor(cam_.x) : cam_.x, camY = classic ? std::floor(cam_.y) : cam_.y;
        cheatMenu_->drawPlacement(r_, ui, world, cam_.x + mouseX_ / cam_.zoom, cam_.y + mouseY_ / cam_.zoom, camX, camY,
                                  cam_.zoom, classic ? sprites_.get() : nullptr);
    }
    if (!paused_) world.decayFlicker();  // the original counts attack-order flicker down per drawn frame
    if (leftDown_ && (std::abs(mouseX_ - leftX_) > 3 || std::abs(mouseY_ - leftY_) > 3)) {
        const SDL_FRect box{std::min(leftX_, mouseX_), std::min(leftY_, mouseY_), std::abs(mouseX_ - leftX_),
                            std::abs(mouseY_ - leftY_)};
        SDL_SetRenderDrawColor(r_, 255, 255, 255, 40);
        SDL_RenderFillRect(r_, &box);
        SDL_SetRenderDrawColor(r_, 255, 255, 255, 220);
        SDL_RenderRect(r_, &box);
    }
    if (peek) drawClassicPeekLabel();
    // Overlays: mission messages (top-left), mission timer (top centre), game end banner.
    messageLog_.erase(std::remove_if(messageLog_.begin(), messageLog_.end(),
                                     [&](const LogLine& l) { return l.until <= world.gameTicks(); }),
                      messageLog_.end());
    float my = classic ? 10 : phone_ ? hud_->barBottom() + 6 * ppp_ : command_->topBarH() + skin_->px(8);
    float mx = 12 * ppp_;
    if (phone_ && minimapArea_.w > 0) mx = minimapArea_.x + minimapArea_.w + 8 * ppp_;
    else if (phone_) mx = safe_.x + 8 * ppp_;
    for (const auto& l : messageLog_) {
        if (classic) break;  // drawn by the original UI
        // One font pixel per window point (2x2 output pixels at Render resolution: Full on Retina).
        text_->draw(l.text, mx, my, render::Colour{240, 225, 190, 255}, ppp_);
        my += text_->height(ppp_) + 3 * ppp_;
    }
    const auto end = world.gameEndState();
    if (end != game::GameEnd::None) {
        // UI_ENG.R16 banners: frames with "MISSION ACCOMPLISHED" / "MISSION FAILED".
        const int frame = end == game::GameEnd::Won ? kWonBannerFrame : kLostBannerFrame;
        if (const auto* sp = uiSprites_->get(frame, -1)) {
            const float sc = classic ? cam_.zoom : 3 * ppp_;
            const SDL_FRect dst{viewW_ / 2.0f - sp->width * sc / 2, viewH / 3.0f, sp->width * sc, sp->height * sc};
            SDL_RenderTexture(r_, sp->texture, nullptr, &dst);
        } else {
            const char* msg = end == game::GameEnd::Won ? "MISSION ACCOMPLISHED" : "MISSION FAILED";
            text_->draw(msg, viewW_ / 2.0f - text_->width(msg, 3 * ppp_) / 2, viewH / 3.0f, render::Colour{255, 200, 90, 255}, 3 * ppp_);
        }
    }
    SDL_SetRenderClipRect(r_, nullptr);
    if (classic) {
        SDL_SetRenderViewport(r_, nullptr);
        std::vector<std::string> lines;  // the original keeps at most 5 message slots
        for (std::size_t i = messageLog_.size() > 5 ? messageLog_.size() - 5 : 0; i < messageLog_.size(); ++i)
            lines.push_back(messageLog_[i].text);
        // The repair/sell modes show as mouse cursors (cursorAt), not as a marker.
        classic_->extendedQueue = settings_.extendedQueue();
        // Game__MainLoopFrame: no tooltips while a page (pause menu, cheats) is open.
        classic_->tooltips = !paused_ && end == game::GameEnd::None;
        classic_->shroudShown = mapRenderer_->showShroud;
        classic_->skirmish = data_->start.skirmish;
        classic_->draw(world, playerSide_, cam.x, cam.y, lines, 0, rawMouseX_, rawMouseY_);
        if (classicTarget_) {
            SDL_SetRenderTarget(r_, render::screenTarget());  // the window, or the EDR canvas (render/hdr.hpp)
            SDL_SetRenderViewport(r_, nullptr);
            SDL_SetRenderClipRect(r_, nullptr);
            SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
            SDL_RenderClear(r_);
            // Whole multiples: nearest; other factors: pixel-art filtering (sharp, even pixels);
            // smaller than the game resolution: linear.
            const float s = present_.w / float(gameW_);
            SDL_SetTextureScaleMode(classicTarget_, s < 1.0f    ? SDL_SCALEMODE_LINEAR
                                                    : s == std::floor(s) ? SDL_SCALEMODE_NEAREST
                                                                         : SDL_SCALEMODE_PIXELART);
            // Screen shake (0x42f800 called with gScreenShakeOffset): the whole frame is copied
            // shifted vertically by -2..2 game pixels; the exposed rows stay black.
            const int o = std::clamp(world.screenShakeOffset(), -gameH_ + 1, gameH_ - 1);
            if (o == 0) {
                SDL_RenderTexture(r_, classicTarget_, nullptr, &present_);
            } else {
                const float k = present_.h / float(gameH_);
                const SDL_FRect src{0, float(o < 0 ? -o : 0), float(gameW_), float(gameH_ - std::abs(o))};
                const SDL_FRect dst{present_.x, present_.y + (o > 0 ? o * k : 0.0f), present_.w,
                                    (gameH_ - std::abs(o)) * k};
                SDL_RenderTexture(r_, classicTarget_, &src, &dst);
            }
        }
        if (paused_) drawPauseMenu(ui);
        else if (end != game::GameEnd::None) drawEndPrompt(ui);
        return;
    }

    // Enhanced in-game UI (docs/enhanced-graphics.md "In-game UI"): the right-hand column (minimap,
    // command buttons, production panel) and the top readouts.
    skin_->setHouse(world.sides()[playerSide_].house);
    render::setUiLanguage(settings_.languageText.empty() ? settings_.language : settings_.languageText);
    if (phone_) {
        renderPhoneUi(world);
        if (paused_) drawPauseMenu(ui);
        else if (end != game::GameEnd::None) drawEndPrompt(ui);
        return;
    }
    command_->drawBlock(world, playerSide_, cursorMode_, sidebar_->upgradesShown(), sidebar_->starportShown(), mouseX_,
                        mouseY_);
    // The radar follows the original's rule (outpost and power >= 100 %, World::radarActive);
    // off: a "radar offline" screen fading in over 64 frames (the original fades its house logo
    // in, FUN_00425fa0).
    if (world.radarActive(playerSide_)) {
        radarFade_ = 0;
        minimap_->rebuild(world, playerSide_);
        skin_->fill(minimapArea_, render::Colour{4, 4, 4, 255});
        std::vector<SDL_FPoint> footprint;
        minimap_->draw(minimapArea_, cam_, viewW_, viewH, 1.0f, true, viewFootprint3D(footprint) ? &footprint : nullptr);
    } else {
        if (radarFade_ == 0) radarFade_ = 1;
        skin_->radarOff(minimapArea_, float(std::min(radarFade_, 64)) / 64.0f, render::tr("RADAR OFFLINE"));
        if (radarFade_ < 0x41) ++radarFade_;
    }
    sidebar_->extendedQueue = settings_.extendedQueue();
    const float blockBottom = command_->blockBottom();
    sidebar_->draw(SDL_FRect{float(viewW_), blockBottom, float(outW_ - viewW_), float(outH_) - blockBottom}, world, playerSide_,
                   mouseX_, mouseY_);
    command_->drawTopBar(world, playerSide_, mouseX_, mouseY_);
    // Hover tooltips (panel icons / buttons); not over menus.
    if (!paused_ && end == game::GameEnd::None) {
        if (sidebar_->contains(mouseX_, mouseY_)) {
            sidebar_->drawTooltip(mouseX_, mouseY_, world, playerSide_, float(outW_), float(outH_));
        } else if (auto tip = command_->tooltipAt(mouseX_, mouseY_, world, playerSide_); !tip.empty()) {
            skin_->tooltip(tip, mouseX_, mouseY_, float(outW_), float(outH_));
        }
    }

    if (paused_) drawPauseMenu(ui);
    else if (end != game::GameEnd::None) drawEndPrompt(ui);
}

Session::CursorChoice Session::cursorAt(float x, float y) {
    layout();
    CursorChoice c;
    const bool classic = classicUi();
    if (classic) {
        toGame(x, y);
        c.scale = present_.w > 0 ? present_.w / float(gameW_) : 1.0f;  // one game pixel
    } else {
        // Enhanced: the original cursor size in screen points (one original pixel per point,
        // like a system cursor), independent of the window size and the map zoom.
        SDL_Window* win = SDL_GetRenderWindow(r_);
        c.scale = win ? std::max(1.0f, SDL_GetWindowPixelDensity(win)) : 1.0f;
        c.hq = true;
    }
    game::World& world = *world_;
    if (paused_ || world.gameEndState() != game::GameEnd::None) {  // menus / prompts: the pointer
        lastCursor_ = render::Cursor::Arrow;
        return c;
    }
    // While the left button is held on the battlefield (DAT_00516458) the cursor stays as it was.
    if (leftDown_) {
        c.id = lastCursor_;
        return c;
    }
    render::CursorQuery q;
    q.side = playerSide_;
    q.mode = placingType_ >= 0 || (cheatMenu_ && cheatMenu_->placing()) ? 1 : cursorMode_;
    q.modernControls = settings_.controls == Settings::Controls::Modern;
    q.shroud = mapRenderer_->showShroud;
    const SDL_Keymod mod = SDL_GetModState();
    q.ctrl = (mod & SDL_KMOD_CTRL) != 0;
    q.shift = (mod & SDL_KMOD_SHIFT) != 0;
    q.alt = (mod & SDL_KMOD_ALT) != 0;
    float mx = x, my = y;
    if (classic) {
        q.overBattlefield = classic_->inBattlefield(x, y);
        my -= float(mapTop_);
        int wx = 0, wy = 0;
        if (!q.overBattlefield) q.overRadar = classic_->radarToWorld(x, y, world, playerSide_, wx, wy);
    } else {
        q.overBattlefield = x >= 0 && y >= 0 && x < viewW_ && y < viewH_ && !overUi(x, y);
    }
    if (q.overBattlefield) {
        float wx = 0, wy = 0;
        screenToWorld(mx, my, wx, wy);
        q.worldX = int(wx);
        q.worldY = int(wy);
    }
    c.id = render::pickCursor(world, q);
    if (q.mode == 0) c.id = render::scrollCursor(c.id, scrollDx_, scrollDy_, scrollBlockedX_, scrollBlockedY_);
    lastCursor_ = c.id;
    return c;
}

void Session::drawEndPrompt(Ui& ui) {
    Ui::ModernScope modern(ui);
    const Uint64 now = SDL_GetTicks();
    if (!endSeenAt_) endSeenAt_ = now;
    if (now - endSeenAt_ < 1500) {
        ui.takeBack();
        ui.takeActivate();
        return;
    }
    // Game__MainLoopFrame 0x448550: a lost skirmish asks REPLAY_MISSION (Replay.uil) over the
    // battlefield: Yes restarts, No leaves (LOADMISSION -> the result page).
    if (classicUi() && data_->start.skirmish && world_->gameEndState() == game::GameEnd::Lost) {
        ensurePages(ui);
        if (!replayShown_) {
            replayShown_ = true;
            pages_->openReplayPrompt();
        }
        const InGameAction a = pages_->frame();
        if (a.kind == InGameAction::Restart) restart();
        else if (a.kind == InGameAction::Abort || a.kind == InGameAction::Resume) result_ = SessionResult::QuitToMenu;
        return;
    }
    ui.textScale = Ui::labelScaleFor(outW_, outH_);
    const std::string msg = render::tr("Click or press Enter to return to the menu");
    const float s = ui.textScale;
    const float w = ui.textWidth(msg, s) + 40, h = ui.textHeight(s) + 18;
    const SDL_FRect box{viewW_ / 2.0f - w / 2, viewH_ / 3.0f + 31 * 3 + 30, w, h};
    ui.panel(box);
    const float pulse = 0.65f + 0.35f * std::sin(now / 300.0f);
    ui.text(msg, box.x + w / 2, box.y + 9, Colour{255, 226, 170, static_cast<unsigned char>(255 * pulse)}, s, Align::Centre);
    if (ui.clicked() || ui.takeActivate() || ui.takeBack()) result_ = SessionResult::QuitToMenu;
}

void Session::openCheats(bool fromPause) {
    if (!cheatMenu_) cheatMenu_ = std::make_unique<CheatMenu>(data_->templates, &data_->colours);
    cheatMenu_->open(*world_, playerSide_);
    cheatMenu_->setTicksPerSecond(settings_.ticksPerSecond());
    cheatsFromPause_ = fromPause;
    cheatsOpen_ = true;
    setPaused(true);
}

void Session::drawCheats(Ui& ui) {
    switch (cheatMenu_->frame(ui, *world_, playerSide_, sprites_.get(), outW_, outH_)) {
    case CheatMenu::Result::None: break;
    case CheatMenu::Result::Close:
        cheatsOpen_ = false;
        if (cheatsFromPause_) pauseOpened_ = true;
        else setPaused(false);
        break;
    case CheatMenu::Result::Place:
        cheatsOpen_ = false;
        placingType_ = -1;
        cursorMode_ = 0;
        setPaused(false);
        break;
    }
}

void Session::drawPauseMenu(Ui& ui) {
    // Our own pages (Enhanced pause menu / options / slots, the cheat menu in any mode) use the
    // TrueType UI font; Graphics: Original draws the original .uil pages with the bitmap font.
    Ui::ModernScope modern(ui, !classicUi() || (cheatsOpen_ && cheatMenu_));
    if (cheatsOpen_ && cheatMenu_) { drawCheats(ui); return; }
    if (classicUi()) { drawOriginalMenu(ui); return; }
    if (pauseOpened_) {
        ui.resetFocus(0);
        pauseOpened_ = false;
    }
    ui.fill(SDL_FRect{0, 0, float(outW_), float(outH_)}, Colour{0, 0, 0, 150});
    if (pauseOptions_) { drawPauseOptions(ui); return; }
    if (pauseSlots_) { drawPauseSlots(ui); return; }
    const float s = Ui::bodyScaleFor(outW_, outH_);
    ui.textScale = Ui::labelScaleFor(outW_, outH_);
    const int kRows = settings_.cheats ? 8 : 7;
    const float w = 360 * s, gap = 10 * s;
    const float bh = std::min(44 * s, (outH_ - 150 * s - (kRows - 1) * gap) / kRows);
    const std::string title = data_->start.title.empty() ? data_->start.map : data_->start.title;
    const float h = 110 * s + kRows * bh + (kRows - 1) * gap + 24 * s;
    const SDL_FRect box{std::round(outW_ / 2.0f - w / 2), std::round(outH_ / 2.0f - h / 2), w, h};
    ui.panel(box);
    const float ts = ui.textScale + 1;
    ui.text(render::tr("PAUSED"), box.x + w / 2, box.y + 18 * s, theme::kTitle, ts, Align::Centre);
    ui.text(ui.ellipsize(title, w - 30 * s, s), box.x + w / 2, box.y + 22 * s + ui.textHeight(ts) + 6 * s, theme::kDim, s,
            Align::Centre);
    float y = box.y + 110 * s;
    const SDL_FRect b{box.x + 24 * s, y, w - 48 * s, bh};
    auto row = [&](int i) { return SDL_FRect{b.x, y + i * (bh + gap), b.w, bh}; };
    if (ui.takeBack()) { setPaused(false); return; }
    if (ui.button(row(0), render::tr("Resume"), ButtonStyle::Primary)) setPaused(false);
    if (ui.button(row(1), render::tr("Options"))) { pauseOptions_ = true; ui.resetFocus(0); }
    // The original's in-game menu offers Save / Load as well (UISaveGame FUN_00474e20).
    const bool canSave = world_->gameEndState() == game::GameEnd::None && !savesDir().empty();
    if (ui.button(row(2), render::tr("Save game"), ButtonStyle::Normal, canSave)) { openSlots(true); ui.resetFocus(0); }
    if (ui.button(row(3), render::tr("Load game"))) { openSlots(false); ui.resetFocus(0); }
    if (ui.button(row(4), render::tr("Restart mission"))) restart();
    if (ui.button(row(5), render::tr("Quit to main menu"))) result_ = SessionResult::QuitToMenu;
    if (ui.button(row(6), render::tr("Quit game"))) result_ = SessionResult::QuitApp;
    if (settings_.cheats &&
        ui.button(row(7), render::tr("Cheats"), ButtonStyle::Subtle, world_->gameEndState() == game::GameEnd::None))
        openCheats(true);
}

void Session::openSlots(bool saving) {
    pauseSlots_ = saving ? 1 : 2;
    pauseOptions_ = false;
    slots_ = listSaveSlots();
    slotStatus_.clear();
    slotScroll_ = 0;
    // Saving: the first empty slot (else the oldest); loading: the newest save.
    slotSel_ = 0;
    std::int64_t best = saving ? INT64_MAX : INT64_MIN;
    bool foundEmpty = false;
    for (const SaveSlot& sl : slots_) {
        if (saving && !sl.used && !foundEmpty) { slotSel_ = sl.index; foundEmpty = true; }
        if (!sl.valid || foundEmpty) continue;
        if (saving ? sl.meta.savedAt < best : sl.meta.savedAt > best) { best = sl.meta.savedAt; slotSel_ = sl.index; }
    }
}

// Save / load slot page of the pause menu.
void Session::drawPauseSlots(Ui& ui) {
    const bool saving = pauseSlots_ == 1;
    const float s = Ui::bodyScaleFor(outW_, outH_);
    ui.textScale = Ui::labelScaleFor(outW_, outH_);
    ui.bodyScale = ui.textScale;
    const float w = std::min(620 * s, outW_ - 20.0f), bh = 40 * s;
    const float rowH = std::round(ui.textHeight(ui.bodyScale) + 10 * ui.bodyScale);  // Ui::list row height
    const float listH = std::min(kSaveSlots * rowH + 8 * s, outH_ - 220 * s);
    const float h = 80 * s + listH + 16 * s + ui.textHeight(ui.textScale) + 16 * s + bh + 24 * s;
    const SDL_FRect box{std::round(outW_ / 2.0f - w / 2), std::round(outH_ / 2.0f - h / 2), w, h};
    ui.panel(box);
    const float ts = ui.textScale + 1;
    ui.text(render::tr(saving ? "SAVE GAME" : "LOAD GAME"), box.x + w / 2, box.y + 18 * s, theme::kTitle, ts, Align::Centre);
    std::vector<ListRow> rows;
    for (const SaveSlot& sl : slots_) rows.push_back({slotTitle(sl, true),
                        sl.used && !sl.valid
                            ? std::string(render::tr(sl.error.find("version") != std::string::npos ? "incompatible version"
                                                                                                    : "unreadable"))
                            : slotDetail(sl),
                        saving || sl.valid});
    const SDL_FRect listR{box.x + 20 * s, box.y + 80 * s, w - 40 * s, listH};
    const int activated = ui.list(listR, rows, slotSel_, slotScroll_);
    const bool selValid = slotSel_ >= 0 && slotSel_ < int(slots_.size());
    const SaveSlot* sel = selValid ? &slots_[slotSel_] : nullptr;
    const float statusY = listR.y + listR.h + 12 * s;
    if (!slotStatus_.empty())
        ui.text(ui.ellipsize(slotStatus_, w - 40 * s, ui.textScale), box.x + w / 2, statusY, theme::kAccent, ui.textScale,
                Align::Centre);
    else if (saving && sel && sel->used)
        ui.text(render::tr("This overwrites the selected save"), box.x + w / 2, statusY, theme::kDim, ui.textScale, Align::Centre);
    const float by = statusY + ui.textHeight(ui.textScale) + 16 * s;
    const bool back = ui.button(SDL_FRect{box.x + 20 * s, by, 180 * s, bh}, render::tr("Back"));
    const bool canAct = sel && (saving || sel->valid);
    const char* label = render::tr(saving ? (sel && sel->used ? "Overwrite" : "Save") : "Load");
    const bool act = ui.button(SDL_FRect{box.x + w - 200 * s, by, 180 * s, bh}, label, ButtonStyle::Primary, canAct) ||
                     (activated >= 0 && canAct);
    if (back || ui.takeBack()) {
        pauseSlots_ = 0;
        ui.resetFocus(saving ? 2 : 3);
        return;
    }
    if (!act) return;
    if (saving) {
        std::string err;
        if (saveToSlot(slotSel_, err)) {
            slots_ = listSaveSlots();
            slotStatus_ = render::trf("Saved to slot %d", slotSel_ + 1);
        } else {
            slotStatus_ = std::string(render::tr("Save failed:")) + " " + render::tr(err);
        }
    } else {
        loadSlot_ = slotSel_;
        result_ = SessionResult::LoadGame;
    }
}

// In-game options (the original's Esc menu has game speed and scroll rate sliders too). The rows
// scroll (wheel, scroll bar, keyboard focus) when the window is too small; Back stays fixed below.
void Session::drawPauseOptions(Ui& ui) {
    const float s = Ui::bodyScaleFor(outW_, outH_);
    ui.textScale = Ui::labelScaleFor(outW_, outH_);
    const float w = std::min(680 * s, outW_ - 20.0f), rh = 34 * s, gap = 6 * s, bh = 40 * s;
    constexpr int kRows = kInGameOptionRows + 2;  // no Graphics row in game; + the volumes
    const float rowsH = kRows * (rh + gap) - gap + 8 * s;
    const float chrome = 80 * s + 16 * s + bh + 24 * s;  // title, gap, Back, bottom margin
    const float h = std::min(chrome + rowsH, outH_ - 20.0f);
    const SDL_FRect box{std::round(outW_ / 2.0f - w / 2), std::round(outH_ / 2.0f - h / 2), w, h};
    ui.panel(box);
    const float ts = ui.textScale + 1;
    ui.text(render::tr("Options"), box.x + w / 2, box.y + 18 * s, theme::kTitle, ts, Align::Centre);
    const SDL_FRect view{box.x + 14 * s, box.y + 76 * s, w - 34 * s, h - chrome + 4 * s};
    const float y = ui.beginScroll(view, pauseOptionsScroll_, rowsH, std::max(4.0f, 5 * s)) + 4 * s;
    auto row = [&](int i) { return SDL_FRect{box.x + 20 * s, y + i * (rh + gap), w - 46 * s, rh}; };
    const int n = inGameOptionRows(ui, settings_, row);
    ui.slider(row(n), render::tr("Music volume"), settings_.musicVolume);
    ui.slider(row(n + 1), render::tr("Sound volume"), settings_.soundVolume);
    ui.endScroll();
    const SDL_FRect back{box.x + w / 2 - 100 * s, box.y + h - bh - 24 * s, 200 * s, bh};
    if (ui.button(back, render::tr("Back"), ButtonStyle::Primary) || ui.takeBack()) {
        pauseOptions_ = false;
        ui.resetFocus(1);
    }
}

}  // namespace d2k::platform
