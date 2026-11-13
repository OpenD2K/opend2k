#pragma once
// The original's sound playback rules (re/notes/audio.md), as pure logic the frontend drives.
//
// The original mixes through Miles (AIL) with six sample channels (SoundClassObject 0x795608):
// channel 0 carries speech (mentat lines, unit responses, UI clicks, money ticks: everything
// QueueAudioToPlay 0x46f820 queues) one sample at a time; channels 1..5 carry the positional
// effects of PlaySoundAt 0x46f540. Volumes are AIL volumes (0..127), pans 0..127 (64 = centre).
// No SDL here: the frontend implements SoundOutput over its audio device.

#include <cstdint>
#include <unordered_map>

namespace d2k::game {

class SoundOutput {
public:
    virtual ~SoundOutput() = default;
    virtual bool busy(int channel) const = 0;    // _AIL_sample_status(...) & 4: still playing
    virtual int sample(int channel) const = 0;   // sample id last started on the channel (+0x20)
    virtual int volume(int channel) const = 0;   // its volume, 0..127
    // Stops whatever plays on `channel` and starts `sample` (FUN_0046f170).
    virtual void start(int channel, int sample, int pan, int volume) = 0;
};

// The battlefield view in map pixels (BattleFieldPosX/Y, BattleFieldWidth/Height).
struct SoundView {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return x <= px && px <= x + w && y <= py && py <= y + h; }
};

class SoundSystem {
public:
    static constexpr int kChannels = 6;
    static constexpr int kQueueSize = 5;  // ring buffers of 5 = 4 waiting samples per priority

    // FUN_0046f210: start `sample` on the first idle channel of [first, last]; when all are
    // busy, replace the quietest one: mode 0 always, 1 never, 2 only if it is quieter than
    // `volume`. Returns the channel or -1.
    static int startSample(SoundOutput& out, int sample, int pan, int volume, int first, int last, int mode);

    // PlaySoundAt 0x46f540: pan and volume of a sound at tile (tileX, tileY) heard from the
    // view's centre: 50 inside the view, 35 fading to 5 over a map width outside it (never
    // silent), panned only when off screen horizontally; scaled by sfxVolume / 100.
    static void positionalMix(int tileX, int tileY, const SoundView& view, int mapWidth, int sfxVolume, int& pan,
                              int& volume);
    // PlaySoundAt: mix + startSample(channels 1..5, mode 2). Negative sample ids are ignored.
    static int playAt(SoundOutput& out, int sample, int tileX, int tileY, const SoundView& view, int mapWidth,
                      int sfxVolume);
    // FUN_004762f0 (UI sounds by name, e.g. the menu "BUTTON"): any channel 0..5, centre,
    // always replacing the quietest when all are busy.
    static int playUi(SoundOutput& out, int sample, int volume) { return startSample(out, sample, 64, volume, 0, 5, 0); }
    // FUN_0046f4f0: is `sample` playing on any channel?
    static bool playing(const SoundOutput& out, int sample);

    // QueueAudioToPlay 0x46f820(sample, always, throttle, priority) at gGameTicks `ticks`.
    //  throttle != 0: dropped when the same sample was requested less than `throttle` ticks ago
    //    (any request that passes, queued or not, restarts the guard).
    //  always == false: only when both queues are empty (then it waits for the current speech);
    //    S_MONEYTICK neither interrupts nor is queued behind another money tick.
    //  always == true: dropped when it is playing right now or already waiting in its queue.
    // Returns whether it was queued.
    bool queue(const SoundOutput& out, int sample, bool always, int throttle, int priority, std::uint32_t ticks);
    // FUN_0046faa0, every frame: when the speech channel is free, the next queued sample
    // (priority 1 first) starts on channel 0 at `volume` (SFXVolume in game, 100 in menus).
    void update(SoundOutput& out, int volume);
    // FUN_0046f980 (mission start / load): empties the queues.
    void reset();

    void setMoneyTick(int sample) { moneyTick_ = sample; }  // GetSoundTableID("S_MONEYTICK")
    bool queuesEmpty() const;                                // FUN_0046f6e0
    int queued(int priority) const;                          // samples waiting in a queue
    bool speaking() const { return active_; }                // DAT_0079562c

private:
    struct Ring {
        int items[kQueueSize] = {};
        unsigned head = 0;  // next write (DAT_00795580[p])
        unsigned tail = 0;  // next read (DAT_007955b0[p])
        bool empty() const { return head == tail; }
    };
    bool push(int priority, int sample);   // FUN_0046f770
    bool contains(int priority, int sample) const;  // FUN_0046f700
    bool pop(int& sample);                  // FUN_0046f7d0

    Ring q_[2];
    bool active_ = false;
    std::unordered_map<int, std::uint32_t> lastPlayed_;  // gSampleTable[id] + 8
    int moneyTick_ = -1;
};

}  // namespace d2k::game
