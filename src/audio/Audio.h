// Audio.h — miniaudio device, procedural bed, optional local wav pack.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace rl {

// UNVERIFIED spatial curves (no published RL source). Ear distances are uu.
// Full volume at or inside 800 uu, inverse distance after that, floor 0.20.
float AudioDistanceGain(float distUu);
// c = 34300 uu/s. radialSpeedUu > 0 means the source is receding from the ear.
// Ratio is clamped to [0.85, 1.15].
float AudioDopplerRatio(float radialSpeedUu);

class Audio {
public:
    Audio();
    ~Audio();

    // 48 kHz device. Returns false if it cannot be opened.
    bool Init();
    void Shutdown();
    bool IsOpen() const { return open_; }

    // manifest.txt from tools/audio/rip_sounds.py. Missing file returns false.
    bool LoadPack(const char* dir);
    bool HasPack() const { return pack_; }

    void SetMuted(bool m);
    bool Muted() const { return muted_.load(); }

    // carDistUu / ballDistUu are source-to-ear distances. radialSpeedUu > 0
    // recedes. Distance and doppler scale the synth and the pack loops.
    void Engine(float speed01, bool boosting, float carDistUu = 800.0f,
                float ballDistUu = 800.0f, float radialSpeedUu = 0.0f);
    // Loop gains, each 0..1. The crowd bed is not here; the pack sets it.
    void Loops(float boost, float tires, float whoosh, float superSonic,
               float ballRoll, float ballFly);

    void PlayJump();
    void PlayDodge();
    void PlayDoubleJump();
    void PlayPickup(bool big);
    void PlayGoal();
    void PlayDemo();
    void PlayBall();
    void PlayLand();
    void PlaySuper();
    void PlaySlide();
    // Powerslide screech follows the held brake and stops when it is released.
    void SetSlide(bool held);

    uint64_t callbacks() const { return callbacks_.load(); }
    uint64_t framesMixed() const { return framesMixed_.load(); }

private:
    struct Impl;
    void EnqueueBlip(int type);
    // True when a pack clip started. False leaves the caller on the synth.
    bool StartCue(int cue);
    void Upload();
    static void SynthProcess(void* node, const float** in, uint32_t* inCount,
                             float** out, uint32_t* outCount);

    enum Cue {
        CueJump = 0, CueDodge, CueDoubleJump, CuePickup, CuePickupBig,
        CueLand, CueBall, CueSlide, CueSuper,
        CueGoalHorn, CueGoalBoom, CueGoalCrowd,
        CueDemoHit, CueDemoBoom,
        CueCount
    };
    enum Loop {
        LoopBoost = 0, LoopTires, LoopWhoosh, LoopSuper,
        LoopBallRoll, LoopBallFly, LoopCrowd, LoopCount
    };
    static constexpr int kCueClips = 8;

    struct CueDef {
        bool layer = false;
        int n = 0;
        int clips[kCueClips] = {};
    };

    std::unique_ptr<Impl> impl_;
    bool open_ = false;
    bool pack_ = false;
    std::atomic<bool> muted_{false};
    std::atomic<float> speed01_{0};
    std::atomic<bool> boosting_{false};
    std::atomic<float> carDist_{800};
    std::atomic<float> ballDist_{800};
    std::atomic<float> radialSpeed_{0};
    CueDef cues_[CueCount] = {};
    int loopClip_[LoopCount] = {};
    std::atomic<float> loopGain_[LoopCount];
    float loopApplied_[LoopCount] = {};
    int shotCursor_ = 0;
    bool slideHeld_ = false;
    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> framesMixed_{0};
};

} // namespace rl
