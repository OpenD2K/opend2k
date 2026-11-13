#include "platform/music.hpp"

#include "formats/aud.hpp"
#include "platform/audio.hpp"

#include <SDL3/SDL.h>

#include <cctype>
#include <exception>
#include <filesystem>
#include <vector>

namespace d2k::platform {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
}  // namespace

MusicPlayer::MusicPlayer(const std::string& dataDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dataDir + "/Music", ec))  // MusicResourcePath
        if (lower(e.path().extension().string()) == ".aud") files_[lower(e.path().filename().string())] = e.path().string();
    seed_ = static_cast<std::uint32_t>(SDL_GetTicks() ^ 0x9E3779B9u);
}

bool MusicPlayer::trackForHouse(int house, const std::string& file) {
    const std::string f = lower(file);
    if (house == 0 && f == "atregain.aud") return true;
    if (house == 1 && (f == "hark_bat.aud" || f == "risehark.aud")) return true;
    if (house == 2 && f == "entordos.aud") return true;
    static const char* const kCommon[] = {"arakatak.aud", "fightpwr.aud", "fremen.aud",   "landsand.aud",
                                          "robotix.aud",  "spicesct.aud", "undercon.aud", "waitgame.aud",
                                          "soldappr.aud", "plotting.aud", "ambush.aud"};
    for (const char* c : kCommon)
        if (f == c) return true;
    return false;
}

void MusicPlayer::play(const std::string& track, int house) {
    current_ = lower(track);
    house_ = house;
    restart_ = true;
    // "*" = a random track (community patch: mission .ini Music=* / Play Music event, PlayRandomMusic).
    if (current_ == "*") {
        current_.clear();
        current_ = pickRandom();
    }
}

void MusicPlayer::stop() {
    current_.clear();
    restart_ = true;
}

// FUN_00481490: a random file of the house's list whose name does not start with the
// current track's.
std::string MusicPlayer::pickRandom() {
    std::vector<std::string> list;
    for (const auto& [name, path] : files_)
        if (trackForHouse(house_, name)) list.push_back(name);
    if (list.empty()) return current_;
    auto next = [&] {
        seed_ = seed_ * 0x343FD + 0x269EC3;
        return list[((seed_ >> 16) & 0x7fff) % list.size()];
    };
    std::string pick = next();
    for (int guard = 0; list.size() > 1 && pick.rfind(current_, 0) == 0 && guard < 100; ++guard) pick = next();
    return pick.substr(0, pick.size() - 4);
}

void MusicPlayer::update(Audio& audio, float volume) {
    if (!audio.ok()) return;
    if (volume != volume_) {
        audio.setMusicVolume(volume);
        volume_ = volume;
    }
    if (!restart_ && (current_.empty() || audio.musicPlaying())) return;
    if (!restart_ && house_ >= 0) current_ = pickRandom();  // the track ended (campaign)
    restart_ = false;
    audio.stopMusic();
    if (current_.empty() || files_.empty()) return;
    const auto it = files_.find(current_ + ".aud");
    if (it == files_.end()) {
        SDL_Log("Music: %s.aud not found", current_.c_str());
        current_.clear();
        return;
    }
    try {
        audio.playMusic(formats::loadAud(it->second), volume);
        SDL_Log("Music: %s.aud%s", current_.c_str(), house_ >= 0 ? " (then random house tracks)" : " (looped)");
    } catch (const std::exception& ex) {
        SDL_Log("Music: %s", ex.what());
        current_.clear();
    }
}

}  // namespace d2k::platform
