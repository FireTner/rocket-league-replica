// PbrGl.cpp — G6a PBR-lite frame state (fixed-function-composited).
//
// Why no shader: GRAPHICS45 §2.4 supersedes the GL2.1/no-shader freeze to
// allow compat-safe GLSL, but fixed-function stays the DEFAULT path and
// the brief forbids `#version 330 core`-only pipelines and compile-in-
// frame-loop. A fixed-function specular + shininess + ambient lift needs
// no program at all, so it is compat-safe on every GL the 4.5-compat
// context accepts (Mesa R4 through NVIDIA), adds zero draws/tris, and
// keeps the default-OFF path byte-identical by construction (this
// function is simply never called when pbr=false).
//
// What each knob does:
//  - specular highlight: GL_SPECULAR material was forced to 0 in
//    Renderer::Init (it washed tints toward white). Here it is raised to
//    a cool-tinted 0.55..0.65 ONLY while pbr is on, and both lights get
//    explicit SPECULAR terms matching their diffuse colors (Mesa default
//    light specular is white; pinning it keeps captures deterministic
//    across drivers instead of depending on the default).
//  - roughness factor: single GL_SHININESS exponent derived from
//    kRoughness (0.35 → 86). One material path for car bodies/ball.
//  - IBL ambient lift: GL_LIGHT_MODEL_AMBIENT raised by +0.08/channel,
//    approximating a hemisphere/IBL mean bounce. Cheap, global, and the
//    arena (diffuse-dominated sheets) barely moves while glossy paint
//    and the ball pick up the lift plus the highlight.
#include "render/PbrGl.h"

#include <GL/gl.h>

namespace rl {
namespace pbr {

void ApplyFrameState() {
    // Specular highlight (material) — off in the shipped path, on here.
    const GLfloat spec[] = {kSpecR, kSpecG, kSpecB, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    // Roughness factor — one exponent for the car/ball coat.
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, kShininess);
    // Pin the lights' specular terms to their diffuse colors so the
    // highlight hue is deterministic (not the white default).
    const GLfloat keySpec[] = {0.72f, 0.74f, 0.80f, 1.0f};
    const GLfloat fillSpec[] = {0.30f, 0.32f, 0.36f, 1.0f};
    glLightfv(GL_LIGHT0, GL_SPECULAR, keySpec);
    glLightfv(GL_LIGHT1, GL_SPECULAR, fillSpec);
    // Simple IBL ambient lift (hemisphere-mean approximation).
    const GLfloat amb[] = {kIblR, kIblG, kIblB, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
}

}  // namespace pbr
}  // namespace rl
