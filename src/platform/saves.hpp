#pragma once
// Save game slots on disk: kSaveSlots files <saves dir>/SAVEn.SAV (n = 0..9). The saves folder
// is "saves" next to settings.ini (SDL_GetPrefPath), or --saves-dir. The original game kept
// GameSave\S%d.SAV (single player) / GameSave\P%d.SAV (multiplayer) with a named, growing list
// (UISaveGame FUN_00474e20); we keep fixed numbered slots named by mission and date.
// The file format is game/savegame.hpp.

#include "game/savegame.hpp"
#include "platform/game_data.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace d2k::platform {

constexpr int kSaveSlots = 10;

struct SaveSlot {
    int index = 0;
    bool used = false;     // a file exists
    bool valid = false;    // ... and its header/metadata could be read
    game::SaveMeta meta;
    std::string error;     // why an existing file is not valid
};

// Folder holding the slots ("" when there is no pref path). `overrideDir` replaces it (--saves-dir).
void setSavesDirOverride(const std::string& dir);
std::string savesDir();
std::string slotPath(int slot);
std::vector<SaveSlot> listSaveSlots();

// Writes the bytes atomically (temporary file + rename). False + `error` on failure.
bool writeSaveSlot(int slot, const std::vector<std::uint8_t>& bytes, std::string& error);
// Removes a slot's file (the original's DeleteGame on the delete page). False + `error` on failure.
bool deleteSaveSlot(int slot, std::string& error);
// Whole file; empty + `error` on failure.
std::vector<std::uint8_t> readSaveSlot(int slot, std::string& error);

// List row text for a slot: "3. H5V1 - Harkonnen 5" / "3. (empty)", detail = save date.
std::string slotTitle(const SaveSlot& s, bool translated = false);  // translated: render::tr (Enhanced pages only)
std::string slotDetail(const SaveSlot& s);
// "Campaign map" / "Battle" ("" for an empty or unreadable slot).
std::string slotKindLabel(const SaveSlot& s, bool translated = false);
bool isCampaignMapSave(const SaveSlot& s);

// What to load to recreate a saved session (map, mission/skirmish setup).
GameStart gameStartOf(const game::SaveMeta& m);
// Metadata from the running session's start parameters (camera, ticks, time set by the caller).
game::SaveMeta saveMetaOf(const GameStart& start, int playerSide);
// A campaign-map save (the territory map of `house` 0..2 before mission `missionNumber`, the
// difficulty gDifficultyLevel 0..2); savedAt is the current time, `name` the description.
game::SaveMeta campaignSaveMeta(int house, int missionNumber, int difficulty, const std::string& name);

}  // namespace d2k::platform
