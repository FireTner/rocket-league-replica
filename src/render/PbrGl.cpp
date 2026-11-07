// PbrGl.cpp — fixed-function specular, shininess, and ambient lift.
#include "render/PbrGl.h"

#include <GL/gl.h>

namespace rl {
namespace pbr {

void ApplyFrameState() {
    // Init forces GL_SPECULAR to 0; this raises it while pbr is on.
    const GLfloat spec[] = {kSpecR, kSpecG, kSpecB, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    // Roughness factor — one exponent for the car/ball coat.
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, kShininess);
    // Pin each light's specular to its diffuse color. The default is white.
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
