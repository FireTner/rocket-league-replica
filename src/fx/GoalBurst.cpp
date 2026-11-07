// GoalBurst.cpp — fixed-function rings and sparks. No textures.
#include "fx/GoalBurst.h"

#include "render/Renderer.h"

#include <GL/gl.h>
#include <cmath>

namespace rl {
namespace {

// +Y scores orange, -Y scores blue. Same sign split as FreeplayBall.
// The RGB values themselves are UNVERIFIED.
void TeamColor(float y, float* r, float* g, float* b) {
	if (y >= 0.0f) {
		*r = 1.0f;
		*g = 0.55f;
		*b = 0.12f;
	} else {
		*r = 0.25f;
		*g = 0.55f;
		*b = 1.0f;
	}
}

void UprightRing(float cx, float cy, float cz, float radius, bool yzPlane,
	int segs, float r, float g, float b, float a) {
	glColor4f(r, g, b, a);
	glBegin(GL_LINE_LOOP);
	for (int i = 0; i < segs; i++) {
		float t = 2.0f * float(M_PI) * float(i) / float(segs);
		float c = std::cos(t) * radius;
		float s = std::sin(t) * radius;
		if (yzPlane)
			glVertex3f(cx, cy + c, cz + s);
		else
			glVertex3f(cx + c, cy, cz + s);
	}
	glEnd();
}

} // namespace

void GoalBurst::Trigger(float x, float y, float z) {
	x_ = x;
	y_ = y;
	z_ = z;
	life_ = kLifeSeconds;
}

void GoalBurst::Update(float dtSeconds) {
	if (life_ <= 0.0f)
		return;
	life_ -= dtSeconds;
	if (life_ < 0.0f)
		life_ = 0.0f;
}

void GoalBurst::Draw(Renderer& renderer) const {
	if (life_ <= 0.0f)
		return;
	const float age = 1.0f - life_ / kLifeSeconds;
	const float alpha = life_ / kLifeSeconds;
	float cr = 1.0f, cg = 1.0f, cb = 1.0f;
	TeamColor(y_, &cr, &cg, &cb);
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glDepthMask(GL_FALSE);
	// Radii are UNVERIFIED. They stay smaller than the existing 2 s
	// shockwave so the two reads stay distinct.
	const float outer = 60.0f + 720.0f * age;
	renderer.DrawRing(x_, y_, 4.0f, outer * 0.82f, outer, 48,
		cr, cg, cb, 0.75f * alpha);
	UprightRing(x_, y_, z_, outer * 0.55f, false, 32, 1.0f, 0.95f, 0.85f, 0.65f * alpha);
	UprightRing(x_, y_, z_, outer * 0.55f, true, 32, 1.0f, 0.95f, 0.85f, 0.65f * alpha);
	glPointSize(3.0f);
	glBegin(GL_POINTS);
	for (int i = 0; i < kParticleCount; i++) {
		const float yaw = float(i) * 2.399963f;
		const float lift = -0.35f + 0.08f * float(i % 9);
		float dx = std::cos(yaw);
		float dy = std::sin(yaw);
		float dz = lift;
		const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
		dx /= len;
		dy /= len;
		dz /= len;
		const float dist = 30.0f + 640.0f * age;
		const float fade = alpha * (0.45f + 0.55f * float(i % 3) / 2.0f);
		glColor4f(cr, cg, cb, fade);
		glVertex3f(x_ + dx * dist, y_ + dy * dist, z_ + dz * dist);
	}
	glEnd();
	glDepthMask(GL_TRUE);
	if (renderer.LightingOn())
		glEnable(GL_LIGHTING);
}

} // namespace rl
