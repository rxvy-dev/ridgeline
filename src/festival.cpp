// The festival loop: a daily Festival Playlist of three challenges (finish all three for a
// wheelspin), a day streak bonus, lucky-cat boards hidden across the island to smash, and a
// livelier festival site with a big wheel lit up at night.
#include "game.h"
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

    // the big wheel behind the festival plaza: legs and hub are static, the rim turns
    vec3 fwd(std::sin(world.hubYaw), 0, std::cos(world.hubYaw)), right(-fwd.z, 0, fwd.x);
    ferrisPos = world.hubPos - fwd * 78.f + right * 34.f;
    ferrisPos.y = world.heightAt(ferrisPos.x, ferrisPos.z);
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
    R.object.set("uModel", mat4());
}

void Game::drawFestivalHud() {
    if (festDay < 0) return;
    float u = hud.ui;
    float x = 18 * u, y = 132 * u, w = 250 * u;
    float a = festShow > 0 ? 1.f : 0.72f;
    hud.rect(x, y, w, 24 * u + 21 * u * 3, vec4(0.03f, 0.03f, 0.04f, 0.62f * a));
    hud.rect(x, y, 5 * u, 24 * u + 21 * u * 3, P_GOLD);
    char head[64];
    std::snprintf(head, sizeof(head), "FESTIVAL PLAYLIST   %d/3", __builtin_popcount(unsigned(festDone)));
    hud.text(x + 14 * u, y + 5 * u, 12 * u, head, vec4(1.f, 0.8f, 0.2f, a), 0, false);
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
        hud.rect(x + 14 * u, ry + 4 * u, 9 * u, 9 * u, done ? vec4(0.4f, 1.f, 0.5f, a) : vec4(1, 1, 1, 0.25f * a));
        hud.text(x + 30 * u, ry, 13 * u, buf, done ? vec4(0.4f, 1.f, 0.5f, a) : vec4(1, 1, 1, 0.85f * a), 0, false);
        if (!done) hud.rect(x + 30 * u, ry + 17 * u, (w - 44 * u) * clampf(festProg[k] / c.target, 0.f, 1.f), 2 * u, vec4(1.f, 0.25f, 0.55f, a));
    }
    int got = 0;
    for (auto h : catHit) got += h;
    char cb[48];
    std::snprintf(cb, sizeof(cb), "LUCKY CATS %d/%zu    STREAK %d", got, cats.size(), streak);
    hud.text(x + 4 * u, y + 30 * u + 21 * u * 3, 11 * u, cb, vec4(1, 1, 1, 0.55f * a), 0, true);
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
