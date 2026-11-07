// Special cars and shortcuts: the time machine's 88 mph jump, the secret spinning
// cat (unlocked with a code typed into the hidden box in the Autoshow) and the
// quick car switcher.
#include "game.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

static const vec4 S_WHITE(1, 1, 1, 1);
static const vec4 S_DIM(1, 1, 1, 0.6f);
static const vec4 S_ACCENT(1.0f, 0.42f, 0.12f, 1.f);

static std::string carName(const CarSpec& s) { return s.make + " " + s.model; }

// ====================================================================== eras
void Game::buildEras() {
    eraAtmo[0] = dayAtmo;
    // the past: a warm, hazy small-town morning
    Atmosphere& a = eraAtmo[1];
    a = dayAtmo;
    a.sunDir = normalize(vec3(0.75f, 0.24f, 0.35f));
    a.sunColor = vec3(2.05f, 1.48f, 0.88f);
    a.skyTop = vec3(0.30f, 0.45f, 0.66f);
    a.skyHorizon = vec3(0.96f, 0.82f, 0.62f);
    a.fogColor = vec3(0.90f, 0.79f, 0.62f);
    a.fogDensity = 0.0014f;
    a.skyAmb = vec3(0.50f, 0.45f, 0.40f);
    a.groundAmb = vec3(0.33f, 0.26f, 0.16f);
    // the future: a dark, stormy afternoon
    Atmosphere& b = eraAtmo[2];
    b = dayAtmo;
    b.sunDir = normalize(vec3(-0.2f, 0.75f, 0.3f));
    b.sunColor = vec3(0.62f, 0.68f, 0.80f);
    b.skyTop = vec3(0.20f, 0.23f, 0.29f);
    b.skyHorizon = vec3(0.47f, 0.51f, 0.57f);
    b.fogColor = vec3(0.45f, 0.49f, 0.55f);
    b.fogDensity = 0.0024f;
    b.skyAmb = vec3(0.55f, 0.60f, 0.68f);
    b.groundAmb = vec3(0.20f, 0.21f, 0.23f);
}

