// Procedural audio: engine synth, tyre squeal, wind, impacts and UI beeps.
#pragma once
#include <SDL3/SDL.h>
#include <atomic>

struct Audio {
    SDL_AudioStream* stream = nullptr;
    std::atomic<float> rpm{900}, throttle{0}, slip{0}, speed{0}, master{0.8f}, engineOn{1};
    std::atomic<int> cylinders{4};
    std::atomic<float> impactReq{0};
    std::atomic<float> beepFreq{0}, beepLen{0};
    std::atomic<int> horn{0};
    float hornEnv = 0, hornPh1 = 0, hornPh2 = 0, hornLp = 0;
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
