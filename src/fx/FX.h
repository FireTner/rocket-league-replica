// FX.h — A4 FX: boost pads, boost flame, trails, supersonic wheel embers,
// goal shockwave ring, demo/pickup feedback, camera-shake overlay.
// All procedural (no textures/audio assets). Ownership: A4.
//
// FX advances on SIM time (the coordinator feeds it tick-delta seconds), so
// every timer, pulse and burst is deterministic for offscreen capture.
#pragma once
#include "render/Renderer.h"

namespace rl {

// Boost pad: big (100) vs small (12) — distinct size+color pulse.
struct Pad {
    float x = 0, y = 0;
    bool big = false;
    bool active = true;
    float cooldown = 0; // s remaining; big=10s, small=4s respawn spin-up
};

class FX {
public:
    FX();

    // Advance timers (respawn spin-up, bursts, slow-mo beat). dt is SIM
    // seconds (tick delta / 120), not wall time — see the class comment.
    void Update(float dt, const Snapshot& s);

    // Draw all pads (glow color + spin-up scale). Call after camera Apply.
    void DrawPads(Renderer& r, const Pad* pads, int n);

    // Ball ground indicator: RL-style highlight under the ball with BOTH
    // circles — outer glow ring (~1.25r..0.98r) + inner filled disc (0.62r),
    // light cyan-white, fading/growing as the ball rises. Call after camera
    // Apply with the current ball snapshot.
    void DrawBallIndicator(Renderer& r, const Snapshot& s);

    // Boost flame behind car, scaled by boost amount + boosting flag.
    void DrawBoostFlame(Renderer& r, const Snapshot& s);

    // Powerslide/drift tire smoke + stall/flip-cancel air streaks. Call after
    // camera Apply with the current snapshot. Deterministic (sim time).
    void DrawDrift(Renderer& r, const Snapshot& s);
    void DrawAir(Renderer& r, const Snapshot& s);

    // Supersonic feedback: fading trail ribbon + wheel embers when
    // carSpeed >= threshold (default 2200 uu/s). Also drives the goal
    // shockwave ring and demo flash.
    void DrawTrail(Renderer& r, const Snapshot& s);

    // Goal explosion + slow-mo beat. Trigger via Snapshot.goalScored.
    // Returns slow-mo scale (1.0 normal, ~0.25 during beat) for sim hookup.
    float GoalBeat() const { return slowmo_; }

    // Goal shockwave ring geometry (W5). Radius grows from the goal-mouth
    // scale outward over the 2 s explosion; alpha fades to 0. Exposed so the
    // VG8 gate can assert the growth curve from captured frames.
    float ShockwaveRadius() const;
    float ShockwaveAlpha() const;
    bool ShockwaveActive() const { return explode_ > 0; }

    // Camera-shake overlay (W5). Presentation-only: the caller adds the
    // returned world x/y offset to the eye. Zero when no event is active,
    // and the coordinator disables it for freecam/capture.
    void ShakeOffset(float out[2]) const;

    // Events (also auto-armed from Snapshot transitions in Update):
    void OnPickup(bool big);   // pickup burst flash
    void OnGoal();              // explosion + shockwave + slow-mo + shake
    void OnDemo();              // demo respawn flash + shake

    void SetSupersonicThreshold(float v) { supersonic_ = v; }

private:
    float time_ = 0;
    float burst_ = 0;      // pickup burst timer
    bool burstBig_ = false;
    float explode_ = 0;    // goal explosion timer (2 s)
    float slowmo_ = 1.0f;
    float demoFlash_ = 0;
    float shake_ = 0;      // camera-shake amplitude (decays)
    bool prevGoal_ = false;
    bool prevDemo_ = false;
    float supersonic_ = 2200.0f;
    // Trail ribbon: last N car positions (uu).
    static const int kTrail = 24;
    float trail_[kTrail][3];
    int trailN_ = 0;
};

} // namespace rl
