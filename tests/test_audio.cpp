// Spatial audio probe: UNVERIFIED gain/doppler curves + dummy-driver mute path.
#include "audio/Audio.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int fail(const char* msg) {
    std::fprintf(stderr, "FAIL %s\n", msg);
    return 1;
}

int main() {
    using rl::AudioDistanceGain;
    using rl::AudioDopplerRatio;
    if (std::fabs(AudioDistanceGain(100.0f) - 1.0f) > 1e-4f)
        return fail("near gain");
    if (std::fabs(AudioDistanceGain(800.0f) - 1.0f) > 1e-4f)
        return fail("ref gain");
    if (std::fabs(AudioDistanceGain(1600.0f) - 0.5f) > 1e-4f)
        return fail("double distance");
    if (std::fabs(AudioDistanceGain(8000.0f) - 0.20f) > 1e-4f)
        return fail("floor");
    if (std::fabs(AudioDopplerRatio(0.0f) - 1.0f) > 1e-4f)
        return fail("still doppler");
    float away = 34300.0f / (34300.0f + 2300.0f);
    if (std::fabs(AudioDopplerRatio(2300.0f) - away) > 1e-4f)
        return fail("recede doppler");
    float toward = 34300.0f / (34300.0f - 2300.0f);
    if (std::fabs(AudioDopplerRatio(-2300.0f) - toward) > 1e-4f)
        return fail("approach doppler");

    setenv("SDL_AUDIODRIVER", "dummy", 1);
    rl::Audio audio;
    if (!audio.Init())
        return fail("init");
    audio.Engine(1.0f, false, 800.0f, 4000.0f, 0.0f);
    audio.PlayGoal();
    SDL_Delay(300);
    audio.SetMuted(true);
    audio.Engine(1.0f, true, 800.0f, 800.0f, 0.0f);
    SDL_Delay(100);
    uint64_t cb = audio.callbacks();
    uint64_t fr = audio.framesMixed();
    bool muted = audio.Muted();
    audio.Shutdown();
    if (!muted)
        return fail("mute");
    if (cb == 0 || fr == 0)
        return fail("callback");
    std::printf("AUDIO PASS gain=1@800 0.5@1600 floor=0.20 doppler(2300)=%.5f "
                "callbacks=%llu frames=%llu mute=1\n",
                (double)away, (unsigned long long)cb, (unsigned long long)fr);
    return 0;
}
