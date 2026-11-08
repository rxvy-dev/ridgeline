// Washi and ink: the shared drawing kit for every menu and the HUD (paper, brush strokes, seals),
// plus the screens built from it. See washi.h for the palette.
#include "game.h"
#include "washi.h"
#include "logo_data.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace wa;

// ====================================================================== the kit
// Paper: a flat sheet with fibres and the odd fleck pressed into it.
void Game::paper(float x, float y, float w, float h, float a, int seed, vec4 tone) {
    float u = hud.ui;
    hud.rect(x, y, w, h, A(tone, a));
    int n = std::min(260, int(w * h / (2600.f * u * u)));
    for (int i = 0; i < n; i++) {
        float fx = x + hash(seed, i * 4) * w, fy = y + hash(seed, i * 4 + 1) * h;
        float ang = hash(seed, i * 4 + 2) * 6.283f, len = (3.f + 14.f * hash(seed, i * 4 + 3)) * u;
        vec2 d(std::cos(ang) * len, std::sin(ang) * len);
        vec2 p0(fx, fy), p1 = p0 + d;
        p1.x = clampf(p1.x, x, x + w);
        p1.y = clampf(p1.y, y, y + h);
        hud.line(p0, p1, 0.8f * u, vec4(INK.x, INK.y, INK.z, (0.035f + 0.04f * hash(seed, i + 900)) * a));
    }
    for (int i = 0; i < n / 3; i++) {
        float fx = x + hash(seed + 7, i * 2) * w, fy = y + hash(seed + 7, i * 2 + 1) * h;
        hud.circle(vec2(fx, fy), (0.6f + 1.2f * hash(seed + 9, i)) * u, A(INK2, 0.08f * a), 6);
    }
}

void Game::inkBox(float x, float y, float w, float h, float th, vec4 col) {
    hud.rect(x, y, w, th, col);
    hud.rect(x, y + h - th, w, th, col);
    hud.rect(x, y + th, th, h - 2 * th, col);
    hud.rect(x + w - th, y + th, th, h - 2 * th, col);
}

// A sumi brush stroke from a to b: swells in, tapers out, ragged edges, dry streaks near the tail.
void Game::brush(vec2 a, vec2 b, float wid, vec4 col, int seed, float prog) {
    if (prog <= 0.001f) return;
    vec2 d = b - a;
    float L = length(d);
    if (L < 1e-3f) return;
    vec2 t = d * (1.f / L), n(-t.y, t.x);
    int N = std::clamp(int(L / (5.f * hud.ui)), 6, 90);
    int M = std::max(1, int(N * std::min(prog, 1.f)));
    auto halfW = [&](int i, int side) {
        float s = i / float(N);
        float taper = smoothstepf(0.f, 0.06f, s) * (1.f - 0.55f * smoothstepf(0.7f, 1.f, s)) + 0.12f;
        float rag = 1.f + (hash(seed + side * 31, i) - 0.5f) * 0.28f + 0.08f * std::sin(s * 9.f + seed);
        return wid * 0.5f * std::min(1.f, taper) * rag;
    };
    for (int i = 0; i < M; i++) {
        vec2 p0 = a + t * (L * i / N), p1 = a + t * (L * (i + 1) / N);
        hud.quad(p0 + n * halfW(i, 0), p1 + n * halfW(i + 1, 0), p1 - n * halfW(i + 1, 1), p0 - n * halfW(i, 1), col);
    }
    // kasure: the brush running dry leaves paper-coloured streaks along the tail
    for (int k = 0; k < 4; k++) {
        float off = (hash(seed + 5, k) - 0.5f) * 0.7f * wid;
        float s0 = 0.84f + 0.1f * hash(seed + 6, k), s1 = std::min(std::min(prog, 1.f), 0.99f);
        if (s1 <= s0) continue;
        hud.line(a + t * (L * s0) + n * off, a + t * (L * s1) + n * off, std::max(1.f, wid * 0.04f), A(PAPER, 0.4f * col.w));
    }
}

// A horizontal swipe of ink filling a box: the menus' selection highlight.
void Game::brushSwipe(float x, float y, float w, float h, vec4 col, int seed, float prog) {
    brush(vec2(x - h * 0.15f, y + h * 0.5f), vec2(x + w + h * 0.25f, y + h * 0.5f), h * 1.08f, col, seed, prog);
}

