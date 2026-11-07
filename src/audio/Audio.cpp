// Audio.cpp — miniaudio engine at 48 kHz. Pack clips stay at their native pitch.
#include "Audio.h"

#include "miniaudio_config.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace rl {

float AudioDistanceGain(float distUu) {
    constexpr float kRefDist = 800.0f;
    constexpr float kMinGain = 0.20f;
    if (distUu < kRefDist) distUu = kRefDist;
    float g = kRefDist / distUu;
    if (g < kMinGain) g = kMinGain;
    return g;
}

float AudioDopplerRatio(float radialSpeedUu) {
    constexpr float kSpeedOfSound = 34300.0f;
    float denom = kSpeedOfSound + radialSpeedUu;
    if (denom < 1000.0f) denom = 1000.0f;
    float ratio = kSpeedOfSound / denom;
    if (ratio < 0.85f) ratio = 0.85f;
    if (ratio > 1.15f) ratio = 1.15f;
    return ratio;
}

namespace {
constexpr int kRate = 48000;
constexpr unsigned kPeriod = 256; // 5.3 ms at 48 kHz. The device may round this up.
constexpr float kMaster = 0.30f;
constexpr float kClipGain = 0.55f;
constexpr int kVoices = 2;
constexpr int kBlips = 8;
constexpr unsigned kFadeMs = 6;

struct Recipe { float f0, f1, dur; int kind; };
Recipe recipeFor(int type) {
    switch (type) {
        case 1: return {220, 440, 0.10f, 0};
        case 2: return {880, 1320, 0.12f, 0};
        case 3: return {660, 990, 0.20f, 0};
        case 4: return {523, 784, 0.80f, 1};
        case 5: return {180, 40, 0.45f, 1};
        default: return {440, 440, 0.05f, 0};
    }
}

constexpr int kCueNames = 14;
constexpr int kLoopNames = 7;

int cueIndex(const std::string& name) {
    static const char* names[kCueNames] = {
        "jump", "dodge", "double_jump", "pickup", "pickup_big",
        "land", "ball", "slide", "super",
        "goal_horn", "goal_boom", "goal_crowd",
        "demo_hit", "demo_boom"
    };
    for (int i = 0; i < kCueNames; i++) {
        if (name == names[i]) return i;
    }
    return -1;
}

int loopIndex(const std::string& name) {
    static const char* names[kLoopNames] = {
        "boost", "tires", "whoosh", "super_loop",
        "ball_roll", "ball_fly", "crowd"
    };
    for (int i = 0; i < kLoopNames; i++) {
        if (name == names[i]) return i;
    }
    return -1;
}

float clamp01(float v) {
    if (v < 0) return 0;
    if (v > 1) return 1;
    return v;
}

// Native rate when the file is already 48 kHz, so those clips are not low-passed again.
std::vector<float> loadWav(const std::string& path) {
    std::vector<float> out;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
    ma_decoder dec;
    if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) {
        std::fprintf(stderr, "[audio] wav %s: decode failed\n", path.c_str());
        return out;
    }
    if (dec.outputSampleRate != (ma_uint32)kRate) {
        ma_decoder_uninit(&dec);
        cfg = ma_decoder_config_init(ma_format_f32, 1, kRate);
        cfg.resampling.linear.lpfOrder = 4;
        if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) {
            std::fprintf(stderr, "[audio] wav %s: resample failed\n", path.c_str());
            return out;
        }
    }
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &frames);
    if (frames < 2 || frames > (ma_uint64)kRate * 180) {
        std::fprintf(stderr, "[audio] wav %s: %llu frames\n",
            path.c_str(), (unsigned long long)frames);
        ma_decoder_uninit(&dec);
        return out;
    }
    out.resize((size_t)frames);
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&dec, out.data(), frames, &got);
    ma_decoder_uninit(&dec);
    if (got < 2) {
        out.clear();
        return out;
    }
    out.resize((size_t)got);
    return out;
}
} // namespace

struct Audio::Impl {
    struct Blip {
        int type = 0;
        uint32_t pos = 0;
    };
    struct Clip {
        std::vector<float> pcm;
        ma_audio_buffer buffer{};
        ma_sound voice[kVoices]{};
        int voices = 0;
        int cursor = 0;
        bool bufferReady = false;
        bool voiceReady[kVoices] = {};
    };

