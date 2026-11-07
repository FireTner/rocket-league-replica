// Analytic Soccar dimensions. X is width, Y is length, Z is up; lengths are uu.
// Values from RocketSim (MIT © 2022 ZealanL).
#pragma once

#include <array>
#include <cstddef>

namespace Arena {

struct Vec3 {
    float x, y, z;
    constexpr Vec3() : x(0), y(0), z(0) {}
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
};

inline constexpr float SIDE_WALL_X = 4096.0f; // RLConst.h:14
inline constexpr float BACK_WALL_Y = 5120.0f; // RLConst.h:15
inline constexpr float CEILING_Z = 2048.0f; // RLConst.h:16
inline constexpr float BACK_NET_Y = 6000.0f;
inline constexpr float GOAL_DEPTH = 880.0f;
inline constexpr float GOAL_HEIGHT = 642.775f;
inline constexpr float GOAL_HALF_WIDTH = 892.755f;
// Scores when |y| exceeds this plus the ball radius.
inline constexpr float GOAL_SCORE_BASE_Y = 5124.25f; // RLConst.h:121
// Corner chamfer is the plane |x| + |y| = CORNER_SUM.
inline constexpr float CORNER_SUM = 8064.0f;
inline constexpr float CORNER_CATHETUS = 1152.0f;
inline constexpr float RAMP_RADIUS = 256.0f;
inline constexpr float CEIL_RADIUS = 256.0f;
inline constexpr float BALL_RADIUS = 91.25f;

inline constexpr std::size_t NUM_PADS = 34;
inline constexpr std::size_t NUM_BIG_PADS = 6;

// Big pads first, then small: is-big is (index < 6). z is 73 or 70.
inline constexpr std::array<Vec3, NUM_PADS> kPads = {{
    {-3584.0f,     0.0f, 73.0f},
    { 3584.0f,     0.0f, 73.0f},
    {-3072.0f,  4096.0f, 73.0f},
    { 3072.0f,  4096.0f, 73.0f},
    {-3072.0f, -4096.0f, 73.0f},
    { 3072.0f, -4096.0f, 73.0f},
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
    { -940.0f,  3308.0f, 70.0f}, // 3308 uu; 3310 is a 2 uu typo
    {  940.0f,  3308.0f, 70.0f},
    {-1792.0f,  4184.0f, 70.0f},
    { 1792.0f,  4184.0f, 70.0f},
    {    0.0f,  4240.0f, 70.0f},
}};

inline constexpr bool IsBigPad(std::size_t i) { return i < NUM_BIG_PADS; }
inline constexpr float PadBoostAmount(std::size_t i) { return IsBigPad(i) ? 100.0f : 12.0f; }
inline constexpr float PadRechargeSeconds(std::size_t i) { return IsBigPad(i) ? 10.0f : 4.0f; }

} // namespace Arena
