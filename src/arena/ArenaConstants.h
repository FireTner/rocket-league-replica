// ArenaConstants.h — Soccar arena analytic constants (stream A2).
//
// Coordinate convention (matches RocketSim / RLGym / RLBot wiki):
//   X = width  (side walls at x = +/-SIDE_WALL_X)
//   Y = length (back walls / goal lines at y = +/-BACK_WALL_Y)
//   Z = up     (floor z = 0, ceiling z = CEILING_Z)
// All lengths in Unreal units (uu); 1 uu = 1 cm in-game.
// (rl-opus uses x/z-planar notation; its "z" is our "y".)
//
// Every reused constant cites source file + line. Single-source constants are
// flagged UNVERIFIED — see docs/arena_deviation.md. MIT/Apache-2.0/CC0 only;
// numeric facts are cited, no geometry is copied (RocketSim itself collides
// against external triangle-mesh dumps, not these numbers).
#pragma once

#include <array>
#include <cstddef>

namespace Arena {

// Minimal 3D vector so the arena stays dependency-free
// (sim/render own their own math types; convert at the boundary).
struct Vec3 {
    float x, y, z;
    constexpr Vec3() : x(0), y(0), z(0) {}
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
};

// ---------------------------------------------------------------------------
// Field dimensions (uu)
// ---------------------------------------------------------------------------

// Distance from field center to each side wall.
// [1] third_party/RocketSim/src/RLConst.h:14 (ARENA_EXTENT_X = 4096)
// [2] RLGym rlgym/rocket_league/common_values.py:8 (SIDE_WALL_X = 4096)
// [3] RLBot wiki Useful-Game-Values "Side wall: x=+/-4096"
// [4] rl-opus src/config.ts:28 (halfWidth 4096)
inline constexpr float SIDE_WALL_X = 4096.0f;

// Distance from field center to each back wall / goal line (excludes net).
// [1] third_party/RocketSim/src/RLConst.h:15 (ARENA_EXTENT_Y = 5120)
// [2] RLGym common_values.py:9 (BACK_WALL_Y = 5120)
// [3] RLBot wiki Useful-Game-Values "Back wall: y=+/-5120"
// [4] rl-opus src/config.ts:29 (halfLength 5120)
inline constexpr float BACK_WALL_Y = 5120.0f;

// Arena height, floor to ceiling plane.
// [1] third_party/RocketSim/src/RLConst.h:16 (ARENA_HEIGHT = 2048) — also used
//     as an analytic ceiling collision plane in
//     third_party/RocketSim/src/Sim/Arena/Arena.cpp:1036+ (ceiling fnAddPlane).
// CONFLICT: RLBot wiki ("Ceiling: z=2044"), RLGym common_values.py:10
// (CEILING_Z = 2044) and rl-opus src/config.ts:30 (ceiling 2044) say 2044.
// Adopted 2048 per mission target + RocketSim analytic plane; the 4 uu
// mismatch is declared in docs/arena_deviation.md and must be measured
// in-harness before acceptance is final.
inline constexpr float CEILING_Z = 2048.0f;

// Distance from field center to the back of the net.
// [1] RLGym common_values.py:11 (BACK_NET_Y = 6000)
// [2] RLBot wiki Useful-Game-Values (goal depth 880 => 5120 + 880 = 6000)
// [3] third_party/RocketSim broadphase AABB maxPos.y = 6000
//     (src/Sim/Arena/ArenaConfig/ArenaConfig.h:23-24)
inline constexpr float BACK_NET_Y = 6000.0f;

// Goal depth (net interior, goal line to back of net) = BACK_NET_Y - BACK_WALL_Y.
// [1] RLBot wiki Useful-Game-Values "Goal depth: 880"
// [2] rl-opus src/config.ts:40 (goal depth 880)
// [3] RLGym derived: BACK_NET_Y(6000) - BACK_WALL_Y(5120) = 880
inline constexpr float GOAL_DEPTH = 880.0f;

// ---------------------------------------------------------------------------
// Goal mouth dimensions (uu)
// ---------------------------------------------------------------------------

// Height of the goal opening.
// [1] third_party/RocketSim/src/Sim/Arena/Arena.cpp:793 (APPROX_GOAL_HEIGHT,
//     citing the RLBot wiki Useful-Game-Values page)
// [2] RLGym common_values.py:17 (GOAL_HEIGHT = 642.775)
// [3] RLBot wiki Useful-Game-Values "Goal height: z=642.775"
// [4] rl-opus src/config.ts:39 (goal height 642.775)
inline constexpr float GOAL_HEIGHT = 642.775f;

// Half-width of the goal opening (center to post).
// [1] third_party/RocketSim/src/Sim/Arena/Arena.cpp:792 (APPROX_GOAL_HALF_WIDTH,
//     citing the RLBot wiki Useful-Game-Values page)
// [2] RLGym common_values.py:18 (GOAL_CENTER_TO_POST = 892.755)
// [3] RLBot wiki Useful-Game-Values "Goal center-to-post: 892.755"
// [4] rl-opus src/config.ts:38 (goal halfWidth 892.755)
inline constexpr float GOAL_HALF_WIDTH = 892.755f;

// Goal-line threshold base: a ball scores when |ballY| exceeds this PLUS the
// ball radius (RocketSim rule). 5124.25 + 91.25 = 5215.5 exactly matches
// RLGym's GOAL_THRESHOLD, so both sources agree.
// [1] third_party/RocketSim/src/RLConst.h:121
//     (SOCCAR_GOAL_SCORE_BASE_THRESHOLD_Y = 5124.25f)
// [2] third_party/RocketSim/src/Sim/Arena/Arena.cpp:900
//     (IsBallScored: |y| > goalBaseThresholdY + ballRadius)
// [3] RLGym common_values.py:52 (GOAL_THRESHOLD = 5215.5, consistent sum)
inline constexpr float GOAL_SCORE_BASE_Y = 5124.25f;

// ---------------------------------------------------------------------------
// Corners + wall/ceiling fillets (uu)
// ---------------------------------------------------------------------------

// Corner walls are flat 45-degree chamfers on the plane |x| + |y| = CORNER_SUM.
// [1] RLBot wiki Useful-Game-Values ("corner planes intersect the axes at
//     +/-8064 at a 45 degrees angle"; corner wall length 1629.174 =
//     1152*sqrt(2), side wall length 7936 = 2*3968, back wall length
//     5888 = 2*2944 — all consistent with |x|+|y| = 8064)
// [2] RLGym common_values.py:12 (FIELD_BOUNDING_BOX_SIZE = 8064) with
//     CORNER_CATHETUS_LENGTH = 1152 (8064 = 4096 + 3968 = 2944 + 5120)
// [3] rl-opus src/config.ts:32 (cornerSum 8064, "|x| + |z| = 8064")
inline constexpr float CORNER_SUM = 8064.0f;

// Straight-wall run consumed by each corner (cathetus). 4096 - 2944 = 1152.
// [1] RLGym common_values.py:13 (CORNER_CATHETUS_LENGTH = 1152)
// [2] RLBot wiki (corner wall length 1629.174 / sqrt(2) = 1152.0)
inline constexpr float CORNER_CATHETUS = 1152.0f;

// Radius of the curved floor->wall transition (drivable ramp).
// [1] RLGym common_values.py:14 (RAMP_HEIGHT = 256)
// [2] rl-opus src/config.ts:34 (rampRadius 256)
inline constexpr float RAMP_RADIUS = 256.0f;

// Radius of the curved wall->ceiling transition. UNVERIFIED (single source).
// [1] rl-opus src/config.ts:36 (ceilRadius 256)
// Adopted symmetrically with RAMP_RADIUS; must be measured in-harness
// (raycast into the ceiling fillet, error bar <= 0.04 m).
inline constexpr float CEIL_RADIUS = 256.0f; // UNVERIFIED — single source

// ---------------------------------------------------------------------------
// Ball reference (uu)
// ---------------------------------------------------------------------------

// Soccar ball radius, used as the default IsGoal parameter.
// [1] RLGym common_values.py (BALL_RADIUS = 91.25)
// [2] rl-opus src/config.ts (BALL radius 91.25)
// [3] third_party/RocketSim mutator default ballRadius 91.25/92.75
//     (ArenaData SoccarArena). NOTE: RocketSim default soccar ball radius is
//     92.75 in some paths; 91.25 is the widely published value. Adopted 91.25.
inline constexpr float BALL_RADIUS = 91.25f;

// ---------------------------------------------------------------------------
// Boost pads — positions match RocketSim exactly.
//
// kPads order = RocketSim internal order: the 6 big pads first (in
// LOCS_BIG_SOCCAR order), then the 28 small pads (in LOCS_SMALL_SOCCAR order),
// because Arena::_InitBoostPads assigns isBig = (i < 6).
// [1] third_party/RocketSim/src/RLConst.h:274-312
//     (LOCS_SMALL_SOCCAR[28] z=70, LOCS_BIG_SOCCAR[6] z=73)
// [2] rl-opus src/config.ts (BIG_PAD_POSITIONS 6 + SMALL_PAD_POSITIONS 28,
//     identical coordinate sets)
// [3] RLGym common_values.py BOOST_LOCATIONS (34 entries; same set except one
//     entry (-940,3310) carries a 2 uu typo vs (-940,3308) in [1] and [2];
//     the RLBot wiki pad list — RLGym's upstream — has the same 3310 typo at
//     index 27, while RocketSim fixed it to 3308. Adopted 3308.)
// [4] RLBot wiki Useful-Game-Values (34 pad coordinates in RLBot order)
//
// Pad z: 73 = big (100 boost), 70 = small (12 boost).
// Amounts/recharge: 100/10 s big, 12/4 s small (RLBot wiki; RocketSim
// RLConst.h:259-267 BOOST_AMOUNT_BIG/SMALL, COOLDOWN_BIG/SMALL).
// ---------------------------------------------------------------------------
inline constexpr std::size_t NUM_PADS = 34;
inline constexpr std::size_t NUM_BIG_PADS = 6;

inline constexpr std::array<Vec3, NUM_PADS> kPads = {{
    // -- big pads (100 boost), RocketSim LOCS_BIG_SOCCAR order --
    {-3584.0f,     0.0f, 73.0f},
    { 3584.0f,     0.0f, 73.0f},
    {-3072.0f,  4096.0f, 73.0f},
    { 3072.0f,  4096.0f, 73.0f},
    {-3072.0f, -4096.0f, 73.0f},
    { 3072.0f, -4096.0f, 73.0f},
    // -- small pads (12 boost), RocketSim LOCS_SMALL_SOCCAR order --
    {    0.0f, -4240.0f, 70.0f},
    {-1792.0f, -4184.0f, 70.0f},
    { 1792.0f, -4184.0f, 70.0f},
    { -940.0f, -3308.0f, 70.0f},
    {  940.0f, -3308.0f, 70.0f},
    {    0.0f, -2816.0f, 70.0f},
    {-3584.0f, -2484.0f, 70.0f},
    { 3584.0f, -2484.0f, 70.0f},
    {-1788.0f, -2300.0f, 70.0f},
    { 1788.0f, -2300.0f, 70.0f},
    {-2048.0f, -1036.0f, 70.0f},
    {    0.0f, -1024.0f, 70.0f},
    { 2048.0f, -1036.0f, 70.0f},
    {-1024.0f,     0.0f, 70.0f},
    { 1024.0f,     0.0f, 70.0f},
    {-2048.0f,  1036.0f, 70.0f},
    {    0.0f,  1024.0f, 70.0f},
    { 2048.0f,  1036.0f, 70.0f},
    {-1788.0f,  2300.0f, 70.0f},
    { 1788.0f,  2300.0f, 70.0f},
    {-3584.0f,  2484.0f, 70.0f},
    { 3584.0f,  2484.0f, 70.0f},
    {    0.0f,  2816.0f, 70.0f},
    { -940.0f,  3308.0f, 70.0f}, // RLGym/RLBot list 3310 here (2 uu typo)
    {  940.0f,  3308.0f, 70.0f},
    {-1792.0f,  4184.0f, 70.0f},
    { 1792.0f,  4184.0f, 70.0f},
    {    0.0f,  4240.0f, 70.0f},
}};

inline constexpr bool IsBigPad(std::size_t i) { return i < NUM_BIG_PADS; }
inline constexpr float PadBoostAmount(std::size_t i) { return IsBigPad(i) ? 100.0f : 12.0f; }
inline constexpr float PadRechargeSeconds(std::size_t i) { return IsBigPad(i) ? 10.0f : 4.0f; }

} // namespace Arena
