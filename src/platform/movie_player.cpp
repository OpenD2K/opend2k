#include "platform/movie_player.hpp"

#include "formats/ps1_str.hpp"
#include "formats/vqa.hpp"
#include "platform/audio.hpp"

#include <algorithm>
#include <cmath>

namespace d2k::platform {

namespace {

constexpr std::size_t kAhead = 8;        // frames decoded ahead of the one on screen
constexpr std::size_t kMaxAhead = 30;    // ... more while the sound runs low
constexpr double kMinAudioAhead = 0.35;  // seconds of sound kept queued

class VqaMovie final : public MovieDecoder {
public:
    bool open(const std::string& path, std::string* error) {
        if (!dec_.open(path)) {
            if (error) *error = dec_.error();
            return false;
        }
        return true;
    }
    int width() const override { return dec_.header().width; }
    int height() const override { return dec_.header().height; }
    double fps() const override { return dec_.header().fps; }
    int sampleRate() const override { return dec_.header().sampleRate; }
    int channels() const override { return dec_.header().channels; }
    int frameCount() const override { return dec_.header().frames; }
    bool nextFrame() override { return dec_.nextFrame(); }
    const std::uint32_t* pixels() const override { return dec_.pixels().data(); }
    std::vector<std::int16_t> takeAudio() override { return dec_.takeAudio(); }

private:
    formats::VqaDecoder dec_;
};

class Ps1Movie final : public MovieDecoder {
public:
    bool open(const std::string& image, const std::string& entry, std::string* error) {
        if (!disc_.open(image)) {
            if (error) *error = disc_.error();
            return false;
        }
        const formats::ps1::FatEntry* e = disc_.xaEntry(entry);
        if (!e || !dec_.open(disc_, *e)) {
            if (error) *error = "no movie " + entry + " on the disc";
            return false;
        }
        return true;
    }
    int width() const override { return dec_.width(); }
    int height() const override { return dec_.height(); }
    double fps() const override { return 15.0; }  // every PS1 Dune 2000 STR
    int sampleRate() const override { return dec_.sampleRate(); }
    int channels() const override { return dec_.channels(); }
    int frameCount() const override { return 0; }
    bool nextFrame() override { return dec_.nextFrame(); }
    const std::uint32_t* pixels() const override { return dec_.pixels().data(); }
    std::vector<std::int16_t> takeAudio() override { return dec_.takeAudio(); }

private:
    formats::ps1::Disc disc_;
    formats::ps1::StrDecoder dec_;
};

}  // namespace

std::unique_ptr<MovieDecoder> openPs1Movie(const std::string& image, const std::string& entry, std::string* error) {
    auto m = std::make_unique<Ps1Movie>();
    if (!m->open(image, entry, error)) return nullptr;
    return m;
}

std::unique_ptr<MovieDecoder> openVqaMovie(const std::string& path, std::string* error) {
    auto m = std::make_unique<VqaMovie>();
    if (!m->open(path, error)) return nullptr;
    return m;
}

MoviePlayer::MoviePlayer(SDL_Renderer* r) : r_(r) {}

MoviePlayer::~MoviePlayer() { stop(); }

void MoviePlayer::stop() {
    if (audio_ && haveAudio_) audio_->stopMovieAudio();
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = nullptr;
    movie_.reset();
    queue_.clear();
    haveAudio_ = false;
}

bool MoviePlayer::start(std::unique_ptr<MovieDecoder> movie, Audio* audio, float volume) {
    stop();
    if (!movie || movie->width() <= 0 || movie->height() <= 0) return false;
    movie_ = std::move(movie);
    audio_ = audio;
    decoded_ = 0;
    shown_ = -1;
    eof_ = skipped_ = false;
    audioBytes_ = 0;
    endNs_ = 0;
    tex_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, movie_->width(),
                             movie_->height());
    haveAudio_ = audio_ && audio_->ok() && movie_->sampleRate() > 0 &&
                 audio_->startMovieAudio(movie_->sampleRate(), movie_->channels(), volume);
    if (movie_->sampleRate() > 0)  // the picture follows the sound's clock when it plays
        SDL_Log("Movie sound: %d Hz, %d ch, %s", movie_->sampleRate(), movie_->channels(), haveAudio_ ? "playing" : "unavailable");
    while (!eof_ && queue_.size() < kAhead) decodeOne();
    if (queue_.empty()) {
        stop();
        return false;
    }
    startNs_ = SDL_GetTicksNS();
    return true;
}

