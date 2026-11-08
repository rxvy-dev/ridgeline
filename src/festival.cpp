// The festival loop: a daily Festival Playlist of three challenges (finish all three for a
// wheelspin), a day streak bonus, lucky-cat boards hidden across the island to smash, and a
// livelier festival site with a big wheel lit up at night.
#include "game.h"
#include "washi.h"
#include "signs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace {
const vec4 P_PINK(1.f, 0.25f, 0.55f, 1.f), P_GOLD(1.f, 0.8f, 0.2f, 1.f), P_DIM(1, 1, 1, 0.6f), P_WHITE(1, 1, 1, 1);

struct Challenge { int type; float target; const char* text; int credits; };
// FS_* are declared in game.h
const Challenge kPool[] = {
    {FS_WIN, 1, "Win a race", 12000},
    {FS_WIN, 3, "Win 3 races", 30000},
    {FS_RACE, 2, "Finish 2 races", 10000},
    {FS_RAMP, 3, "Hit 3 stunt ramps", 12000},
    {FS_RAMP, 6, "Hit 6 stunt ramps", 20000},
    {FS_SKILL, 25000, "Bank 25,000 skill points", 15000},
    {FS_SKILL, 60000, "Bank 60,000 skill points", 25000},
    {FS_SPEED, 250, "Reach 250 km/h", 10000},
    {FS_SPEED, 320, "Reach 320 km/h", 20000},
    {FS_KM, 15, "Drive 15 km", 10000},
    {FS_KM, 40, "Drive 40 km", 22000},
    {FS_CAT, 3, "Smash 3 lucky cat boards", 15000},
    {FS_STARS, 3, "Earn 3 PR stunt stars", 15000},
    {FS_STARS, 6, "Earn 6 PR stunt stars", 25000},
};
const int kPoolN = int(sizeof(kPool) / sizeof(kPool[0]));

int today() {
    std::time_t t = std::time(nullptr);
    std::tm lt = *std::localtime(&t);
    return (lt.tm_year + 1900) * 400 + lt.tm_yday;
}
}  // namespace

const char* Game::festText(int k) const { return kPool[festPick[k]].text; }

void Game::festNewDay() {
    int d = today();
    if (d == festDay) return;
    bool consecutive = lastDay >= 0 && d == lastDay + 1;
    streak = consecutive ? streak + 1 : 1;
    lastDay = d;
    festDay = d;
    // three different kinds of challenge, picked from the date
    Rng rng(uint32_t(d) * 2654435761u + 7u);
    int used[3] = {-1, -1, -1};
    for (int k = 0; k < 3; k++) {
        for (int tries = 0; tries < 50; tries++) {
            int pick = rng.irange(0, kPoolN - 1);
            bool clash = false;
            for (int j = 0; j < k; j++) clash |= kPool[used[j]].type == kPool[pick].type;
            if (!clash) { used[k] = pick; break; }
        }
        if (used[k] < 0) used[k] = k;
        festPick[k] = used[k];
        festProg[k] = 0;
    }
    festDone = 0;
    // daily login bonus that grows with the streak
    int bonus = 5000 * std::min(streak, 7);
    credits += bonus;
    showPopup("FESTIVAL PLAYLIST", streak > 1 ? "Day " + std::to_string(streak) + " streak" : "New day", "+" + fmtThousands(bonus) + " CR", 0, true, P_GOLD);
    festShow = 8.f;
    save();
}

void Game::festEvent(int type, float amount) {
    if (festDay < 0) return;
    for (int k = 0; k < 3; k++) {
        const Challenge& c = kPool[festPick[k]];
        if (c.type != type || (festDone & (1 << k))) continue;
        if (type == FS_SPEED) festProg[k] = std::max(festProg[k], amount);
        else festProg[k] += amount;
        festShow = std::max(festShow, 4.f);
        if (festProg[k] >= c.target) {
            festDone |= 1 << k;
            credits += c.credits;
            addXP(2000);
            showPopup("CHALLENGE COMPLETE", c.text, "+" + fmtThousands(c.credits) + " CR  +2,000 XP", 3, true, P_PINK);
            audio.beep(1760, 0.4f);
            if (festDone == 7) {
                wheelspins += 2;
                toast("FESTIVAL PLAYLIST COMPLETE - 2 wheelspins!", P_GOLD);
            }
            save();
        }
    }
}

