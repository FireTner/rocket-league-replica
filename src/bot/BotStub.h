// src/bot/BotStub.h — MIT License
// A6 VS-bot STUB scaffolding ONLY. No gameplay AI: three deterministic modes
// for soak/feel testing. Real bot AI is explicitly out of scope this round.
#pragma once

#include <cmath>

namespace rlbot {

// Minimal car-control output. A5's real Controls struct supersedes this when
// it lands; field names mirror tools/tas/TAPE_FORMAT.md columns.
struct BotInput {
    float throttle = 0.0f;   // [-1, 1]
    float steer = 0.0f;      // [-1, 1]
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    bool jump = false;
    bool boost = false;
    bool handbrake = false;
};

// Minimal read-only snapshot the stub needs. A1's SimAPI replaces this.
struct BotView {
    float carX = 0, carY = 0, carZ = 0;
    float carYaw = 0;
    float ballX = 0, ballY = 0, ballZ = 0;
    float dt = 1.0f / 120.0f;
};

enum class StubMode {
    Stationary,  // all zeros — parked car, baseline/idle load
    Mirror,      // deterministic mirror of player input (soak load). NOT AI.
    SlowChase,   // drives toward ball at capped 0.3 throttle — moving target only
};

class BotStub {
public:
    explicit BotStub(StubMode mode = StubMode::Stationary) : mode_(mode) {}

    void setMode(StubMode m) { mode_ = m; }
    StubMode mode() const { return mode_; }

    // Mirror mode needs the player's current input; ignored by other modes.
    void setPlayerInput(const BotInput& in) { player_ = in; }

    BotInput tick(const BotView& view) {
        switch (mode_) {
            case StubMode::Stationary: return BotInput{};  // parked
            case StubMode::Mirror: return mirrorTick();
            case StubMode::SlowChase: return chaseTick(view);
        }
        return BotInput{};
    }

    static const char* modeName(StubMode m) {
        switch (m) {
            case StubMode::Stationary: return "stationary";
            case StubMode::Mirror: return "mirror";
            case StubMode::SlowChase: return "slow-chase";
        }
        return "unknown";
    }

private:
    // Deterministic echo with fixed 6-tick delay; no decisions, no ball logic.
    BotInput mirrorTick() {
        history_[head_] = player_;
        head_ = (head_ + 1) % kDelay;
        return history_[(head_ + 1) % kDelay];
    }

    // P-controller on yaw toward the ball, throttle capped at 0.3. Deliberately
    // too dumb to play: no jump, no boost, no prediction, no defense.
    BotInput chaseTick(const BotView& view) {
        BotInput out;
        float dx = view.ballX - view.carX;
        float dy = view.ballY - view.carY;
        // crude heading error in [-pi, pi]
        float want = atan2f(dy, dx);
        float err = want - view.carYaw;
        while (err > 3.14159265f) err -= 2 * 3.14159265f;
        while (err < -3.14159265f) err += 2 * 3.14159265f;
        // Proportional steer (was bang-bang ±1: circled the ball forever).
        // steer +1 = LEFT matches the canonical sign (Controls.h).
        out.steer = err > 1.0f ? 1.0f : (err < -1.0f ? -1.0f : err);
        if (err > 1.2f || err < -1.2f) {
            out.throttle = 0.0f;  // facing away: stop instead of circling
        } else {
            out.throttle = 0.3f;  // capped: never competitive
        }
        return out;
    }

    static constexpr int kDelay = 6;
    StubMode mode_;
    BotInput player_{};
    BotInput history_[kDelay]{};
    int head_ = 0;
};

}  // namespace rlbot
