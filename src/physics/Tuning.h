// Tuning.h — A3 CAR & BALL PHYSICS
// Every gameplay constant lives here, transcribed from RocketSim authority.
// MIT only. RocketSim (c) 2022 ZealanL — credited.
//
// CITATION FORMAT: each group cites "RLConst.h:<line>" (or Car.cpp / MutatorConfig)
// against third_party/RocketSim @ rev c2baacb (file: src/RLConst.h, 530 lines).
// If RocketSim updates, re-run src/physics/verify_vs_rocketsim.py — it re-checks
// every value below against the authority files.
//
// CONVENTIONS:
//  - Units are Unreal Units (uu), seconds, radians unless noted.
//  - Divisions are written out (e.g. 2975.f/3.f) exactly as in RLConst.h so the
//    transcription can be diffed by eye.
//  - Curve tables mirror RocketSim LinearPieceCurve point lists point-for-point.
#pragma once

namespace Physics::Tuning {

// --- World / gravity -------------------------------------------------------
// RLConst.h:12
inline constexpr float kGravityZ = -650.f;

// --- Masses ----------------------------------------------------------------
// RLConst.h:28-29 (BALL_MASS_BT = CAR_MASS_BT / 6; reddit ref in source comment)
inline constexpr float kCarMass = 180.f;
inline constexpr float kBallMass = 180.f / 6.f; // = 30

// --- Ball (soccar; the free-play default) -----------------------------------
// RLConst.h:43-47,117. Hoops/Dropshot radii listed for completeness (A1 may
// need them for other modes; free-play uses SOCCAR).
inline constexpr float kBallRadiusSoccar = 91.25f;
inline constexpr float kBallRadiusHoops = 96.3831f;
inline constexpr float kBallRadiusDropshot = 100.2565f;
inline constexpr float kBallRestZ = 93.15f; // > radius: arena mesh margin
inline constexpr float kBallMaxAngSpeed = 6.f; // rad/s
inline constexpr float kBallDrag = 0.03f;
inline constexpr float kBallFriction = 0.35f;
inline constexpr float kBallRestitution = 0.6f;
inline constexpr float kBallMaxSpeed = 6000.f;

// --- Speed caps ------------------------------------------------------------
// RLConst.h:53
inline constexpr float kCarMaxSpeed = 2300.f; // boost cap (hard clamp, Car.cpp:195)

// --- Jump ------------------------------------------------------------------
// RLConst.h:94-99. Mechanics: immediate impulse 875/3 ~= 291.67 plus a held
// accel 4375/3 ~= 1458.33 while held (max 0.2 s, min 0.025 s); pre-min-time
// accel is scaled by 0.62 (Car.cpp:585). Second jump/dodge window 1.25 s.
inline constexpr float kJumpAccel = 4375.f / 3.f;
inline constexpr float kJumpImmediateForce = 875.f / 3.f; // ~= 291.67 "impulse 292"
inline constexpr float kJumpMinTime = 0.025f;
inline constexpr float kJumpResetTimePad = (1.f / 40.f);
inline constexpr float kJumpMaxTime = 0.2f; // hold bonus window
inline constexpr float kJumpPreMinAccelScale = 0.62f; // Car.cpp:585 (not in RLConst)
inline constexpr float kDoubleJumpMaxDelay = 1.25f;

// --- Dodge / flip ----------------------------------------------------------
// RLConst.h:101-115. Source comment cites RLUtilities dodge.cc for most.
// Torque applied 0.65 s; initial impulse 500 uu/s scaled by speed ratio.
inline constexpr float kFlipZDamp120 = 0.35f;
inline constexpr float kFlipZDampStart = 0.15f;
inline constexpr float kFlipZDampEnd = 0.21f;
inline constexpr float kFlipTorqueTime = 0.65f;
inline constexpr float kFlipTorqueMinTime = 0.41f;
inline constexpr float kFlipPitchLockTime = 1.f;
inline constexpr float kFlipPitchLockExtraTime = 0.3f;
inline constexpr float kFlipInitialVelScale = 500.f;
inline constexpr float kFlipTorqueX = 260.f; // left/right
inline constexpr float kFlipTorqueY = 224.f; // forward/backward
inline constexpr float kFlipFwdImpulseMaxSpeedScale = 1.f;
inline constexpr float kFlipSideImpulseMaxSpeedScale = 1.9f;
inline constexpr float kFlipBackImpulseMaxSpeedScale = 2.5f;
inline constexpr float kFlipBackImpulseScaleX = 16.f / 15.f;

// --- Boost -----------------------------------------------------------------
// RLConst.h:56-64. 991.667 ground / 1058.333 air uu/s accel @ 33.33/s drain;
// min burn 0.1 s; recharge 10/s after 0.25 s (charge path also gated by
// MutatorConfig::rechargeBoostEnabled; free-play default ON per A1).
inline constexpr float kBoostMax = 100.f;
inline constexpr float kBoostUsedPerSecond = 100.f / 3.f; // ~= 33.33
inline constexpr float kBoostMinTime = 0.1f;
inline constexpr float kBoostAccelGround = 2975.f / 3.f; // ~= 991.667
inline constexpr float kBoostAccelAir = 3175.f / 3.f;    // ~= 1058.333
inline constexpr float kBoostSpawnAmount = 100.f / 3.f;
inline constexpr float kRechargeBoostPerSecond = 10.f;
inline constexpr float kRechargeBoostDelay = 0.25f;

// --- Supersonic ------------------------------------------------------------
// RLConst.h:68-76. Enter at >= 2200; keep while >= 2100 for up to 1 s.
inline constexpr float kSupersonicStartSpeed = 2200.f;
inline constexpr float kSupersonicMaintainMinSpeed = 2200.f - 100.f; // 2100
inline constexpr float kSupersonicMaintainMaxTime = 1.f;

// --- Throttle / brake / steering -------------------------------------------
// RLConst.h:84-92. Top throttle-only speed (~1410) falls OUT of the torque
// factor curve (RLConst.h:447-453): factor hits 0 at 1410.
inline constexpr float kThrottleTorqueAmount = 180.f * 400.f;
inline constexpr float kBrakeTorqueAmount = 180.f * (14.25f + (1.f / 3.f));
inline constexpr float kStoppingForwardVel = 25.f;
inline constexpr float kCoastingBrakeFactor = 0.15f;
inline constexpr float kBrakingNoThrottleSpeedThresh = 0.01f;
inline constexpr float kThrottleDeadzone = 0.001f;
inline constexpr float kThrottleAirAccel = 200.f / 3.f;

// Powerslide ramp rates (per second). RLConst.h:81-82.
inline constexpr float kPowerslideRiseRate = 5.f;
inline constexpr float kPowerslideFallRate = 2.f;

// Steering angle from forward speed, radians. RLConst.h:418-443.
struct CurvePoint { float x, y; };
inline constexpr CurvePoint kSteerAngleFromSpeed[] = {
    {0, 0.53356f}, {500, 0.31930f}, {1000, 0.18203f},
    {1500, 0.10570f}, {1750, 0.08507f}, {3000, 0.03454f},
};
inline constexpr CurvePoint kSteerAngleFromSpeedThreeWheel[] = {
    {0, 0.342473f}, {2300, 0.034837f},
};
inline constexpr CurvePoint kPowerslideSteerAngleFromSpeed[] = {
    {0, 0.39235f}, {2500, 0.12610f},
};
// Drive torque scale — the 1410 gate. RLConst.h:447-453.
inline constexpr CurvePoint kDriveSpeedTorqueFactor[] = {
    {0, 1.0f}, {1400, 0.1f}, {1410, 0.0f},
};

// --- Tire friction ---------------------------------------------------------
// RLConst.h:455-494. LONG_FRICTION_CURVE is EMPTY in authority (default 1
// when not powersliding; see Car.cpp:465).
inline constexpr CurvePoint kNonStickyFrictionFactor[] = {
    {0, 0.1f}, {0.7075f, 0.5f}, {1, 1.0f},
};
inline constexpr CurvePoint kLatFriction[] = { {0, 1.0f}, {1, 0.2f} };
inline constexpr CurvePoint kLatFrictionThreeWheel[] = { {0, 0.30f}, {1, 0.25f} };
inline constexpr CurvePoint kHandbrakeLatFrictionFactor[] = { {0, 0.1f} };
inline constexpr CurvePoint kHandbrakeLongFrictionFactor[] = { {0, 0.5f}, {1, 0.9f} };

// --- Air control -----------------------------------------------------------
// RLConst.h:66,149-151. Torque PYR (130,95,400), damping (30,20,50).
inline constexpr float kCarMaxAngSpeed = 5.5f; // rad/s
inline constexpr float kCarAirTorqueX = 130.f; // pitch
inline constexpr float kCarAirTorqueY = 95.f;  // yaw
inline constexpr float kCarAirTorqueZ = 400.f; // roll
inline constexpr float kCarAirDampingX = 30.f;
inline constexpr float kCarAirDampingY = 20.f;
inline constexpr float kCarAirDampingZ = 50.f;
// RLConst.h:124. Scales analog stick input into bullet-torque units.
inline constexpr float kCarTorqueScale = 2.f * 3.141592653589793f / 65536.f * 1000.f;

// --- Car-ball contact: Psyonix extra impulse -------------------------------
// RLConst.h:135-139 + curve RLConst.h:496-503. Extra "pop" on top of the
// bullet contact, capped by max-delta-vel.
inline constexpr float kBallCarExtraImpulseZScale = 0.35f;
inline constexpr float kBallCarExtraImpulseZScaleHoopsGround = 0.35f * 1.55f;
inline constexpr float kBallCarExtraImpulseForwardScale = 0.65f;
inline constexpr float kBallCarExtraImpulseMaxDeltaVel = 4600.f;
inline constexpr float kBallCarExtraImpulseHoopsNormalZThresh = 0.1f;
inline constexpr CurvePoint kBallCarExtraImpulseFactor[] = {
    {0, 0.65f}, {500.f, 0.65f}, {2300.f, 0.55f}, {4600.f, 0.30f},
};

// Contact material pairs. RLConst.h:31-41.
inline constexpr float kCarCollisionFriction = 0.3f;
inline constexpr float kCarCollisionRestitution = 0.1f;
inline constexpr float kCarBallFriction = 2.0f;
inline constexpr float kCarBallRestitution = 0.0f;
inline constexpr float kCarWorldFriction = 0.3f;
inline constexpr float kCarWorldRestitution = 0.3f;
inline constexpr float kCarCarFriction = 0.09f;
inline constexpr float kCarCarRestitution = 0.1f;

// --- Bump / demo -----------------------------------------------------------
// RLConst.h:144-146 + curves RLConst.h:505-527.
inline constexpr float kBumpCooldownTime = 0.25f;
inline constexpr float kBumpMinForwardDist = 64.5f;
inline constexpr float kDemoRespawnTime = 3.f;
inline constexpr CurvePoint kBumpVelGround[] = {
    {0.f, 5.f / 6.f}, {1400.f, 1100.f}, {2200.f, 1530.f},
};
inline constexpr CurvePoint kBumpVelAir[] = {
    {0.f, 5.f / 6.f}, {1400.f, 1390.f}, {2200.f, 1945.f},
};
inline constexpr CurvePoint kBumpUpwardVel[] = {
    {0.f, 2.f / 6.f}, {1400.f, 278.f}, {2200.f, 417.f},
};

// --- Boost pads ------------------------------------------------------------
// RLConst.h:263-267 (BoostPads namespace). Big = 100 boost / 10 s;
// small = 12 boost / 4 s. Pickup volumes (RLConst.h:254-261, citing
// Rocket Science video): big cyl r208 / box r160, small cyl r144 / box r120,
// heights 95 (cyl) / 64 (box) — consumed by A4 arena, listed here for the gate.
inline constexpr float kPadAmountBig = 100.f;
inline constexpr float kPadAmountSmall = 12.f;
inline constexpr float kPadCooldownBig = 10.f;
inline constexpr float kPadCooldownSmall = 4.f;

// --- Bullet vehicle (suspension) -------------------------------------------
// RLConst.h:155-166. RocketSim comment (l.156) notes these MIGHT vary per car
// but are currently shared; per-preset rest lengths live in CarPresets.h.
inline constexpr float kSuspForceScaleFront = 36.f - (1.f / 4.f);
inline constexpr float kSuspForceScaleBack = 54.f + (1.f / 4.f) + (1.5f / 100.f);
inline constexpr float kSuspStiffness = 500.f;
inline constexpr float kWheelsDampingCompression = 25.f;
inline constexpr float kWheelsDampingRelaxation = 40.f;
inline constexpr float kMaxSuspensionTravel = 12.f;
inline constexpr float kSuspensionSubtraction = 0.05f;

// --- Spawn / respawn -------------------------------------------------------
// RLConst.h:141-142.
inline constexpr float kCarSpawnRestZ = 17.f;
inline constexpr float kCarRespawnZ = 36.f;

// --- Auto-flip / auto-roll (wall/ground recovery) --------------------------
// RLConst.h:126-133.
inline constexpr float kAutoFlipImpulse = 200.f;
inline constexpr float kAutoFlipTorque = 50.f;
inline constexpr float kAutoFlipTime = 0.4f;
inline constexpr float kAutoFlipNormZThresh = 0.7071067811865475f; // M_SQRT1_2
inline constexpr float kAutoFlipRollThresh = 2.8f;
inline constexpr float kAutoRollForce = 100.f;
inline constexpr float kAutoRollTorque = 80.f;

} // namespace Physics::Tuning