// ---------------------------------------------------------------- lucky cat boards
void Game::buildFestival() {
    // board spots: just off the roads all over the island, spread apart
    cats.clear();
    Rng rng(777);
    float total = 0;
    for (auto& r : world.roads) total += float(r.pts.size());
    for (int tries = 0; tries < 6000 && cats.size() < kCatCount; tries++) {
        float pick = rng.f() * total;
        int ri = 0;
        for (; ri < int(world.roads.size()); ri++) { if (pick < world.roads[ri].pts.size()) break; pick -= float(world.roads[ri].pts.size()); }
        if (ri >= int(world.roads.size())) continue;
        const Road& r = world.roads[ri];
        int i = std::clamp(int(pick), 0, int(r.pts.size()) - 1);
        vec3 p = r.pts[i], t = r.tan[i], side(-t.z, 0, t.x);
        p = p + side * ((rng.f() < 0.5f ? -1.f : 1.f) * (r.width * 0.5f + 4.5f));
        p.y = world.heightAt(p.x, p.z);
        if (p.y < world.waterLevel + 1.f || world.roadDistAt(p.x, p.z) < 3.f) continue;
        if (std::fabs(world.heightAt(p.x, p.z) - r.pts[i].y) > 3.f) continue;  // keep them reachable
        bool close = false;
        for (auto& c : cats) close |= std::hypot(c.p.x - p.x, c.p.z - p.z) < 140.f;
        if (close) continue;
        cats.push_back({p, std::atan2(-side.x, -side.z)});
    }
    if (catHit.size() != cats.size()) catHit.resize(cats.size(), 0);
    catDirty = true;

    // ---- the festival grounds (Horizon style): a pink main stage, a field of white marquees with
    // string lights, grandstands along the road, rides, light towers and balloons over it all
    vec3 F = world.festCenter;
    vec3 N = normalize(vec3(F.x - world.hubPos.x, 0, F.z - world.hubPos.z)), Rt(N.z, 0, -N.x);
    auto at = [&](float r, float n) { vec3 p = F + Rt * r + N * n; p.y = world.heightAt(p.x, p.z); return p; };
    auto solid = [&](vec3 c, float hx, float hz, float top) {
        // long rotated things (grandstands) as a row of small boxes, so the axis-aligned
        // collision boxes hug them instead of reaching out over the road
        int pieces = std::max(1, int(std::ceil(hx / std::max(hz, 4.f))));
        for (int k = 0; k < pieces; k++) {
            float off = -hx + (2.f * hx) * (k + 0.5f) / pieces, phx = hx / pieces;
            vec3 pc = c + Rt * off;
            float ex = std::fabs(Rt.x) * phx + std::fabs(N.x) * hz, ez = std::fabs(Rt.z) * phx + std::fabs(N.z) * hz;
            world.boxes.push_back({pc.x - ex * 0.9f, pc.z - ez * 0.9f, pc.x + ex * 0.9f, pc.z + ez * 0.9f, top});
        }
    };
    {
        MeshBuilder b;
        Rng fr(99);
        const vec4 white(0.93f, 0.93f, 0.94f, 19), pink(0.95f, 0.12f, 0.5f, 19), teal(0.1f, 0.75f, 0.8f, 19), steel(0.55f, 0.56f, 0.58f, 7);
        const vec4 bulbs[] = {{1.f, 0.85f, 0.5f, 15}, {1.f, 0.3f, 0.7f, 15}, {0.3f, 0.9f, 1.f, 15}};
        // main stage: deck, LED wall, light truss and a sweeping pink canopy
        {
            vec3 c = at(0, 120);
            mat4 xf = basis(Rt, vec3(0, 1, 0), N, c);
            b.box(vec3(0, 1.2f, 0), vec3(24, 1.2f, 11), vec4(0.12f, 0.12f, 0.14f, 0), &xf);
            b.box(vec3(0, 9.f, 9.5f), vec3(17, 7.f, 0.4f), vec4(0.9f, 0.35f, 1.f, 20), &xf);   // LED wall
            b.box(vec3(0, 9.f, 9.1f), vec3(8, 4.f, 0.1f), vec4(0.3f, 0.95f, 1.f, 20), &xf);
            for (int k = -1; k <= 1; k += 2) {
                b.box(vec3(k * 26.f, 11.f, 4.f), vec3(1.2f, 11.f, 1.2f), steel, &xf);  // speaker/light towers
                for (int j = 0; j < 4; j++) b.box(vec3(k * 26.f, 6.f + j * 4.5f, 2.6f), vec3(1.0f, 0.5f, 0.2f), bulbs[j % 3], &xf);
            }
            // canopy: arched ribs with pink membranes between them, swooping up at the front
            const int ribs = 9;
            for (int i = 0; i < ribs; i++) {
                float x0 = -26.f + 52.f * i / (ribs - 1), x1 = -26.f + 52.f * (i + 1) / (ribs - 1);
                auto prof = [&](float x, float u) {  // u: 0 back .. 1 front
                    float lift = 1.f - std::pow(std::fabs(x) / 30.f, 2.f);
                    return vec3(x, 14.f + 9.f * lift + 6.f * u * u, 12.f - 26.f * u);
                };
                for (int k = 0; k < 6; k++) {
                    float u0 = k / 6.f, u1 = (k + 1) / 6.f;
                    vec3 a = xf.transformPoint(prof(x0, u0)), c2 = xf.transformPoint(prof(x1, u0));
                    vec3 d = xf.transformPoint(prof(x1, u1)), e = xf.transformPoint(prof(x0, u1));
                    if (i < ribs - 1) { b.flatQuad(a, c2, d, e, i % 2 ? pink : vec4(1.f, 0.35f, 0.65f, 19)); b.flatQuad(e, d, c2, a, pink); }
                    b.tube(a, e, 0.25f, 0.25f, 5, steel);
                }
            }
            solid(c, 26, 12, c.y + 25.f);
        }
        // marquees: white peaked tents in rows either side of the central avenue
        int tents = 0;
        for (float n = -40.f; n <= 80.f; n += 22.f)
            for (float r = -190.f; r <= 190.f; r += 19.f) {
                if (std::fabs(r) < 22.f) continue;                       // the avenue
                if (r > 95.f && r < 150.f && n > 10.f) continue;          // the big wheel
                if (r < -95.f && r > -165.f && n > 30.f) continue;        // rides
                if (fr.f() < 0.18f) continue;
                vec3 c = at(r + fr.range(-2.f, 2.f), n + fr.range(-3.f, 3.f));
                float s = fr.range(4.f, 6.5f), H = 2.6f;
                bool accent = fr.f() < 0.3f;
                mat4 xf = basis(Rt, vec3(0, 1, 0), N, c);
                b.box(vec3(0, H * 0.5f, 0), vec3(s, H * 0.5f, s), white, &xf);
                vec3 top = xf.transformPoint(vec3(0, H + s * 0.75f, 0));
                vec3 q[4] = {xf.transformPoint(vec3(-s - 0.3f, H, -s - 0.3f)), xf.transformPoint(vec3(s + 0.3f, H, -s - 0.3f)),
                             xf.transformPoint(vec3(s + 0.3f, H, s + 0.3f)), xf.transformPoint(vec3(-s - 0.3f, H, s + 0.3f))};
                for (int k = 0; k < 4; k++) {
                    b.flatTri(q[(k + 1) % 4], q[k], top, accent && k % 2 ? (fr.f() < 0.5f ? pink : teal) : white);
                    // a string of bulbs along each roof edge
                    for (int j = 1; j < 5; j++) b.box(lerp(q[k], top, j / 5.f) + vec3(0, 0.08f, 0), vec3(0.09f, 0.09f, 0.09f), bulbs[(j + k) % 3]);
                }
                b.box(vec3(0, 1.2f, -s - 0.02f), vec3(s * 0.6f, 0.9f, 0.03f), vec4(1.f, 0.85f, 0.6f, 20), &xf);  // lit opening
                {   // stall banner over the opening: takoyaki, yakisoba, kakigori...
                    int cell = fr.f() < 0.7f ? SH_FEST0 + fr.irange(0, SH_FEST_N - 1) : SH_SHOP0 + fr.irange(0, SH_SHOP_N - 1);
                    signBoardBox(b, xf.transformPoint(vec3(0, H - 0.3f, -s - 0.1f)), -N, std::min(s * 0.8f, 3.2f), 0.4f, 0.05f, cell, false, false, 0.2f);
                }
                solid(c, s, s, c.y + H + s * 0.75f);
                tents++;
            }
        // grandstands facing the road either side of the hub entrance
        for (int k = -1; k <= 1; k += 2) {
            vec3 hc = world.hubPos - N * 36.f + Rt * (k * 100.f);
            hc.y = world.heightAt(hc.x, hc.z);
            if (world.roadDistAt(hc.x, hc.z) < 12.f || world.roadDistAt((hc + Rt * 34.f).x, (hc + Rt * 34.f).z) < 9.f ||
                world.roadDistAt((hc - Rt * 34.f).x, (hc - Rt * 34.f).z) < 9.f) hc = hc + N * 14.f;  // keep it off the road where it curves
            mat4 xf = basis(Rt, vec3(0, 1, 0), N, hc);
            const vec4 seats[] = {{0.9f, 0.15f, 0.45f, 0}, {0.15f, 0.7f, 0.85f, 0}, {0.95f, 0.85f, 0.2f, 0}};
            for (int t = 0; t < 7; t++) {
                b.box(vec3(0, 0.6f + t * 0.9f, -6.f + t * 1.7f), vec3(34, 0.6f + t * 0.45f * 0.0f, 0.85f), vec4(0.45f, 0.45f, 0.47f, 0), &xf);
                b.box(vec3(0, 1.35f + t * 0.9f, -6.f + t * 1.7f), vec3(33, 0.18f, 0.6f), seats[t % 3], &xf);
            }
            b.box(vec3(0, 3.5f, 6.5f), vec3(34, 3.5f, 0.3f), vec4(0.3f, 0.3f, 0.33f, 0), &xf);   // back wall
            for (int p = -2; p <= 2; p++) b.box(vec3(p * 16.f, 7.5f, 6.f), vec3(0.3f, 4.f, 0.3f), steel, &xf);
            mat4 rf = xf * translate(vec3(0, 11.2f, 0.f)) * rotateX(0.12f);
            b.box(vec3(0, 0, 0), vec3(35, 0.2f, 8.f), white, &rf);                              // roof
            b.box(vec3(0, 11.f, -7.9f), vec3(30, 0.9f, 0.1f), vec4(0.06f, 0.04f, 0.08f, 0), &xf);   // LED banner
            for (int j = 0; j < 4; j++)
                signFitted(b, xf.transformPoint(vec3(-22.5f + j * 15.f, 11.f, -8.02f)), -N, 7.f, 0.85f, j % 2 ? SH_WELCOME : SH_FESTIVAL, false, 1.f);
            solid(hc, 35, 8, hc.y + 12.f);
        }
        // the entrance arch over the avenue, and nobori flags down both sides of it
        {
            vec3 c = at(0, -56);
            mat4 xf = basis(Rt, vec3(0, 1, 0), N, c);
            const vec4 red(0.82f, 0.12f, 0.08f, 0);
            for (int k = -1; k <= 1; k += 2) {
                b.box(vec3(k * 11.5f, 4.6f, 0), vec3(0.45f, 4.6f, 0.45f), red, &xf);
                b.box(vec3(k * 11.5f, 0.4f, 0), vec3(0.65f, 0.4f, 0.65f), vec4(0.1f, 0.1f, 0.1f, 0), &xf);
            }
            b.box(vec3(0, 9.5f, 0), vec3(13.5f, 0.35f, 0.5f), vec4(0.08f, 0.08f, 0.09f, 0), &xf);
            b.box(vec3(0, 7.6f, 0), vec3(9.6f, 2.0f, 0.18f), signBoard(SH_FESTIVAL, false), &xf);
            signFitted(b, xf.transformPoint(vec3(0, 7.6f, -0.2f)), -N, 9.4f, 1.9f, SH_FESTIVAL, false, 0.4f);
            signFitted(b, xf.transformPoint(vec3(0, 7.6f, 0.2f)), N, 9.4f, 1.9f, SH_WELCOME, false, 0.4f);
            for (int k = -1; k <= 1; k += 2) solid(xf.transformPoint(vec3(k * 11.5f, 0, 0)), 0.6f, 0.6f, c.y + 9.5f);
            for (float n = -44.f; n < 98.f; n += 7.f)
                for (int sd = -1; sd <= 1; sd += 2) {
                    vec3 q = at(sd * 12.2f, n);
                    b.cylinder(q, 0.035f, 4.2f, 5, vec4(0.85f, 0.85f, 0.86f, 7));
                    int cell = SV_SHOP0 + fr.irange(0, SV_SHOP_N - 1);
                    vec3 cc = q + vec3(0, 2.75f, 0) + N * 0.36f;
                    signFace(b, cc + Rt * 0.01f, Rt, 0.34f, 1.3f, cell, true);
                    signFace(b, cc - Rt * 0.01f, -Rt, 0.34f, 1.3f, cell, true);
                }
        }
        // drop tower and a swing ride
        {
            vec3 c = at(-130, 70);
            b.cylinder(c, 1.4f, 62.f, 12, steel);
            for (int j = 0; j < 12; j++) b.cylinder(c + vec3(0, 4.f + j * 4.8f, 0), 1.55f, 0.35f, 12, bulbs[j % 3]);
            b.cylinder(c + vec3(0, 38.f, 0), 3.4f, 1.6f, 16, pink);
            b.cylinder(c + vec3(0, 62.f, 0), 2.f, 2.f, 12, white);
            solid(c, 3.5f, 3.5f, c.y + 62.f);
            vec3 sw = at(-110, 110);
            b.cylinder(sw, 0.9f, 16.f, 10, steel);
            b.cone(sw + vec3(0, 15.f, 0), 9.f, 4.f, 20, teal);
            for (int j = 0; j < 16; j++) {
                float a = j / 16.f * 2 * PI;
                vec3 tip = sw + vec3(std::cos(a) * 11.f, 7.f, std::sin(a) * 11.f), hang = sw + vec3(std::cos(a) * 8.6f, 15.f, std::sin(a) * 8.6f);
                b.tube(hang, tip, 0.04f, 0.04f, 3, steel);
                b.box(tip, vec3(0.35f, 0.25f, 0.35f), j % 2 ? pink : white);
                b.box(sw + vec3(std::cos(a) * 9.f, 15.f, std::sin(a) * 9.f), vec3(0.15f, 0.15f, 0.15f), bulbs[j % 3]);
            }
            solid(sw, 2, 2, sw.y + 16.f);
        }
        // festoon lights: strings of bulbs on poles down the avenue and round the grounds
        {
            auto festoon = [&](vec3 a, vec3 c2) {
                vec3 pa = a + vec3(0, 6.5f, 0), pc = c2 + vec3(0, 6.5f, 0);
                b.cylinder(a, 0.12f, 6.6f, 6, steel);
                float L = length(pc - pa);
                int nb = int(L / 1.8f);
                for (int j = 1; j < nb; j++) {
                    float u = j / float(nb);
                    vec3 q = lerp(pa, pc, u) - vec3(0, 1.4f * 4.f * u * (1.f - u), 0);
                    b.box(q, vec3(0.13f, 0.13f, 0.13f), bulbs[j % 3]);
                }
            };
            // red paper lanterns (chochin) hung down the avenue, matsuri style
            for (float n = -50.f; n < 100.f; n += 3.f)
                for (int sd = -1; sd <= 1; sd += 2) {
                    vec3 l = at(sd * 9.5f, n) + vec3(0, 4.2f, 0);
                    b.sphere(l, 0.42f, 1, vec4(1.f, 0.2f, 0.1f, 15));
                    b.box(l + vec3(0, 0.45f, 0), vec3(0.2f, 0.06f, 0.2f), vec4(0.1f, 0.1f, 0.1f, 0));
                    b.box(l - vec3(0, 0.45f, 0), vec3(0.2f, 0.06f, 0.2f), vec4(0.1f, 0.1f, 0.1f, 0));
                }
            for (float n = -50.f; n < 100.f; n += 25.f) {
                festoon(at(-14, n), at(-14, n + 25.f));
                festoon(at(14, n), at(14, n + 25.f));
                festoon(at(-14, n), at(14, n + 12.f));
            }
            for (int k = 0; k < 24; k++) {
                float a0 = k / 24.f * 2 * PI, a1 = (k + 1) / 24.f * 2 * PI;
                vec3 p0 = F + vec3(std::cos(a0) * 205.f, 0, std::sin(a0) * 135.f), p1 = F + vec3(std::cos(a1) * 205.f, 0, std::sin(a1) * 135.f);
                p0.y = world.heightAt(p0.x, p0.z); p1.y = world.heightAt(p1.x, p1.z);
                festoon(p0, p1);
            }
        }
        // light towers round the edge
        for (int k = 0; k < 10; k++) {
            float a = k / 10.f * 2 * PI;
            vec3 c = F + vec3(std::cos(a) * 225.f, 0, std::sin(a) * 160.f);
            c.y = world.heightAt(c.x, c.z);
            b.cylinder(c, 0.35f, 20.f, 8, steel);
            b.box(c + vec3(0, 20.5f, 0), vec3(1.6f, 0.9f, 0.4f), vec4(1.f, 0.95f, 0.85f, 20));
        }
        // hot air balloons drifting over the grounds
        for (int k = 0; k < 7; k++) {
            vec3 c = F + vec3(fr.range(-450.f, 450.f), 0, fr.range(-300.f, 350.f));
            c.y = world.heightAt(c.x, c.z) + fr.range(90.f, 220.f);
            vec4 col = k % 3 == 0 ? vec4(0.95f, 0.3f, 0.2f, 19) : k % 3 == 1 ? vec4(0.95f, 0.8f, 0.2f, 19) : vec4(0.25f, 0.55f, 0.95f, 19);
            b.sphere(c, 8.f, 2, col);
            b.cone(c - vec3(0, 11.5f, 0), 3.f, 5.f, 10, col);
            b.box(c - vec3(0, 14.5f, 0), vec3(1.2f, 0.8f, 1.2f), vec4(0.45f, 0.3f, 0.18f, 0));
        }
        b.build(festSite);
        SDL_Log("Festival: %d marquees", tents);
    }
    world.rebuildBoxGrid();

    // the big wheel at the east end of the grounds: legs and hub are static, the rim turns
    vec3 right = Rt, fwd = -N;
    ferrisPos = at(122, 40);
    ferrisAxis = right;
    const float R0 = 26.f, Hh = R0 + 6.f;
    {
        MeshBuilder b;
        vec4 white(0.9f, 0.9f, 0.92f, 7);
        for (int s = -1; s <= 1; s += 2) {
            vec3 top = ferrisPos + vec3(0, Hh, 0) + right * (s * 3.2f);
            b.tube(ferrisPos + fwd * 11.f + right * (s * 5.f), top, 0.6f, 0.45f, 8, white);
            b.tube(ferrisPos - fwd * 11.f + right * (s * 5.f), top, 0.6f, 0.45f, 8, white);
        }
        b.tube(ferrisPos + vec3(0, Hh, 0) - right * 3.6f, ferrisPos + vec3(0, Hh, 0) + right * 3.6f, 1.1f, 1.1f, 12, white);
        b.box(ferrisPos + vec3(0, 1.5f, 0), vec3(6, 1.5f, 4), vec4(0.85f, 0.15f, 0.4f, 19));  // ticket booth
        b.build(ferrisStatic);
    }
    {
        // rim in local space (axis = x), spokes, neon rings
        MeshBuilder b;
        vec4 rim(0.92f, 0.92f, 0.95f, 7), pink(1.f, 0.2f, 0.6f, 15), cyan(0.2f, 0.9f, 1.f, 15);
        const int S = 48;
        for (int k = 0; k < S; k++) {
            float a0 = k / float(S) * 2 * PI, a1 = (k + 1) / float(S) * 2 * PI;
            for (int s = -1; s <= 1; s += 2) {
                vec3 p0(s * 2.2f, std::cos(a0) * R0, std::sin(a0) * R0), p1(s * 2.2f, std::cos(a1) * R0, std::sin(a1) * R0);
                b.tube(p0, p1, 0.35f, 0.35f, 5, rim);
                b.tube(p0 * vec3(1.f, 1.02f, 1.02f), p1 * vec3(1.f, 1.02f, 1.02f), 0.16f, 0.16f, 4, k % 2 ? pink : cyan);
            }
        }
        for (int k = 0; k < 16; k++) {
            float a = k / 16.f * 2 * PI;
            for (int s = -1; s <= 1; s += 2) b.tube(vec3(s * 3.f, 0, 0), vec3(s * 2.2f, std::cos(a) * R0, std::sin(a) * R0), 0.18f, 0.18f, 4, rim);
        }
        b.build(ferrisMesh);
        MeshBuilder g;
        g.box(vec3(0, -2.2f, 0), vec3(1.5f, 1.3f, 1.3f), vec4(0.95f, 0.75f, 0.2f, 19));
        g.box(vec3(0, -0.6f, 0), vec3(0.08f, 0.6f, 0.08f), rim);
        g.box(vec3(0, -2.0f, 1.31f), vec3(1.2f, 0.6f, 0.02f), vec4(1.f, 0.85f, 0.6f, 20));
        g.build(gondolaMesh);
        { MeshBuilder rb; buildRiverboat(rb, true); rb.build(riverboatMesh); }
        { MeshBuilder sb; buildSwanBoat(sb); sb.build(swanMesh); }
    }
}