    struct Synth {
        ma_node_base base;
        Audio* owner = nullptr;
        bool ready = false;
    };

    Audio* owner = nullptr;
    ma_engine engine{};
    Synth synth{};
    bool engineReady = false;
    int channels = 2;
    std::deque<Clip> clips; // ma_sound is not movable; deque growth leaves elements put.
    std::mutex blipLock;
    Blip blips[kBlips]{};
    uint32_t phase = 0;
    uint32_t noise = 0x12345678;
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    for (int i = 0; i < LoopCount; i++) {
        loopClip_[i] = -1;
        loopGain_[i].store(0);
        loopApplied_[i] = 0;
    }
}

Audio::~Audio() { Shutdown(); }

void Audio::SynthProcess(void* node, const float** /*in*/, uint32_t* /*inCount*/,
                          float** outBuses, uint32_t* outCount) {
    Audio* self = ((Impl::Synth*)node)->owner;
    Impl* impl = self->impl_.get();
    int ch = impl->channels > 0 ? impl->channels : 1;
    float* frames = outBuses[0];
    uint64_t frameCount = *outCount;
    bool muted = self->muted_.load();
    float spd = self->speed01_.load();
    bool boosting = self->boosting_.load();
    // The saw-plus-noise bed is a constant hiss under the pack. It only plays with --synth.
    float engScale = self->pack_ ? 0.0f : 1.0f;
    float carGain = AudioDistanceGain(self->carDist_.load());
    float ballGain = AudioDistanceGain(self->ballDist_.load());
    float freq = (55.0f + 165.0f * spd) * AudioDopplerRatio(self->radialSpeed_.load());
    float engGain = muted ? 0.0f : (0.10f + 0.12f * spd) * kMaster * engScale * carGain;
    bool boostClip = self->loopClip_[LoopBoost] >= 0;

    Impl::Blip active[kBlips];
    {
        std::lock_guard<std::mutex> lock(impl->blipLock);
        for (int i = 0; i < kBlips; i++) active[i] = impl->blips[i];
    }

    int n = (int)frameCount;
    for (int i = 0; i < n; i++) {
        impl->phase++;
        float t = (float)impl->phase / kRate;
        impl->noise ^= impl->noise << 13;
        impl->noise ^= impl->noise >> 17;
        impl->noise ^= impl->noise << 5;
        float nz = ((impl->noise >> 9) & 0x7FFF) / 16384.0f - 1.0f;
        float ph = t * freq;
        float saw = 2.0f * (ph - std::floor(ph)) - 1.0f;
        float sub = std::sin(2.0f * 3.14159265f * ph * 0.5f);
        float s = engGain * (0.6f * saw + 0.4f * sub) + engGain * 0.15f * nz;
        if (boosting && !muted && !boostClip)
            s += 0.25f * kMaster * carGain * nz;
        for (int b = 0; b < kBlips; b++) {
            if (active[b].type == 0) continue;
            Recipe rc = recipeFor(active[b].type);
            float bt = (float)active[b].pos / kRate;
            if (bt >= rc.dur) {
                active[b].type = 0;
                continue;
            }
            float f = rc.f0 + (rc.f1 - rc.f0) * (bt / rc.dur);
            float env = 1.0f - bt / rc.dur;
            float osc = rc.kind == 0
                ? std::sin(2.0f * 3.14159265f * f * bt)
                : (std::fmod(2.0f * f * bt, 2.0f) < 1.0f ? 0.6f : -0.6f);
            // Goal horn is the ball one-shot; other blips stay at the car.
            float srcGain = active[b].type == 4 ? ballGain : carGain;
            if (!muted) s += 0.5f * kMaster * srcGain * env * osc;
            active[b].pos++;
        }
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        if (muted) s = 0;
        for (int c = 0; c < ch; c++) frames[i * ch + c] = s;
    }

    {
        std::lock_guard<std::mutex> lock(impl->blipLock);
        for (int i = 0; i < kBlips; i++) impl->blips[i] = active[i];
    }
    self->callbacks_.fetch_add(1);
    self->framesMixed_.fetch_add(frameCount);
}