std::string Game::eraDate(int era) const {
    if (era == 1) return "NOV 05 1955   06:00 AM";
    if (era == 2) return "OCT 21 2015   04:29 PM";
    static const std::time_t start = std::time(nullptr);  // game clock: steady even while recording
    std::time_t t = start + std::time_t(simTime);
    std::tm lt = *std::localtime(&t);
    static const char* mon[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int h = lt.tm_hour % 12;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %02d %04d   %02d:%02d %s", mon[lt.tm_mon], lt.tm_mday, lt.tm_year + 1900, h == 0 ? 12 : h, lt.tm_min,
                  lt.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

// ====================================================================== per-frame effects
void Game::updateSpecial(float dt) {
    bool driving = state == ST_FREEROAM || state == ST_RACE;
    const CarSpec& s = *player.car.spec;
    float u = player.car.forwardSpeed() * 3.6f;
    float sp = settings.mph ? u / 1.609344f : u;  // "88" in whatever unit the speedo shows

    // ---- time machine
    bool tmCar = s.fx == "timemachine";
    float target = 0;
    if (tmCar && driving && !rewinding) target = smoothstepf(66.f, 88.f, sp) * (player.car.grounded ? 1.f : 0.6f);
    tm.charge += (target - tm.charge) * damp(target > tm.charge ? 2.5f : 4.f, dt);
    if (sp < 76.f) tm.armed = true;
    audio.charge = driving ? tm.charge : 0.f;
    tm.panel = std::max(0.f, tm.panel - dt);
    if (tm.charge > 0.12f && driving) {
        // electricity crawling over the body
        mat4 B = player.car.bodyMatrix();
        const CarModel& m = *player.model;
        float L = s.length, W = s.width, H = s.height;
        int n = int(tm.charge * tm.charge * 5.f + fx.rnd());
        for (int i = 0; i < n; i++) {
            vec3 p(fx.rnd() - 0.5f, 0.25f + fx.rnd() * 0.75f, fx.rnd() - 0.5f);
            p = vec3(p.x * W, p.y * H, p.z * L);
            if (fx.rnd() < 0.5f) p.x = (p.x > 0 ? 0.5f : -0.5f) * W * 1.02f;  // stick to the flanks
            vec3 q = p + vec3((fx.rnd() - 0.5f) * 0.4f, (fx.rnd() - 0.5f) * 0.9f, (fx.rnd() - 0.5f) * 1.6f);
            vec4 a = B * vec4(p.x, p.y, p.z, 1), b = B * vec4(q.x, q.y, q.z, 1);
            fx.arc(vec3(a.x, a.y, a.z), vec3(b.x, b.y, b.z), 0.012f + 0.02f * tm.charge, vec3(0.6f, 0.85f, 1.6f) * (2.f + 3.f * tm.charge), 0.35f);
            if (fx.rnd() < 0.5f)
                fx.glow(vec3(b.x, b.y, b.z), player.car.vel * 0.6f + vec3(fx.rnd() - 0.5f, fx.rnd(), fx.rnd() - 0.5f) * 3.f, 0.25f, 0.05f,
                        vec3(1.2f, 1.8f, 3.5f), 6.f);
        }
        (void)m;
    }
    if (tmCar && driving && tm.armed && sp >= 88.f && tm.charge > 0.85f) {
        // 88: jump
        tm.armed = false;
        tm.lastEra = tm.era;
        tm.era = (tm.era + 1) % 3;
        tm.panel = 6.f;
        tm.trailT = 1.1f;
        tm.jumpT = 0;
        flash = 1.f;
        shake = std::max(shake, 0.7f);
        audio.boomReq = 1.f;
        mat4 B = player.car.bodyMatrix();
        for (int i = 0; i < 160; i++) {
            vec3 d = normalize(vec3(fx.rnd() - 0.5f, fx.rnd() * 0.8f, fx.rnd() - 0.5f));
            vec4 c = B * vec4(d.x * s.width * 0.5f, 0.6f + d.y, d.z * s.length * 0.5f, 1);
            fx.glow(vec3(c.x, c.y, c.z), d * (6.f + fx.rnd() * 10.f), 0.5f + fx.rnd() * 0.5f, 0.12f + fx.rnd() * 0.12f,
                    fx.rnd() < 0.5f ? vec3(4.f, 4.5f, 6.f) : vec3(1.2f, 2.f, 5.f), 4.f);
        }
        for (int k = 0; k < 2; k++) {
            vec3 wp = player.model->wheelPos[2 + k];
            vec4 q = player.car.chassisMatrix() * vec4(wp.x, 0.f, wp.z, 1.f);
            tm.last[k] = vec3(q.x, world.heightAt(q.x, q.z), q.z);
        }
        showPopup("TIME CIRCUITS", eraDate(tm.era), tm.era == 0 ? "BACK TO THE PRESENT" : tm.era == 1 ? "WELCOME TO 1955" : "WELCOME TO 2015", 0, false,
                  vec4(1.f, 0.25f, 0.2f, 1));
    }
    tm.jumpT += dt;
    if (tm.trailT > 0) {
        // twin trails of fire where the tyres were
        tm.trailT -= dt;
        for (int k = 0; k < 2; k++) {
            vec3 wp = player.model->wheelPos[2 + k];
            vec4 q = player.car.chassisMatrix() * vec4(wp.x, 0.f, wp.z, 1.f);
            vec3 g(q.x, world.heightAt(q.x, q.z), q.z);
            if (length(g - tm.last[k]) > 0.6f) {
                if (length(g - tm.last[k]) < 12.f) fx.fireLine(tm.last[k], g);
                tm.last[k] = g;
            }
        }
    }

    // ---- the spinning cat
    auto spinCar = [&](Racer& r, bool spinning) {
        Vehicle& c = r.car;
        if (!c.spec || c.spec->fx != "oiia") return;
        if (spinning) c.fxSpinVel += (17.f - c.fxSpinVel) * damp(7.f, dt);
        else {
            float twoPi = 2.f * PI;
            float home = std::round(c.fxSpin / twoPi) * twoPi;
            if (std::fabs(c.fxSpinVel) > 5.f) c.fxSpinVel *= 1.f - std::min(1.f, 2.5f * dt);
            else c.fxSpinVel += ((home - c.fxSpin) * 40.f - c.fxSpinVel * 9.f) * dt;
        }
        c.fxSpin += c.fxSpinVel * dt;
        if (c.fxSpin > 200.f * PI && std::fabs(c.fxSpinVel) < 1.f) c.fxSpin = std::fmod(c.fxSpin, 2.f * PI);
    };
    bool catSpin = driving && !rewinding && (playerCtl.brake > 0.2f || playerCtl.handbrake > 0.5f);
    spinCar(player, catSpin);
    bool cat = s.fx == "oiia";
    if (cat && s.sound != songFile) {
        songFile = s.sound;
        audio.loadSong(songFile);
    }
    if (cat && catSpin && !catWasSpinning) audio.songRestart = 1;
    catWasSpinning = cat && catSpin;
    audio.songOn = catWasSpinning ? 1 : 0;
    if (cat && catSpin && player.car.speed() > 3.f && fx.rnd() < 0.3f)
        fx.glow(player.car.pos + vec3(0, 1.f, 0), vec3(fx.rnd() - 0.5f, 1.5f, fx.rnd() - 0.5f) * 3.f, 0.6f, 0.08f,
                vec3(2.5f, 1.2f + fx.rnd(), 2.5f), 2.f);
}

Atmosphere Game::currentAtmo() const {
    if (state == ST_TITLE || keyArt) return titleAtmo;
    return eraAtmo[std::clamp(tm.era, 0, 2)];
}

// ====================================================================== HUD
void Game::drawTimeCircuits() {
    bool tmCar = player.car.spec && player.car.spec->fx == "timemachine";
    float vis = std::max(smoothstepf(0.02f, 0.2f, tm.charge), std::min(1.f, tm.panel));
    if (!tmCar || vis < 0.01f) return;
    float u = hud.ui;
    float w = 430 * u, rh = 44 * u, x = hud.w * 0.5f - w * 0.5f, y = 22 * u;
    if (hud.w < 900 * u) { w = std::min(w, hud.w - 40 * u); x = hud.w * 0.5f - w * 0.5f; y = 128 * u; }  // narrow / portrait: below the info panel
    hud.roundRect(x - 10 * u, y - 10 * u, w + 20 * u, rh * 3 + 34 * u, 8 * u, vec4(0.06f, 0.06f, 0.07f, 0.88f * vis));
    struct Row { const char* label; int era; vec4 col; };
    Row rows[3] = {{"DESTINATION TIME", (tm.era + 1) % 3, vec4(1.f, 0.22f, 0.15f, 1)},
                   {"PRESENT TIME", tm.era, vec4(0.25f, 1.f, 0.35f, 1)},
                   {"LAST TIME DEPARTED", tm.lastEra, vec4(1.f, 0.72f, 0.15f, 1)}};
    for (int i = 0; i < 3; i++) {
        float ry = y + i * (rh + 6 * u);
        vec4 c = rows[i].col;
        hud.rect(x, ry, w, rh - 12 * u, vec4(0.01f, 0.01f, 0.01f, vis));
        bool blink = i == 0 && tm.charge > 0.85f && std::fmod(R.time, 0.3f) < 0.15f;
        vec4 tc(c.x, c.y, c.z, vis * (blink ? 0.35f : 1.f));
        hud.text(x + w * 0.5f, ry + 3 * u, 22 * u, eraDate(rows[i].era), tc, 1, false);
        hud.rect(x + w * 0.5f - 70 * u, ry + rh - 11 * u, 140 * u, 10 * u, vec4(0.75f, 0.12f, 0.1f, vis * 0.9f));
        hud.text(x + w * 0.5f, ry + rh - 11 * u, 9 * u, rows[i].label, vec4(1, 1, 1, vis), 1, false);
    }
    // flux meter: how close the car is to 88
    float my = y + rh * 3 + 30 * u;
    hud.rect(x, my, w, 6 * u, vec4(1, 1, 1, 0.12f * vis));
    hud.rect(x, my, w * tm.charge, 6 * u, vec4(0.55f, 0.8f, 1.f, vis));
    if (tm.charge > 0.05f && tm.armed)
        hud.text(x + w * 0.5f, my + 10 * u, 12 * u, settings.mph ? "88 MPH" : "88 KM/H", vec4(0.7f, 0.9f, 1.f, vis * 0.9f), 1, false);
}

// ====================================================================== secret code box (Autoshow)
void Game::drawCheatBox() {
    float u = hud.ui;
    float w = 150 * u, h = 24 * u, x = hud.w - w - 14 * u, y = hud.h - h - 10 * u;
    cheatRect = vec4(x, y, w, h);
    cheatFlash = std::max(0.f, cheatFlash - 1.f / 60.f);
    cheatFail = std::max(0.f, cheatFail - 1.f / 60.f);
    if (!cheatFocus) {
        // nearly invisible until you know where to look
        hud.rect(x, y, w, h, vec4(1, 1, 1, 0.025f));
        hud.text(x + 8 * u, y + 5 * u, 10 * u, "promo code", vec4(1, 1, 1, 0.07f), 0, false);
        return;
    }
    float shakeX = cheatFail > 0 ? std::sin(cheatFail * 60.f) * 6.f * u * cheatFail : 0.f;
    vec4 edge = cheatFail > 0 ? vec4(1, 0.3f, 0.25f, 1) : cheatFlash > 0 ? vec4(0.4f, 1, 0.5f, 1) : S_ACCENT;
    hud.rect(x - 2 * u + shakeX, y - 2 * u, w + 4 * u, h + 4 * u, edge);
    hud.rect(x + shakeX, y, w, h, vec4(0.03f, 0.03f, 0.04f, 0.95f));
    std::string t = cheatBuf;
    if (std::fmod(R.time, 1.f) < 0.5f) t += "_";
    hud.text(x + 8 * u + shakeX, y + 4 * u, 14 * u, t.empty() ? " " : t, S_WHITE, 0, false);
    hud.text(x + w, y - 18 * u, 11 * u, "ENTER CODE", S_DIM, 2, false);
}

void Game::setCheatFocus(bool on) {
    if (on == cheatFocus) return;
    cheatFocus = on;
    if (on) { cheatBuf.clear(); SDL_StartTextInput(win); }
    else SDL_StopTextInput(win);
}

bool Game::cheatEvent(const SDL_Event& e) {
    if (state != ST_DEALER) { if (cheatFocus) setCheatFocus(false); return false; }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        float d = SDL_GetWindowPixelDensity(win);
        float mx = e.button.x * d, my = e.button.y * d;
        bool inside = mx >= cheatRect.x && my >= cheatRect.y && mx <= cheatRect.x + cheatRect.z && my <= cheatRect.y + cheatRect.w;
        setCheatFocus(inside);
        return inside;
    }
    if (e.type == SDL_EVENT_TEXT_INPUT && cheatFocus) {
        for (const char* p = e.text.text; *p; p++) {
            char c = char(std::tolower((unsigned char)*p));
            if (std::isalnum((unsigned char)c) && cheatBuf.size() < 20) cheatBuf += c;
        }
        return true;
    }
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (!cheatFocus) {
            if (e.key.key == SDLK_SLASH) { setCheatFocus(true); return true; }
            return false;
        }
        SDL_Keycode k = e.key.key;
        if (k == SDLK_ESCAPE) setCheatFocus(false);
        else if (k == SDLK_BACKSPACE) { if (!cheatBuf.empty()) cheatBuf.pop_back(); }
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) submitCheat();
        return true;  // everything else is typing
    }
    return false;
}

