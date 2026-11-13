#include "game/sound_system.hpp"

#include <cstdlib>

namespace d2k::game {

int SoundSystem::startSample(SoundOutput& out, int sample, int pan, int volume, int first, int last, int mode) {
    if (sample < 0) return -1;
    if (last < first) last = first;
    if (first >= kChannels || last >= kChannels) return -1;
    int quietest = -1, quietVol = 0xff;
    for (int ch = first; ch <= last; ++ch) {
        if (!out.busy(ch)) {
            out.start(ch, sample, pan, volume);
            return ch;
        }
        if (out.volume(ch) < quietVol) {
            quietVol = out.volume(ch);
            quietest = ch;
        }
    }
    if (mode != 1 && quietest >= 0 && (quietVol < volume || mode == 0)) {
        out.start(quietest, sample, pan, volume);
        return quietest;
    }
    return -1;
}

void SoundSystem::positionalMix(int tileX, int tileY, const SoundView& view, int mapWidth, int sfxVolume, int& pan,
                                int& volume) {
    const unsigned mw = static_cast<unsigned>(mapWidth > 0 ? mapWidth : 1);
    const int halfW = (view.w / 2) >> 5, halfH = (view.h / 2) >> 5;
    const unsigned cx = static_cast<unsigned>((view.x >> 5) + halfW), cy = static_cast<unsigned>((view.y >> 5) + halfH);
    const unsigned x = static_cast<unsigned>(tileX) & 0xff, y = static_cast<unsigned>(tileY) & 0xff;
    const unsigned dx = static_cast<unsigned>(std::abs(static_cast<int>(cx - x)));
    const unsigned dy = static_cast<unsigned>(std::abs(static_cast<int>(cy - y)));
    unsigned p = 0x40;
    if (static_cast<int>(dx) > halfW) {
        p = 0x40 - (dx * 0x40) / mw;
        if (p < 0x10) p = 0x10;
        if (cx < x) p = 0x80 - p;
    }
    unsigned v = 0x32;
    const unsigned m = dx > dy ? dx : dy;
    if ((static_cast<int>(dx) > halfW || static_cast<int>(dy) > halfH) && (mw <= m || (v = 0x23 - (m * 0x23) / mw) < 5))
        v = 5;
    pan = static_cast<int>(p);
    volume = static_cast<int>((static_cast<long long>(v) * sfxVolume) / 100);
}

int SoundSystem::playAt(SoundOutput& out, int sample, int tileX, int tileY, const SoundView& view, int mapWidth,
                        int sfxVolume) {
    if (sample < 0) return -1;
    int pan = 0x40, vol = 0;
    positionalMix(tileX, tileY, view, mapWidth, sfxVolume, pan, vol);
    return startSample(out, sample, pan, vol, 1, 5, 2);
}

bool SoundSystem::playing(const SoundOutput& out, int sample) {
    for (int ch = 0; ch < kChannels; ++ch)
        if (out.busy(ch) && out.sample(ch) == sample) return true;
    return false;
}

bool SoundSystem::queuesEmpty() const { return q_[0].empty() && q_[1].empty(); }

int SoundSystem::queued(int priority) const {
    const Ring& r = q_[priority & 1];
    return static_cast<int>((r.head + kQueueSize - r.tail) % kQueueSize);
}

bool SoundSystem::push(int priority, int sample) {
    Ring& r = q_[priority & 1];
    const unsigned next = (r.head + 1) % kQueueSize;
    if (next == r.tail) return false;  // full
    r.items[r.head] = sample;
    r.head = next;
    return true;
}

bool SoundSystem::contains(int priority, int sample) const {
    const Ring& r = q_[priority & 1];
    for (unsigned i = r.tail; i != r.head; i = (i + 1) % kQueueSize)
        if (r.items[i] == sample) return true;
    return false;
}

bool SoundSystem::pop(int& sample) {
    for (int p = 1; p >= 0; --p) {
        Ring& r = q_[p];
        if (r.empty()) continue;
        sample = r.items[r.tail];
        r.tail = (r.tail + 1) % kQueueSize;
        return true;
    }
    return false;
}

bool SoundSystem::queue(const SoundOutput& out, int sample, bool always, int throttle, int priority,
                        std::uint32_t ticks) {
    if (sample < 0) return false;
    if (priority < 0 || priority > 1) priority = 0;  // DebugFatal "Error with SFX priority"
    if (throttle != 0) {
        std::uint32_t& last = lastPlayed_[sample];
        if (last != 0 && ticks < static_cast<std::uint32_t>(throttle) + last) return false;
        last = ticks;
    }
    const bool talking = out.busy(0);
    if (!always) {
        if (!queuesEmpty()) return false;
        if (talking && (sample == moneyTick_ || out.sample(0) == moneyTick_)) return false;
        return push(priority, sample);
    }
    if (talking && out.sample(0) == sample) return false;
    if (contains(priority, sample)) return false;
    return push(priority, sample);
}

void SoundSystem::update(SoundOutput& out, int volume) {
    if (!queuesEmpty() && !active_) {
        int sample = -1;
        pop(sample);
        active_ = true;
        startSample(out, sample, 0x40, volume, 0, 0, 0);
    }
    if (active_ && !out.busy(0)) active_ = false;
}

void SoundSystem::reset() {
    q_[0] = Ring{};
    q_[1] = Ring{};
    active_ = false;
}

}  // namespace d2k::game
