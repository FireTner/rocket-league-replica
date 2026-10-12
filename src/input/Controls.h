// A5-owned contract: car control state shared with A1 (sim) / A4 (render) / A6 (bot/TAS).
// Field names are FROZEN at schema v1: they mirror RocketSim CarControls
// (throttle/steer/pitch/yaw/roll/jump/boost/handbrake) so A1 can memcpy/map
// them onto the sim input struct or shim them 1:1. Do not rename without a
// failing contract test per CONTRACTS.md rule 1.
// MIT License. No third-party code.
#pragma once

namespace rl {

// Analog/digital car controls sampled once per poll. Floats are -1..1
// (0 neutral). Bools are HELD state; edge detection (e.g. jump) is the
// consumer's job (compare with the previous frame's copy).
//
// CANONICAL SIGNS (coordinator ruling): tools/tas/TAPE_FORMAT.md §2 labels
// are authoritative. Tape floats -> Controls -> RocketSim CarControls is a
// VERBATIM passthrough with ZERO negation anywhere in A5. Physical truth is
// now MEASURED headless (build/headless_sim + tools/tas/tapes/air_*):
// steer+1=LEFT, pitch+1=nose UP, yaw+1=left (facing-level), roll+1=roll-LEFT
// (up-vector tips left). No API changes.
struct Controls {
    float throttle = 0.0f;   // +1 forward, -1 reverse
    float steer = 0.0f;      // +1 LEFT, -1 right (MEASURED steer_left_360)
    float pitch = 0.0f;      // +1 nose UP, -1 nose-down (MEASURED air_pitch_240)
    float yaw = 0.0f;        // +1 left, -1 right (MEASURED air_yaw_240)
    float roll = 0.0f;       // +1 roll-LEFT, -1 roll-right
                             //   (MEASURED air_roll_300: facing +y, up tips -x)
    bool jump = false;       // held
    bool boost = false;      // held
    bool handbrake = false;  // held: powerslide on ground, roll-modifier in air
};

// One-shot UI / free-play events derived from key PRESSES (edge-triggered
// inside Input::Poll). Cleared every poll; consume immediately.
struct InputEvents {
    bool toggleBallcam = false;
    bool resetCar = false;
    bool resetBall = false;
    bool toggleUnlimitedBoost = false;
    bool openMenu = false;
    int kickoffIndex = -1;  // -1 = none, else 0..4 (F1..F5 spawns)
};

}  // namespace rl
