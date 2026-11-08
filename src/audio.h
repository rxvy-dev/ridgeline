// Procedural audio: engine synth, tyre squeal, wind, impacts and UI beeps.
#pragma once
#include <SDL3/SDL.h>
#include <atomic>
#include <string>
#include <vector>

struct Audio {
    SDL_AudioStream* stream = nullptr;
    std::atomic<float> rpm{900}, throttle{0}, slip{0}, speed{0}, master{0.8f}, engineOn{1};
    std::atomic<int> cylinders{4};
    std::atomic<float> impactReq{0};
    // exhaust: one-shot bangs, a crackle level and the rev-limiter cut
    std::atomic<float> popReq{0}, crackleLvl{0}, limiter{0}, electric{0};
    float evPh1 = 0, evPh2 = 0, evPh3 = 0, evLp = 0;
    float popEnv = 0, popPh = 0, popF = 70, popLp = 0, popLp2 = 0, limPh = 0;
    std::atomic<float> beepFreq{0}, beepLen{0};
    std::atomic<int> horn{0};
    std::atomic<float> rain{0};
    float rainLp = 0, rainHp = 0, dropEnv = 0;
    float hornEnv = 0, hornPh1 = 0, hornPh2 = 0, hornLp = 0;
    // secret-car song (a wav file next to the car, or a little vowel synth if there is none)
    std::vector<float> song;          // mono, 48 kHz
    std::atomic<int> songOn{0};
    std::atomic<int> songRestart{0};
    size_t songPos = 0;
    float songEnv = 0, chantT = 0, chantPh = 0, f1a = 0, f1b = 0, f2a = 0, f2b = 0;
    // time machine: rising charge whine and the boom when it jumps
    std::atomic<float> charge{0};
    std::atomic<float> boomReq{0};
    float chargeSm = 0, chargePh = 0, boomEnv = 0, boomLp = 0, boomPh = 0;
    bool loadSong(const std::string& path);
    // synth state (audio thread only)
    double phase = 0, subPhase = 0;
    float lp = 0, lp2 = 0, noiseLp = 0, bp1 = 0, bp2 = 0, windLp = 0;
    float smRpm = 900, smThr = 0, smSlip = 0, smSpeed = 0;
    float impactEnv = 0, beepEnv = 0, beepPh = 0, beepF = 0;
    uint32_t rng = 22222;

    bool init();
    void shutdown();
    void beep(float freq, float len) { beepFreq = freq; beepLen = len; }
    void thud(float strength) { impactReq = strength; }
    void generate(float* out, int frames);
};
