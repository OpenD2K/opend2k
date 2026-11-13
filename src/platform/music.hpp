#pragma once
// Music the way the original picks it (re/notes/audio.md): Sound__PlayMusic 0x4700e0 streams
// one data/Music/*.AUD track and loops it (FUN_0046ff30 rewinds at the end). The main menu
// plays options.aud, other front-end pages are silent; a mission starts with its track
// (campaign: campaign.uib, other games: arakatak) and in the campaign, whenever a track ends,
// FUN_00481460 picks a random other one of the house's list (FUN_00481490 / FUN_00481700).

#include <cstdint>
#include <map>
#include <string>

namespace d2k::platform {

class Audio;

class MusicPlayer {
public:
    MusicPlayer() = default;
    explicit MusicPlayer(const std::string& dataDir);

    // Starts `track` (file stem, case-insensitive, e.g. "options") from the beginning.
    // house < 0: it loops; house 0..2: when it ends a random track of that house follows.
    void play(const std::string& track, int house = -1);
    void stop();
    const std::string& current() const { return current_; }
    // Streams / switches tracks; volume 0..1.
    void update(Audio& audio, float volume);
    bool empty() const { return files_.empty(); }

    // FUN_00481700: is `file` (e.g. "atregain.aud") in the random list of `house`?
    static bool trackForHouse(int house, const std::string& file);

private:
    std::string pickRandom();  // FUN_00481490 (not the game RNG: the original's rand() here is CRT-shared)

    std::map<std::string, std::string> files_;  // lowercase file name -> path
    std::string current_;                       // lowercase stem playing ("" = silence)
    int house_ = -1;
    bool restart_ = false;
    float volume_ = -1;
    std::uint32_t seed_ = 0;
};

}  // namespace d2k::platform
