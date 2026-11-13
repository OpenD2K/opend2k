#include "platform/game_data.hpp"

#include "platform/language.hpp"

#include "formats/io.hpp"
#include "formats/language.hpp"
#include "formats/mission_ini.hpp"
#include "game/scenario.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace d2k::platform {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Mission__LoadObjectives forces the tileset/TILEATR of some stock multiplayer maps by name
// (re/notes/map-and-spawn.md §3.2); everything else keeps _SPAWN.MIS's.
void applyMpTilesetOverride(const std::string& stem, formats::MisFile& mis) {
    const std::string s = upper(stem);
    if (s == "2PLAY11") { mis.tileset = "BLOXTREE"; mis.tileAttributes = "TILEATR1"; }
    else if (s == "2PLAY12") { mis.tileset = "BLOXBASE"; mis.tileAttributes = "TILEATR2"; }
    else if (s == "6PLAY3") { mis.tileset = "BLOXWAST"; mis.tileAttributes = "TILEATR4"; }
    else if (s == "2PLAY13" || s == "4PLAY9" || s == "4PLAY10" || s == "6PLAY4" || s == "6PLAY5" || s == "2PLAY14" ||
             s == "4PLAY8") {
        mis.tileset = "BLOXXMAS";
        mis.tileAttributes = "TILEATR7";
    }
}

// First music track of a mission (Game__MainLoopFrame 0x448550): campaign (GameType 0)
// "%s.aud" of gCampaignTable[MissionNumber + MySideID * 10].music (0x7957fc; loaded from
// UI_DATA/campaign.uib: 0x104-byte records, the track name at +0xF4); other game types
// "arakatak.aud". Campaign maps are named <A|H|O><mission>V<n>.
std::string missionMusic(const std::string& dataDir, const std::string& stem, bool skirmish, int side) {
    const std::string s = upper(stem);
    const bool campaign = !skirmish && s.size() >= 4 && (s[0] == 'A' || s[0] == 'H' || s[0] == 'O') &&
                          std::isdigit(static_cast<unsigned char>(s[1]));
    if (!campaign || side < 0 || side > 2) return "arakatak";
    const int mission = std::atoi(s.c_str() + 1);
    const std::string uib = formats::findFileNoCase(dataDir + "/UI_DATA", "campaign.uib");
    if (uib.empty() || mission < 0 || mission > 9) return "arakatak";
    const auto bytes = formats::readFile(uib);
    const std::size_t at = static_cast<std::size_t>(mission + side * 10) * 0x104 + 0xF4;
    if (at + 16 > bytes.size()) return "arakatak";
    std::string name;
    for (std::size_t i = at; i < at + 16 && bytes[i]; ++i) name += static_cast<char>(bytes[i]);
    return name.empty() ? "arakatak" : name;
}