void Game::rebuildCatMesh() {
    MeshBuilder b;
    for (size_t i = 0; i < cats.size(); i++) {
        if (catHit[i]) continue;
        const LuckyCat& c = cats[i];
        mat4 xf = translate(c.p) * rotateY(c.yaw);
        vec4 post(0.3f, 0.3f, 0.32f, 7), board(0.98f, 0.78f, 0.16f, 19), face(0.97f, 0.97f, 0.95f, 21), red(0.9f, 0.1f, 0.12f, 21), glow(1.f, 0.4f, 0.75f, 15);
        b.box(vec3(-1.4f, 1.3f, 0), vec3(0.09f, 1.3f, 0.09f), post, &xf);
        b.box(vec3(1.4f, 1.3f, 0), vec3(0.09f, 1.3f, 0.09f), post, &xf);
        b.box(vec3(0, 2.7f, 0), vec3(1.7f, 1.0f, 0.08f), board, &xf);
        b.box(vec3(0, 2.7f, 0), vec3(1.8f, 1.08f, 0.05f), glow, &xf);   // neon edge
        // a beckoning cat face: white head, ears, red collar (both sides)
        for (int s = -1; s <= 1; s += 2) {
            float z = s * 0.1f;
            b.box(vec3(0, 2.75f, z), vec3(0.62f, 0.5f, 0.02f), face, &xf);
            b.box(vec3(-0.42f, 3.35f, z), vec3(0.16f, 0.16f, 0.02f), face, &xf);
            b.box(vec3(0.42f, 3.35f, z), vec3(0.16f, 0.16f, 0.02f), face, &xf);
            b.box(vec3(0, 2.2f, z), vec3(0.5f, 0.07f, 0.025f), red, &xf);
            b.box(vec3(-0.22f, 2.85f, z * 1.2f), vec3(0.07f, 0.09f, 0.02f), vec4(0.05f, 0.05f, 0.05f, 21), &xf);
            b.box(vec3(0.22f, 2.85f, z * 1.2f), vec3(0.07f, 0.09f, 0.02f), vec4(0.05f, 0.05f, 0.05f, 21), &xf);
        }
    }
    b.build(catMesh);
    catDirty = false;
}

