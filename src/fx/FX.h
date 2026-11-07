// FX.h — pads, boost flame, trails, shockwave, and shake. Timers use sim seconds.
#pragma once
#include "fx/ContactSparks.h"
#include "fx/WheelEmbers.h"
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

    // dt is sim seconds (tick delta / 120), not wall time.
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

    // Powerslide tire smoke. Orange grind sparks here are separate from the
    // supersonic black sparkles.
    void DrawDrift(Renderer& r, const Snapshot& s);
    // Flip-reset disc (src/fx/FlipReset) while Snapshot.flipReset is set.
    // The old airborne spin arcs stay removed.
    void DrawAir(Renderer& r, const Snapshot& s);

    // Supersonic streak + black sparkles while Snapshot.supersonic is set
    // (RocketSim's flag, not a speed threshold). Also the goal shockwave
    // ring and demo flash, which are not part of that streak.
    void DrawTrail(Renderer& r, const Snapshot& s);

    // Supersonic wheel embers. Own particles at the four tyre contacts.
    // Not the position streak above and not the boost flame.
    void DrawWheelEmbers(Renderer& r, const Snapshot& s);

    // Contact sparks (workstream G). Own draw: not the boost flame, not
    // DrawTrail's streak or black sparkles, not the ball path streak.
    void DrawContactSparks(Renderer& r, const ContactSparkFrame& c);

    // Goal explosion + slow-mo beat. Trigger via Snapshot.goalScored.
    // Returns slow-mo scale (1.0 normal, ~0.25 during beat) for sim hookup.
    float GoalBeat() const { return slowmo_; }

    // Shockwave radius grows over the 2 s explosion; alpha fades to 0.
    float ShockwaveRadius() const;
    float ShockwaveAlpha() const;
    bool ShockwaveActive() const { return explode_ > 0; }

    // World x/y offset added to the eye. Zero when no event is active.
    void ShakeOffset(float out[2]) const;

    // Events (also auto-armed from Snapshot transitions in Update):
    void OnPickup(bool big);   // pickup burst flash
    void OnGoal();              // explosion + shockwave + slow-mo + shake
    void OnDemo();              // demo respawn flash + shake

    void SetSupersonicThreshold(float v) { supersonic_ = v; }

    // uu/s. Not the car's 2200 uu/s supersonic line.
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
    // Contact-spark burst. Sim-time deadline and last hit point.
    float ballSparkUntil_ = 0;
    float ballSparkX_ = 0, ballSparkY_ = 0, ballSparkZ_ = 0;
    WheelEmbers wheelEmbers_;
};

} // namespace rl
