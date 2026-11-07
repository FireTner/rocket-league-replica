#include "render/MatchHud.h"

#include "render/Renderer.h"

namespace rl {

void MatchHud::Draw(Renderer& renderer, uint64_t tick, uint64_t kickoffTick,
	bool enabled) {
	if (!enabled)
		return;
	char clock[32];
	if (FormatClock(clock, (int)sizeof(clock), tick)) {
		renderer.FillRect2D(560, 16, 160, 28, 0.02f, 0.03f, 0.05f, 0.55f);
		renderer.DrawText2D(588, 22, clock, 3, 0.95f, 0.97f, 1.0f);
	}
	int digit = CountdownDigit(tick, kickoffTick);
	if (digit >= 1 && digit <= 3) {
		char num[2] = {char('0' + digit), 0};
		renderer.DrawText2D(600, 300, num, 8, 1.0f, 0.92f, 0.35f);
	}
}

} // namespace rl
