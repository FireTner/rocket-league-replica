// Wheel embers follow Snapshot.supersonic and stay off the ground when airborne.
#include "fx/WheelEmbers.h"

#include <cmath>
#include <cstdio>

static int fails = 0;

static void Expect(bool ok, const char* msg) {
    if (!ok) {
        std::printf("FAIL %s\n", msg);
        fails++;
    }
}

static rl::Snapshot Car() {
    rl::Snapshot s;
    s.carPos[0] = 100;
    s.carPos[1] = -40;
    s.carPos[2] = 17;
    s.carForward[0] = 1;
    s.carForward[1] = 0;
    s.carForward[2] = 0;
    s.carUp[0] = 0;
    s.carUp[1] = 0;
    s.carUp[2] = 1;
    s.supersonic = true;
    s.airborne = false;
    return s;
}

int main() {
    float p[3];
    Expect(rl::WheelContactPoint(Car(), 0, p), "wheel 0");
    Expect(std::fabs(p[2]) < 0.01f, "contact on z=0 when origin z is the radius");
    Expect(std::fabs(p[0] - (100.0f + rl::kWheelEmberHalfL)) < 0.01f, "front x");
    Expect(!rl::WheelContactPoint(Car(), 4, p), "wheel 4 rejected");

    rl::WheelEmbers embers;
    rl::Snapshot s = Car();
    embers.Update(1.0f / 120.0f, s);
    int born = embers.AliveCount();
    Expect(born == 4, "one ember per wheel on the first grounded supersonic step");

    s.supersonic = false;
    for (int i = 0; i < 30; ++i)
        embers.Update(1.0f / 120.0f, s);
    Expect(embers.AliveCount() == 0, "embers die and do not respawn below the flag");

    s = Car();
    s.airborne = true;
    rl::WheelEmbers air;
    for (int i = 0; i < 10; ++i)
        air.Update(1.0f / 120.0f, s);
    Expect(air.AliveCount() == 0, "no embers while airborne");

    s = Car();
    s.supersonic = false;
    rl::WheelEmbers slow;
    slow.Update(1.0f / 120.0f, s);
    Expect(slow.AliveCount() == 0, "subsonic emits nothing");

    if (fails) {
        std::printf("WHEEL EMBERS FAIL %d\n", fails);
        return 1;
    }
    std::printf("WHEEL EMBERS PASS\n");
    return 0;
}