void Game::updateFestival(float dt) {
    festShow = std::max(0.f, festShow - dt);
    ferrisAng += dt * 0.12f;
    boatT += dt;
    // fireworks over the festival from dusk till dawn
    if (R.night > 0.2f && length(cam.pos - world.festCenter) < 3500.f) {
        fwTimer -= dt;
        if (fwTimer <= 0) {
            fwTimer = 0.35f + float(rand() % 100) * 0.012f;
            vec3 c = world.festCenter + vec3(float(rand() % 300 - 150), 0, float(rand() % 200 - 60));
            c.y = world.festCenter.y + 110.f + float(rand() % 90);
            const vec3 cols[] = {{2.6f, 0.6f, 1.4f}, {0.6f, 1.8f, 2.8f}, {2.8f, 2.0f, 0.5f}, {2.6f, 0.8f, 0.3f}, {1.2f, 2.6f, 0.9f}, {2.6f, 2.6f, 2.6f}};
            vec3 col = cols[rand() % 6], col2 = cols[rand() % 6];
            float sp = 14.f + float(rand() % 100) * 0.12f;
            for (int k = 0; k < 190; k++) {
                float u = float(rand() % 1000) / 1000.f * 2.f - 1.f, a = float(rand() % 1000) / 1000.f * 2.f * PI;
                float rr = std::sqrt(1.f - u * u);
                vec3 d(rr * std::cos(a), u, rr * std::sin(a));
                fx.glow(c, d * sp * (0.85f + float(rand() % 100) * 0.003f), 1.6f + float(rand() % 100) * 0.008f, 1.5f, (k % 3 ? col : col2) * 1.6f, 3.5f);
            }
            fx.glow(c, vec3(0, 0, 0), 0.22f, 14.f, col * 1.2f, 0.f);  // the flash
            for (int k = 0; k < 12; k++) fx.glow(c - vec3(0, k * 9.f, 0), vec3(0, -2.f, 0), 0.4f + k * 0.02f, 0.3f, vec3(1.5f, 1.0f, 0.5f), 0.f);  // the rocket's trail
        }
    }
    if (state != ST_FREEROAM && state != ST_RACE) return;
    Vehicle& c = player.car;
    float v = c.speed();
    kmDriven += v * dt / 1000.f;
    festKmAcc += v * dt / 1000.f;
    if (festKmAcc > 0.1f) { festEvent(FS_KM, festKmAcc); festKmAcc = 0; }
    festEvent(FS_SPEED, c.speedKmh());
    // smash a lucky cat board
    for (size_t i = 0; i < cats.size(); i++) {
        if (catHit[i]) continue;
        if (std::fabs(cats[i].p.x - c.pos.x) > 4.f || std::fabs(cats[i].p.z - c.pos.z) > 4.f) continue;
        if (std::hypot(cats[i].p.x - c.pos.x, cats[i].p.z - c.pos.z) > 3.6f || v < 4.f) continue;
        catHit[i] = 1;
        catDirty = true;
        int got = 0;
        for (auto h : catHit) got += h;
        bool tenth = got % 10 == 0;
        credits += 2500;
        addXP(1000);
        if (tenth) wheelspins++;
        showPopup("LUCKY CAT", std::to_string(got) + " / " + std::to_string(cats.size()), tenth ? "+1,000 XP  WHEELSPIN!" : "+1,000 XP  +2,500 CR", 0, tenth, P_GOLD);
        audio.beep(1320, 0.15f);
        shake = std::max(shake, 0.25f);
        for (int k = 0; k < 40; k++) {
            vec3 d(float(rand() % 200 - 100) * 0.06f, float(rand() % 100) * 0.09f, float(rand() % 200 - 100) * 0.06f);
            fx.glow(cats[i].p + vec3(0, 2.6f, 0), c.vel * 0.4f + d, 0.6f + float(rand() % 100) * 0.006f, 0.12f, k % 2 ? vec3(2.5f, 1.8f, 0.4f) : vec3(2.5f, 0.6f, 1.6f), 6.f);
        }
        festEvent(FS_CAT, 1);
        save();
    }
}