// The enso: one confident circle of ink, thick where the brush lands, thin where it lifts.
void Game::enso(vec2 c, float r, float wid, vec4 col, float prog, int seed) {
    const int N = 64;
    float a0 = -2.2f + hash(seed, 1) * 0.6f, span = 6.283f * 0.9f * clampf(prog, 0.f, 1.f);
    int M = std::max(1, int(N * clampf(prog, 0.f, 1.f)));
    for (int i = 0; i < M; i++) {
        float s0 = i / float(N), s1 = (i + 1) / float(N);
        float q0 = a0 + span * (i / float(M)), q1 = a0 + span * ((i + 1) / float(M));
        float w0 = wid * (1.f - 0.75f * s0) * (1.f + 0.15f * (hash(seed, i) - 0.5f));
        float w1 = wid * (1.f - 0.75f * s1) * (1.f + 0.15f * (hash(seed, i + 1) - 0.5f));
        vec2 d0(std::cos(q0), std::sin(q0)), d1(std::cos(q1), std::sin(q1));
        hud.quad(c + d0 * (r - w0 * 0.5f), c + d1 * (r - w1 * 0.5f), c + d1 * (r + w1 * 0.5f), c + d0 * (r + w0 * 0.5f), col);
    }
}

// A hanko seal: a vermilion square with a character in it, pressed a little crooked.
void Game::hanko(vec2 c, float s, const std::string& k, vec4 col, float rot, float a) {
    if (a <= 0.01f) return;
    float u = hud.ui;
    vec2 ax(std::cos(rot), std::sin(rot)), ay(-ax.y, ax.x);
    float h = s * 0.5f, th = s * 0.09f;
    auto P = [&](float x, float y) { return c + ax * x + ay * y; };
    vec4 cc = A(col, a);
    hud.quad(P(-h, -h), P(h, -h), P(h, -h + th), P(-h, -h + th), cc);
    hud.quad(P(-h, h - th), P(h, h - th), P(h, h), P(-h, h), cc);
    hud.quad(P(-h, -h), P(-h + th, -h), P(-h + th, h), P(-h, h), cc);
    hud.quad(P(h - th, -h), P(h, -h), P(h, h), P(h - th, h), cc);
    float px = s * 0.62f;
    hud.text(c.x, c.y - px * 0.56f, px, k, cc, 1, false);
    for (int i = 0; i < 6; i++)  // where the ink didn't take
        hud.circle(P((hash(int(s), i) - 0.5f) * s * 0.9f, (hash(int(s) + 3, i) - 0.5f) * s * 0.9f), (0.8f + hash(5, i) * 1.4f) * u, A(PAPER, 0.7f * a), 6);
}

// Text set top to bottom, one character under the next.
void Game::vtext(float x, float y, float px, const std::string& s, vec4 col) {
    size_t i = 0;
    float yy = y;
    while (i < s.size()) {
        size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) j++;
        hud.text(x, yy, px, s.substr(i, j - i), col, 1, false);
        yy += px * 1.08f;
        i = j;
    }
}

// A card laid on the paper: lighter stock, an ink border and a hard offset shadow like a woodblock print.
void Game::card(float x, float y, float w, float h, float a, bool sel, int seed) {
    float u = hud.ui, off = (sel ? 8.f : 5.f) * u;
    hud.rect(x + off, y + off, w, h, A(sel ? VERM : INK, 0.9f * a));
    paper(x, y, w, h, a, seed, CARD);
    inkBox(x, y, w, h, (sel ? 3.f : 2.f) * u, A(INK, a));
}

vec4 Game::washiEventColor(int type) const {
    if (type == EV_CIRCUIT) return VERM;
    if (type == EV_SPRINT) return INDIGO;
    if (type == EV_DRIFT) return PLUM;
    return MOSS;
}

static vec4 classInk(char c) {
    switch (c) {
        case 'D': return INDIGO;
        case 'C': return MOSS;
        case 'B': return GOLD;
        case 'A': return VERM;
        case 'S': return PLUM;
        default: return INK;
    }
}

