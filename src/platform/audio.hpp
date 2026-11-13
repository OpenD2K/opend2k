#pragma once
// SDL3 audio output shaped like the original's Miles setup (re/notes/audio.md): six sample
// channels (0 = speech, 1..5 = effects; game::SoundSystem decides what goes where) and one
// streamed music track. Each channel is an SDL_AudioStream bound to the default device.

#include "formats/aud.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

struct SDL_AudioStream;

namespace d2k::platform {

class Audio {
public:
    static constexpr int kChannels = 6;

    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool ok() const { return device_ != 0; }
    // Replaces whatever plays on `channel` with `clip` (null: the channel just stops).
    // volume / pan are AIL values: volume 0..127, pan 0..127 (64 = centre).
    void startChannel(int channel, const formats::AudClip* clip, int sample, int volume, int pan);
    bool channelBusy(int channel) const;
    int channelSample(int channel) const { return ch_[channel % kChannels].sample; }
    int channelVolume(int channel) const { return ch_[channel % kChannels].volume; }
    void stopAll();
    // Pauses / resumes the device (iPad: the app went to the background and back).
    void setDevicePaused(bool paused);
    // Releases finished channel streams; call once per frame.
    void update();

    // Background music: one stream at a time; volume 0..1.
    void playMusic(const formats::AudClip& clip, float volume);
    bool musicPlaying() const;
    void setMusicVolume(float volume);
    void stopMusic();

    // Cutscene soundtrack (platform/movie_player.hpp): one stream fed while the movie plays.
    // Signed 16-bit interleaved samples; volume 0..1.
    bool startMovieAudio(int sampleRate, int channels, float volume);
    void queueMovieAudio(const std::int16_t* samples, std::size_t count);
    // Bytes queued but not yet played (0 when there is no movie stream).
    std::size_t movieAudioQueuedBytes() const;
    void stopMovieAudio();

private:
    struct Channel {
        SDL_AudioStream* stream = nullptr;
        int sample = -1;
        int volume = 0;
    };
    unsigned device_ = 0;
    std::array<Channel, kChannels> ch_{};
    SDL_AudioStream* music_ = nullptr;
    SDL_AudioStream* movie_ = nullptr;
};

}  // namespace d2k::platform
