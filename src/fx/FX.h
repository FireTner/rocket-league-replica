// FX.h — A4 FX: boost pads, boost flame, supersonic streak, black sparkles
// (TUNED owner stand-in), goal shockwave ring, demo/pickup feedback,
// camera-shake overlay. Airborne spin circles are not drawn.
// All procedural (no textures/audio assets). Ownership: A4.
//
// FX advances on SIM time (the coordinator feeds it tick-delta seconds), so
// every timer, pulse and burst is deterministic for offscreen capture.
#pragma once
#include "fx/ContactSparks.h"
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

    // Ball path streak. Own samples (ballTrail_), not trail_ and not the
    // ground marker. Last-touch team colour. No draw before a touch, and
    // none while the ball is under kBallTrailSpeedFloor.
    void DrawBallTrail(Renderer& r, const Snapshot& s);

    // Short bright exhaust behind the car, full 3D (pitch and roll), only
    // while boost is firing. Length follows remaining boost. Not a smoke cloud.
    void DrawBoostFlame(Renderer& r, const Snapshot& s);

    // Powerslide/drift tire smoke. The orange grind sparks in here are contact
    // sparks (workstream G), separate from the supersonic black sparkles.
    // Call after camera Apply. Deterministic (sim time).
    void DrawDrift(Renderer& r, const Snapshot& s);
    // Formerly airborne spin arcs and a stall ring. Now a no-op: a flip is
    // readable from the car's motion, and the circles are not replaced.
    void DrawAir(Renderer& r, const Snapshot& s);

    // Supersonic streak + black sparkles while Snapshot.supersonic is set
    // (RocketSim's flag, not a speed threshold). Also the goal shockwave
    // ring and demo flash, which are not part of that streak.
    void DrawTrail(Renderer& r, const Snapshot& s);

    // Contact sparks (workstream G). Own draw: not the boost flame, not
    // DrawTrail's streak or black sparkles, not the ball path streak.
    void DrawContactSparks(Renderer& r, const ContactSparkFrame& c);

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

    // uu/s. UNVERIFIED — issue I2 stays open. No game function names this
    // floor, and it is not the car's 2200 uu/s supersonic line. Round split
    // of two contacts measured in this sim (tests/test_last_touch.cpp,
    // seed 1, fallback arena): soft 386.6 (throttle 0.25, no boost, y=-220)
    // and hard 2797.6 (throttle 1 + boost, y=-2000). 1500 sits in that gap.
    static constexpr float kBallTrailSpeedFloor = 1500.0f;

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
    // Ball streak samples. Not the car ribbon above.
    static const int kBallTrail = 6;
    float ballTrail_[kBallTrail][3] = {};
    int ballTrailN_ = 0;
    uint32_t ballTrailTick_ = 0;
    bool ballTrailHaveTick_ = false;
    float ballTrailFade_ = 0.0f;
    // Contact-spark burst (workstream G). Sim-time deadline + last hit point.
    float ballSparkUntil_ = 0;
    float ballSparkX_ = 0, ballSparkY_ = 0, ballSparkZ_ = 0;
};

} // namespace rl
