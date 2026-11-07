// Goal burst is a timer anchored at the latch position. No GL, no sim.
#include <cstdio>

#include "fx/GoalBurst.h"

static int gFails = 0;

static void Check(bool cond, const char* msg) {
	if (cond) {
		std::printf("  ok   %s\n", msg);
	} else {
		std::printf("  FAIL %s\n", msg);
		++gFails;
	}
}

int main() {
	rl::GoalBurst burst;
	Check(!burst.IsActive(), "idle before the latch");
	burst.Trigger(10.0f, -4000.0f, 120.0f);
	Check(burst.IsActive(), "active on trigger");
	Check(burst.OriginX() == 10.0f && burst.OriginY() == -4000.0f &&
		burst.OriginZ() == 120.0f, "keeps the scored ball point");
	Check(burst.Life() == rl::GoalBurst::kLifeSeconds, "full life on trigger");
	burst.Update(0.25f);
	Check(burst.IsActive(), "still active mid-burst");
	Check(burst.Life() > 0.50f && burst.Life() < 0.60f, "sim time subtracts");
	burst.Trigger(1.0f, 2.0f, 3.0f);
	Check(burst.OriginY() == 2.0f, "a second latch retargets");
	burst.Update(rl::GoalBurst::kLifeSeconds + 0.1f);
	Check(!burst.IsActive(), "expires after its life");
	Check(burst.Life() == 0.0f, "life clamps at zero");
	if (gFails) {
		std::printf("FAIL goal burst (%d)\n", gFails);
		return 1;
	}
	std::printf("PASS goal burst\n");
	return 0;
}
