#include "platform/saves.hpp"

#include "platform/settings.hpp"
#include "render/ui_text.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace d2k::platform {

namespace {
std::string gOverride;
}

void setSavesDirOverride(const std::string& dir) { gOverride = dir; }

std::string savesDir() {
    if (!gOverride.empty()) return gOverride;
    const std::string settings = Settings::path();
    if (settings.empty()) return {};
    return (std::filesystem::path(settings).parent_path() / "saves").string();
}

std::string slotPath(int slot) {
    const std::string dir = savesDir();
    if (dir.empty()) return {};
    char name[32];
    std::snprintf(name, sizeof name, "SAVE%d.SAV", slot);
    return (std::filesystem::path(dir) / name).string();
}

std::vector<std::uint8_t> readSaveSlot(int slot, std::string& error) {
    const std::string path = slotPath(slot);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return {};
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) error = path + " is empty";
    return bytes;
}

std::vector<SaveSlot> listSaveSlots() {
    std::vector<SaveSlot> out(kSaveSlots);
    for (int i = 0; i < kSaveSlots; ++i) {
        SaveSlot& s = out[i];
        s.index = i;
        const std::string path = slotPath(i);
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec)) continue;
        s.used = true;
        std::string err;
        const auto bytes = readSaveSlot(i, err);
        if (bytes.empty()) {
            s.error = err;
            continue;
        }
        try {
            s.meta = game::readSaveMeta(bytes);
            s.valid = true;
        } catch (const std::exception& e) {
            s.error = e.what();
        }
    }
    return out;
}

bool writeSaveSlot(int slot, const std::vector<std::uint8_t>& bytes, std::string& error) {
    const std::string path = slotPath(slot);
    if (path.empty()) {
        error = "no folder for save games";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + tmp;
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "write failed: " + tmp;
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "cannot replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

bool deleteSaveSlot(int slot, std::string& error) {
    const std::string path = slotPath(slot);
    if (path.empty()) {
        error = "no saves folder";
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        error = "cannot remove " + path + ": " + ec.message();
        return false;
    }
    return true;
}

std::string slotTitle(const SaveSlot& s, bool translated) {
    const std::string n = std::to_string(s.index + 1) + ". ";
    if (!s.used) return n + (translated ? render::tr("(empty)") : "(empty)");
    if (!s.valid) return n + (translated ? render::tr("(unreadable save)") : "(unreadable save)");
    return n + (s.meta.name.empty() ? s.meta.map : s.meta.name);
}

std::string slotDetail(const SaveSlot& s) {
    if (!s.used) return {};
    if (!s.valid) return s.error;
    const std::time_t t = static_cast<std::time_t>(s.meta.savedAt);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

bool isCampaignMapSave(const SaveSlot& s) { return s.valid && s.meta.kind == game::SaveKind::CampaignMap; }

std::string slotKindLabel(const SaveSlot& s, bool translated) {
    if (!s.valid) return {};
    const char* k = isCampaignMapSave(s) ? "Campaign map" : "Battle";
    return translated ? render::tr(k) : std::string(k);
}

game::SaveMeta campaignSaveMeta(int house, int missionNumber, int difficulty, const std::string& name) {
    static const char* const kHouse[3] = {"Atreides", "Harkonnen", "Ordos"};
    game::SaveMeta m;
    m.kind = game::SaveKind::CampaignMap;
    m.campaignHouse = house;
    m.missionNumber = missionNumber;
    m.difficulty = difficulty;
    m.playerHouse = house;
    m.playerSide = house;
    m.name = name.empty() ? std::string(kHouse[house < 0 || house > 2 ? 0 : house]) + " - territory map " +
                                std::to_string(missionNumber)
                          : name;
    m.savedAt = static_cast<std::int64_t>(std::time(nullptr));
    return m;
}

GameStart gameStartOf(const game::SaveMeta& m) {
    GameStart g;
    g.map = m.map;
    g.tileset = m.tileset;
    g.skirmish = m.skirmish;
    g.sk.playerHouse = m.playerHouse;
    g.sk.aiPlayers = m.aiPlayers;
    g.sk.techLevel = m.techLevel;
    g.sk.startingCredits = m.startingCredits;
    g.sk.unitCount = m.unitCount;
    g.sk.worms = m.worms;
    g.sk.difficulty = m.difficulty;
    g.title = m.name;
    g.campaignHouse = m.campaignHouse;
    g.missionIndex = m.missionIndex;
    g.playAsSide = m.playAsSide;
    return g;
}

game::SaveMeta saveMetaOf(const GameStart& start, int playerSide) {
    game::SaveMeta m;
    m.name = start.title.empty() ? start.map : start.title;
    m.map = start.map;
    m.tileset = start.tileset;
    m.skirmish = start.skirmish;
    m.playerHouse = start.sk.playerHouse;
    m.aiPlayers = start.sk.aiPlayers;
    m.techLevel = start.sk.techLevel;
    m.startingCredits = start.sk.startingCredits;
    m.unitCount = start.sk.unitCount;
    m.worms = start.sk.worms;
    m.difficulty = start.sk.difficulty;
    m.campaignHouse = start.campaignHouse;
    m.missionIndex = start.missionIndex;
    m.playerSide = playerSide;
    m.playAsSide = start.playAsSide;
    return m;
}

}  // namespace d2k::platform