// The kanji that labels a section, for the side column, tabs and seals.
std::string Game::kanjiFor(const std::string& s) const {
    static const std::pair<const char*, const char*> k[] = {
        {"FESTIVAL", "祭"}, {"CARS", "車"}, {"HOUSES", "家"}, {"EVENTLAB", "創作"}, {"OPTIONS", "設定"}, {"SETTINGS", "設定"},
        {"MY GARAGE", "車庫"}, {"AUTOSHOW", "販売"}, {"CUSTOMIZE", "塗装"}, {"UPGRADE SHOP", "改造"}, {"WHEELSPIN", "福引"},
        {"MY EVENTS", "記録"}, {"WORLD MAP", "地図"}, {"RESULTS", "結果"}, {"PAUSED", "休憩"}, {"PHOTO MODE", "写真"},
        {"QUICK SWITCH", "車"}};
    for (auto& e : k) if (s == e.first) return e.second;
    return "峠";
}

// A kitsune mask for the driver: white face, vermilion markings, ink outline.
void Game::kitsune(float x, float y, float s, float a) {
    vec2 c(x + s * 0.5f, y + s * 0.55f);
    float k = s / 96.f;
    vec4 face = A(CARD, a), red = A(VERM, a), ink = A(INK, a);
    auto T = [&](vec2 p0, vec2 p1, vec2 p2, vec4 col) { hud.triangle(c + p0 * k, c + p1 * k, c + p2 * k, col); };
    // ears and outline (ink, a touch larger), then the face
    T(vec2(-38, -42), vec2(-10, -20), vec2(-34, -2), ink); T(vec2(38, -42), vec2(34, -2), vec2(10, -20), ink);
    T(vec2(-37, -20), vec2(37, -20), vec2(0, 38), ink);
    T(vec2(-34, -36), vec2(-12, -18), vec2(-30, -4), face); T(vec2(34, -36), vec2(30, -4), vec2(12, -18), face);
    T(vec2(-32, -17), vec2(32, -17), vec2(0, 33), face);
    T(vec2(-30, -32), vec2(-18, -20), vec2(-27, -12), red); T(vec2(30, -32), vec2(27, -12), vec2(18, -20), red);
    hud.line(c + vec2(-20, -4) * k, c + vec2(-6, -1) * k, 3.f * k, ink);
    hud.line(c + vec2(20, -4) * k, c + vec2(6, -1) * k, 3.f * k, ink);
    hud.line(c + vec2(-16, -10) * k, c + vec2(-4, -8) * k, 2.f * k, red);
    hud.line(c + vec2(16, -10) * k, c + vec2(4, -8) * k, 2.f * k, red);
    hud.circle(c + vec2(0, 28) * k, 4.f * k, ink, 10);
    hud.circle(c + vec2(0, -14) * k, 3.f * k, red, 8);
}

// ====================================================================== shared menu chrome
void Game::drawThemeBackdrop(float alpha) {
    if (alpha <= 0.01f) return;
    float u = hud.ui, T = R.time;
    paper(0, 0, hud.w, hud.h, alpha, 11, PAPER);
    hud.rectV(0, hud.h * 0.55f, hud.w, hud.h * 0.45f, A(PAPER2, 0.f), A(PAPER2, 0.7f * alpha));
    // the sun and Fuji in a wash of thin ink, drifting very slowly
    float drift = std::sin(T * 0.05f) * 20 * u;
    hud.circle(vec2(hud.w * 0.80f + drift * 0.5f, hud.h * 0.30f), hud.h * 0.13f, A(VERM, 0.13f * alpha), 64);
    {
        float bx = hud.w * 0.62f + drift, by = hud.h * 1.02f, W = hud.w * 0.55f, H = hud.h * 0.48f;
        vec2 peak(bx + W * 0.5f, by - H);
        hud.triangle(vec2(bx, by), vec2(bx + W, by), peak, A(INK2, 0.07f * alpha));
        // snow cap, the paper showing through
        float cap = 0.22f;
        vec2 l = peak + (vec2(bx, by) - peak) * cap, r = peak + (vec2(bx + W, by) - peak) * cap;
        hud.triangle(l, r, peak, A(PAPER, 0.5f * alpha));
        for (int k = 0; k < 4; k++) {  // ragged snowline
            float s0 = k / 4.f, s1 = (k + 1) / 4.f;
            vec2 q0 = l + (r - l) * s0, q1 = l + (r - l) * s1, m = (q0 + q1) * 0.5f + vec2(0, H * 0.06f * (k % 2 ? 1.f : 0.6f));
            hud.triangle(q0, q1, m, A(PAPER, 0.5f * alpha));
        }
        // a far ridge in front
        hud.triangle(vec2(bx - W * 0.6f, by), vec2(bx + W * 0.3f, by), vec2(bx - W * 0.1f, by - H * 0.35f), A(INK2, 0.05f * alpha));
    }
    // the ink column down the left edge, with the section in vertical kanji
    float cw = 58 * u;
    hud.rect(0, 0, cw, hud.h, A(INK, alpha));
    hud.rect(cw, 0, 2 * u, hud.h, A(VERM, alpha));
    if (!washiSide.empty()) vtext(cw * 0.5f, 40 * u, 30 * u, washiSide, A(PAPER, alpha));
    hanko(vec2(cw * 0.5f, hud.h - 60 * u), 34 * u, "峠", A(VERM, 1.f), -0.08f, alpha);
    drawPetals(alpha * 0.6f, T);
}

