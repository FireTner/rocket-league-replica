// Flip-reset availability. Presentation needs RocketSim's flag for "this
// airborne flip came from the wheels, not from a jump." Snapshot does not
// carry it. This reads Sim::HasFlipReset only — no physics write.
//
// HasFlipReset is CarState::HasFlipReset (Car.cpp): airborne, still able to
// flip or jump, and hasJumped is false (left the ground from wheel contact).
// A normal jump keeps hasJumped, so the dodge window is not a reset.
//
// Build: see Makefile /tmp/t_flip.
#include <cmath>
#include <cstdio>

#include "sim/Sim.h"

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            std::printf("  ok   %s\n", msg);                          \
        } else {                                                        \
            std::printf("  FAIL %s\n", msg);                            \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void Hold(Sim* sim, int car, bool jump) {
    sim->SetControls(car, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, jump, false, false);
}

// Drop the ball onto the wheels and look for the tick the reset sticks
// after the wheels leave the ball. Returns false if none appears.
static bool SeekReset(Sim* sim, int car) {
    Sim::BodySample c;
    if (!sim->GetCar(car, &c)) return false;
    // Wheel contact at rest is about 17 uu under the origin (spawn z).
    // Ball collision radius is 91.25 (RLConst BALL_COLLISION_RADIUS_SOCCAR).
    const float ballR = 91.25f;
    const float wheelDrop = 17.0f;
    for (int attempt = 0; attempt < 12; attempt++) {
        float lift = (attempt - 4) * 4.0f;
        if (!sim->GetCar(car, &c)) return false;
        float bz = c.z - wheelDrop - ballR + lift;
        if (bz < ballR) bz = ballR;
        sim->SetBall(c.x, c.y, bz, 0.0f, 0.0f, 40.0f);
        for (int i = 0; i < 40; i++) {
            Hold(sim, car, false);
            sim->Step120();
            if (sim->HasFlipReset(car)) return true;
            Sim::CarScrape scrape;
            if (!sim->GetCarScrape(car, &scrape) || scrape.wheelsTouching < 3)
                continue;
            // Leave the ball. HasFlipReset is false while the wheels still
            // count as ground (CarState::HasFlipReset requires !isOnGround).
            sim->SetBall(0.0f, 8000.0f, 200.0f, 0.0f, 0.0f, 0.0f);
            Hold(sim, car, false);
            sim->Step120();
            if (sim->HasFlipReset(car)) return true;
        }
    }
    return false;
}

int main() {
    std::printf("flip-reset\n");
    Sim* sim = Sim::CreateSoccar(nullptr);
    CHECK(sim != nullptr, "CreateSoccar");
    if (!sim) return 1;
    sim->SetSeed(1);
    int car = sim->AddCar(Sim::kBlue);
    CHECK(car == 0, "car idx 0");
    CHECK(!sim->HasFlipReset(-1), "bad index is not a reset");
    CHECK(!sim->HasFlipReset(car), "spawn is on the ground, not a reset");

    Hold(sim, car, true);
    sim->Step120();
    Hold(sim, car, false);
    bool jumpedAir = false;
    for (int i = 0; i < 90; i++) {
        sim->Step120();
        Sim::BodySample c;
        sim->GetCar(car, &c);
        if (c.z > 80.0f && c.vz > 0.0f) {
            jumpedAir = true;
            break;
        }
    }
    CHECK(jumpedAir, "jump leaves the floor");
    CHECK(!sim->HasFlipReset(car), "a jump dodge is not a flip reset");

    bool reset = SeekReset(sim, car);
    std::printf("  SEEK reset=%d\n", reset ? 1 : 0);
    CHECK(reset, "wheels on the ball grant a flip reset");
    CHECK(sim->HasFlipReset(car), "reset stays while the flip is unused");

    delete sim;
    if (failures) {
        std::printf("FLIP RESET FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("FLIP RESET PASS\n");
    return 0;
}