void Game::drawFestivalWorld(const Camera& cam_) {
    (void)cam_;
    if (catDirty) rebuildCatMesh();
    R.object.set("uModel", mat4());
    catMesh.draw();
    festSite.draw();
    ferrisStatic.draw();
    vec3 hubTop = ferrisPos + vec3(0, 32.f, 0);
    mat4 rot = basis(ferrisAxis, vec3(0, 1, 0), cross(ferrisAxis, vec3(0, 1, 0)), hubTop) * rotateX(ferrisAng);
    R.object.set("uModel", rot);
    ferrisMesh.draw();
    // gondolas hang level from the rim
    vec3 fw = cross(ferrisAxis, vec3(0, 1, 0));
    for (int k = 0; k < 16; k++) {
        float a = ferrisAng + k / 16.f * 2 * PI;
        vec3 p = hubTop + vec3(0, std::cos(a) * 26.f, 0) + fw * (std::sin(a) * 26.f);
        R.object.set("uModel", basis(ferrisAxis, vec3(0, 1, 0), fw, p));
        gondolaMesh.draw();
    }
    // riverboats poled up and down the canal: one in each long pool between the weirs, some in
    // pairs passing each other (they keep to the left, as boats do in Japan)
    const auto& path = world.canalPath;
    if (path.size() > 3) {
        std::vector<float> acc(path.size(), 0.f);
        for (size_t k = 1; k < path.size(); k++) acc[k] = acc[k - 1] + length(vec3(path[k].x - path[k - 1].x, 0, path[k].z - path[k - 1].z));
        int boatN = 0;
        for (size_t pi = 0; pi < world.canalPools.size() && boatN < 14; pi++) {
            int j0 = world.canalPools[pi].first, j1 = world.canalPools[pi].second;
            float s0 = acc[j0] + 9.f, s1 = acc[j1] - 9.f;
            if (s1 - s0 < 50.f) continue;
            int inPool = s1 - s0 > 220.f ? 2 : 1;
            for (int bi = 0; bi < inPool; bi++, boatN++) {
                const float v = 1.2f + 0.15f * (boatN % 3), pause = 6.f;
                float run = (s1 - s0) / v, cyc = 2 * (run + pause);
                float tc = std::fmod(boatT + boatN * 37.f + bi * run, cyc);
                float sPos;
                bool back;
                if (tc < run) { sPos = s0 + tc * v; back = false; }
                else if (tc < run + pause) { sPos = s1; back = false; }
                else if (tc < 2 * run + pause) { sPos = s1 - (tc - run - pause) * v; back = true; }
                else { sPos = s0; back = true; }
                size_t k = size_t(j0) + 1;
                while (k < size_t(j1) && acc[k] < sPos) k++;
                float u = clampf((sPos - acc[k - 1]) / std::max(acc[k] - acc[k - 1], 1e-3f), 0.f, 1.f);
                vec3 p = lerp(path[k - 1], path[k], u), d = path[k] - path[k - 1];
                d.y = 0;
                d = normalize(d);
                vec3 side(-d.z, 0, d.x);
                p = p + side * (((back ? 1 : 0) ^ bi) ? 0.1f : -2.4f);  // two lanes; the moored boats lie along the far wall
                float yaw = std::atan2(d.x, d.z);  // poled, never turned: the bow always points up the canal
                vec3 f(std::sin(yaw), 0, std::cos(yaw)), r(f.z, 0, -f.x);
                p.y = path[k].y - 0.15f + std::sin(boatT * 1.3f + boatN) * 0.03f;
                R.object.set("uModel", basis(r, vec3(0, 1, 0), f, p));
                riverboatMesh.draw();
            }
        }
    }
    // swan boats paddling round the torii out in the lake
    if (world.lakeTorii.y > -1e8f)
        for (int k = 0; k < 4; k++) {
            float rad = 18.f + k * 7.f, spd = (0.9f + 0.15f * k) / rad * (k % 2 ? -1.f : 1.f);
            float a = boatT * spd + k * 1.7f;
            vec3 p = world.lakeTorii + vec3(std::cos(a) * rad, 0, std::sin(a) * rad);
            p.y = world.waterLevel - 0.12f + std::sin(boatT * 1.7f + k) * 0.04f;
            vec3 f = normalize(vec3(-std::sin(a), 0, std::cos(a)) * (spd > 0 ? 1.f : -1.f)), r(f.z, 0, -f.x);
            mat4 m = basis(r, vec3(0, 1, 0), f, p) * rotateZ(std::sin(boatT * 1.1f + k) * 0.03f);
            R.object.set("uModel", m);
            swanMesh.draw();
        }
    R.object.set("uModel", mat4());
}