float Game::drawThemeHeader(float x0, float y0, float W, float in) {
    float u = hud.ui;
    const CarSpec& sp = *player.car.spec;
    // the car, cut out, resting on a short brush stroke like a painting's ground line
    float iw = 230 * u, ih = iw * 0.5f;
    int si = current < int(garage.size()) ? garage[current].spec : -1;
    GLuint img = si >= 0 && si < int(carThumb.size()) ? carThumb[si] : 0;
    float ix = x0;
    brush(vec2(ix + 10 * u, y0 + ih - 6 * u), vec2(ix + iw - 10 * u, y0 + ih - 8 * u), 9 * u, A(INK2, 0.35f * in), 77, in);
    if (img) hud.image(ix, y0 - 4 * u, iw, ih, img, vec4(1, 1, 1, in), vec2(0.1f, 0.84f), vec2(0.9f, 0.2f));
    // make (small vermilion capitals), model (brush serif), class seal and PI
    float bx = ix + iw + 26 * u;
    std::string mk = sp.make;
    for (auto& ch : mk) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    hud.text(bx, y0 + 6 * u, 15 * u, mk, A(VERM, in), 0, false);
    hud.textFit(bx, y0 + 26 * u, 34 * u, sp.model, A(INK, in), 0, W * 0.4f, false);
    int pi = rateCar(sp).pi;
    vec4 cc = classInk(sp.cls);
    hud.rect(bx, y0 + 74 * u, 30 * u, 28 * u, A(cc, in));
    hud.text(bx + 15 * u, y0 + 78 * u, 20 * u, std::string(1, sp.cls), A(CARD, in), 1, false);
    inkBox(bx + 30 * u, y0 + 74 * u, 62 * u, 28 * u, 2 * u, A(INK, in));
    hud.text(bx + 61 * u, y0 + 78 * u, 19 * u, std::to_string(pi), A(INK, in), 1, false);
    hud.text(bx + 104 * u, y0 + 81 * u, 13 * u, "PERFORMANCE", A(MUTED, in), 0, false);
    // the driver: kitsune mask, name, level in kanji and credits
    float rx = x0 + W;
    const char* user = getenv("USER");
    std::string name = user && *user ? user : "Driver";
    std::string cr = fmtThousands((long long)(creditsShown + 0.5));
    float nameW = std::max({hud.textWidth(26 * u, name), hud.textWidth(24 * u, cr) + 44 * u, 120 * u});
    float tx = rx - nameW, ax = tx - 16 * u - 92 * u;
    card(ax, y0, 92 * u, 92 * u, in, false, 5);
    kitsune(ax + 4 * u, y0 + 2 * u, 84 * u, in);
    hud.text(tx, y0 + 2 * u, 26 * u, name, A(INK, in), 0, false);
    std::string lv = "段 " + std::to_string(level);
    hud.text(tx, y0 + 36 * u, 18 * u, lv, A(VERM, in), 0, false);
    float lw = nameW * clampf(xp / float(xpToNext()), 0.f, 1.f);
    hud.rect(tx, y0 + 60 * u, nameW, 2 * u, A(INK, 0.15f * in));
    hud.rect(tx, y0 + 60 * u, lw, 2 * u, A(VERM, in));
    hud.circle(vec2(tx + 12 * u, y0 + 82 * u), 11 * u, A(GOLD, in), 20);
    hud.text(tx + 12 * u, y0 + 75 * u, 11 * u, "CR", A(CARD, in), 1, false);
    hud.text(tx + 30 * u, y0 + 68 * u, 24 * u, cr, A(INK, in), 0, false);
    // a long stroke of ink under it all, painted on as the menu opens
    float bottom = y0 + 112 * u;
    brush(vec2(x0 - 8 * u, bottom), vec2(x0 + W + 8 * u, bottom + 2 * u), 7 * u, A(INK, in), 3, in);
    return bottom + 6 * u;
}

