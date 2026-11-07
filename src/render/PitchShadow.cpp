// Pitch-plane sun blobs for a loaded stadium pack. See PitchShadow.h.
#include "render/PitchShadow.h"

#include "render/Renderer.h"

#include <GL/gl.h>

namespace rl {

namespace {

// Models.h dims, plus a small margin so the blob covers the body. UNVERIFIED
// as a visual pad; the hitbox itself is 120.5 x 86.7 and the ball is 91.25.
constexpr float kCarAlong = 72.f;
constexpr float kCarAcross = 52.f;
constexpr float kBallR = 91.25f;
constexpr float kDecalZ = 3.f;
constexpr float kRadToDeg = 57.2957795f;

float Shade(float z) {
	if (z < 0.f)
		z = 0.f;
	float t = z / 2044.f;
	if (t > 1.f)
		t = 1.f;
	return 1.f - 0.45f * t;
}

struct ShadowState {
	ShadowState() {
		light_ = glIsEnabled(GL_LIGHTING);
		tex_ = glIsEnabled(GL_TEXTURE_2D);
		blend_ = glIsEnabled(GL_BLEND);
		depth_ = glIsEnabled(GL_DEPTH_TEST);
		offset_ = glIsEnabled(GL_POLYGON_OFFSET_FILL);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &mask_);
		glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &factor_);
		glGetFloatv(GL_POLYGON_OFFSET_UNITS, &units_);
		glGetIntegerv(GL_BLEND_SRC, &src_);
		glGetIntegerv(GL_BLEND_DST, &dst_);
		glDisable(GL_LIGHTING);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_POLYGON_OFFSET_FILL);
		// Stronger than the pack field-line decal (-1.5, -2) so the blob
		// sits on the lightmapped turf instead of under it.
		glPolygonOffset(-4.f, -8.f);
	}
	~ShadowState() {
		glPolygonOffset(factor_, units_);
		if (!offset_)
			glDisable(GL_POLYGON_OFFSET_FILL);
		glBlendFunc((GLenum)src_, (GLenum)dst_);
		glDepthMask(mask_);
		if (!depth_)
			glDisable(GL_DEPTH_TEST);
		if (!blend_)
			glDisable(GL_BLEND);
		if (tex_)
			glEnable(GL_TEXTURE_2D);
		else
			glDisable(GL_TEXTURE_2D);
		if (light_)
			glEnable(GL_LIGHTING);
		else
			glDisable(GL_LIGHTING);
	}
	GLboolean light_, tex_, blend_, depth_, offset_, mask_;
	GLfloat factor_, units_;
	GLint src_, dst_;
};

void DrawBlob(Renderer& renderer, const PitchShadow::ShadowBlob& blob, float shade) {
	if (!blob.visible || shade <= 0.02f)
		return;
	glPushMatrix();
	glTranslatef(blob.x, blob.y, kDecalZ);
	glRotatef(blob.angle * kRadToDeg, 0.f, 0.f, 1.f);
	glScalef(blob.major, blob.minor, 1.f);
	renderer.DrawDisc(0.f, 0.f, 0.f, 1.f, 32, 0.f, 0.f, 0.f, shade * 0.22f);
	renderer.DrawDisc(0.f, 0.f, 0.f, 0.58f, 24, 0.f, 0.f, 0.f, shade * 0.42f);
	glPopMatrix();
}

} // namespace

void DrawPitchSunShadows(Renderer& renderer, const Snapshot& player,
                         const Snapshot& opponent) {
	if (!renderer.HasArenaPack())
		return;
	ShadowState state;
	DrawBlob(renderer,
	         PitchShadow::CarBlob(player.carPos, player.carForward, kCarAlong, kCarAcross),
	         Shade(player.carPos[2]));
	DrawBlob(renderer,
	         PitchShadow::CarBlob(opponent.carPos, opponent.carForward, kCarAlong, kCarAcross),
	         Shade(opponent.carPos[2]));
	DrawBlob(renderer,
	         PitchShadow::BallBlob(player.ballPos[0], player.ballPos[1], player.ballPos[2], kBallR),
	         Shade(player.ballPos[2]));
}

} // namespace rl
