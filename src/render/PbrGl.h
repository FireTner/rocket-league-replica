// PbrGl.h — fixed-function specular, shininess, and ambient lift.
#pragma once

namespace rl {
namespace pbr {

// Roughness for the car/ball coat. Maps to fixed-function shininess
// as shininess = (1 - roughness) * 120 + 8  →  0.35 ≈ 86.
inline constexpr float kRoughness = 0.35f;
inline constexpr float kShininess = 86.0f;

// Specular strength (tinted cool like the key light, not white, so
// saturated team paint does not wash out).
inline constexpr float kSpecR = 0.55f;
inline constexpr float kSpecG = 0.58f;
inline constexpr float kSpecB = 0.65f;

// Simple IBL ambient lift: hemisphere-mean approximation. Added on top
// of the shipped global ambient (0.28,0.34,0.30) → (0.36,0.42,0.38).
inline constexpr float kIblR = 0.36f;
inline constexpr float kIblG = 0.42f;
inline constexpr float kIblB = 0.38f;

// Apply the PBR-lite frame state (GL context must be live; called once
// per BeginFrame while `[render] pbr=true`). Pure fixed-function state,
// idempotent — safe to re-apply every frame.
void ApplyFrameState();

}  // namespace pbr
}  // namespace rl