// LoadTileset 0x462d10, bloxxmas branch: read BLOXXMAS.R8, build a 256-entry 16-bit palette
// from bin\Palette.bin (FUN_0046abe0: 256 x RGB, 6-bit VGA values) as
// GetColor16bit(r << 2, g << 2, b << 2) (0x42f3d0; 555 mode: each 8-bit channel >> 3), and
// raw-blit every 32x32 tile through it into a new 16 bpp image (index 0 -> palette[0], black).
formats::R16File loadXmasTileset(const std::string& r8Path, const std::string& palettePath) {
    formats::R16File f = formats::loadR16(r8Path);
    const auto pal = formats::readFile(palettePath);
    if (pal.size() < 0x300) throw std::runtime_error("bin/Palette.bin: too short");
    std::array<std::uint16_t, 256> c16{};
    for (int i = 0; i < 256; ++i) {
        const std::uint8_t r = static_cast<std::uint8_t>(pal[i * 3] << 2), g = static_cast<std::uint8_t>(pal[i * 3 + 1] << 2),
                           b = static_cast<std::uint8_t>(pal[i * 3 + 2] << 2);
        c16[i] = static_cast<std::uint16_t>((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
    }
    for (auto& fr : f.frames) {
        if (fr.empty() || fr.bpp != 8) continue;
        std::vector<std::uint8_t> px(fr.pixels.size() * 2);
        for (std::size_t i = 0; i < fr.pixels.size(); ++i) {
            px[i * 2] = static_cast<std::uint8_t>(c16[fr.pixels[i]] & 0xff);
            px[i * 2 + 1] = static_cast<std::uint8_t>(c16[fr.pixels[i]] >> 8);
        }
        fr.pixels = std::move(px);
        fr.bpp = 16;
        fr.palette = -1;
    }
    f.palettes.clear();
    return f;
}

// The game folder (the parent of the data folder).
std::string gameFolder(const std::string& dataDir) {
    std::filesystem::path d(dataDir);
    if (!d.has_filename()) d = d.parent_path();
    return d.parent_path().string();
}

}  // namespace

// rules.ini.c LoadVars: the VARS.BIN variables and the patch's rules by name (case-insensitive,
// like GetPrivateProfileInt). Byte-sized variables keep the low byte as the patch's
// assignments do (SandWormShotSleep=40000 -> 64).
void applyMissionVars(const formats::IniSection& vars, formats::Vars& v, game::RulesExt& ext, bool missionFile) {
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto boolOf = [&](const std::string& s) {
        const std::string l = lower(s);
        return l == "yes" || l == "true" || l == "1" || l == "on";
    };
    for (const auto& [key, value] : vars) {
        const std::string k = lower(key);
        const long n = std::strtol(value.c_str(), nullptr, 10);
        const auto u32 = static_cast<std::uint32_t>(n);
        const auto u8 = static_cast<std::uint8_t>(n & 0xff);
        if (k == "harvestunloaddelay") v.harvestUnloadDelay = u32;
        else if (k == "harvestblobvalue") v.harvestBlobValue = u32;
        else if (k == "harvestloadspicedelay") v.harvestLoadSpiceDelay = u32;
        else if (k == "starportupdatedelay") v.starportUpdateDelay = u32;
        else if (k == "starportstockincreasedelay") v.starportStockIncreaseDelay = u32;
        else if (k == "starportstockincreaseprob") v.starportStockIncreaseProb = u32;
        else if (k == "starportcostvariationpercent") v.starportCostVariationPercent = u32;
        else if (k == "starportfrigatedelay") v.starportFrigateDelay = u32;
        else if (k == "refineryexplosionoffsetx") v.refineryExplosionOffsetX = std::int32_t(n);
        else if (k == "refineryexplosionoffsety") v.refineryExplosionOffsetY = std::int32_t(n);
        else if (k == "harvesterdrivedistance") v.harvesterDriveDistance = u32;
        else if (k == "repairdrivedistance") v.repairDriveDistance = u32;
        else if (k == "buildingrepairvalue") v.buildingRepairValue = u32;
        else if (k == "unitrepairvalue") v.unitRepairValue = u32;
        else if (k == "singleplayerdelay") v.singlePlayerDelay = u32;
        else if (k == "numberoffremen") v.numberOfFremen = u8;
        else if (k == "sandwormappetite") v.sandWormAppetite = u8;
        else if (k == "sandworminitialsleep") v.sandWormInitialSleep = u8;
        else if (k == "sandwormfedsleep") v.sandWormFedSleep = u8;
        else if (k == "sandwormshotsleep") v.sandWormShotSleep = u8;
        else if (k == "numberofcrates") v.numberOfCrates = u8;
        else if (k == "cratesperplayer") v.cratesPerPlayer = boolOf(value) ? 1 : 0;
        else if (k == "devastatorexplodedelay") v.devastatorExplodeDelay = u8;
        else if (k == "ignoredistance") v.ignoreDistance = u32;
        else if (k == "cratecash") v.crateCash = u32;
        else if (k == "showwarnings") v.showWarnings = boolOf(value) ? 1 : 0;
        else if (k == "deathhandaccuracy") v.deathHandAccuracy = u8;
        else if (k == "infinitespice") { if (missionFile) ext.infiniteSpice = boolOf(value); }
        else if (k == "buildingsalwaysneedprerequisites") ext.buildingsAlwaysNeedPrerequisites = boolOf(value);
        else if (k == "guardmoderadius") ext.guardModeRadius = u32;
        else if (k == "costpercentageeasy") ext.costPercentageEasy = u32;
        else if (k == "costpercentagehard") ext.costPercentageHard = u32;
        else if (k == "buildspeedpercentageeasy") ext.buildSpeedPercentageEasy = u32;
        else if (k == "buildspeedpercentagehard") ext.buildSpeedPercentageHard = u32;
        else if (k == "returncreditstospicestorage") ext.returnCreditsToSpiceStorage = boolOf(value);
        else if (k == "infantryreleaselimit") ext.infantryReleaseLimit = u8;
        else if (k == "infantryreleasechance") ext.infantryReleaseChance = u8;
        else if (k == "uncloakremainingstealthunit") ext.uncloakRemainingStealthUnit = boolOf(value);
        else if (k == "showneutralbecomehostilemsg") ext.showNeutralBecomeHostileMsg = boolOf(value);
        else if (k == "intervalsareoffbyonetick") ext.intervalsAreOffByOneTick = boolOf(value);
        else SDL_Log("Mission rule %s=%s: not supported yet", key.c_str(), value.c_str());
    }
}

game::Rules rulesOf(const GameData& g) {
    return {&g.templates, &g.speed, &g.armour, &g.buildingArt, g.builExp.empty() ? nullptr : &g.builExp, &g.vars,
            g.circles.empty() ? nullptr : &g.circles, &g.rulesExt};
}

game::SkirmishSetup skirmishSetupOf(const GameData& g) {
    game::SkirmishSetup s;
    s.techPos = g.haveTechPos ? &g.techPos : nullptr;
    s.playerHouse = g.start.sk.playerHouse;
    s.aiPlayers = g.start.sk.aiPlayers;
    s.techLevel = g.start.sk.techLevel;
    s.startingCredits = g.start.sk.startingCredits;
    s.unitCount = g.start.sk.unitCount;
    s.worms = g.start.sk.worms;
    s.difficulty = g.start.sk.difficulty;
    s.crates = g.start.sk.crates;
    s.playerColor = g.start.sk.playerColor;
    return s;
}

GameData loadGameData(const std::string& dataDir, const GameStart& start) {
    using formats::findFileNoCase;
    GameData g;
    g.dataDir = dataDir;
    g.start = start;
    const std::string& data = dataDir;
    const std::string mapsDir = data + "/maps", missionsDir = data + "/Missions";
    auto require = [](const std::string& path, const std::string& what) {
        if (path.empty()) throw std::runtime_error(what + " not found");
        return path;
    };

    // Community missions (docs/community-campaigns.md): Missions/<stem>.ini can name a mods
    // folder in CustomCampaignData whose data/ files replace the game's for this mission (the
    // Gruntmods launcher copies them over data/ before the start; we look there first instead).
    const std::string stem = std::filesystem::path(start.map).stem().string();
    formats::MissionIni mi;
    if (!start.skirmish) mi = formats::loadMissionIni(findFileNoCase(missionsDir, stem + ".ini"));
    const std::string modsData = formats::missionModsDataDir(dataDir, mi);
    if (!modsData.empty()) SDL_Log("Mission data overrides: %s", modsData.c_str());
    g.modsDataDir = modsData;
    auto dfind = [&](const std::string& sub, const std::string& name) {
        return formats::findOverlayFile(modsData, data, sub, name);
    };

    std::string mapPath = findFileNoCase(mapsDir, start.map);
    if (mapPath.empty()) mapPath = findFileNoCase(missionsDir, start.map);
    const auto map = formats::loadMap(require(mapPath, "map " + start.map));

    // The mission file names the tileset and assigns houses to sides. Campaign maps
    // have their own _NAME.MIS / NAME.MIS; skirmish maps share _SPAWN.MIS.
    std::string misPath;
    if (!start.skirmish) {
        misPath = findFileNoCase(mapsDir, "_" + stem + ".MIS");
        if (misPath.empty()) misPath = findFileNoCase(missionsDir, stem + ".MIS");
        if (misPath.empty()) misPath = findFileNoCase(missionsDir, "_" + stem + ".MIS");  // Gruntmods missions
    }
    if (misPath.empty()) misPath = findFileNoCase(mapsDir, "_SPAWN.MIS");
    auto mis = formats::loadMis(require(misPath, "mission file"));
    SDL_Log("Mission: %s", misPath.c_str());
    if (start.skirmish) {
        applyMpTilesetOverride(stem, mis);
    }

    g.tilesetName = start.tileset.empty() ? mis.tileset + ".R16" : start.tileset;
    if (upper(std::filesystem::path(g.tilesetName).stem().string()) == "BLOXXMAS") {
        // LoadTileset 0x462d10: "bloxxmas" (strcmpi) is always read from the 8-bit .R8 and
        // converted to 16 bpp through bin\Palette.bin, even when a .R16 exists.
        g.tilesetName = "BLOXXMAS.R8";
        g.tiles = loadXmasTileset(require(dfind("", "BLOXXMAS.R8"), "tileset BLOXXMAS.R8"),
                                  require(dfind("bin", "PALETTE.BIN"), "bin/Palette.bin"));
    } else {
        g.tiles = formats::loadR16(require(dfind("", g.tilesetName), "tileset " + g.tilesetName));
    }
    const std::string spritesPath = require(dfind("", "DATA.R16"), "DATA.R16");
    const std::string templatesPath = require(dfind("bin", "Templates.bin"), "Templates.bin");
    g.sprites = formats::loadR16(spritesPath);
    g.templates = formats::loadTemplates(templatesPath);
    g.tileData = formats::loadTileData(require(dfind("bin", "TILEDATA.BIN"), "TILEDATA.BIN"));
    // [Data] ColoursFile=: CustomCampaignData/<folder>/Colours/<file> replaces bin/COLOURS.BIN.
    std::string coloursPath;
    if (!mi.coloursFile.empty()) {
        const std::string dir = formats::customCampaignDir(dataDir, mi.campaignFolder);
        const std::string cdir = dir.empty() ? std::string() : findFileNoCase(dir, "Colours");
        if (!cdir.empty()) coloursPath = findFileNoCase(cdir, mi.coloursFile);
        if (coloursPath.empty()) SDL_Log("ColoursFile %s not found", mi.coloursFile.c_str());
    }
    if (coloursPath.empty()) coloursPath = dfind("bin", "COLOURS.BIN");
    g.colours = formats::loadColours(require(coloursPath, "COLOURS.BIN"));
    g.speed = formats::loadSpeed(require(dfind("bin", "SPEED.BIN"), "SPEED.BIN"));
    g.armour = formats::loadArmour(require(dfind("bin", "ARMOUR.BIN"), "ARMOUR.BIN"));
    const std::string atrName = mis.tileAttributes + ".BIN";
    const std::string tileAtrPath = require(dfind("bin", atrName), atrName);
    g.tileAtr = formats::loadTileAtr(tileAtrPath);
    const char* modArtEnv = std::getenv("D2K_MOD_ART");  // "0": off (A/B checks), "1": list the types / frames
    if (!modsData.empty() && !(modArtEnv && modArtEnv[0] == '0')) {
        // Community art (render/mod_art.hpp): the mods folder's files compared with the base install's.
        auto base = [&](const std::string& sub, const std::string& name) {
            return formats::findOverlayFile(std::string(), data, sub, name);
        };
        render::ModArtInput in;
        in.sprites = &g.sprites;
        in.spritesPath = spritesPath;
        in.baseSpritesPath = base("", "DATA.R16");
        in.templates = &g.templates;
        in.templatesPath = templatesPath;
        in.baseTemplatesPath = base("bin", "Templates.bin");
        in.tiles = &g.tiles;
        in.tilesetStem = std::filesystem::path(g.tilesetName).stem().string();
        in.tilesPath = dfind("", g.tilesetName);
        in.baseTilesPath = base("", g.tilesetName);
        in.tilesConverted = g.tilesetName == "BLOXXMAS.R8";
        in.tileAtrFromMod = tileAtrPath != base("bin", atrName);
        g.modArt = render::analyseModArt(in);
        if (g.modArt.active) {
            SDL_Log("%s", g.modArt.summary.c_str());
            if (modArtEnv && modArtEnv[0] == '1') {
                for (const auto& t : g.modArt.ownTypes) SDL_Log("Mod art: %s", t.c_str());
                std::string ranges;  // the campaign's own frames, as ranges
                const int n = int(g.modArt.frameSource.size());
                for (int i = 0; i < n; ++i) {
                    if (!g.modArt.ownFrame(i)) continue;
                    int j = i;
                    while (j + 1 < n && g.modArt.ownFrame(j + 1)) ++j;
                    ranges += " " + std::to_string(i) + (j > i ? "-" + std::to_string(j) : std::string());
                    i = j;
                }
                if (!ranges.empty()) SDL_Log("Mod art: own frames%s", ranges.c_str());
            }
        }
        else if (in.tileAtrFromMod) SDL_Log("Mod art: none; terrain: campaign %s (designed terrain follows its flags)", atrName.c_str());
    }
    g.mission = mis;
    // [Basic] TextUib=: the mission's string table instead of Text.UIB (spawn.ini TextUib).
    std::string textPath = mi.textUib.empty() ? std::string() : dfind("UI_DATA", mi.textUib);
    if (!mi.textUib.empty() && textPath.empty()) SDL_Log("TextUib %s not found", mi.textUib.c_str());
    if (textPath.empty()) textPath = dfind("UI_DATA", "Text.UIB");
    // The active language's values merged in by key (platform/language.hpp).
    if (!textPath.empty()) g.text = loadGameText(data, textPath);
    // Sidebar art: UI_ENG.R16, or the language's ui_<lng>.r16 (Setup__LoadUI_LanguageFile 0x460ec0).
    {
        std::string ui = modsData.empty() ? std::string() : dfind("", "UI_ENG.R16");
        if (ui.empty() || ui.rfind(data, 0) == 0) ui = uiR16Path(data);
        if (!ui.empty()) g.ui = formats::loadR16(ui, formats::R16Layout::Ui);
    }
    (void)require(dfind("", "FONT.FNT"), "FONT.FNT");
    g.font = loadGameFont(data, false);
    if (auto p = dfind("", "UIBB.R16"); !p.empty()) {
        g.uibb = formats::readFile(p);
        if (g.uibb.size() < 640u * 400u * 2u) g.uibb.clear();
    }
    try {
        const auto fnt = dfind("", "FONTCOL.FNT"), fpl = dfind("", "FONTCOL.FPL");
        if (!fnt.empty() && !fpl.empty()) {
            g.fontCol = loadGameFont(data, true);
            g.fontColPalette = formats::loadFontPalette(fpl);
            g.haveFontCol = true;
        }
    } catch (const std::exception& e) {
        SDL_Log("FONTCOL: %s", e.what());
    }
    g.map = map;
    g.scenario = game::buildScenario(map, mis, g.templates, g.tileData);
    if (const std::string p = dfind("bin", "BUILEXP.BIN"); !p.empty()) g.builExp = formats::readFile(p);
    if (const std::string p = dfind("bin", "VARS.BIN"); !p.empty()) g.vars = formats::loadVars(p);
    if (!start.skirmish && mi.present) {
        // LoadRulesFromMap (1.06p rules.ini.c): VARS.BIN, then the game folder's rules.ini
        // [Vars], then the mission .ini [Vars] (InfiniteSpice only from the mission).
        const auto rootIni = formats::readIni(findFileNoCase(gameFolder(dataDir), "rules.ini"));
        if (const formats::IniSection* v = formats::iniSection(rootIni, "Vars")) applyMissionVars(*v, g.vars, g.rulesExt, false);
        applyMissionVars(mi.vars, g.vars, g.rulesExt, true);
        // In-game strings are bytes of the active code page (like the game's text table): the
        // .ini's own code page converted (formats/language.hpp recode).
        const int cp = formats::lang::activeCodepage();
        for (const auto& [k, v] : mi.text) g.missionText[k] = formats::lang::recode(v, mi.codepage, cp);
        for (int i = 0; i < 8; ++i) g.sideNames[std::size_t(i)] = formats::lang::recode(mi.sides[std::size_t(i)], mi.codepage, cp);
        if (!mi.playersFile.empty()) {
            const std::string dir = formats::customCampaignDir(dataDir, mi.campaignFolder);
            const std::string pdir = dir.empty() ? std::string() : findFileNoCase(dir, "Players");
            const std::string pf = pdir.empty() ? std::string() : findFileNoCase(pdir, mi.playersFile);
            if (!pf.empty()) {
                const auto names = formats::loadPlayersFile(pf);
                for (int i = 0; i < 8; ++i)
                    if (!names[std::size_t(i)].empty())
                        g.sideNames[std::size_t(i)] = formats::lang::recode(names[std::size_t(i)], formats::lang::kUtf8, cp);
            }
        }
    }
    if (const std::string p = dfind("bin", "CIRCLES.BIN"); !p.empty()) g.circles = formats::readFile(p);
    if (const std::string p = dfind("bin", "TECHPOS.BIN"); !p.empty()) {
        g.techPos = formats::loadTechPos(p);
        g.haveTechPos = true;
    }
    // The campaign player's side (Harkonnen missions use side 1, Ordos side 2): "SideId=" in the
    // mission's .ini next to the map. Skirmish: the local player is side 0.
    std::string iniMusic;
    if (!start.skirmish && mi.present) {
        if (mi.sideId >= 0) g.playerSide = mi.sideId & 7;
        // "Music=NAME.AUD": a community-patch extension of the mission .ini (music-func.c).
        iniMusic = mi.music;
        if (const auto dot = iniMusic.rfind('.'); dot != std::string::npos) iniMusic.resize(dot);
    }
    g.missionSide = g.playerSide;
    // The mission's music stays the one of its own side (campaign.uib by mission and house).
    g.musicTrack = missionMusic(dataDir, stem, start.skirmish, g.playerSide);
    // Play as any side (not original): the human takes the chosen side; World gets missionSide
    // as its script side (AI swap + outcome rules, docs/play-as.md).
    if (!start.skirmish && start.playAsSide >= 0 && start.playAsSide < game::kMaxSides) {
        g.playerSide = start.playAsSide;
        SDL_Log("Play as side %d (house %d) instead of side %d", g.playerSide, int(g.mission.houseId[g.playerSide]),
                g.missionSide);
    }
    g.campaignMusic = !start.skirmish;
    if (!iniMusic.empty()) g.musicTrack = iniMusic;
    // Building art: [base][normal 0..N-1][damaged ...] per art (re/notes/rendering.md §3).
    const auto& T = g.templates;
    const int artCount = T.bin.buildingArtCount;  // (packed struct: copy before std::min)
    const int arts = std::min(artCount, static_cast<int>(T.buildingArtFirstFrame.size()));
    g.buildingArt.base.resize(static_cast<std::size_t>(arts));
    g.buildingArt.normal.resize(static_cast<std::size_t>(arts) * 32);
    auto sizeOf = [&](int frame) {
        game::ArtSize a;
        if (frame >= 0 && frame < static_cast<int>(g.sprites.frames.size()) && !g.sprites.frames[frame].empty()) {
            a.height = static_cast<std::uint8_t>(g.sprites.frames[frame].frameHeight);
            a.width = static_cast<std::uint8_t>(g.sprites.frames[frame].frameWidth);
            a.yoff = g.sprites.frames[frame].y;
        }
        return a;
    };
    for (int art = 0; art < arts; ++art) {
        const int first = T.buildingArtFirstFrame[art];
        g.buildingArt.base[art] = sizeOf(first);
        const int dirs = T.bin.buildingArtDirections[art];
        const int n = std::min(32, dirs);
        for (int i = 0; i < n; ++i) g.buildingArt.normal[static_cast<std::size_t>(art) * 32 + i] = sizeOf(first + 1 + i);
    }
    return g;
}

const char* houseName(int house) {
    static const char* const kNames[8] = {"Atreides", "Harkonnen", "Ordos",     "Emperor",
                                          "Fremen",   "Smugglers", "Mercenaries", "Sandworm"};
    return house >= 0 && house < 8 ? kNames[house] : "?";
}

std::vector<PlayAsOption> playAsOptions(const std::string& dataDir, const std::string& map) {
    using formats::findFileNoCase;
    std::vector<PlayAsOption> out;
    try {
        const std::string mapsDir = dataDir + "/maps", missionsDir = dataDir + "/Missions";
        const std::string stem = std::filesystem::path(map).stem().string();
        std::string mapPath = findFileNoCase(mapsDir, map);
        if (mapPath.empty()) mapPath = findFileNoCase(missionsDir, map);
        // (the same .MIS search as loadGameData; no _SPAWN.MIS fallback: campaign-style missions only)
        std::string misPath = findFileNoCase(mapsDir, "_" + stem + ".MIS");
        if (misPath.empty()) misPath = findFileNoCase(missionsDir, stem + ".MIS");
        if (misPath.empty()) misPath = findFileNoCase(missionsDir, "_" + stem + ".MIS");
        const std::string tdPath = findFileNoCase(dataDir + "/bin", "TILEDATA.BIN");
        if (mapPath.empty() || misPath.empty() || tdPath.empty()) return out;
        const formats::MissionIni mi = formats::loadMissionIni(findFileNoCase(missionsDir, stem + ".ini"));
        const int own = mi.present && mi.sideId >= 0 ? (mi.sideId & 7) : 0;
        const auto mis = formats::loadMis(misPath);
        const auto counts = game::sideObjectCounts(formats::loadMap(mapPath), formats::loadTileData(tdPath));
        for (int s = 0; s < game::kMaxSides; ++s) {
            const int house = mis.houseId[std::size_t(s)];
            if (s != own && (counts[std::size_t(s)] == 0 || house == 7)) continue;  // nothing there / sandworms
            PlayAsOption o{s, house, counts[std::size_t(s)], s == own};
            if (s == own) out.insert(out.begin(), o);
            else out.push_back(o);
        }
    } catch (const std::exception& e) {
        SDL_Log("Play as: %s: %s", map.c_str(), e.what());
        out.clear();
    }
    return out;
}

}  // namespace d2k::platform