float Game::drawThemeTabs(float x0, float y0, float W, const char* const* names, int count, int sel, float in) {
    float u = hud.ui;
    float h = 46 * u;
    // Q/E caps either end
    if (count > 1) {
        for (int side = 0; side < 2; side++) {
            float cx = side ? x0 + W - 34 * u : x0;
            inkBox(cx, y0 + 8 * u, 34 * u, 30 * u, 2 * u, A(INK, in));
            hud.text(cx + 17 * u, y0 + 13 * u, 16 * u, side ? (pad ? "RB" : "E") : (pad ? "LB" : "Q"), A(INK, in), 1, false);
        }
    }
    std::vector<float> tw(count);
    float total = 0;
    for (int i = 0; i < count; i++) { tw[i] = hud.textWidth(22 * u, names[i]) + 70 * u; total += tw[i]; }
    float tx = x0 + (W - total) * 0.5f;
    for (int i = 0; i < count; i++) {
        bool on = i == sel;
        float cx = tx + tw[i] * 0.5f;
        if (on) {
            if (tabSlideW <= 0) { tabSlideX = tx; tabSlideW = tw[i]; }
            tabSlideX += (tx - tabSlideX) * 0.25f;
            tabSlideW += (tw[i] - tabSlideW) * 0.25f;
        }
        if (i > 0) hud.rect(tx, y0 + 12 * u, 1.5f * u, h - 24 * u, A(INK, 0.3f * in));
        tx += tw[i];
        (void)cx;
    }
    // the ink swipe slides under the chosen tab, then the labels go on top
    brushSwipe(tabSlideX + 6 * u, y0 + 4 * u, tabSlideW - 12 * u, h - 8 * u, A(INK, in), 41, 1.f);
    tx = x0 + (W - total) * 0.5f;
    for (int i = 0; i < count; i++) {
        bool on = i == sel;
        float cx = tx + tw[i] * 0.5f;
        std::string k = kanjiFor(names[i]);
        float kw = hud.textWidth(16 * u, k), nw = hud.textWidth(22 * u, names[i]);
        float sx = cx - (kw + 8 * u + nw) * 0.5f;
        hud.text(sx, y0 + 14 * u, 16 * u, k, A(on ? VERM : MUTED, in), 0, false);
        hud.text(sx + kw + 8 * u, y0 + 10 * u, 22 * u, names[i], A(on ? PAPER : INK, in), 0, false);
        tx += tw[i];
    }
    return y0 + h;
}

void Game::drawThemePrompts(const std::string& spec, float px) {
    float u = hud.ui;
    hud.capFill = INK; hud.capText = PAPER; hud.labelCol = INK; hud.labelShadow = false; hud.capRound = 0.08f;
    float w = hud.prompts(0, 0, 15 * u, spec, 0, 1.f, true);
    float x = px >= 0 ? px : 90 * u, y = hud.h - 50 * u;
    card(x - 14 * u, y - 10 * u, w + 28 * u, 40 * u, 0.95f, false, 23);
    hud.prompts(x, y, 15 * u, spec, 0);
    hud.capFill = vec4(1, 1, 1, 0.92f); hud.capText = vec4(0.06f, 0.06f, 0.08f, 1); hud.labelCol = vec4(1, 1, 1, 0.88f);
    hud.labelShadow = true; hud.capRound = 0.22f;
}

void Game::themePanel(float x, float y, float w, float h, float a, bool lit) {
    float u = hud.ui;
    card(x, y, w, h, a, false, int(x * 0.37f + y * 0.11f));
    if (lit) hud.rect(x + 2 * u, y + 2 * u, w - 4 * u, 4 * u, A(VERM, a));
}

