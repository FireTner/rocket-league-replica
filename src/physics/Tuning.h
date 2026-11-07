// Gameplay constants transcribed from RocketSim. Units are uu, seconds, and radians.
// Values from RocketSim (MIT © 2022 ZealanL).
#pragma once

namespace Physics::Tuning {

inline constexpr float kGravityZ = -650.f; // RLConst.h:12

inline constexpr float kCarMass = 180.f; // RLConst.h:28-29
inline constexpr float kBallMass = 180.f / 6.f;

inline constexpr float kBallRadiusSoccar = 91.25f; // RLConst.h:43-47
inline constexpr float kBallRadiusHoops = 96.3831f;
inline constexpr float kBallRadiusDropshot = 100.2565f;
inline constexpr float kBallRestZ = 93.15f; // above the radius: arena mesh margin
inline constexpr float kBallMaxAngSpeed = 6.f; // rad/s
inline constexpr float kBallDrag = 0.03f;
inline constexpr float kBallFriction = 0.35f;
inline constexpr float kBallRestitution = 0.6f;
inline constexpr float kBallMaxSpeed = 6000.f; // RLConst.h:117

inline constexpr float kCarMaxSpeed = 2300.f; // RLConst.h:53

inline constexpr float kJumpAccel = 4375.f / 3.f; // RLConst.h:94-99
inline constexpr float kJumpImmediateForce = 875.f / 3.f;
inline constexpr float kJumpMinTime = 0.025f;
inline constexpr float kJumpResetTimePad = (1.f / 40.f);
inline constexpr float kJumpMaxTime = 0.2f;
inline constexpr float kJumpPreMinAccelScale = 0.62f; // Car.cpp:585
inline constexpr float kDoubleJumpMaxDelay = 1.25f;

inline constexpr float kFlipZDamp120 = 0.35f; // RLConst.h:101-115
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

inline constexpr float kBoostMax = 100.f; // RLConst.h:56-64
inline constexpr float kBoostUsedPerSecond = 100.f / 3.f;
inline constexpr float kBoostMinTime = 0.1f;
inline constexpr float kBoostAccelGround = 2975.f / 3.f;
inline constexpr float kBoostAccelAir = 3175.f / 3.f;
inline constexpr float kBoostSpawnAmount = 100.f / 3.f;
inline constexpr float kRechargeBoostPerSecond = 10.f;
inline constexpr float kRechargeBoostDelay = 0.25f;

inline constexpr float kSupersonicStartSpeed = 2200.f; // RLConst.h:68-76
inline constexpr float kSupersonicMaintainMinSpeed = 2200.f - 100.f;
inline constexpr float kSupersonicMaintainMaxTime = 1.f;

inline constexpr float kThrottleTorqueAmount = 180.f * 400.f; // RLConst.h:84-92
inline constexpr float kBrakeTorqueAmount = 180.f * (14.25f + (1.f / 3.f));
inline constexpr float kStoppingForwardVel = 25.f;
inline constexpr float kCoastingBrakeFactor = 0.15f;
inline constexpr float kBrakingNoThrottleSpeedThresh = 0.01f;
inline constexpr float kThrottleDeadzone = 0.001f;
inline constexpr float kThrottleAirAccel = 200.f / 3.f;

inline constexpr float kPowerslideRiseRate = 5.f; // RLConst.h:81-82
inline constexpr float kPowerslideFallRate = 2.f;

// Steering angle from forward speed, radians.
struct CurvePoint { float x, y; };
inline constexpr CurvePoint kSteerAngleFromSpeed[] = { // RLConst.h:418-443
    {0, 0.53356f}, {500, 0.31930f}, {1000, 0.18203f},
    {1500, 0.10570f}, {1750, 0.08507f}, {3000, 0.03454f},
};
inline constexpr CurvePoint kSteerAngleFromSpeedThreeWheel[] = {
    {0, 0.342473f}, {2300, 0.034837f},
};
inline constexpr CurvePoint kPowerslideSteerAngleFromSpeed[] = {
    {0, 0.39235f}, {2500, 0.12610f},
};
inline constexpr CurvePoint kDriveSpeedTorqueFactor[] = { // RLConst.h:447-453
    {0, 1.0f}, {1400, 0.1f}, {1410, 0.0f},
};

inline constexpr CurvePoint kNonStickyFrictionFactor[] = { // RLConst.h:455-494
    {0, 0.1f}, {0.7075f, 0.5f}, {1, 1.0f},
};
inline constexpr CurvePoint kLatFriction[] = { {0, 1.0f}, {1, 0.2f} };
inline constexpr CurvePoint kLatFrictionThreeWheel[] = { {0, 0.30f}, {1, 0.25f} };
inline constexpr CurvePoint kHandbrakeLatFrictionFactor[] = { {0, 0.1f} };
inline constexpr CurvePoint kHandbrakeLongFrictionFactor[] = { {0, 0.5f}, {1, 0.9f} };

inline constexpr float kCarMaxAngSpeed = 5.5f; // rad/s. RLConst.h:66
inline constexpr float kCarAirTorqueX = 130.f; // pitch. RLConst.h:149-151
inline constexpr float kCarAirTorqueY = 95.f;  // yaw
inline constexpr float kCarAirTorqueZ = 400.f; // roll
inline constexpr float kCarAirDampingX = 30.f;
inline constexpr float kCarAirDampingY = 20.f;
inline constexpr float kCarAirDampingZ = 50.f;
inline constexpr float kCarTorqueScale = 2.f * 3.141592653589793f / 65536.f * 1000.f; // RLConst.h:124

inline constexpr float kBallCarExtraImpulseZScale = 0.35f; // RLConst.h:135-139
inline constexpr float kBallCarExtraImpulseZScaleHoopsGround = 0.35f * 1.55f;
inline constexpr float kBallCarExtraImpulseForwardScale = 0.65f;
inline constexpr float kBallCarExtraImpulseMaxDeltaVel = 4600.f;
inline constexpr float kBallCarExtraImpulseHoopsNormalZThresh = 0.1f;
inline constexpr CurvePoint kBallCarExtraImpulseFactor[] = { // RLConst.h:496-503
    {0, 0.65f}, {500.f, 0.65f}, {2300.f, 0.55f}, {4600.f, 0.30f},
};

inline constexpr float kCarCollisionFriction = 0.3f; // RLConst.h:31-41
inline constexpr float kCarCollisionRestitution = 0.1f;
inline constexpr float kCarBallFriction = 2.0f;
inline constexpr float kCarBallRestitution = 0.0f;
inline constexpr float kCarWorldFriction = 0.3f;
inline constexpr float kCarWorldRestitution = 0.3f;
inline constexpr float kCarCarFriction = 0.09f;
inline constexpr float kCarCarRestitution = 0.1f;

inline constexpr float kBumpCooldownTime = 0.25f; // RLConst.h:144-146
inline constexpr float kBumpMinForwardDist = 64.5f;
inline constexpr float kDemoRespawnTime = 3.f;
inline constexpr CurvePoint kBumpVelGround[] = { // RLConst.h:505-527
    {0.f, 5.f / 6.f}, {1400.f, 1100.f}, {2200.f, 1530.f},
};
inline constexpr CurvePoint kBumpVelAir[] = {
    {0.f, 5.f / 6.f}, {1400.f, 1390.f}, {2200.f, 1945.f},
};
inline constexpr CurvePoint kBumpUpwardVel[] = {
    {0.f, 2.f / 6.f}, {1400.f, 278.f}, {2200.f, 417.f},
};

inline constexpr float kPadAmountBig = 100.f; // RLConst.h:263-267
inline constexpr float kPadAmountSmall = 12.f;
inline constexpr float kPadCooldownBig = 10.f;
inline constexpr float kPadCooldownSmall = 4.f;

inline constexpr float kSuspForceScaleFront = 36.f - (1.f / 4.f); // RLConst.h:155-166
inline constexpr float kSuspForceScaleBack = 54.f + (1.f / 4.f) + (1.5f / 100.f);
inline constexpr float kSuspStiffness = 500.f;
inline constexpr float kWheelsDampingCompression = 25.f;
inline constexpr float kWheelsDampingRelaxation = 40.f;
inline constexpr float kMaxSuspensionTravel = 12.f;
inline constexpr float kSuspensionSubtraction = 0.05f;

inline constexpr float kCarSpawnRestZ = 17.f; // RLConst.h:141-142
inline constexpr float kCarRespawnZ = 36.f;

inline constexpr float kAutoFlipImpulse = 200.f; // RLConst.h:126-133
inline constexpr float kAutoFlipTorque = 50.f;
inline constexpr float kAutoFlipTime = 0.4f;
inline constexpr float kAutoFlipNormZThresh = 0.7071067811865475f; // M_SQRT1_2
inline constexpr float kAutoFlipRollThresh = 2.8f;
inline constexpr float kAutoRollForce = 100.f;
inline constexpr float kAutoRollTorque = 80.f;

} // namespace Physics::Tuning
