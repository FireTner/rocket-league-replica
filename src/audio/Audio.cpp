// Audio.cpp — procedural synth implementation (MIT).
#include "Audio.h"

#include <cmath>
#include <cstdio>

namespace rl {

namespace {
constexpr int kRate = 44100;
constexpr float kMaster = 0.30f;

// One-shot recipes: {freq0, freq1, durSeconds, kind} kind 0=sine,1=square-ish.
struct Recipe { float f0, f1, dur; int kind; };
Recipe recipeFor(int type) {
    switch (type) {
        case 1: return {220, 440, 0.10f, 0};   // jump sweep up
        case 2: return {880, 1320, 0.12f, 0};  // small pad blip
        case 3: return {660, 990, 0.20f, 0};   // big pad blip (lower, longer)
        case 4: return {523, 784, 0.80f, 1};   // goal horn-ish swell
        case 5: return {180, 40, 0.45f, 1};    // demolition: low descending boom
        default: return {440, 440, 0.05f, 0};
    }
}
} // namespace

Audio::Audio() = default;

Audio::~Audio() { Shutdown(); }

bool Audio::Init() {
    if (device_ != 0) return true;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::fprintf(stderr, "[audio] SDL_INIT_AUDIO failed (%s) — silent\n", SDL_GetError());
        return false;
    }
    blipLock_ = SDL_CreateMutex();
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = kRate;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = &Audio::Cb;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        std::fprintf(stderr, "[audio] unavailable (%s) — silent\n", SDL_GetError());
        return false;
    }
    if (have.freq != kRate || have.format != AUDIO_F32SYS || have.channels != 1) {
        std::fprintf(stderr, "[audio] want 44100/f32/mono, got %d/%d/%d — silent\n",
            have.freq, (int)have.format, (int)have.channels);
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return false;
    }
    SDL_PauseAudioDevice(device_, 0);
    std::fprintf(stderr, "[audio] synth on (44100Hz f32 mono, procedural, no assets)\n");
    return true;
}

void Audio::Shutdown() {
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    if (blipLock_) {
        SDL_DestroyMutex(blipLock_);
        blipLock_ = nullptr;
    }
}

void Audio::Engine(float speed01, bool boosting) {
    if (speed01 < 0) speed01 = 0;
    if (speed01 > 1) speed01 = 1;
    speed01_.store(speed01);
    boosting_.store(boosting);
}

void Audio::PlayJump() {
    if (!blipLock_) return;
    SDL_LockMutex(blipLock_);
    for (int i = 0; i < kBlips; i++) {
        if (blips_[i].type == 0) {
            blips_[i].type = 1;
            blips_[i].pos = 0;
            break;
        }
    }
    SDL_UnlockMutex(blipLock_);
}

void Audio::PlayPickup(bool big) {
    if (!blipLock_) return;
    SDL_LockMutex(blipLock_);
    for (int i = 0; i < kBlips; i++) {
        if (blips_[i].type == 0) {
            blips_[i].type = big ? 3 : 2;
            blips_[i].pos = 0;
            break;
        }
    }
    SDL_UnlockMutex(blipLock_);
}

void Audio::PlayGoal() {
    if (!blipLock_) return;
    SDL_LockMutex(blipLock_);
    for (int i = 0; i < kBlips; i++) {
        if (blips_[i].type == 0) {
            blips_[i].type = 4;
            blips_[i].pos = 0;
            break;
        }
    }
    SDL_UnlockMutex(blipLock_);
}

void Audio::PlayDemo() {
    if (!blipLock_) return;
    SDL_LockMutex(blipLock_);
    for (int i = 0; i < kBlips; i++) {
        if (blips_[i].type == 0) {
            blips_[i].type = 5;
            blips_[i].pos = 0;
            break;
        }
    }
    SDL_UnlockMutex(blipLock_);
}

void Audio::SDLCALL Audio::Cb(void* userdata, Uint8* stream, int len) {
    Audio* self = (Audio*)userdata;
    float* out = (float*)stream;
    int n = len / (int)sizeof(float);
    bool muted = self->muted_.load();
    float spd = self->speed01_.load();
    bool boosting = self->boosting_.load();

    // Engine: saw-ish 55+165*spd Hz + sub sine + breath noise.
    float freq = 55.0f + 165.0f * spd;
    float engGain = muted ? 0.0f : (0.10f + 0.12f * spd) * kMaster;

    SDL_LockMutex(self->blipLock_);
    Blip active[kBlips];
    for (int i = 0; i < kBlips; i++) active[i] = self->blips_[i];
    SDL_UnlockMutex(self->blipLock_);

    for (int i = 0; i < n; i++) {
        self->phase_++;
        float t = (float)self->phase_ / kRate;
        // xorshift noise
        self->noise_ ^= self->noise_ << 13;
        self->noise_ ^= self->noise_ >> 17;
        self->noise_ ^= self->noise_ << 5;
        float nz = ((self->noise_ >> 9) & 0x7FFF) / 16384.0f - 1.0f;

        float ph = t * freq;
        float saw = 2.0f * (ph - std::floor(ph)) - 1.0f;
        float sub = std::sin(2.0f * 3.14159265f * ph * 0.5f);
        float s = engGain * (0.6f * saw + 0.4f * sub) + engGain * 0.15f * nz;

        if (boosting && !muted)
            s += 0.25f * kMaster * nz; // boost hiss

        // One-shots.
        for (int b = 0; b < kBlips; b++) {
            if (active[b].type == 0) continue;
            Recipe rc = recipeFor(active[b].type);
            float bt = (float)active[b].pos / kRate;
            float dur = rc.dur;
            if (bt >= dur) {
                active[b].type = 0;
                continue;
            }
            float f = rc.f0 + (rc.f1 - rc.f0) * (bt / dur);
            float env = 1.0f - bt / dur;
            float osc = rc.kind == 0
                ? std::sin(2.0f * 3.14159265f * f * bt)
                : (std::fmod(2.0f * f * bt, 2.0f) < 1.0f ? 0.6f : -0.6f);
            if (!muted) s += 0.5f * kMaster * env * osc;
            active[b].pos++;
        }
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = s;
    }

    SDL_LockMutex(self->blipLock_);
    for (int i = 0; i < kBlips; i++) self->blips_[i] = active[i];
    SDL_UnlockMutex(self->blipLock_);
    self->callbacks_.fetch_add(1);
    self->framesMixed_.fetch_add((uint64_t)n);
}

} // namespace rl