bool Audio::LoadPack(const char* dir) {
    static_assert(CueCount == kCueNames, "cue name table");
    static_assert(LoopCount == kLoopNames, "loop name table");
    if (!dir || !dir[0] || !impl_ || impl_->engineReady) return false;
    namespace fs = std::filesystem;
    fs::path root(dir);
    fs::path manifest = root / "manifest.txt";
    if (!fs::is_regular_file(manifest)) return false;
    std::ifstream in(manifest);
    if (!in) return false;
    int cuesLoaded = 0;
    int loopsLoaded = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::string name, mode;
        std::vector<std::string> files;
        size_t i = 0;
        auto token = [&](std::string* out) {
            while (i < line.size() && line[i] == ' ') i++;
            size_t j = i;
            while (j < line.size() && line[j] != ' ') j++;
            if (j == i) return false;
            *out = line.substr(i, j - i);
            i = j;
            return true;
        };
        if (!token(&name) || !token(&mode)) continue;
        std::string file;
        while (token(&file)) files.push_back(file);
        if (files.empty() || (mode != "pick" && mode != "layer" && mode != "loop")) {
            std::fprintf(stderr, "[audio] bad manifest line: %s\n", name.c_str());
            continue;
        }
        int ids[kCueClips];
        int n = 0;
        for (const std::string& rel : files) {
            if (n >= kCueClips) break;
            if (rel.find('/') != std::string::npos || rel.find('\\') != std::string::npos) {
                std::fprintf(stderr, "[audio] rejected path %s\n", rel.c_str());
                continue;
            }
            std::vector<float> pcm = loadWav((root / rel).string());
            if (pcm.empty()) continue;
            ids[n++] = (int)impl_->clips.size();
            impl_->clips.emplace_back();
            impl_->clips.back().pcm = std::move(pcm);
        }
        if (!n) continue;
        int loop = loopIndex(name);
        if (loop >= 0) {
            int best = ids[0];
            for (int k = 1; k < n; k++) {
                if (impl_->clips[(size_t)ids[k]].pcm.size() >
                    impl_->clips[(size_t)best].pcm.size())
                    best = ids[k];
            }
            loopClip_[loop] = best;
            loopsLoaded++;
            continue;
        }
        int cue = cueIndex(name);
        if (cue < 0) {
            std::fprintf(stderr, "[audio] unknown cue %s\n", name.c_str());
            continue;
        }
        cues_[cue].layer = mode == "layer";
        cues_[cue].n = n;
        for (int k = 0; k < n; k++) cues_[cue].clips[k] = ids[k];
        cuesLoaded++;
    }
    pack_ = cuesLoaded > 0 || loopsLoaded > 0;
    if (pack_) {
        std::fprintf(stderr, "[audio] sound pack %s (%d cues, %d loops, %d clips)\n",
            dir, cuesLoaded, loopsLoaded, (int)impl_->clips.size());
    }
    return pack_;
}

void Audio::Upload() {
    const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
    for (size_t i = 0; i < impl_->clips.size(); i++) {
        Impl::Clip& clip = impl_->clips[i];
        if (clip.pcm.size() < 2) continue;
        bool loop = false;
        for (int L = 0; L < LoopCount; L++) {
            if (loopClip_[L] == (int)i) loop = true;
        }
        ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
            ma_format_f32, 1, (ma_uint64)clip.pcm.size(), clip.pcm.data(), nullptr);
        cfg.sampleRate = kRate;
        if (ma_audio_buffer_init(&cfg, &clip.buffer) != MA_SUCCESS) continue;
        clip.bufferReady = true;
        int voices = loop ? 1 : kVoices;
        ma_uint32 voiceFlags = loop ? (flags | MA_SOUND_FLAG_LOOPING) : flags;
        for (int v = 0; v < voices; v++) {
            if (ma_sound_init_from_data_source(&impl_->engine, &clip.buffer, voiceFlags,
                    nullptr, &clip.voice[v]) != MA_SUCCESS)
                break;
            clip.voiceReady[v] = true;
            clip.voices++;
            ma_sound_set_volume(&clip.voice[v], loop ? 0.0f : kClipGain);
        }
        // The crowd bed is a 70 s noise floor. Leave it stopped.
        if (loop && clip.voiceReady[0] && loopClip_[LoopCrowd] != (int)i)
            ma_sound_start(&clip.voice[0]);
    }
}

