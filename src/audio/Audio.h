// Audio.h — procedural game audio, zero assets (MIT).
// SDL2 audio callback synth: engine hum (pitch follows car speed), boost
// noise, and one-shot blips (jump / pad pickup / goal horn). No samples, no
// files, no dependencies beyond SDL2. Game-owned; the sim never sees it.
#pragma once

#include <SDL2/SDL.h>

#include <atomic>
#include <cstdint>

namespace rl {

class Audio {
public:
    Audio();
    ~Audio();

    // Open the device (44.1 kHz f32 mono). Returns false if unavailable —
    // the game runs silent (never fatal). forceDummy env override is the
    // caller's business (SDL_AUDIODRIVER).
    bool Init();
    void Shutdown();
    bool IsOpen() const { return device_ != 0; }

    void SetMuted(bool m) { muted_.store(m); }
    bool Muted() const { return muted_.load(); }

    // Per-frame continuous state (call from the render loop).
    void Engine(float speed01, bool boosting);

    // One-shots (call on game events).
    void PlayJump();
    void PlayPickup(bool big);
    void PlayGoal();
    void PlayDemo(); // demolition: low descending boom

    // Verification: callback invocations + mixed frames since Init.
    uint64_t callbacks() const { return callbacks_.load(); }
    uint64_t framesMixed() const { return framesMixed_.load(); }

private:
    static void SDLCALL Cb(void* userdata, Uint8* stream, int len);

    struct Blip {
        int type = 0; // 0 none, 1 jump, 2 pickup-small, 3 pickup-big, 4 goal, 5 demo
        uint32_t pos = 0;
    };
    static constexpr int kBlips = 8;

    SDL_AudioDeviceID device_ = 0;
    std::atomic<bool> muted_{false};
    std::atomic<float> speed01_{0};
    std::atomic<bool> boosting_{false};
    Blip blips_[kBlips] = {};
    SDL_mutex* blipLock_ = nullptr;
    uint32_t phase_ = 0;   // engine oscillator phase (callback-owned)
    uint32_t noise_ = 0x12345678; // xorshift (callback-owned)
    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> framesMixed_{0};
};

} // namespace rl