void Game::themeRow(float x, float y, float w, float h, bool sel, float a) {
    float u = hud.ui;
    if (!sel) {
        for (float dx = 0; dx < w; dx += 8 * u) hud.rect(x + dx, y + h - 1 * u, 3 * u, 1.2f * u, A(INK, 0.25f * a));
        return;
    }
    static float t0 = 0;
    static float lastY = -1;
    if (std::fabs(lastY - y) > 1.f) { lastY = y; t0 = R.time; }
    float prog = ease((R.time - t0) / 0.18f);
    brushSwipe(x, y + 1 * u, w, h - 2 * u, A(INK, a), int(y), prog);
    hud.rect(x - 10 * u, y + h * 0.3f, 5 * u, h * 0.4f, A(VERM, a));
}

void Game::drawMenuToasts(float ty) {
    float u = hud.ui;
    for (auto& t : toasts) {
        float age = 3.f - t.t;
        float in = clampf(age / 0.25f, 0.f, 1.f);
        float a = clampf(t.t * 2.f, 0.f, 1.f) * in;
        float tw = hud.textWidth(19 * u, t.text) + 60 * u;
        float x = hud.w * 0.5f - tw * 0.5f, y = ty - (1.f - ease(in)) * 18 * u;
        card(x, y - 6 * u, tw, 38 * u, a, false, 61);
        hud.rect(x + 2 * u, y - 4 * u, 8 * u, 34 * u, A(VERM, a));
        hud.text(hud.w * 0.5f + 4 * u, y + 2 * u, 19 * u, t.text, A(INK, a), 1, false);
        ty += 48 * u;
    }
}

// Showroom screens (garage, autoshow, customise, upgrades...): a strip of paper across the top, so the
// car stays in view beneath, with the section name, its kanji and the driver.
void Game::drawMenuHeader(float x0, float y0, float W, const std::string& section) {
    float u = hud.ui;
    float in = ease(stateTime / 0.35f);
    float dy = (1.f - in) * -40 * u;
    float bandH = y0 + 70 * u;
    paper(0, dy, hud.w, bandH, 0.97f, 13, PAPER);
    // deckled bottom edge
    for (float x = 0; x < hud.w; x += 14 * u) {
        float d = (3.f + 5.f * hash(17, int(x))) * u;
        hud.triangle(vec2(x, dy + bandH), vec2(x + 14 * u, dy + bandH), vec2(x + 7 * u, dy + bandH + d), A(PAPER, 0.97f));
    }
    brush(vec2(0, dy + bandH + 4 * u), vec2(hud.w, dy + bandH + 6 * u), 4 * u, A(INK, in), 8, in);
    // RIDGELINE in spaced capitals, a seal, then the section with its kanji
    hanko(vec2(x0 + 20 * u, dy + y0 + 20 * u), 36 * u, "峠", VERM, -0.1f, in);
    hud.text(x0 + 52 * u, dy + y0 + 6 * u, 22 * u, "R I D G E L I N E", A(INK, in), 0, false);
    float sx = x0 + 52 * u + hud.textWidth(22 * u, "R I D G E L I N E") + 30 * u;
    hud.rect(sx - 16 * u, dy + y0 + 2 * u, 2 * u, 40 * u, A(INK, 0.4f * in));
    std::string k = kanjiFor(section);
    hud.text(sx, dy + y0 + 4 * u, 22 * u, k, A(VERM, in), 0, false);
    hud.text(sx + hud.textWidth(22 * u, k) + 10 * u, dy + y0 + 2 * u, 28 * u, section, A(INK, in), 0, false);
    // the driver
    float rx = x0 + W;
    std::string cr = fmtThousands((long long)(creditsShown + 0.5));
    hud.text(rx, dy + y0 + 22 * u, 22 * u, cr, A(INK, in), 2, false);
    float crW = hud.textWidth(22 * u, cr);
    hud.circle(vec2(rx - crW - 16 * u, dy + y0 + 34 * u), 10 * u, A(GOLD, in), 18);
    hud.text(rx - crW - 16 * u, dy + y0 + 28 * u, 10 * u, "CR", A(CARD, in), 1, false);
    const char* user = getenv("USER");
    std::string name = user && *user ? user : "Driver";
    std::string who = name + "   段 " + std::to_string(level);
    hud.text(rx, dy + y0 - 2 * u, 15 * u, who, A(MUTED, in), 2, false);
    float mx = rx - std::max(hud.textWidth(15 * u, who), crW + 32 * u) - 64 * u;
    card(mx, dy + y0 - 6 * u, 50 * u, 50 * u, in, false, 3);
    kitsune(mx + 2 * u, dy + y0 - 5 * u, 46 * u, in);
}