bool Audio::Init() {
    if (open_) return true;
    if (!impl_) return false;
    ma_engine_config cfg = ma_engine_config_init();
    cfg.sampleRate = kRate;
    cfg.channels = 2;
    cfg.periodSizeInFrames = kPeriod;
    cfg.periodSizeInMilliseconds = 0;
    cfg.monoExpansionMode = ma_mono_expansion_mode_duplicate;
    if (ma_engine_init(&cfg, &impl_->engine) != MA_SUCCESS) {
        std::fprintf(stderr, "[audio] miniaudio device failed — silent\n");
        return false;
    }
    impl_->engineReady = true;
    impl_->channels = (int)ma_engine_get_channels(&impl_->engine);
    {
        static const ma_node_vtable vtable = {&Audio::SynthProcess, nullptr, 0, 1, 0};
        ma_uint32 channels = (ma_uint32)impl_->channels;
        ma_node_config nodeCfg = ma_node_config_init();
        nodeCfg.vtable = &vtable;
        nodeCfg.pOutputChannels = &channels;
        impl_->synth.owner = this;
        if (ma_node_init(ma_engine_get_node_graph(&impl_->engine), &nodeCfg, nullptr,
                &impl_->synth.base) == MA_SUCCESS &&
            ma_node_attach_output_bus(&impl_->synth.base, 0,
                ma_node_graph_get_endpoint(ma_engine_get_node_graph(&impl_->engine)), 0) == MA_SUCCESS) {
            impl_->synth.ready = true;
        }
    }
    ma_engine_set_volume(&impl_->engine, muted_.load() ? 0.0f : 1.0f);
    if (pack_) Upload();
    open_ = true;
    unsigned rate = kRate;
    unsigned period = kPeriod;
    if (impl_->engine.pDevice) {
        rate = impl_->engine.pDevice->sampleRate;
        if (impl_->engine.pDevice->playback.internalPeriodSizeInFrames)
            period = impl_->engine.pDevice->playback.internalPeriodSizeInFrames;
    }
    std::fprintf(stderr, "[audio] miniaudio %u Hz, period %u frames (%.1f ms), %s\n",
        rate, period, 1000.0 * period / (rate ? rate : 1),
        pack_ ? "sound pack" : "procedural, no assets");
    return true;
}

void Audio::Shutdown() {
    if (!impl_) return;
    if (impl_->engineReady) {
        ma_engine_stop(&impl_->engine);
        if (impl_->synth.ready) {
            ma_node_uninit(&impl_->synth.base, nullptr);
            impl_->synth.ready = false;
        }
        for (Impl::Clip& clip : impl_->clips) {
            for (int v = 0; v < kVoices; v++) {
                if (!clip.voiceReady[v]) continue;
                ma_sound_uninit(&clip.voice[v]);
                clip.voiceReady[v] = false;
            }
            if (clip.bufferReady) {
                ma_audio_buffer_uninit(&clip.buffer);
                clip.bufferReady = false;
            }
        }
        ma_engine_uninit(&impl_->engine);
        impl_->engineReady = false;
    }
    open_ = false;
}

void Audio::SetMuted(bool m) {
    muted_.store(m);
    if (impl_ && impl_->engineReady)
        ma_engine_set_volume(&impl_->engine, m ? 0.0f : 1.0f);
}

void Audio::Engine(float speed01, bool boosting, float carDistUu,
                   float ballDistUu, float radialSpeedUu) {
    speed01_.store(clamp01(speed01));
    boosting_.store(boosting);
    carDist_.store(carDistUu);
    ballDist_.store(ballDistUu);
    radialSpeed_.store(radialSpeedUu);
}

