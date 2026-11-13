#include "platform/sound_bank.hpp"

#include "formats/io.hpp"
#include "platform/audio.hpp"
#include "platform/language.hpp"

#include <cstdlib>

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace d2k::platform {

SoundBank::SoundBank(const std::string& dataDir) : dataDir_(dataDir) {
    using formats::findFileNoCase;
    const char* env = std::getenv("D2K_SOUND_LOG");
    logFiles_ = env && *env && *env != '0';
    try {
        const auto uib = findFileNoCase(dataDir + "/UI_DATA", "samples.uib");
        if (!uib.empty()) samples_ = formats::loadUib(uib);
        // The speech language's folder (platform/language.hpp) first: its AUDs and SOUND.RS.
        voicesDir_ = activeLanguage().voicesDir;
        if (const std::string& img = activeLanguage().ps1Speech; !img.empty()) {
            ps1_ = std::make_unique<formats::ps1::SpeechBanks>();
            if (!ps1_->open(img)) SDL_Log("PS1 speech %s: %s", img.c_str(), ps1_->error().c_str());
        }
        auto rs = voicesDir_.empty() ? std::string() : findFileNoCase(voicesDir_, "SOUND.RS");
        if (rs.empty()) rs = findFileNoCase(dataDir + "/GAMESFX", "SOUND.RS");
        if (!rs.empty()) archive_ = formats::SoundArchive::load(rs);
    } catch (const std::exception& e) {
        SDL_Log("Sound data unavailable: %s", e.what());
    }
    baseSamples_ = samples_;
}

void SoundBank::setOverlay(const std::string& modsData) {
    if (modsData == overlay_) return;
    overlay_ = modsData;
    samples_ = baseSamples_;
    overlayArchive_.reset();
    if (modsData.empty()) return;
    using formats::findFileNoCase;
    try {
        if (const auto ui = findFileNoCase(modsData, "UI_DATA"); !ui.empty())
            if (const auto uib = findFileNoCase(ui, "samples.uib"); !uib.empty()) samples_ = formats::loadUib(uib);
        if (const auto sfx = findFileNoCase(modsData, "GAMESFX"); !sfx.empty())
            if (const auto rs = findFileNoCase(sfx, "SOUND.RS"); !rs.empty()) overlayArchive_ = formats::SoundArchive::load(rs);
    } catch (const std::exception& e) {
        SDL_Log("Mission sounds %s: %s", modsData.c_str(), e.what());
    }
}

const formats::AudClip* SoundBank::resolve(const std::string& file) {
    const std::string key = overlay_.empty() ? file : overlay_ + "|" + file;
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second ? &*it->second : nullptr;
    std::optional<formats::AudClip> clip;
    std::string source;
    try {
        if (!overlay_.empty()) {
            if (overlayArchive_ && overlayArchive_->contains(file + ".wav")) {
                clip = overlayArchive_->clip(file + ".wav");
                source = "mods SOUND.RS";
            } else {
                for (const char* dir : {"GAMESFX", "GAMESFXEng"}) {
                    const auto d = formats::findFileNoCase(overlay_, dir);
                    const auto path = d.empty() ? std::string() : formats::findFileNoCase(d, file + ".aud");
                    if (!path.empty()) { clip = formats::loadAud(path); source = path; break; }
                }
            }
        }
        if (clip) {
        } else if (ps1_ && ps1_->isOpen() && (clip = ps1_->clip(file))) {
            // Русский (PS1): the disc's speech for the PC file (formats/ps1_sound.hpp).
            std::string bank;
            int slot = -1;
            formats::ps1::speechSlotFor(file, bank, slot);
            source = "ps1:" + bank + "#" + std::to_string(slot);
        } else if (archive_ && archive_->contains(file + ".wav")) {
            clip = archive_->clip(file + ".wav");
            source = "SOUND.RS";
        } else {
            // Translation voices first, then the game's own. Some translations ship 1-second silent
            // placeholders (the Stream dub's *_MAP* and *_PRMRY lines): skip those so the original plays.
            auto silent = [](const formats::AudClip& c) {
                for (std::int16_t v : c.samples)
                    if (v > 64 || v < -64) return false;
                return true;
            };
            std::vector<std::string> candidates;
            if (!voicesDir_.empty()) candidates.push_back(formats::findFileNoCase(voicesDir_, file + ".aud"));
            for (const char* dir : {"/GAMESFX", "/GAMESFXEng"})
                candidates.push_back(formats::findFileNoCase(dataDir_ + dir, file + ".aud"));
            for (const auto& path : candidates) {
                if (path.empty()) continue;
                auto c = formats::loadAud(path);
                if (silent(c) && &path != &candidates.back()) continue;
                clip = std::move(c);
                source = path;
                break;
            }
        }
    } catch (const std::exception& e) {
        SDL_Log("Sound %s: %s", file.c_str(), e.what());
    }
    if (logFiles_) SDL_Log("sound file: %s -> %s", file.c_str(), source.empty() ? "[missing]" : source.c_str());
    sources_[key] = source;
    auto [ins, _] = cache_.emplace(key, std::move(clip));
    return ins->second ? &*ins->second : nullptr;
}

const formats::AudClip* SoundBank::byId(int id) {
    if (id < 0 || id >= static_cast<int>(samples_.entries.size())) return nullptr;
    return resolve(samples_.entries[id].second);
}

const formats::AudClip* SoundBank::byName(const std::string& sampleName) {
    const std::string* file = samples_.find(sampleName);
    return file ? resolve(*file) : nullptr;
}

std::string SoundBank::sourceOf(const std::string& sampleName) {
    const std::string* file = samples_.find(sampleName);
    if (!file) return {};
    resolve(*file);
    const std::string key = overlay_.empty() ? *file : overlay_ + "|" + *file;
    auto it = sources_.find(key);
    return it == sources_.end() ? std::string() : it->second;
}

int SoundBank::idOf(const std::string& sampleName) const {
    auto it = samples_.index.find(sampleName);
    return it == samples_.index.end() ? -1 : static_cast<int>(it->second);
}

const std::string& SoundBank::nameOf(int id) const {
    static const std::string none;
    return id >= 0 && id < static_cast<int>(samples_.entries.size()) ? samples_.entries[id].first : none;
}

ChannelOutput::ChannelOutput(Audio* audio, SoundBank* bank) : audio_(audio), bank_(bank) {
    const char* env = std::getenv("D2K_SOUND_LOG");
    log_ = env && *env && *env != '0';
}

bool ChannelOutput::busy(int channel) const { return audio_ && audio_->channelBusy(channel); }
int ChannelOutput::sample(int channel) const { return audio_ ? audio_->channelSample(channel) : -1; }
int ChannelOutput::volume(int channel) const { return audio_ ? audio_->channelVolume(channel) : 0; }

void ChannelOutput::start(int channel, int sample, int pan, int volume) {
    if (!audio_ || !bank_) return;
    const formats::AudClip* clip = bank_->byId(sample);
    if (log_)
        SDL_Log("sound: ch%d %s (#%d) vol %d pan %d%s", channel, bank_->nameOf(sample).c_str(), sample, volume, pan,
                clip ? "" : " [missing]");
    audio_->startChannel(channel, clip, sample, volume, pan);
}

}  // namespace d2k::platform