void Game::drawFestivalHud() {
    if (festDay < 0) return;
    float u = hud.ui;
    float x = 18 * u, y = 142 * u, w = std::max(250 * u, hudPanelW);
    float a = festShow > 0 ? 1.f : 0.72f;
    // widen to the longest line so progress numbers never run off the panel
    for (int k = 0; k < 3; k++) w = std::max(w, hud.textWidth(13 * u, std::string(kPool[festPick[k]].text) + "   00,000/00,000") + 44 * u);
    float ph = 24 * u + 21 * u * 3 + 20 * u;  // includes the lucky cats line
    themePanel(x, y, w, ph, 0.85f + 0.15f * a);
    char head[64];
    std::snprintf(head, sizeof(head), "FESTIVAL PLAYLIST   %d/3", __builtin_popcount(unsigned(festDone)));
    {   // title on a black slanted tab
        float tw = hud.textWidth(12 * u, head) + 28 * u;
        hud.rect(x, y, tw, 20 * u, wa::INK);
        hud.text(x + 12 * u, y + 4 * u, 12 * u, head, wa::PAPER, 0, false);
    }
    for (int k = 0; k < 3; k++) {
        const Challenge& c = kPool[festPick[k]];
        bool done = festDone & (1 << k);
        float ry = y + 24 * u + k * 21 * u;
        char buf[96];
        if (done) std::snprintf(buf, sizeof(buf), "%s", c.text);
        else if (c.type == FS_SPEED) std::snprintf(buf, sizeof(buf), "%s   %d", c.text, int(festProg[k]));
        else if (c.type == FS_KM) std::snprintf(buf, sizeof(buf), "%s   %.1f", c.text, festProg[k]);
        else if (c.type == FS_SKILL) std::snprintf(buf, sizeof(buf), "%s   %s", c.text, fmtThousands(int(festProg[k])).c_str());
        else std::snprintf(buf, sizeof(buf), "%s   %d/%d", c.text, int(festProg[k]), int(c.target));
        hud.rect(x + 14 * u, ry + 4 * u, 9 * u, 9 * u, done ? wa::VERM : wa::A(wa::INK, 0.3f));
        hud.text(x + 30 * u, ry, 13 * u, buf, done ? wa::MOSS : wa::INK, 0, false);
        if (!done) {
            hud.rect(x + 30 * u, ry + 17 * u, w - 44 * u, 2 * u, wa::A(wa::INK, 0.15f));
            hud.rect(x + 30 * u, ry + 17 * u, (w - 44 * u) * clampf(festProg[k] / c.target, 0.f, 1.f), 2 * u, wa::VERM);
        }
    }
    int got = 0;
    for (auto h : catHit) got += h;
    char cb[48];
    std::snprintf(cb, sizeof(cb), "LUCKY CATS %d/%zu    STREAK %d", got, cats.size(), streak);
    hud.text(x + 14 * u, y + 27 * u + 21 * u * 3, 11 * u, cb, wa::MUTED, 0, false);
}

void Game::drawCatsOnMinimap(vec2 c, float Rr, std::function<vec2(vec3)> toScreen) {
    for (size_t i = 0; i < cats.size(); i++) {
        if (catHit[i]) continue;
        vec2 p = toScreen(cats[i].p);
        vec2 d = p - c;
        if (length(d) > Rr - 6.f) continue;
        hud.circle(p, 3.5f * hud.ui, vec4(0.05f, 0.05f, 0.05f, 0.9f), 8);
        hud.circle(p, 2.5f * hud.ui, P_GOLD, 8);
    }
}
