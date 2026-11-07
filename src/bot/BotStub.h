// Deterministic opponent: parked, a delayed copy of the player, or a slow chase.
#pragma once

#include <cmath>

namespace rlbot {

struct BotInput {
    float throttle = 0.0f;   // [-1, 1]
    float steer = 0.0f;      // [-1, 1], +1 left
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    bool jump = false;
    bool boost = false;
    bool handbrake = false;
};

struct BotView {
    float carX = 0, carY = 0, carZ = 0;
    float carYaw = 0;
    float ballX = 0, ballY = 0, ballZ = 0;
    float dt = 1.0f / 120.0f;
};

enum class StubMode {
    Stationary,
    Mirror,
    SlowChase,
};

class BotStub {
public:
    explicit BotStub(StubMode mode = StubMode::Stationary) : mode_(mode) {}

    void setMode(StubMode m) { mode_ = m; }
    StubMode mode() const { return mode_; }

    // Used by Mirror. Ignored by the other modes.
    void setPlayerInput(const BotInput& in) { player_ = in; }

    BotInput tick(const BotView& view) {
        switch (mode_) {
            case StubMode::Stationary: return BotInput{};
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
    // Echoes the player input after a fixed 6-tick delay.
    BotInput mirrorTick() {
        history_[head_] = player_;
        head_ = (head_ + 1) % kDelay;
        return history_[(head_ + 1) % kDelay];
    }

    // Steers toward the ball. Throttle stays at 0.3, and drops to 0 when facing away.
    BotInput chaseTick(const BotView& view) {
        BotInput out;
        float dx = view.ballX - view.carX;
        float dy = view.ballY - view.carY;
        float want = atan2f(dy, dx);
        float err = want - view.carYaw;
        while (err > 3.14159265f) err -= 2 * 3.14159265f;
        while (err < -3.14159265f) err += 2 * 3.14159265f;
        // Steer +1 is left. Full lock past 1 radian; proportional inside that.
        out.steer = err > 1.0f ? 1.0f : (err < -1.0f ? -1.0f : err);
        if (err > 1.2f || err < -1.2f) {
            out.throttle = 0.0f;
        } else {
            out.throttle = 0.3f;
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