void Audio::Loops(float boost, float tires, float whoosh, float superSonic,
                  float ballRoll, float ballFly) {
    float gain[6] = {clamp01(boost), clamp01(tires), clamp01(whoosh),
        clamp01(superSonic), clamp01(ballRoll), clamp01(ballFly)};
    for (int i = 0; i < 6; i++) {
        loopGain_[i].store(gain[i]);
        if (!impl_ || !impl_->engineReady || loopClip_[i] < 0) continue;
        Impl::Clip& clip = impl_->clips[(size_t)loopClip_[i]];
        if (!clip.voiceReady[0]) continue;
        float spatial = (i >= LoopBallRoll) ? AudioDistanceGain(ballDist_.load())
                                           : AudioDistanceGain(carDist_.load());
        float vol = gain[i] * kClipGain * spatial;
        if (std::fabs(loopApplied_[i] - vol) < 0.02f) continue;
        ma_sound_set_fade_in_milliseconds(&clip.voice[0], loopApplied_[i], vol, kFadeMs);
        loopApplied_[i] = vol;
    }
}

void Audio::EnqueueBlip(int type) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->blipLock);
    for (int i = 0; i < kBlips; i++) {
        if (impl_->blips[i].type == 0) {
            impl_->blips[i].type = type;
            impl_->blips[i].pos = 0;
            break;
        }
    }
}

bool Audio::StartCue(int cue) {
    if (!pack_ || !impl_ || !impl_->engineReady || cue < 0 || cue >= CueCount || cues_[cue].n <= 0)
        return false;
    int n = cues_[cue].n;
    int first = cues_[cue].layer ? 0 : (shotCursor_++ % n);
    int last = cues_[cue].layer ? n : first + 1;
    bool started = false;
    for (int c = first; c < last; c++) {
        Impl::Clip& clip = impl_->clips[(size_t)cues_[cue].clips[c]];
        if (clip.voices <= 0) continue;
        int v = clip.cursor++ % clip.voices;
        if (!clip.voiceReady[v]) continue;
        ma_sound_seek_to_pcm_frame(&clip.voice[v], 0);
        if (ma_sound_start(&clip.voice[v]) == MA_SUCCESS) started = true;
    }
    return started;
}

void Audio::PlayJump() {
    if (!StartCue(CueJump)) EnqueueBlip(1);
}
void Audio::PlayDodge() { StartCue(CueDodge); }
void Audio::PlayDoubleJump() { StartCue(CueDoubleJump); }
void Audio::PlayPickup(bool big) {
    if (!StartCue(big ? CuePickupBig : CuePickup)) EnqueueBlip(big ? 3 : 2);
}
void Audio::PlayGoal() {
    bool horn = StartCue(CueGoalHorn);
    bool boom = StartCue(CueGoalBoom);
    bool crowd = StartCue(CueGoalCrowd);
    if (!horn && !boom && !crowd) EnqueueBlip(4);
}
void Audio::PlayDemo() {
    bool hit = StartCue(CueDemoHit);
    bool boom = StartCue(CueDemoBoom);
    if (!hit && !boom) EnqueueBlip(5);
}
void Audio::PlayBall() { StartCue(CueBall); }
void Audio::PlayLand() { StartCue(CueLand); }
void Audio::PlaySuper() { StartCue(CueSuper); }
void Audio::PlaySlide() { SetSlide(true); }

void Audio::SetSlide(bool held) {
    if (held == slideHeld_) return;
    slideHeld_ = held;
    if (!impl_ || !impl_->engineReady || cues_[CueSlide].n <= 0) return;
    if (!held) {
        for (int c = 0; c < cues_[CueSlide].n; c++) {
            Impl::Clip& clip = impl_->clips[(size_t)cues_[CueSlide].clips[c]];
            for (int v = 0; v < clip.voices; v++) {
                if (!clip.voiceReady[v]) continue;
                ma_sound_set_fade_in_milliseconds(&clip.voice[v], -1.0f, 0.0f, 40);
            }
        }
        return;
    }
    int n = cues_[CueSlide].n;
    Impl::Clip& clip = impl_->clips[(size_t)cues_[CueSlide].clips[shotCursor_++ % n]];
    if (clip.voices <= 0) return;
    int v = clip.cursor++ % clip.voices;
    if (!clip.voiceReady[v]) return;
    ma_sound_set_volume(&clip.voice[v], 0.22f);
    ma_sound_seek_to_pcm_frame(&clip.voice[v], 0);
    ma_sound_start(&clip.voice[v]);
}

} // namespace rl
