// MatchHud.h — free-play clock and kickoff countdown (presentation only).
// Time is sim ticks at 120 Hz. Nothing here steps, delays, or writes the sim.
#pragma once

#include <cstdint>
#include <cstdio>

namespace rl {

class Renderer;

struct MatchHud {
	static constexpr int kTicksPerSecond = 120;
	static constexpr int kCountdownSeconds = 3;

	// "mm:ss" from tick/120. Minutes grow past 99. Returns false if buf is short.
	static bool FormatClock(char* buf, int cap, uint64_t tick) {
		if (!buf || cap < 6)
			return false;
		uint64_t total = tick / (uint64_t)kTicksPerSecond;
		uint64_t minutes = total / 60ull;
		uint64_t seconds = total % 60ull;
		std::snprintf(buf, (size_t)cap, "%02llu:%02llu",
			(unsigned long long)minutes, (unsigned long long)seconds);
		return true;
	}
	// 3, 2, 1 while inside the kickoff window, otherwise 0.
	static int CountdownDigit(uint64_t tick, uint64_t kickoffTick) {
		if (tick < kickoffTick)
			return 0;
		uint64_t elapsed = tick - kickoffTick;
		uint64_t window = (uint64_t)kCountdownSeconds * (uint64_t)kTicksPerSecond;
		if (elapsed >= window)
			return 0;
		int second = (int)(elapsed / (uint64_t)kTicksPerSecond);
		return kCountdownSeconds - second;
	}
	// enabled=false skips every pixel (gated --shot-at-tick captures).
	static void Draw(Renderer& renderer, uint64_t tick, uint64_t kickoffTick,
		bool enabled);
};

} // namespace rl
