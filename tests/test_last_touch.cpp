// Last touch: which team last hit the ball. Reads Sim::GetLastTouch only.
#include <cmath>
#include <cstdio>

#include "sim/Sim.h"

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            std::printf("  ok   %s\n", msg);                          \
        } else {                                                      \
            std::printf("  FAIL %s\n", msg);                          \
            ++failures;                                               \
        }                                                             \
    } while (0)

static float BallSpeed(Sim* sim) {
    Sim::BodySample b;
    if (!sim->GetBall(&b)) return 0.0f;
    return std::sqrt(b.vx * b.vx + b.vy * b.vy + b.vz * b.vz);
}

// Drive `car` from (x, y) along `yaw` until that car is the last touch.
// *outSpeed is the peak ball speed over the hit tick and the next 15.
static bool DriveHit(Sim* sim, int car, float x, float y, float yaw,
                      float throttle, bool boost, int maxTicks,
                      Sim::LastTouch* touch, float* outSpeed) {
    sim->Teleport(car, x, y, yaw);
    sim->SetBoost(car, 100.0f);
    sim->SetControls(car, throttle, 0.0f, 0.0f, 0.0f, 0.0f, false, boost, false);
    for (int i = 0; i < maxTicks; i++) {
        sim->Step120();
        if (!sim->GetLastTouch(touch) || touch->car != car) continue;
        float peak = BallSpeed(sim);
        for (int k = 0; k < 15; k++) {
            sim->Step120();
            float s = BallSpeed(sim);
            if (s > peak) peak = s;
        }
        *outSpeed = peak;
        return true;
    }
    return false;
}

int main() {
    std::printf("last-touch\n");
    Sim* sim = Sim::CreateSoccar(nullptr);
    CHECK(sim != nullptr, "CreateSoccar");
    if (!sim) return 1;
    sim->SetSeed(1);
    int blue = sim->AddCar(Sim::kBlue);
    int orange = sim->AddCar(Sim::kOrange);
    CHECK(blue == 0 && orange == 1, "blue idx 0, orange idx 1");
    sim->ResetBall();

    Sim::LastTouch touch;
    CHECK(!sim->GetLastTouch(nullptr), "null out is not a touch");
    CHECK(!sim->GetLastTouch(&touch), "no touch at kickoff");

    // Soft: creep, no boost. Speed is printed so the trail floor can be
    // placed above a real contact instead of invented.
    float soft = 0.0f;
    bool softHit = DriveHit(sim, blue, 0.0f, -220.0f, 1.5707963f,
                             0.25f, false, 360, &touch, &soft);
    std::printf("  SOFT hit=%d team=%d car=%d speed=%.1f\n",
                softHit ? 1 : 0, touch.team, touch.car, soft);
    CHECK(softHit, "soft contact registers a touch");
    CHECK(softHit && touch.car == blue && touch.team == Sim::kBlue,
          "soft touch is the blue car");

    // Hard: fresh arena, full boost, a long run-up. Same print.
    Sim* hard = Sim::CreateSoccar(nullptr);
    CHECK(hard != nullptr, "second sim");
    if (!hard) return 1;
    hard->SetSeed(1);
    int hBlue = hard->AddCar(Sim::kBlue);
    int hOrange = hard->AddCar(Sim::kOrange);
    hard->ResetBall();
    CHECK(!hard->GetLastTouch(&touch), "hard sim starts untouched");
    float hardSpeed = 0.0f;
    bool hardHit = DriveHit(hard, hBlue, 0.0f, -2000.0f, 1.5707963f,
                             1.0f, true, 600, &touch, &hardSpeed);
    std::printf("  HARD hit=%d team=%d car=%d tick=%llu speed=%.1f\n",
                hardHit ? 1 : 0, touch.team, touch.car,
                (unsigned long long)touch.tick, hardSpeed);
    CHECK(hardHit && touch.car == hBlue && touch.team == Sim::kBlue,
          "boosted hit is the blue car");
    uint64_t blueTick = touch.tick;

    // The other car. Teleport only orange (SetState wipes THAT car's hit
    // record) and drop the ball on it, far from blue so blue's still-valid
    // earlier hit loses on tick order. SetBall is the existing harness; it
    // does not clear ballHitInfo.
    hard->SetControls(hBlue, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false, false);
    hard->SetBall(0.0f, 4000.0f, 140.0f, 0.0f, 0.0f, -80.0f);
    hard->Teleport(hOrange, 0.0f, 4000.0f, 0.0f);
    bool switched = false;
    for (int i = 0; i < 60; i++) {
        hard->Step120();
        if (!hard->GetLastTouch(&touch)) continue;
        if (touch.car == hOrange && touch.team == Sim::kOrange &&
            touch.tick > blueTick) {
            switched = true;
            break;
        }
    }
    std::printf("  SWITCH team=%d car=%d tick=%llu\n",
                touch.team, touch.car, (unsigned long long)touch.tick);
    CHECK(switched, "later orange touch replaces blue");

    delete hard;
    delete sim;
    if (failures) {
        std::printf("LAST TOUCH FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("LAST TOUCH PASS\n");
    return 0;
}
