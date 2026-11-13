#include "platform/audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

namespace d2k::platform {

Audio::Audio() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Audio init failed: %s", SDL_GetError());
        return;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) SDL_Log("Audio device failed: %s", SDL_GetError());
}

Audio::~Audio() {
    stopAll();
    stopMovieAudio();
    if (music_) SDL_DestroyAudioStream(music_);
    if (device_) SDL_CloseAudioDevice(device_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void Audio::setDevicePaused(bool paused) {
    if (!device_) return;
    if (paused) SDL_PauseAudioDevice(device_);
    else SDL_ResumeAudioDevice(device_);
}

namespace {
// A bound stream fed with `data` (bind first: SDL 3.4 rejects data put into a stream that has
// no destination format yet).
SDL_AudioStream* makeStream(unsigned device, const SDL_AudioSpec& spec, const void* data, int bytes, float gain) {
    SDL_AudioStream* s = SDL_CreateAudioStream(&spec, nullptr);
    if (!s) return nullptr;
    SDL_SetAudioStreamGain(s, gain);
    if (!SDL_BindAudioStream(device, s) || !SDL_PutAudioStreamData(s, data, bytes)) {
        SDL_Log("Audio: %s", SDL_GetError());
        SDL_DestroyAudioStream(s);
        return nullptr;
    }
    SDL_FlushAudioStream(s);
    return s;
}
}  // namespace

void Audio::startChannel(int channel, const formats::AudClip* clip, int sample, int volume, int pan) {
    if (channel < 0 || channel >= kChannels) return;
    Channel& c = ch_[channel];
    if (c.stream) SDL_DestroyAudioStream(c.stream);  // _AIL_stop_sample / end_sample
    c.stream = nullptr;
    c.sample = sample;
    c.volume = volume;
    if (!device_ || !clip || clip->samples.empty()) return;
    const float gain = std::clamp(volume, 0, 127) / 127.0f;
    if (clip->channels != 1) {
        const SDL_AudioSpec spec{SDL_AUDIO_S16LE, clip->channels, clip->sampleRate};
        c.stream = makeStream(device_, spec, clip->samples.data(), int(clip->samples.size() * sizeof(std::int16_t)), gain);
        return;
    }
    // Mono samples are panned into a stereo stream (AIL pan 0 = left, 64 = centre, 127 = right).
    const int p = std::clamp(pan, 0, 127);
    const float l = std::min(1.0f, (127 - p) / 63.5f), r = std::min(1.0f, p / 63.5f);
    std::vector<std::int16_t> st(clip->samples.size() * 2);
    for (std::size_t i = 0; i < clip->samples.size(); ++i) {
        st[2 * i] = std::int16_t(clip->samples[i] * l);
        st[2 * i + 1] = std::int16_t(clip->samples[i] * r);
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, 2, clip->sampleRate};
    c.stream = makeStream(device_, spec, st.data(), int(st.size() * sizeof(std::int16_t)), gain);
}

bool Audio::channelBusy(int channel) const {
    if (channel < 0 || channel >= kChannels) return false;
    const SDL_AudioStream* s = ch_[channel].stream;
    return s && SDL_GetAudioStreamQueued(const_cast<SDL_AudioStream*>(s)) > 0;
}

void Audio::stopAll() {
    for (auto& c : ch_) {
        if (c.stream) SDL_DestroyAudioStream(c.stream);
        c = Channel{};
    }
}

void Audio::playMusic(const formats::AudClip& clip, float volume) {
    if (!device_ || clip.samples.empty()) return;
    if (music_) SDL_DestroyAudioStream(music_);
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, clip.channels, clip.sampleRate};
    music_ = makeStream(device_, spec, clip.samples.data(), int(clip.samples.size() * sizeof(std::int16_t)), volume);
}

bool Audio::musicPlaying() const { return music_ && SDL_GetAudioStreamQueued(music_) > 0; }

void Audio::setMusicVolume(float volume) {
    if (music_) SDL_SetAudioStreamGain(music_, volume);
}

void Audio::stopMusic() {
    if (music_) SDL_DestroyAudioStream(music_);
    music_ = nullptr;
}

bool Audio::startMovieAudio(int sampleRate, int channels, float volume) {
    stopMovieAudio();
    if (!device_ || sampleRate <= 0 || channels < 1) return false;
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, channels, sampleRate};
    movie_ = SDL_CreateAudioStream(&spec, nullptr);
    if (!movie_) return false;
    SDL_SetAudioStreamGain(movie_, volume);
    if (!SDL_BindAudioStream(device_, movie_)) {  // bind before the first put (SDL 3.4)
        SDL_Log("Audio: %s", SDL_GetError());
        SDL_DestroyAudioStream(movie_);
        movie_ = nullptr;
        return false;
    }
    return true;
}

void Audio::queueMovieAudio(const std::int16_t* samples, std::size_t count) {
    if (movie_ && count) SDL_PutAudioStreamData(movie_, samples, int(count * sizeof(std::int16_t)));
}

std::size_t Audio::movieAudioQueuedBytes() const {
    if (!movie_) return 0;
    const int q = SDL_GetAudioStreamQueued(movie_);
    return q > 0 ? std::size_t(q) : 0;
}

void Audio::stopMovieAudio() {
    if (movie_) SDL_DestroyAudioStream(movie_);
    movie_ = nullptr;
}

void Audio::update() {
    for (auto& c : ch_)
        if (c.stream && SDL_GetAudioStreamQueued(c.stream) == 0) {
            SDL_DestroyAudioStream(c.stream);  // also unbinds
            c.stream = nullptr;
        }
}

}  // namespace d2k::platform