bool MoviePlayer::decodeOne() {
    if (eof_) return false;
    const bool more = movie_->nextFrame();
    if (const std::vector<std::int16_t> a = movie_->takeAudio(); haveAudio_ && !a.empty()) {
        audio_->queueMovieAudio(a.data(), a.size());  // also the sound after the last frame
        audioBytes_ += a.size() * sizeof(std::int16_t);
    }
    if (!more) {
        eof_ = true;
        return false;
    }
    Frame f;
    f.index = decoded_++;
    if (!pool_.empty()) {
        f.px = std::move(pool_.back());
        pool_.pop_back();
    }
    const std::size_t n = std::size_t(movie_->width()) * movie_->height();
    f.px.assign(movie_->pixels(), movie_->pixels() + n);
    queue_.push_back(std::move(f));
    return true;
}

double MoviePlayer::clockSeconds() const {
    if (haveAudio_) {
        const std::uint64_t queued = audio_->movieAudioQueuedBytes();
        const std::uint64_t played = audioBytes_ > queued ? audioBytes_ - queued : 0;
        return double(played) / (2.0 * movie_->channels() * movie_->sampleRate());
    }
    return double(SDL_GetTicksNS() - startNs_) / 1e9;
}

void MoviePlayer::handleEvent(const SDL_Event& e) {
    if (!movie_) return;
    bool skip = false;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        const SDL_Keycode k = e.key.key;
        skip = k == SDLK_ESCAPE || k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_KP_ENTER;
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        skip = true;
    }
    if (skip) {
        skipped_ = true;
        stop();
    }
}

void MoviePlayer::render(bool smooth) {
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    if (!movie_) return;
    while (!eof_ && queue_.size() < kAhead) decodeOne();
    if (haveAudio_) {
        auto queuedSec = [&] {
            return double(audio_->movieAudioQueuedBytes()) / (2.0 * movie_->channels() * movie_->sampleRate());
        };
        while (!eof_ && queuedSec() < kMinAudioAhead && queue_.size() < kMaxAhead)
            if (!decodeOne()) break;
    }
    const double fps = movie_->fps() > 0 ? movie_->fps() : 15.0;
    const int due = int(std::floor(clockSeconds() * fps));
    while (!queue_.empty() && queue_.front().index <= due) {
        Frame f = std::move(queue_.front());
        queue_.pop_front();
        if (queue_.empty() || queue_.front().index > due) {  // the newest frame due: show it
            if (tex_) SDL_UpdateTexture(tex_, nullptr, f.px.data(), movie_->width() * 4);
            shown_ = f.index;
        }
        pool_.push_back(std::move(f.px));
    }
    if (eof_ && queue_.empty()) {
        const Uint64 now = SDL_GetTicksNS();
        if (endNs_ == 0) endNs_ = now + Uint64((haveAudio_ ? 1.0 : 1.0 / fps) * 1e9);
        const bool soundDone = !haveAudio_ || audio_->movieAudioQueuedBytes() == 0;
        if ((soundDone && (haveAudio_ || clockSeconds() * fps >= decoded_)) || now >= endNs_) {
            stop();
            return;
        }
    }
    if (!tex_ || shown_ < 0) return;
    SDL_SetTextureScaleMode(tex_, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    int ow = 0, oh = 0;
    SDL_GetCurrentRenderOutputSize(r_, &ow, &oh);
    const double aspect = movie_->displayAspect();
    double w = ow, h = ow / aspect;
    if (h > oh) {
        h = oh;
        w = oh * aspect;
    }
    if (!smooth) {
        // Original graphics: whole-number scale when it fits (crisp pixels), else fit.
        const double sx = std::floor(w / movie_->width());
        if (sx >= 1 && std::abs(aspect - double(movie_->width()) / movie_->height()) < 1e-6) {
            w = sx * movie_->width();
            h = sx * movie_->height();
        }
    }
    const SDL_FRect dst{float((ow - w) / 2), float((oh - h) / 2), float(w), float(h)};
    SDL_RenderTexture(r_, tex_, nullptr, &dst);
}

}  // namespace d2k::platform
