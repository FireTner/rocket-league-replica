// Ball orientation is the integral of physics angVel. Read-only.
// noBallRot leaves the body basis at identity.
// A marked point stays fixed at zero spin and moves by |w|*dt when rolling.
#include <cmath>
#include <cstdio>

#include "models/BallOrient.h"
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

static float Len3(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static float Dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

static void MapUnit(const float rot[9], const float local[3], float o[3]) {
    rl::MapBallPoint(rot, local[0], local[1], local[2], o[0], o[1], o[2]);
}

// Body point least aligned with ω, so the mark is visible.
static void MarkLocal(const float w[3], float local[3]) {
    const float axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    int best = 0;
    float bestAbs = 2.0f;
    float wlen = Len3(w);
    for (int i = 0; i < 3; i++) {
        float c = wlen > 0.0f ? std::fabs(Dot3(w, axes[i]) / wlen) : 0.0f;
        if (c < bestAbs) {
            bestAbs = c;
            best = i;
        }
    }
    local[0] = axes[best][0];
    local[1] = axes[best][1];
    local[2] = axes[best][2];
}

// Signed angle of a→b about a unit axis. Matches Rodrigues of |ω|·dt.
static float AngleAbout(const float a[3], const float b[3], const float axis[3]) {
    float da = Dot3(a, axis);
    float db = Dot3(b, axis);
    float q0[3] = {a[0] - axis[0] * da, a[1] - axis[1] * da, a[2] - axis[2] * da};
    float q1[3] = {b[0] - axis[0] * db, b[1] - axis[1] * db, b[2] - axis[2] * db};
    float s[3];
    Cross3(q0, q1, s);
    return std::atan2(Dot3(axis, s), Dot3(q0, q1));
}

static bool SpinStepMatches(const Sim::BallSpin& before, const float local[3],
                            const Sim::BallSpin& after, float dt) {
    float p0[3], p1[3];
    MapUnit(before.rot, local, p0);
    MapUnit(after.rot, local, p1);
    float wlen = Len3(after.angVel);
    if (wlen < 1.0e-8f)
        return Len3(p1) > 0.0f && std::fabs(p1[0] - p0[0]) < 1.0e-6f &&
               std::fabs(p1[1] - p0[1]) < 1.0e-6f && std::fabs(p1[2] - p0[2]) < 1.0e-6f;
    float axis[3] = {after.angVel[0] / wlen, after.angVel[1] / wlen, after.angVel[2] / wlen};
    float ang = AngleAbout(p0, p1, axis);
    float expect = wlen * dt;
    return std::fabs(ang - expect) < 2.0e-4f;
}

int main() {
    std::printf("ball-spin\n");
    const float dt = float(Sim::kTickDt);

    // Layout: the GL matrix and the marked point are the same columns.
    {
        float rot[9];
        rl::SetBallRotIdentity(rot);
        rl::IntegrateBallSpin(rot, 0.0f, 2.0f, 0.0f, dt);
        float p[3];
        rl::MapBallPoint(rot, 1.0f, 0.0f, 0.0f, p[0], p[1], p[2]);
        float m[16];
        rl::BallOrientationMatrix(rot, m);
        float theta = 2.0f * dt;
        CHECK(std::fabs(p[0] - std::cos(theta)) < 1.0e-5f &&
                  std::fabs(p[1]) < 1.0e-5f &&
                  std::fabs(p[2] + std::sin(theta)) < 1.0e-5f,
              "pure +Y spin sends +X toward -Z");
        CHECK(std::fabs(m[0] - p[0]) < 1.0e-6f && std::fabs(m[1] - p[1]) < 1.0e-6f &&
                  std::fabs(m[2] - p[2]) < 1.0e-6f,
              "GL matrix column 0 is the marked point");
        float held[9];
        rl::SetBallRotIdentity(held);
        rl::IntegrateBallSpin(held, 0.0f, 0.0f, 0.0f, dt);
        CHECK(rl::BallRotIsIdentity(held), "zero rate leaves identity untouched");
    }

    Sim* sim = Sim::CreateSoccar(nullptr);
    CHECK(sim != nullptr, "CreateSoccar");
    if (!sim)
        return 1;
    sim->SetSeed(1);
    CHECK(sim->AddCar(Sim::kBlue) == 0, "one car so the arena steps");

    // Zero spin. ResetBall writes angVel = 0 (unlike SetBall's at-rest kick).
    sim->ResetBall();
    Sim::BallSpin z0;
    CHECK(sim->GetBallSpin(&z0), "GetBallSpin after reset");
    float zMark[3] = {1, 0, 0};
    float zP0[3], zP1[3];
    MapUnit(z0.rot, zMark, zP0);
    for (int i = 0; i < 240; i++)
        sim->Step120();
    Sim::BallSpin z1;
    CHECK(sim->GetBallSpin(&z1), "GetBallSpin after rest");
    MapUnit(z1.rot, zMark, zP1);
    float zMoved = std::sqrt((zP1[0] - zP0[0]) * (zP1[0] - zP0[0]) +
                             (zP1[1] - zP0[1]) * (zP1[1] - zP0[1]) +
                             (zP1[2] - zP0[2]) * (zP1[2] - zP0[2]));
    std::printf("BALLSPIN ZERO ticks=240 |w|=%.8f mark=(%.6f,%.6f,%.6f) moved=%.8f\n",
                Len3(z1.angVel), zP1[0], zP1[1], zP1[2], zMoved);
    CHECK(Len3(z1.angVel) < 1.0e-6f, "reset ball stays at zero spin");
    CHECK(zMoved < 1.0e-5f, "zero-spin marked point stays fixed");

    // Turf roll. BALL_REST_Z is 93.15 (RLConst.h). Non-zero velocity stores
    // angVel = 0; friction has to create the spin. 400 uu/s is under the
    // no-slip rate that would hit BALL_MAX_ANG_SPEED (6 rad/s at r=91.25).
    sim->SetBall(0.0f, 0.0f, 93.15f, 400.0f, 0.0f, 0.0f);
    for (int i = 0; i < 240; i++)
        sim->Step120();
    Sim::BallSpin roll;
    Sim::BodySample ball;
    CHECK(sim->GetBallSpin(&roll) && sim->GetBall(&ball), "sample rolling ball");
    std::printf("BALLSPIN ROLL settle |w|=%.4f w=(%.4f,%.4f,%.4f) v=(%.1f,%.1f,%.1f) z=%.2f\n",
                Len3(roll.angVel), roll.angVel[0], roll.angVel[1], roll.angVel[2],
                ball.vx, ball.vy, ball.vz, ball.z);
    CHECK(Len3(roll.angVel) > 0.5f, "sliding ball picks up physics spin");

    float local[3];
    MarkLocal(roll.angVel, local);
    float m0[3], m1[3];
    MapUnit(roll.rot, local, m0);
    const int kRollTicks = 60;
    Sim::BallSpin prev = roll;
    bool stepsOk = true;
    for (int i = 0; i < kRollTicks; i++) {
        sim->Step120();
        Sim::BallSpin now;
        if (!sim->GetBallSpin(&now) || !SpinStepMatches(prev, local, now, dt))
            stepsOk = false;
        prev = now;
    }
    MapUnit(prev.rot, local, m1);
    float moved = std::sqrt((m1[0] - m0[0]) * (m1[0] - m0[0]) +
                            (m1[1] - m0[1]) * (m1[1] - m0[1]) +
                            (m1[2] - m0[2]) * (m1[2] - m0[2]));
    float wlen = Len3(prev.angVel);
    float axis[3] = {0, 1, 0};
    if (wlen > 1.0e-8f) {
        axis[0] = prev.angVel[0] / wlen;
        axis[1] = prev.angVel[1] / wlen;
        axis[2] = prev.angVel[2] / wlen;
    }
    float ang = AngleAbout(m0, m1, axis);
    std::printf("BALLSPIN ROLL ticks=%d w=(%.4f,%.4f,%.4f) mark=(%.4f,%.4f,%.4f)->(%.4f,%.4f,%.4f) moved=%.4f ang=%.4f\n",
                kRollTicks, prev.angVel[0], prev.angVel[1], prev.angVel[2],
                m0[0], m0[1], m0[2], m1[0], m1[1], m1[2], moved, ang);
    CHECK(stepsOk, "each tick rotates the mark by |w|*dt about w");
    CHECK(moved > 0.2f, "marked point moves across the scripted roll");

    // Reset clears the drawn orientation and a zero-spin ball stays there.
    sim->ResetBall();
    Sim::BallSpin cleared;
    CHECK(sim->GetBallSpin(&cleared), "GetBallSpin after second reset");
    float c0[3], c1[3];
    MapUnit(cleared.rot, zMark, c0);
    for (int i = 0; i < 120; i++)
        sim->Step120();
    Sim::BallSpin stayed;
    CHECK(sim->GetBallSpin(&stayed), "GetBallSpin after rested reset");
    MapUnit(stayed.rot, zMark, c1);
    float cMoved = std::sqrt((c1[0] - c0[0]) * (c1[0] - c0[0]) +
                             (c1[1] - c0[1]) * (c1[1] - c0[1]) +
                             (c1[2] - c0[2]) * (c1[2] - c0[2]));
    std::printf("BALLSPIN RESET |w|=%.8f mark=(%.6f,%.6f,%.6f) moved=%.8f\n",
                Len3(stayed.angVel), c1[0], c1[1], c1[2], cMoved);
    CHECK(std::fabs(c0[0] - 1.0f) < 1.0e-6f && std::fabs(c0[1]) < 1.0e-6f &&
              std::fabs(c0[2]) < 1.0e-6f,
          "reset returns the mark to +X");
    CHECK(Len3(stayed.angVel) < 1.0e-6f && cMoved < 1.0e-5f,
          "zero spin after reset stays fixed");

    delete sim;
    if (failures) {
        std::printf("BALLSPIN FAIL %d\n", failures);
        return 1;
    }
    std::printf("BALLSPIN PASS\n");
    return 0;
}