void Game::submitCheat() {
    std::string code = cheatBuf;
    cheatBuf.clear();
    if (code.empty()) { setCheatFocus(false); return; }
    for (int i = 0; i < int(kCars.size()); i++) {
        if (kCars[i].secret.empty() || kCars[i].secret != code) continue;
        setCheatFocus(false);
        for (int g = 0; g < int(garage.size()); g++)
            if (garage[g].spec == i) {
                setPlayerCar(g);
                toast(carName(kCars[i]) + " is already yours", S_ACCENT);
                return;
            }
        int paint = 0;
        float bd = 1e9f;
        for (int p = 0; p < kPaintCount; p++) {
            vec3 dd = kPaints[p] - kCars[i].color;
            if (dot(dd, dd) < bd) { bd = dot(dd, dd); paint = p; }
        }
        garage.push_back({i, paint, false});
        setPlayerCar(int(garage.size()) - 1);
        save();
        audio.beep(1760, 0.5f);
        flash = 0.6f;
        showPopup("SECRET CAR UNLOCKED", carName(kCars[i]), "HOLD BRAKE", 0, false, vec4(1.f, 0.25f, 0.55f, 1));
        toast("Secret car unlocked: " + carName(kCars[i]) + "!", vec4(1.f, 0.35f, 0.65f, 1));
        return;
    }
    cheatFail = 0.5f;
    audio.beep(200, 0.25f);
}

