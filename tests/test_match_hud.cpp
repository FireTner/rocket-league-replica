// Clock and countdown are pure functions of the 120 Hz tick. No GL, no sim.
#include "render/MatchHud.h"

#include <cstdio>
#include <cstring>

static int fails = 0;

static void Expect(bool ok, const char* msg) {
	if (!ok) {
		std::fprintf(stderr, "FAIL %s\n", msg);
		fails++;
	}
}

int main() {
	char buf[16];
	Expect(rl::MatchHud::FormatClock(buf, 16, 0) && std::strcmp(buf, "00:00") == 0, "zero");
	Expect(rl::MatchHud::FormatClock(buf, 16, 119) && std::strcmp(buf, "00:00") == 0, "pre-second");
	Expect(rl::MatchHud::FormatClock(buf, 16, 120) && std::strcmp(buf, "00:01") == 0, "one second");
	Expect(rl::MatchHud::FormatClock(buf, 16, 120 * 65) && std::strcmp(buf, "01:05") == 0, "1:05");
	Expect(!rl::MatchHud::FormatClock(buf, 4, 0), "short buffer");
	Expect(rl::MatchHud::CountdownDigit(0, 0) == 3, "3");
	Expect(rl::MatchHud::CountdownDigit(119, 0) == 3, "3 end");
	Expect(rl::MatchHud::CountdownDigit(120, 0) == 2, "2");
	Expect(rl::MatchHud::CountdownDigit(240, 0) == 1, "1");
	Expect(rl::MatchHud::CountdownDigit(359, 0) == 1, "1 end");
	Expect(rl::MatchHud::CountdownDigit(360, 0) == 0, "done");
	Expect(rl::MatchHud::CountdownDigit(500, 400) == 3, "rearm");
	Expect(rl::MatchHud::CountdownDigit(10, 20) == 0, "before kickoff");
	if (fails) {
		std::fprintf(stderr, "MATCH_HUD FAIL %d\n", fails);
		return 1;
	}
	std::printf("MATCH_HUD PASS\n");
	return 0;
}
