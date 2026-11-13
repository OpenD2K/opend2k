#pragma once
// Resolves the game's sample ids/names to decoded clips: UI_DATA/samples.uib maps
// sample index -> (name, file); files live in GAMESFX/SOUND.RS (*.wav) or as
// GAMESFX[Eng]/*.AUD. The speech language (platform/language.hpp) puts its own folder (GAMESFXFre,
// the Russian translation's GAMESFX) or the Russian PS1 disc's speech banks first. Clips are
// decoded lazily and cached.

#include "formats/aud.hpp"
#include "formats/ps1_sound.hpp"
#include "formats/soundrs.hpp"
#include "formats/uib.hpp"
#include "game/sound_system.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace d2k::platform {

class SoundBank {
public:
    explicit SoundBank(const std::string& dataDir);
    const formats::AudClip* byId(int id);
    const formats::AudClip* byName(const std::string& sampleName);  // e.g. "S_MANDYING1"
    // Where the sample's clip comes from (file path, "SOUND.RS", "ps1:<bank>#<slot>"; "" = missing).
    std::string sourceOf(const std::string& sampleName);
    int idOf(const std::string& sampleName) const;  // GetSoundTableID 0x48dd90 (-1: unknown)
    const std::string& nameOf(int id) const;
    // Community missions: a mods folder's data/ (GAMESFX*, UI_DATA/samples.uib) is searched
    // before the game's while set ("" = none; docs/community-campaigns.md). Clips stay cached
    // per folder, so sounds still playing keep their data.
    void setOverlay(const std::string& modsData);

private:
    const formats::AudClip* resolve(const std::string& file);
    std::string dataDir_;
    std::string voicesDir_;  // the speech language's GAMESFX folder (searched first; "" = none)
    std::unique_ptr<formats::ps1::SpeechBanks> ps1_;  // Русский (PS1): the disc's speech banks
    bool logFiles_ = false;  // D2K_SOUND_LOG: log which file each sample comes from
    formats::StringTable samples_;
    formats::StringTable baseSamples_;
    std::optional<formats::SoundArchive> archive_;
    std::string overlay_;
    std::optional<formats::SoundArchive> overlayArchive_;
    std::map<std::string, std::optional<formats::AudClip>> cache_;
    std::map<std::string, std::string> sources_;  // cache key -> source (sourceOf, D2K_SOUND_LOG)
};

class Audio;

// game::SoundSystem's channels on the SDL device (sample ids resolved through the bank).
// D2K_SOUND_LOG=1 in the environment logs every started sample.
class ChannelOutput : public game::SoundOutput {
public:
    ChannelOutput(Audio* audio, SoundBank* bank);
    bool busy(int channel) const override;
    int sample(int channel) const override;
    int volume(int channel) const override;
    void start(int channel, int sample, int pan, int volume) override;
    bool ok() const { return audio_ && bank_; }
    SoundBank* bank() const { return bank_; }

private:
    Audio* audio_;
    SoundBank* bank_;
    bool log_ = false;
};

}  // namespace d2k::platform
