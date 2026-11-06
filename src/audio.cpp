#include "audio.h"
#include <cmath>
#include <vector>

static const int RATE = 48000;

static void SDLCALL audioCallback(void* ud, SDL_AudioStream* s, int additional, int) {
    Audio* a = static_cast<Audio*>(ud);
    int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) return;
    static thread_local std::vector<float> buf;
    buf.resize(size_t(frames) * 2);
    a->generate(buf.data(), frames);
    SDL_PutAudioStreamData(s, buf.data(), frames * int(sizeof(float)) * 2);
}

bool Audio::init() {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = RATE;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, this);
    if (!stream) {
        SDL_Log("Audio disabled: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void Audio::shutdown() {
    if (stream) SDL_DestroyAudioStream(stream);
    stream = nullptr;
}

void Audio::generate(float* out, int frames) {
    const float dt = 1.f / RATE;
    float tRpm = rpm, tThr = throttle, tSlip = slip, tSpeed = speed, vol = master, on = engineOn;
    int cyl = cylinders;
    float req = impactReq.exchange(0.f);
    if (req > 0) impactEnv = std::min(1.f, impactEnv + req);
    float bf = beepFreq.exchange(0.f);
    if (bf > 0) { beepF = bf; beepEnv = 1.f; }
    float bl = beepLen;
    for (int i = 0; i < frames; i++) {
        smRpm += (tRpm - smRpm) * 0.0015f;
        smThr += (tThr - smThr) * 0.001f;
        smSlip += (tSlip - smSlip) * 0.0008f;
        smSpeed += (tSpeed - smSpeed) * 0.0005f;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float noise = (rng & 0xFFFF) / 32768.f - 1.f;

        // ---- engine: firing-frequency sawtooth through a throttle-controlled low-pass + sub rumble
        float fire = smRpm / 60.f * cyl * 0.5f;
        phase += fire * dt;
        if (phase > 1.0) phase -= 1.0;
        subPhase += fire * 0.5 * dt;
        if (subPhase > 1.0) subPhase -= 1.0;
        float saw = float(phase) * 2.f - 1.f;
        float pulse = std::exp(-float(phase) * 6.f) * 2.f - 0.6f;
        float raw = saw * 0.45f + pulse * 0.55f + noise * 0.12f * (0.4f + smThr);
        float cutoff = 0.05f + smThr * 0.18f + smRpm / 9000.f * 0.12f;
        lp += (raw - lp) * cutoff;
        lp2 += (lp - lp2) * cutoff;
        float sub = std::sin(float(subPhase) * 6.2831853f) * 0.35f;
        float engine = (lp2 * 1.4f + sub * (0.6f + smThr * 0.4f)) * (0.32f + smThr * 0.5f) * on;

        // ---- tyres: band-passed noise
        float f = 0.14f;
        bp1 += f * (noise - bp1 - bp2 * 0.5f);
        bp2 += f * bp1;
        float squeal = bp2 * 0.5f * smSlip * smSlip;

        // ---- wind
        windLp += (noise - windLp) * 0.02f;
        float wind = windLp * std::min(1.f, smSpeed / 70.f) * 0.5f;

        // ---- impact
        float imp = 0;
        if (impactEnv > 0.001f) {
            noiseLp += (noise - noiseLp) * 0.05f;
            imp = noiseLp * impactEnv * 2.5f;
            impactEnv *= 0.9993f;
        }
        // ---- UI beep
        float bp = 0;
        if (beepEnv > 0.001f) {
            beepPh += beepF * dt;
            if (beepPh > 1) beepPh -= 1;
            bp = std::sin(beepPh * 6.2831853f) * beepEnv * 0.25f;
            beepEnv -= dt / std::max(bl, 0.02f);
            if (beepEnv < 0) beepEnv = 0;
        }
        float mix = (engine + squeal + wind + imp + bp) * vol;
        mix = std::tanh(mix);
        out[i * 2] = mix;
        out[i * 2 + 1] = mix;
    }
}