// ====================================================================== quick car switch
void Game::openSwitcher() {
    if (garage.size() < 2) { toast("Buy another car at the Autoshow to switch", S_DIM); return; }
    switchFrom = state;
    state = ST_SWITCH;
    subSel = current;
    stateTime = 0;
    audio.beep(880, 0.05f);
}

void Game::drawSwitcher() {
    float u = hud.ui;
    hud.rectV(0, hud.h - 330 * u, hud.w, 330 * u, vec4(0, 0, 0, 0), vec4(0.01f, 0.01f, 0.02f, 0.9f));
    const CarSpec& s = kCars[garage[std::clamp(subSel, 0, int(garage.size()) - 1)].spec];
    hud.text(48 * u, hud.h - 300 * u, 13 * u, "QUICK SWITCH", S_DIM, 0, false);
    hud.text(48 * u, hud.h - 282 * u, 30 * u, carName(s), S_WHITE);
    drawCarCards(true, hud.h - 236 * u, 160 * u);
    hud.text(hud.w * 0.5f, hud.h - 38 * u, 14 * u,
             pad ? "LEFT/RIGHT choose   (A) drive   (B) cancel" : "LEFT/RIGHT choose   ENTER drive   G / ESC cancel", vec4(1, 1, 1, 0.75f), 1, false);
}

void Game::switcherAction(Action a) {
    int n = int(garage.size());
    if (a == A_LEFT) { subSel = (subSel - 1 + n) % n; audio.beep(880, 0.04f); }
    else if (a == A_RIGHT) { subSel = (subSel + 1) % n; audio.beep(880, 0.04f); }
    else if (a == A_CONFIRM || a == A_INTERACT) {
        if (subSel != current) {
            setPlayerCar(subSel);
            player.car.vel = vec3(0, 0, 0);
            toast("Now driving: " + carName(kCars[garage[current].spec]), S_ACCENT);
            flash = 0.25f;
            save();
        }
        state = switchFrom;
    } else if (a == A_BACK || a == A_PAUSE || a == A_SWITCH) state = switchFrom;
}
