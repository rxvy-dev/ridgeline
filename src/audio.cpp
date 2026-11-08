#include "audio.h"
#include "mathx.h"
#include <cmath>
#include <cstring>
#include <string>
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO_DEPRECATION
#include "dr_mp3.h"
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

static bool fileExists(const std::string& p) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

// Loads the secret car's song: a .wav or .mp3 next to the car (same name, either extension).
bool Audio::loadSong(const std::string& path) {
    std::string stem = path.substr(0, path.find_last_of('.'));
    std::vector<std::string> tries = {path, stem + ".wav", stem + ".mp3"};
    SDL_AudioSpec want{SDL_AUDIO_F32, 1, RATE};
    std::vector<float> out;
    for (const auto& p : tries) {
        if (!fileExists(p)) continue;
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 len = 0;
        bool mp3 = p.size() > 4 && (p.compare(p.size() - 4, 4, ".mp3") == 0 || p.compare(p.size() - 4, 4, ".MP3") == 0);
        float* pcm = nullptr;
        if (mp3) {
            drmp3_config cfg;
            drmp3_uint64 frames = 0;
            pcm = drmp3_open_file_and_read_pcm_frames_f32(p.c_str(), &cfg, &frames, nullptr);
            if (!pcm) continue;
            spec = SDL_AudioSpec{SDL_AUDIO_F32, int(cfg.channels), int(cfg.sampleRate)};
            data = reinterpret_cast<Uint8*>(pcm);
            len = Uint32(frames * cfg.channels * sizeof(float));
        } else if (!SDL_LoadWAV(p.c_str(), &spec, &data, &len)) {
            continue;
        }
        Uint8* conv = nullptr;
        int clen = 0;
        bool ok = SDL_ConvertAudioSamples(&spec, data, int(len), &want, &conv, &clen);
        if (mp3) drmp3_free(pcm, nullptr); else SDL_free(data);
        if (!ok) continue;
        out.resize(size_t(clen) / sizeof(float));
        std::memcpy(out.data(), conv, out.size() * sizeof(float));
        SDL_free(conv);
        SDL_Log("Song loaded: %s (%.1f s)", p.c_str(), out.size() / float(RATE));
        break;
    }
    if (stream) SDL_LockAudioStream(stream);
    song.swap(out);
    songPos = 0;
    if (stream) SDL_UnlockAudioStream(stream);
    return !song.empty();
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
        // ---- horn: two detuned square waves through a soft low-pass
        float hn = 0;
        hornEnv += ((horn ? 1.f : 0.f) - hornEnv) * 0.004f;
        if (hornEnv > 0.001f) {
            hornPh1 += 392.f * dt; if (hornPh1 > 1) hornPh1 -= 1;
            hornPh2 += 494.f * dt; if (hornPh2 > 1) hornPh2 -= 1;
            float sq = (hornPh1 < 0.5f ? 1.f : -1.f) + (hornPh2 < 0.5f ? 1.f : -1.f);
            hornLp += (sq - hornLp) * 0.18f;
            hn = hornLp * 0.16f * hornEnv;
        }
        // ---- secret car song
        float sg = 0;
        songEnv += ((songOn ? 1.f : 0.f) - songEnv) * 0.002f;
        if (songRestart.exchange(0)) { songPos = 0; chantT = 0; }
        if (songEnv > 0.001f) {
            if (!song.empty()) {
                sg = song[songPos] * 0.9f * songEnv;
                if (++songPos >= song.size()) songPos = 0;
            }
        }
        // ---- time machine: charging whine + crackle, then a boom
        float tm = 0;
        chargeSm += (charge - chargeSm) * 0.0005f;
        if (chargeSm > 0.01f) {
            chargePh += (300.f + 2400.f * chargeSm * chargeSm) * dt;
            if (chargePh > 1) chargePh -= 1;
            float crackle = (rng & 0x7F) == 0 ? noise * 3.f : 0.f;
            tm += (std::sin(chargePh * 6.2831853f) * 0.08f + crackle * 0.25f * chargeSm) * chargeSm;
        }
        float br = boomReq.exchange(0.f);
        if (br > 0) { boomEnv = 1.f; boomPh = 0; }
        if (boomEnv > 0.0005f) {
            boomPh += dt;
            boomLp += (noise - boomLp) * 0.05f;
            float thump = std::sin(6.2831853f * (55.f - 20.f * boomPh) * boomPh) * std::exp(-boomPh * 5.f);
            tm += (boomLp * 1.6f + thump * 0.9f) * boomEnv;
            boomEnv *= 0.99993f;
        }
        // ---- rain: hiss plus the odd louder drop on the roof
        float rn = 0;
        float ra = rain.load(std::memory_order_relaxed);
        if (ra > 0.01f) {
            rainLp += (noise - rainLp) * 0.35f;
            rainHp += (rainLp - rainHp) * 0.02f;
            if ((rng & 0xFFF) < 6) dropEnv = 1.f;
            dropEnv *= 0.995f;
            rn = ((rainLp - rainHp) * 0.16f + (rainLp - rainHp) * dropEnv * 0.25f) * ra;
        }
        float mix = (engine + squeal + wind + imp + bp + hn + sg + tm + rn) * vol;
        mix = std::tanh(mix);
        out[i * 2] = mix;
        out[i * 2 + 1] = mix;
    }
}
