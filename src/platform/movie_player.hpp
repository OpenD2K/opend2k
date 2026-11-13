#pragma once
// Cutscene playback: frames of a MovieDecoder (PC VQA, PS1 STR) streamed into an SDL texture
// and its soundtrack into Audio's movie stream, kept in sync with the audio clock (the samples
// the device has played; wall time for silent movies). The frame is scaled to the window with
// the movie's aspect kept (letterbox): nearest neighbour in Original graphics, linear in
// Enhanced.
//
// Skipping: the original's VQA loop (FUN_00406d10, called from CMovie FUN_0048d8f0 ->
// FUN_00403460) polls a "stop" flag (0x7983ab) once per frame and returns early when it is set;
// the front end's key handler (FUN_0043cb70) handles Esc. Here Esc, Space, Enter and a mouse
// click skip.

#include <SDL3/SDL.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace d2k::platform {

class Audio;

// One decoded movie stream.
class MovieDecoder {
public:
    virtual ~MovieDecoder() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double fps() const = 0;
    virtual int sampleRate() const = 0;  // 0 = silent
    virtual int channels() const = 0;
    virtual int frameCount() const = 0;  // 0 = unknown
    // Width / height the frame is shown at (PS1 videos use non-square pixels).
    virtual double displayAspect() const { return double(width()) / double(height()); }
    // Decodes the next frame; false at the end (or on an error).
    virtual bool nextFrame() = 0;
    virtual const std::uint32_t* pixels() const = 0;  // width*height 0xAARRGGBB
    // Audio decoded since the last call (signed 16-bit, interleaved).
    virtual std::vector<std::int16_t> takeAudio() = 0;
};

// Opens a PC VQA movie (formats/vqa.hpp); null on failure (`error` set).
std::unique_ptr<MovieDecoder> openVqaMovie(const std::string& path, std::string* error = nullptr);
// Opens the STR movie `entry` ("A_BR01.STR") of a PS1 disc image (.cue / .bin;
// formats/ps1_str.hpp); null on failure.
std::unique_ptr<MovieDecoder> openPs1Movie(const std::string& image, const std::string& entry,
                                           std::string* error = nullptr);

class MoviePlayer {
public:
    explicit MoviePlayer(SDL_Renderer* r);
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

    // Starts playing; `audio` may be null (silent). Returns false when the movie has no frame.
    bool start(std::unique_ptr<MovieDecoder> movie, Audio* audio, float volume);
    bool active() const { return movie_ != nullptr; }
    void stop();
    // Esc / Space / Enter / mouse button: skip the rest.
    void handleEvent(const SDL_Event& e);
    // Advances to the frame due now and draws it over the whole output (black bars).
    // The movie ends (active() turns false) after its last frame and its sound.
    void render(bool smooth);
    bool skipped() const { return skipped_; }

private:
    struct Frame {
        int index = 0;
        std::vector<std::uint32_t> px;
    };
    bool decodeOne();
    double clockSeconds() const;

    SDL_Renderer* r_;
    Audio* audio_ = nullptr;
    std::unique_ptr<MovieDecoder> movie_;
    SDL_Texture* tex_ = nullptr;
    std::deque<Frame> queue_;
    std::vector<std::vector<std::uint32_t>> pool_;
    int decoded_ = 0;
    int shown_ = -1;
    bool eof_ = false;
    bool skipped_ = false;
    bool haveAudio_ = false;
    std::uint64_t audioBytes_ = 0;  // put into the stream so far
    Uint64 startNs_ = 0;
    Uint64 endNs_ = 0;  // wall-clock deadline once the last frame is out
};

}  // namespace d2k::platform
