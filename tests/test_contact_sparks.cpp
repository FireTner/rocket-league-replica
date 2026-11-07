// Contact sparks: read-only scrape and touch view, plus the presentation decision.
// Fails to compile until Sim::GetCarScrape and GetBallTouch exist.
#include <cmath>
#include <cstdio>

#include "fx/ContactSparks.h"
#include "sim/Sim.h"

static int gFails = 0;

static void Check(bool cond, const char* msg) {
	if (cond) {
		std::printf("  ok   %s\n", msg);
	} else {
		std::printf("  FAIL %s\n", msg);
		++gFails;
	}
}

static bool Sample(Sim* sim, int car, rl::ContactSparkFrame* frame, rl::ContactDiag* diag) {
	Sim::CarScrape scrape;
	Sim::BallTouch touch;
	if (!sim->GetCarScrape(car, &scrape) || !sim->GetBallTouch(car, &touch))
		return false;
	*frame = rl::ClassifyContact(scrape, touch, diag);
	return true;
}

int main() {
	// Synthetic decision, no sim. Locks the predicate the draw path uses.
	{
		Sim::CarScrape scrape;
		scrape.wheels[0].touching = true;
		scrape.wheels[0].x = 10;
		scrape.wheels[0].y = 0;
		scrape.wheels[0].z = 4;
		scrape.wheels[0].nz = 1;
		scrape.wheels[0].lat = 400;
		scrape.wheels[0].lon = 80;
		scrape.handbrake = 1;
		scrape.wheelsTouching = 1;
		scrape.vx = 500;
		scrape.vy = 0;
		Sim::BallTouch quiet;
		rl::ContactDiag diag;
		rl::ContactSparkFrame frame = rl::ClassifyContact(scrape, quiet, &diag);
		Check(frame.scrape && frame.count >= 1, "synthetic grind emits a patch");
		Check(std::fabs(frame.bx) > 0.9f && std::fabs(frame.by) < 0.1f,
			"synthetic grind trails opposite the slide");

		scrape.wheels[0].lat = 8;
		scrape.wheels[0].lon = 1400;
		scrape.handbrake = 0;
		frame = rl::ClassifyContact(scrape, quiet, &diag);
		Check(!frame.scrape, "synthetic clean roll stays quiet");

		Sim::BallTouch glance;
		glance.hitThisTick = true;
		glance.x = 1;
		glance.y = 2;
		glance.z = 90;
		glance.nx = 1;
		glance.ny = 0;
		glance.nz = 0;
		glance.relVx = 0;
		glance.relVy = 900;
		glance.relVz = 0;
		glance.fx = 0;
		glance.fy = 1;
		glance.fz = 0;
		glance.ballZ = 93;
		glance.carOnGround = true;
		frame = rl::ClassifyContact(scrape, glance, &diag);
		Check(frame.awkward, "synthetic glance is awkward");
		Check(!frame.scrape, "synthetic glance does not invent a surface scrape");

		Sim::BallTouch clean = glance;
		clean.nx = 0;
		clean.ny = -1;
		clean.relVx = 0;
		clean.relVy = -1200;
		clean.relVz = 80;
		frame = rl::ClassifyContact(scrape, clean, &diag);
		Check(!frame.awkward, "synthetic square hit stays quiet");
	}

	Sim* sim = Sim::CreateSoccar(nullptr);
	Check(sim != nullptr, "CreateSoccar");
	if (!sim)
		return 1;
	sim->SetSeed(1);
	int car = sim->AddCar(Sim::kBlue);
	Check(car >= 0, "AddCar");

	// Slow grind: powerslide on the turf, below supersonic.
	sim->Teleport(car, 0, 0, 1.5707963f);
	sim->SetBall(3000, 3000, 200, 0, 0, 0);
	bool grind = false;
	float grindSpeed = 0;
	float grindLat = 0;
	float grindZ = 0;
	for (int i = 0; i < 180; ++i) {
		sim->SetControls(car, 1, 1, 0, 0, 0, false, false, true);
		sim->Step120();
		rl::ContactSparkFrame frame;
		rl::ContactDiag diag;
		if (!Sample(sim, car, &frame, &diag))
			continue;
		Sim::BodySample body;
		sim->GetCar(car, &body);
		float speed = std::sqrt(body.vx * body.vx + body.vy * body.vy + body.vz * body.vz);
		if (frame.scrape && speed < 2200.0f && frame.count > 0) {
			grind = true;
			grindSpeed = speed;
			grindLat = diag.maxLat;
			grindZ = frame.z[0];
			std::printf("  grind tick=%d speed=%.1f lat=%.1f ratio=%.2f patchZ=%.1f\n",
				i + 1, speed, diag.maxLat, diag.maxRatio, frame.z[0]);
			break;
		}
	}
	Check(grind, "slow turf grind throws a scrape");
	Check(grind && grindZ < 80.0f, "grind sparks sit at the wheels or belly");
	std::printf("  grind summary speed=%.1f lat=%.1f patchZ=%.1f\n",
		grindSpeed, grindLat, grindZ);

	// Scrape stops once the slide stops.
	bool stopped = false;
	for (int i = 0; i < 360; ++i) {
		Sim::BodySample body;
		sim->GetCar(car, &body);
		float fx = 0, fy = 1, fz = 0;
		sim->GetCarForward(car, &fx, &fy, &fz);
		float fwd = body.vx * fx + body.vy * fy + body.vz * fz;
		float throttle = fwd > 30.0f ? -1.0f : (fwd < -30.0f ? 1.0f : 0.0f);
		sim->SetControls(car, throttle, 0, 0, 0, 0, false, false, false);
		sim->Step120();
		rl::ContactSparkFrame frame;
		rl::ContactDiag diag;
		Sample(sim, car, &frame, &diag);
		float speed = std::sqrt(body.vx * body.vx + body.vy * body.vy + body.vz * body.vz);
		if (speed < 40.0f && !frame.scrape) {
			stopped = true;
			std::printf("  grind stopped after %d brake ticks speed=%.1f\n", i + 1, speed);
			break;
		}
	}
	Check(stopped, "sparks stop when the scrape stops");

	auto runTouch = [](float ballX, const char* name, bool* hit, bool* awkward,
		Sim::BodySample* ballOut) {
		Sim* s = Sim::CreateSoccar(nullptr);
		s->SetSeed(1);
		int c = s->AddCar(Sim::kBlue);
		s->Teleport(c, 0, 0, 1.5707963f);
		s->SetBoost(c, 100);
		s->SetBall(ballX, 280, 93.15f, 0, 0, 0);
		*hit = false;
		*awkward = false;
		for (int i = 0; i < 90; ++i) {
			s->SetControls(c, 1, 0, 0, 0, 0, false, true, false);
			s->Step120();
			rl::ContactSparkFrame frame;
			rl::ContactDiag diag;
			Sim::CarScrape scrape;
			Sim::BallTouch touch;
			s->GetCarScrape(c, &scrape);
			s->GetBallTouch(c, &touch);
			frame = rl::ClassifyContact(scrape, touch, &diag);
			if (touch.hitThisTick) {
				*hit = true;
				*awkward = frame.awkward;
				s->GetBall(ballOut);
				std::printf("  %s hit tick=%d awkward=%d tangent=%.1f approach=%.1f align=%.2f nz=%.2f ballZ=%.1f hit %.1f %.1f %.1f\n",
					name, i + 1, frame.awkward ? 1 : 0, diag.tangent, diag.approach,
					diag.align, touch.nz, touch.ballZ, touch.x, touch.y, touch.z);
				std::printf("  %s ball %.2f %.2f %.2f vel %.2f %.2f %.2f\n",
					name, ballOut->x, ballOut->y, ballOut->z,
					ballOut->vx, ballOut->vy, ballOut->vz);
				delete s;
				return;
			}
		}
		std::printf("  %s no hit\n", name);
		delete s;
	};

	bool gHit = false, gAwkward = false, cHit = false, cAwkward = false;
	Sim::BodySample gBall, cBall, gBall2;
	// 100 uu to the side: ball radius 91.25 (RLConst.h:117) plus the Octane
	// half-width still overlap, and the contact is off the nose.
	runTouch(100.0f, "glance", &gHit, &gAwkward, &gBall);
	runTouch(100.0f, "glance-again", &gHit, &gAwkward, &gBall2);
	runTouch(0.0f, "clean", &cHit, &cAwkward, &cBall);
	Check(gHit && gAwkward, "glancing ball touch throws a burst");
	Check(cHit && !cAwkward, "square ball touch stays quiet");
	Check(gHit && gBall.x == gBall2.x && gBall.y == gBall2.y && gBall.z == gBall2.z &&
		gBall.vx == gBall2.vx && gBall.vy == gBall2.vy && gBall.vz == gBall2.vz,
		"glancing touch is deterministic and the ball physics is untouched");

	// Straight run, no handbrake. Contact sparks must stay off, including
	// once the car is supersonic. This is NOT worker B's sparkle check.
	{
		Sim* s = Sim::CreateSoccar(nullptr);
		s->SetSeed(1);
		int c = s->AddCar(Sim::kBlue);
		s->Teleport(c, 0, -1500, 1.5707963f);
		s->SetBoost(c, 100);
		s->SetBall(3500, 3500, 400, 0, 0, 0);
		int scrapes = 0;
		float top = 0;
		bool super = false;
		for (int i = 0; i < 260; ++i) {
			s->SetControls(c, 1, 0, 0, 0, 0, false, true, false);
			s->Step120();
			rl::ContactSparkFrame frame;
			rl::ContactDiag diag;
			Sample(s, c, &frame, &diag);
			if (frame.scrape || frame.awkward)
				++scrapes;
			Sim::BodySample body;
			s->GetCar(c, &body);
			float speed = std::sqrt(body.vx * body.vx + body.vy * body.vy + body.vz * body.vz);
			if (speed > top)
				top = speed;
			if (s->IsSupersonic(c))
				super = true;
		}
		std::printf("  straight scrapes=%d top=%.1f supersonic=%d (NOT_B_SPARKLE_CHECK)\n",
			scrapes, top, super ? 1 : 0);
		Check(scrapes == 0 && super && top >= 2200.0f,
			"straight supersonic run has no contact sparks");
		delete s;
	}

	delete sim;
	if (gFails) {
		std::printf("CONTACT SPARKS FAIL %d\n", gFails);
		return 1;
	}
	std::printf("CONTACT SPARKS PASS\n");
	return 0;
}
