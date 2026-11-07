// Car controls sampled once per poll. Floats are -1..1; bools are held.
#pragma once

namespace rl {

// Steer +1 is left, pitch +1 is nose up, yaw +1 is left, roll +1 rolls left.
struct Controls {
    float throttle = 0.0f;   // +1 forward
    float steer = 0.0f;      // +1 left
    float pitch = 0.0f;      // +1 nose up
    float yaw = 0.0f;        // +1 left
    float roll = 0.0f;       // +1 rolls left
    bool jump = false;
    bool boost = false;
    bool handbrake = false;  // powerslide on the ground; turns steer into roll in the air
};

// Edge-triggered UI events. Cleared every poll.
struct InputEvents {
    bool toggleBallcam = false;
    bool resetCar = false;
    bool resetBall = false;
    bool toggleUnlimitedBoost = false;
    bool openMenu = false;
    int kickoffIndex = -1;  // -1 = none, else 0..4
};

}  // namespace rl
