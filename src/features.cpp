// Horizon-style systems: XP & levels, wheelspins, skill chains, speed traps / zones,
// rewind, photo mode, car upgrades.
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static const vec4 F_ACCENT(1.0f, 0.42f, 0.12f, 1.f);
static const vec4 F_WHITE(1, 1, 1, 1);
static const vec4 F_DIM(1, 1, 1, 0.6f);
static const vec4 F_PINK(1.f, 0.25f, 0.55f, 1.f);
static const vec4 F_TRAP(1.f, 0.55f, 0.1f, 1.f);
static const vec4 F_ZONE(0.25f, 0.7f, 1.f, 1.f);

static std::string fname(const CarSpec& s) { return s.make + " " + s.model; }
static int stockPaint_(const CarSpec& s) {
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < kPaintCount; i++) {
        vec3 d = kPaints[i] - s.color;
        if (dot(d, d) < bd) { bd = dot(d, d); best = i; }
    }
    return best;
}
static int classRank(char c) { return c == 'S' ? 4 : c == 'A' ? 3 : c == 'B' ? 2 : c == 'C' ? 1 : 0; }
static char piClass(int pi) { return pi >= 800 ? 'S' : pi >= 640 ? 'A' : pi >= 540 ? 'B' : pi >= 430 ? 'C' : 'D'; }

// ====================================================================== XP
void Game::addXP(int amount) {
    if (amount <= 0) return;
    xp += amount;
    while (xp >= xpToNext()) {
        xp -= xpToNext();
        level++;
        wheelspins++;
        showPopup("LEVEL UP", "Level " + std::to_string(level), "WHEELSPIN EARNED", 0, true, F_PINK);
        audio.beep(1568, 0.5f);
    }
}

// ====================================================================== upgrades
CarSpec Game::tunedSpec(const OwnedCar& oc) const {
    CarSpec s = kCars[oc.spec];
    s.powerKW *= 1.f + 0.11f * oc.up[0];
    s.topKmh *= 1.f + 0.035f * oc.up[0];
    s.massKg *= 1.f - 0.045f * oc.up[1];
    s.grip += 0.035f * oc.up[2];
    s.offroad = std::min(1.f, s.offroad + 0.02f * oc.up[2]);
    char c = piClass(rateCar(s).pi);
    if (classRank(c) > classRank(s.cls)) s.cls = c;
    return s;
}

void Game::rebuildTuned() {
    tuned.resize(garage.size());
    for (size_t i = 0; i < garage.size(); i++) tuned[i] = tunedSpec(garage[i]);
}

int Game::upgradeCost(const OwnedCar& oc, int part) const {
    int base = std::max(kCars[oc.spec].price, 30000);
    const float mul[3] = {0.14f, 0.10f, 0.08f};
    int lvl = oc.up[part] + 1;
    return int(base * mul[part] * lvl / 500.f + 0.5f) * 500;
}

void Game::drawUpgrades() {
    float u = hud.ui;
    OwnedCar& oc = garage[subSel];
    CarSpec before = kCars[oc.spec];
    CarSpec now = tunedSpec(oc);
    hud.rectV(0, 0, hud.w, 150 * u, vec4(0.01f, 0.01f, 0.02f, 0.85f), vec4(0, 0, 0, 0));
    float x0 = 48 * u;
    hud.text(x0, 30 * u, 13 * u, "MY GARAGE  /  UPGRADES", F_DIM, 0, false);
    hud.text(x0, 48 * u, 38 * u, "UPGRADE SHOP", F_WHITE);
    hud.rect(x0, 98 * u, 90 * u, 4 * u, F_ACCENT);
    hud.text(hud.w - x0, 34 * u, 26 * u, "CR " + fmtThousands(credits), F_WHITE, 2);
    hud.text(hud.w - x0, 70 * u, 14 * u, fname(before), F_DIM, 2, false);

    float pw = 520 * u, px = x0, py = 140 * u;
    const char* names[3] = {"ENGINE", "WEIGHT REDUCTION", "TYRES & SUSPENSION"};
    const char* desc[3] = {"Intake, exhaust, ECU and forced induction", "Lightweight panels, seats and glass", "Stickier compound, stiffer springs"};
    for (int i = 0; i < 3; i++) {
        float ry = py + i * 104 * u;
        bool sel = i == upSel;
        hud.rectH(px, ry, pw, 92 * u, sel ? vec4(0.12f, 0.12f, 0.13f, 0.95f) : vec4(0.05f, 0.05f, 0.06f, 0.85f), vec4(0.05f, 0.05f, 0.06f, 0.5f));
        hud.rect(px, ry, 5 * u, 92 * u, sel ? F_ACCENT : vec4(1, 1, 1, 0.15f));
        hud.text(px + 22 * u, ry + 12 * u, 20 * u, names[i], F_WHITE, 0, false);
        hud.text(px + 22 * u, ry + 38 * u, 13 * u, desc[i], F_DIM, 0, false);
        for (int k = 0; k < 3; k++) {
            float bx = px + 22 * u + k * 46 * u;
            hud.rect(bx, ry + 64 * u, 40 * u, 8 * u, k < oc.up[i] ? F_ACCENT : vec4(1, 1, 1, 0.15f));
        }
        std::string price = oc.up[i] >= 3 ? "MAXED" : "CR " + fmtThousands(upgradeCost(oc, i));
        vec4 pc = oc.up[i] >= 3 ? vec4(0.4f, 1, 0.5f, 1) : (credits >= upgradeCost(oc, i) ? F_WHITE : vec4(1, 0.45f, 0.4f, 1));
        hud.text(px + pw - 22 * u, ry + 14 * u, 20 * u, price, pc, 2, false);
        if (oc.up[i] < 3) hud.text(px + pw - 22 * u, ry + 44 * u, 12 * u, "Stage " + std::to_string(oc.up[i] + 1), F_DIM, 2, false);
    }
    // performance comparison
    CarRatings rb = rateCar(before), rn = rateCar(now);
    OwnedCar next = oc;
    if (next.up[upSel] < 3) next.up[upSel]++;
    CarSpec ns = tunedSpec(next);
    CarRatings rx = rateCar(ns);
    float qx = hud.w - 430 * u, qy = 140 * u, qw = 382 * u;
    hud.rectH(qx, qy, qw, 300 * u, vec4(0.03f, 0.03f, 0.04f, 0.55f), vec4(0.03f, 0.03f, 0.04f, 0.9f));
    hud.rect(qx, qy, 4 * u, 300 * u, F_ACCENT);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%c %d", now.cls, rn.pi);
    hud.text(qx + 22 * u, qy + 16 * u, 13 * u, "PERFORMANCE INDEX", F_DIM, 0, false);
    hud.text(qx + 22 * u, qy + 34 * u, 34 * u, buf, F_WHITE);
    if (rx.pi != rn.pi) {
        std::snprintf(buf, sizeof(buf), "->  %c %d", ns.cls, rx.pi);
        hud.text(qx + 160 * u, qy + 44 * u, 20 * u, buf, vec4(0.4f, 1, 0.5f, 1), 0, false);
    }
    const char* labels[] = {"SPEED", "ACCELERATION", "HANDLING", "OFF-ROAD"};
    float vb[] = {rb.speed, rb.accel, rb.handling, rb.offroad};
    float vn[] = {rn.speed, rn.accel, rn.handling, rn.offroad};
    float vx[] = {rx.speed, rx.accel, rx.handling, rx.offroad};
    for (int i = 0; i < 4; i++) {
        float ry = qy + 96 * u + i * 36 * u;
        hud.text(qx + 22 * u, ry, 12 * u, labels[i], F_DIM, 0, false);
        float bx = qx + 22 * u, by = ry + 18 * u, bw = qw - 44 * u;
        hud.rect(bx, by, bw, 7 * u, vec4(1, 1, 1, 0.1f));
        hud.rect(bx, by, bw * vx[i] / 10.f, 7 * u, vec4(0.3f, 1, 0.45f, 0.9f));
        hud.rect(bx, by, bw * vn[i] / 10.f, 7 * u, F_ACCENT);
        hud.rect(bx, by, bw * vb[i] / 10.f, 7 * u, F_WHITE);
    }
    std::snprintf(buf, sizeof(buf), "%d HP   %d KG   grip %.2f", int(now.powerKW * 1.341f), int(now.massKg), now.grip);
    hud.text(qx + 22 * u, qy + 252 * u, 14 * u, buf, F_WHITE, 0, false);
    hud.text(hud.w * 0.5f, hud.h - 38 * u, 14 * u, pad ? "D-pad select   (A) buy upgrade   (B) back" : "UP/DOWN select   ENTER buy upgrade   ESC back", vec4(1, 1, 1, 0.75f), 1, false);
    float ty = hud.h * 0.72f;
    for (auto& t : toasts) { hud.text(hud.w * 0.5f, ty, 22 * u, t.text, t.col, 1); ty += 30 * u; }
}

// ====================================================================== PR stunts
void Game::buildStunts() {
    stunts.clear();
    for (int r = 0; r < int(world.roads.size()); r++) {
        const Road& rd = world.roads[r];
        if (rd.surface != SURF_ASPHALT || rd.length < 1200.f) continue;
        int n = int(rd.pts.size());
        // speed trap on the straightest stretch
        int best = -1;
        float bestCurv = 1e9f;
        for (int i = 30; i < n - 30; i += 3) {
            float c = 0;
            for (int k = -20; k <= 20; k += 4) c += 1.f - dot(rd.tan[(i + k + n) % n], rd.tan[i]);
            if (c < bestCurv) { bestCurv = c; best = i; }
        }
        if (best >= 0) {
            PRStunt t;
            t.type = 0;
            t.name = rd.name + " Speed Trap";
            t.road = r;
            t.a = best;
            t.thr[0] = 120; t.thr[1] = 165; t.thr[2] = 210;
            stunts.push_back(t);
        }
        // speed zone over ~650 m starting well away from the trap
        int za = (best + n / 2) % n;
        if (!rd.closed) za = std::clamp(n / 4, 10, n - 200);
        float d0 = rd.dist[za];
        int zb = za;
        for (int k = 1; k < n; k++) {
            int j = (za + k) % n;
            if (!rd.closed && j < za) break;
            float d = rd.dist[j] - d0;
            if (d < 0) d += rd.length;
            if (d > 650.f) { zb = j; break; }
        }
        if (zb != za) {
            PRStunt z;
            z.type = 1;
            z.name = rd.name + " Speed Zone";
            z.road = r;
            z.a = za;
            z.b = zb;
            z.thr[0] = 100; z.thr[1] = 135; z.thr[2] = 170;
            stunts.push_back(z);
        }
    }
    // roadside boards
    MeshBuilder b;
    auto board = [&](const Road& rd, int i, vec4 col) {
        vec3 p = rd.pts[i], t = rd.tan[i], right(-t.z, 0, t.x);
        for (int side = -1; side <= 1; side += 2) {
            vec3 base = p + right * (side * (rd.width * 0.5f + 2.2f));
            base.y = world.heightAt(base.x, base.z);
            float yaw = std::atan2(t.x, t.z);
            mat4 xf = translate(base) * rotateY(yaw);
            b.box(vec3(0, 2.0f, 0), vec3(0.09f, 2.0f, 0.09f), vec4(0.55f, 0.56f, 0.58f, 7), &xf);
            b.box(vec3(0, 3.9f, 0), vec3(1.5f, 0.75f, 0.08f), vec4(0.12f, 0.12f, 0.13f, 9), &xf);
            b.box(vec3(0, 3.9f, -0.09f), vec3(1.38f, 0.63f, 0.02f), vec4(col.x * 1.6f, col.y * 1.6f, col.z * 1.6f, 3), &xf);
            b.box(vec3(0, 3.9f, 0.09f), vec3(1.38f, 0.63f, 0.02f), vec4(col.x * 1.6f, col.y * 1.6f, col.z * 1.6f, 3), &xf);
        }
    };
    for (auto& s : stunts) {
        const Road& rd = world.roads[s.road];
        board(rd, s.a, s.type == 0 ? F_TRAP : F_ZONE);
        if (s.type == 1) board(rd, s.b, F_ZONE);
    }
    b.build(stuntMesh);
}

void Game::showPopup(const std::string& title, const std::string& name, const std::string& value, int stars, bool best, vec4 col) {
    popup = {title, name, value, stars, best, 4.5f, col};
}

static int starsFor(const PRStunt& s, float v) { return v >= s.thr[2] ? 3 : v >= s.thr[1] ? 2 : v >= s.thr[0] ? 1 : 0; }

void Game::updateStunts(float dt) {
    if (popup.t > 0) popup.t -= dt;
    Vehicle& c = player.car;
    vec3 p = c.pos;
    for (int i = 0; i < int(stunts.size()); i++) {
        PRStunt& s = stunts[i];
        const Road& rd = world.roads[s.road];
        vec3 a = rd.pts[s.a], t = rd.tan[s.a];
        vec3 d = p - a;
        float along = d.x * t.x + d.z * t.z;
        float side = std::fabs(d.x * -t.z + d.z * t.x);
        float prevAlong = along - (c.vel.x * t.x + c.vel.z * t.z) * dt;
        bool crossed = side < rd.width * 0.5f + 6.f && prevAlong < 0 && along >= 0 && std::fabs(d.y) < 6.f;
        bool crossedRev = side < rd.width * 0.5f + 6.f && prevAlong > 0 && along <= 0 && std::fabs(d.y) < 6.f;
        if (s.type == 0 && (crossed || crossedRev)) {
            float kmh = c.speedKmh();
            int st = starsFor(s, kmh);
            bool nb = kmh > s.best;
            if (nb) s.best = kmh;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %s", speedStr(kmh / 3.6f).c_str(), settings.mph ? "MPH" : "KM/H");
            showPopup("SPEED TRAP", s.name, buf, st, nb, F_TRAP);
            addXP(st * 150 + (nb ? 100 : 0) + 50);
            credits += st * 500;
            audio.beep(st >= 2 ? 1400 : 900, 0.18f);
        }
        if (s.type == 1) {
            if (zoneActive < 0 && crossed) {
                zoneActive = i;
                zoneTime = 0;
                zoneDist = rd.dist[s.b] - rd.dist[s.a];
                if (zoneDist < 0) zoneDist += rd.length;
                audio.beep(1000, 0.08f);
            }
        }
    }
    if (zoneActive >= 0) {
        PRStunt& s = stunts[zoneActive];
        const Road& rd = world.roads[s.road];
        zoneTime += dt;
        int r, smp;
        bool onRoad = world.nearestRoad(p, r, smp, rd.width * 0.5f + 25.f) && r == s.road;
        vec3 b = rd.pts[s.b], tb = rd.tan[s.b];
        vec3 d = p - b;
        float along = d.x * tb.x + d.z * tb.z;
        float side = std::fabs(d.x * -tb.z + d.z * tb.x);
        if (!onRoad || zoneTime > 120.f) {
            zoneActive = -1;
        } else if (along >= 0 && side < rd.width * 0.5f + 6.f) {
            float avg = zoneDist / std::max(zoneTime, 0.1f) * 3.6f;
            int st = starsFor(s, avg);
            bool nb = avg > s.best;
            if (nb) s.best = avg;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %s AVG", speedStr(avg / 3.6f).c_str(), settings.mph ? "MPH" : "KM/H");
            showPopup("SPEED ZONE", s.name, buf, st, nb, F_ZONE);
            addXP(st * 200 + (nb ? 100 : 0) + 50);
            credits += st * 700;
            audio.beep(st >= 2 ? 1400 : 900, 0.18f);
            zoneActive = -1;
        }
    }
}

void Game::drawPopup() {
    float u = hud.ui;
    // live speed-zone readout
    if (zoneActive >= 0 && state == ST_FREEROAM) {
        float avg = 0;
        const PRStunt& s = stunts[zoneActive];
        const Road& rd = world.roads[s.road];
        int r, smp;
        if (world.nearestRoad(player.car.pos, r, smp, 50.f) && r == s.road) {
            float d = rd.dist[smp] - rd.dist[s.a];
            if (d < 0) d += rd.length;
            avg = d / std::max(zoneTime, 0.1f) * 3.6f;
        }
        float w = 300 * u, x = hud.w * 0.5f - w * 0.5f, y = 96 * u;
        hud.rectH(x, y, w * 0.5f, 54 * u, vec4(0.02f, 0.05f, 0.1f, 0.0f), vec4(0.02f, 0.05f, 0.1f, 0.85f));
        hud.rectH(x + w * 0.5f, y, w * 0.5f, 54 * u, vec4(0.02f, 0.05f, 0.1f, 0.85f), vec4(0.02f, 0.05f, 0.1f, 0.0f));
        hud.text(hud.w * 0.5f, y + 4 * u, 12 * u, "SPEED ZONE", F_ZONE, 1, false);
        hud.text(hud.w * 0.5f, y + 18 * u, 26 * u, speedStr(avg / 3.6f) + (settings.mph ? " MPH" : " KM/H"), F_WHITE, 1);
    }
    if (popup.t <= 0) return;
    float a = clampf(popup.t * 3.f, 0.f, 1.f) * clampf((4.5f - popup.t) * 4.f, 0.f, 1.f);
    float slide = (1.f - clampf((4.5f - popup.t) * 4.f, 0.f, 1.f)) * 40 * u;
    float w = 460 * u, h = popup.stars > 0 || popup.title == "SPEED TRAP" || popup.title == "SPEED ZONE" ? 132 * u : 104 * u;
    float x = hud.w - w - 10 * u + slide, y = hud.h * 0.30f;
    vec4 col = popup.col;
    hud.rectH(x, y, w * 0.5f, h, vec4(0.03f, 0.03f, 0.04f, 0.0f), vec4(0.03f, 0.03f, 0.04f, 0.9f * a));
    hud.rectH(x + w * 0.5f, y, w * 0.5f, h, vec4(0.03f, 0.03f, 0.04f, 0.9f * a), vec4(0.03f, 0.03f, 0.04f, 0.0f));
    hud.rect(x + w * 0.2f, y, w * 0.6f, 3 * u, vec4(col.x, col.y, col.z, a));
    float cx = x + w * 0.5f;
    hud.text(cx, y + 10 * u, 14 * u, popup.title, vec4(col.x, col.y, col.z, a), 1, false);
    hud.text(cx, y + 28 * u, 15 * u, popup.name, vec4(1, 1, 1, 0.7f * a), 1, false);
    hud.text(cx, y + 48 * u, 32 * u, popup.value, vec4(1, 1, 1, a), 1);
    if (popup.title == "SPEED TRAP" || popup.title == "SPEED ZONE") {
        for (int i = 0; i < 3; i++) {
            vec2 c(cx + (i - 1) * 34 * u, y + 104 * u);
            bool on = i < popup.stars;
            // five-point star
            vec2 pts[10];
            for (int k = 0; k < 10; k++) {
                float ang = -PI * 0.5f + k * PI / 5.f;
                float r = (k % 2 ? 6.f : 14.f) * u;
                pts[k] = c + vec2(std::cos(ang), std::sin(ang)) * r;
            }
            vec4 sc = on ? vec4(1.f, 0.8f, 0.15f, a) : vec4(1, 1, 1, 0.18f * a);
            for (int k = 0; k < 10; k++) hud.triangle(c, pts[k], pts[(k + 1) % 10], sc);
        }
        if (popup.best) hud.text(cx + 80 * u, y + 96 * u, 13 * u, "NEW BEST", vec4(0.4f, 1, 0.5f, a), 0, false);
    } else if (popup.best) {
        hud.text(cx, y + 84 * u, 13 * u, wheelspins > 0 ? "Open the pause menu to spin" : "", vec4(1, 1, 1, 0.7f * a), 1, false);
    }
}

void Game::drawStuntsOnMinimap(vec2 c, float Rr, std::function<vec2(vec3)> toScreen) {
    float u = hud.ui;
    for (auto& s : stunts) {
        const Road& rd = world.roads[s.road];
        vec2 p = toScreen(rd.pts[s.a]);
        if (length(p - c) > Rr - 6 * u) continue;
        vec4 col = s.type == 0 ? F_TRAP : F_ZONE;
        hud.rect(p.x - 5.5f * u, p.y - 5.5f * u, 11 * u, 11 * u, vec4(0.05f, 0.05f, 0.05f, 1));
        hud.rect(p.x - 4 * u, p.y - 4 * u, 8 * u, 8 * u, col);
    }
}

// ====================================================================== skill chain
void Game::chainAdd(const std::string& skill, int points) {
    if (points <= 0) return;
    chainScore += points * chainMult;
    if (!chainSkills.empty() && chainSkills.back().first == skill) chainSkills.back().second += int(points * chainMult);
    else {
        chainSkills.push_back({skill, int(points * chainMult)});
        chainMult = std::min(5.f, chainMult + 0.25f);
        if (chainSkills.size() > 4) chainSkills.erase(chainSkills.begin());
    }
    chainTimer = 0;
    chainFlash = 0.25f;
}

void Game::chainBank() {
    if (chainScore >= 100.f) {
        int cr = int(chainScore / 5.f);
        int x = int(chainScore / 4.f);
        credits += cr;
        addXP(x);
        toast("SKILL CHAIN  " + fmtThousands(int(chainScore)) + "   +" + fmtThousands(cr) + " CR  +" + fmtThousands(x) + " XP", F_ACCENT);
        audio.beep(1250, 0.12f);
    }
    chainScore = 0;
    chainMult = 1;
    chainSkills.clear();
}

void Game::chainBreak() {
    if (chainScore >= 100.f) {
        toast("SKILL CHAIN BROKEN", vec4(1, 0.35f, 0.3f, 1));
        audio.beep(200, 0.25f);
    }
    chainScore = 0;
    chainMult = 1;
    chainSkills.clear();
}

void Game::updateSkills(float dt) {
    Vehicle& c = player.car;
    float v = c.speed();
    if (chainFlash > 0) chainFlash -= dt;
    if (c.impact > 7.f && chainScore > 0) { chainBreak(); c.impact = 0; }
    bool any = false;
    // drift
    bool drifting = c.grounded && v > 9.f && std::fabs(c.slipAngle) > 0.26f && c.forwardSpeed() > 5.f;
    if (drifting) { chainAdd("DRIFT", int(v * std::fabs(c.slipAngle) * dt * 14.f + 0.5f)); any = true; }
    driftScore = 0;
    // speed skill above 180 km/h on the ground
    if (c.grounded && c.speedKmh() > 180.f) {
        speedSkillAcc += (c.speedKmh() - 150.f) * dt * 0.6f;
        if (speedSkillAcc > 10.f) { chainAdd("SPEED", int(speedSkillAcc)); speedSkillAcc = 0; }
        any = true;
    }
    // air
    if (c.airTime > 0) { lastAirT = c.airTime; any = true; }
    else if (lastAirT > 0.45f) {
        chainAdd(lastAirT > 1.5f ? "BIG AIR" : "AIR", int(lastAirT * 350.f));
        lastAirT = 0;
        any = true;
    } else lastAirT = 0;
    // near misses and passes in races
    if (nearMissCool > 0) nearMissCool -= dt;
    if (state == ST_RACE && nearMissCool <= 0) {
        for (auto& b : bots) {
            vec3 d = b.car.pos - c.pos;
            float dist = std::sqrt(d.x * d.x + d.z * d.z);
            vec3 rv = c.vel - b.car.vel;
            float rs = std::sqrt(rv.x * rv.x + rv.z * rv.z);
            if (dist < 3.6f && dist > 2.0f && rs > 6.f && c.impact < 1.f) {
                chainAdd("NEAR MISS", 250);
                nearMissCool = 1.0f;
                any = true;
                break;
            }
        }
    }
    if (!any && chainScore > 0) {
        chainTimer += dt;
        if (chainTimer > 2.2f) chainBank();
    }
}

void Game::drawChain() {
    if (chainScore < 1.f) return;
    float u = hud.ui;
    float y = hud.h * 0.24f;
    float pulse = 1.f + std::max(0.f, chainFlash) * 0.4f;
    float t = clampf(1.f - chainTimer / 2.2f, 0.f, 1.f);
    hud.text(hud.w * 0.5f, y, 13 * u, "SKILL CHAIN", vec4(1, 1, 1, 0.75f), 1, false);
    char m[16];
    std::snprintf(m, sizeof(m), "x%.2f", chainMult);
    hud.text(hud.w * 0.5f, y + 16 * u, 34 * u * pulse, fmtThousands(int(chainScore)), F_WHITE, 1);
    hud.text(hud.w * 0.5f + 90 * u, y + 24 * u, 18 * u, m, F_ACCENT, 0, false);
    float bw = 180 * u;
    hud.rect(hud.w * 0.5f - bw * 0.5f, y + 60 * u, bw, 3 * u, vec4(1, 1, 1, 0.15f));
    hud.rect(hud.w * 0.5f - bw * 0.5f, y + 60 * u, bw * t, 3 * u, F_ACCENT);
    float ly = y + 70 * u;
    for (int i = int(chainSkills.size()) - 1; i >= 0; i--) {
        auto& sk = chainSkills[i];
        float al = i == int(chainSkills.size()) - 1 ? 1.f : 0.55f;
        hud.text(hud.w * 0.5f, ly, 14 * u, sk.first + "  " + fmtThousands(sk.second), vec4(1, 1, 1, al), 1, false);
        ly += 18 * u;
    }
}

// ====================================================================== rewind
void Game::recordRewind() {
    if (rewindBuf.size() != 360) { rewindBuf.assign(360, {}); rewindHead = rewindCount = 0; }
    if (++rewindTick % 2) return;
    Snapshot& s = rewindBuf[rewindHead];
    s.racers.clear();
    s.racers.push_back(player);
    for (auto& b : bots) s.racers.push_back(b);
    s.raceTime = raceTime;
    s.camYaw = camYaw;
    rewindHead = (rewindHead + 1) % 360;
    rewindCount = std::min(rewindCount + 1, 360);
}

bool Game::stepRewind() {
    if (rewindCount <= 1) return false;
    rewindHead = (rewindHead - 1 + 360) % 360;
    rewindCount--;
    Snapshot& s = rewindBuf[(rewindHead - 1 + 360) % 360];
    if (s.racers.empty() || s.racers.size() != bots.size() + 1) return false;
    player = s.racers[0];
    for (size_t i = 0; i < bots.size(); i++) bots[i] = s.racers[i + 1];
    raceTime = s.raceTime;
    camYaw = s.camYaw;
    chainScore = 0;
    chainSkills.clear();
    chainMult = 1;
    return true;
}

bool Game::rewindHeld() {
    const bool* k = SDL_GetKeyboardState(nullptr);
    bool held = k[SDL_SCANCODE_BACKSPACE];
    if (pad && nearEvent < 0 && !nearHub && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH)) held = true;
    return held && !autopilot;
}

// ====================================================================== photo mode
void Game::enterPhoto() {
    photoReturn = state;
    state = ST_PHOTO;
    photoYaw = camYaw - player.car.yaw + PI + 0.6f;
    photoPitch = 0.18f;
    photoDist = player.car.spec->length * 1.35f + 1.5f;
    photoFov = 0.85f;
    photoHideUi = false;
    audio.engineOn = 0;
}

void Game::updatePhoto(float dt) {
    const bool* k = SDL_GetKeyboardState(nullptr);
    float yr = 0, pr = 0, zr = 0, fr = 0;
    if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]) yr -= 1;
    if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) yr += 1;
    if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) pr += 1;
    if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) pr -= 1;
    if (k[SDL_SCANCODE_Q]) zr -= 1;
    if (k[SDL_SCANCODE_E]) zr += 1;
    if (k[SDL_SCANCODE_Z]) fr -= 1;
    if (k[SDL_SCANCODE_X]) fr += 1;
    if (pad) {
        float lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f, ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
        float rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.f, ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.f;
        if (std::fabs(lx) > 0.15f) yr += lx;
        if (std::fabs(ly) > 0.15f) pr -= ly;
        if (std::fabs(ry) > 0.15f) zr += ry;
        if (std::fabs(rx) > 0.15f) fr += rx;
    }
    photoYaw += yr * dt * 1.4f;
    photoPitch = clampf(photoPitch + pr * dt * 0.9f, -0.05f, 1.3f);
    photoDist = clampf(photoDist + zr * dt * 6.f, 2.5f, 30.f);
    photoFov = clampf(photoFov + fr * dt * 0.5f, 0.25f, 1.4f);
    Vehicle& c = player.car;
    float a = c.yaw + photoYaw;
    vec3 off(std::sin(a) * std::cos(photoPitch), std::sin(photoPitch), std::cos(a) * std::cos(photoPitch));
    vec3 tgt = c.pos + vec3(0, 0.6f, 0);
    vec3 p = tgt + off * photoDist;
    float g = world.heightAt(p.x, p.z) + 0.35f;
    if (p.y < g) p.y = g;
    cam.pos = p;
    cam.target = tgt;
    cam.fov = photoFov;
    cam.aspect = float(R.width) / float(R.height);
    cam.update();
}

void Game::drawPhoto() {
    if (photoHideUi) return;
    float u = hud.ui;
    hud.rect(0, 0, hud.w, 54 * u, vec4(0, 0, 0, 0.55f));
    hud.rect(0, hud.h - 54 * u, hud.w, 54 * u, vec4(0, 0, 0, 0.55f));
    hud.text(40 * u, 14 * u, 22 * u, "PHOTO MODE", F_WHITE);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f mm", 18.f / std::tan(photoFov * 0.5f));
    hud.text(hud.w - 40 * u, 18 * u, 16 * u, buf, F_DIM, 2, false);
    // rule-of-thirds guides
    for (int i = 1; i < 3; i++) {
        hud.rect(hud.w * i / 3.f, 54 * u, 1, hud.h - 108 * u, vec4(1, 1, 1, 0.12f));
        hud.rect(0, hud.h * i / 3.f, hud.w, 1, vec4(1, 1, 1, 0.12f));
    }
    hud.text(hud.w * 0.5f, hud.h - 36 * u, 14 * u,
             pad ? "L-stick orbit   R-stick zoom / lens   (A) take photo   (Y) hide UI   (B) exit"
                 : "WASD orbit   Q/E zoom   Z/X lens   ENTER take photo   H hide UI   ESC exit",
             vec4(1, 1, 1, 0.8f), 1, false);
}

// ====================================================================== wheelspin
void Game::startWheelspin() {
    if (wheelspins <= 0 || spinT >= 0) return;
    wheelspins--;
    spinReel.clear();
    uint32_t seed = uint32_t(SDL_GetTicks() * 2654435761u) ^ uint32_t(xp * 7919 + level);
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed & 0xFFFFFF) / float(0x1000000); };
    for (int i = 0; i < 40; i++) {
        SpinPrize p;
        float r = rnd();
        if (r < 0.12f) {
            // a car you don't own yet, weighted towards cheaper ones
            std::vector<int> pool;
            for (int k = 0; k < int(kCars.size()); k++) {
                bool owned = false;
                for (auto& g : garage) owned |= g.spec == k;
                if (!owned && kCars[k].price < 400000) pool.push_back(k);
            }
            if (!pool.empty()) p.car = pool[int(rnd() * pool.size()) % pool.size()];
            else p.credits = 50000;
        } else {
            const int tiers[] = {5000, 8000, 10000, 15000, 20000, 25000, 40000, 60000, 100000};
            float w = rnd();
            int t = w < 0.3f ? 0 : w < 0.5f ? 1 : w < 0.65f ? 2 : w < 0.77f ? 3 : w < 0.86f ? 4 : w < 0.92f ? 5 : w < 0.96f ? 6 : w < 0.99f ? 7 : 8;
            p.credits = tiers[t];
        }
        spinReel.push_back(p);
    }
    spinResult = 34;
    spinT = 0;
    spinPos = 0;
    audio.beep(1000, 0.1f);
}

void Game::updateWheelspin(float dt) {
    if (spinT < 0) return;
    float dur = 4.2f;
    float prev = spinPos;
    spinT += dt;
    float t = clampf(spinT / dur, 0.f, 1.f);
    float e = 1.f - std::pow(1.f - t, 3.f);
    spinPos = e * spinResult;
    if (int(spinPos) != int(prev)) audio.beep(700 + 600 * (1.f - t), 0.03f);
    if (spinT >= dur && spinT - dt < dur) {
        const SpinPrize& p = spinReel[spinResult];
        if (p.car >= 0) {
            garage.push_back({p.car, stockPaint_(kCars[p.car]), kCars[p.car].stripes});
            rebuildTuned();
            showPopup("WHEELSPIN", "New car", fname(kCars[p.car]), 0, true, F_PINK);
        } else {
            credits += p.credits;
            showPopup("WHEELSPIN", "Credits", "CR " + fmtThousands(p.credits), 0, false, F_PINK);
        }
        audio.beep(1760, 0.5f);
        save();
    }
}

void Game::drawWheelspin() {
    float u = hud.ui;
    hud.rectV(0, 0, hud.w, hud.h, vec4(0.08f, 0.02f, 0.06f, 0.88f), vec4(0.02f, 0.01f, 0.03f, 0.94f));
    float x0 = 48 * u;
    hud.text(x0, 30 * u, 13 * u, "FESTIVAL  /  REWARDS", F_DIM, 0, false);
    hud.text(x0, 48 * u, 38 * u, "WHEELSPIN", F_WHITE);
    hud.rect(x0, 98 * u, 90 * u, 4 * u, F_PINK);
    hud.text(hud.w - x0, 34 * u, 26 * u, std::to_string(wheelspins) + (wheelspins == 1 ? " SPIN" : " SPINS"), F_WHITE, 2);
    hud.text(hud.w - x0, 70 * u, 14 * u, "Level " + std::to_string(level) + "   |   CR " + fmtThousands(credits), F_DIM, 2, false);
    // reel
    float cy = hud.h * 0.5f, rowH = 92 * u, w = 560 * u, x = hud.w * 0.5f - w * 0.5f;
    hud.rect(x - 12 * u, cy - rowH * 2.5f, w + 24 * u, rowH * 5, vec4(0, 0, 0, 0.45f));
    if (!spinReel.empty()) {
        hud.clip(0, 0, 0);
        int first = int(spinPos) - 3;
        for (int i = first; i <= first + 7; i++) {
            if (i < 0 || i >= int(spinReel.size())) continue;
            float yy = cy + (i - spinPos) * rowH - rowH * 0.5f;
            if (yy < cy - rowH * 2.5f - 1 || yy + rowH > cy + rowH * 2.5f + 1) continue;
            const SpinPrize& p = spinReel[i];
            bool car = p.car >= 0;
            vec4 col = car ? vec4(0.62f, 0.25f, 0.95f, 1) : p.credits >= 40000 ? vec4(1.f, 0.75f, 0.1f, 1) : p.credits >= 15000 ? vec4(0.3f, 0.7f, 1.f, 1) : vec4(0.6f, 0.6f, 0.64f, 1);
            hud.rectH(x, yy + 4 * u, w, rowH - 8 * u, vec4(col.x * 0.35f, col.y * 0.35f, col.z * 0.35f, 0.95f), vec4(0.06f, 0.06f, 0.07f, 0.95f));
            hud.rect(x, yy + 4 * u, 6 * u, rowH - 8 * u, col);
            if (car && carThumb[p.car]) hud.image(x + w - 150 * u, yy + 10 * u, 136 * u, rowH - 20 * u, carThumb[p.car], F_WHITE, vec2(0.05f, 0.95f), vec2(0.95f, 0.05f));
            hud.text(x + 28 * u, yy + 16 * u, 13 * u, car ? "CAR" : "CREDITS", F_DIM, 0, false);
            hud.text(x + 28 * u, yy + 36 * u, 28 * u, car ? fname(kCars[p.car]) : "CR " + fmtThousands(p.credits), F_WHITE);
        }
    }
    // selector
    hud.rect(x - 16 * u, cy - rowH * 0.5f, 4 * u, rowH, F_PINK);
    hud.rect(x + w + 12 * u, cy - rowH * 0.5f, 4 * u, rowH, F_PINK);
    hud.triangle(vec2(x - 30 * u, cy - 12 * u), vec2(x - 30 * u, cy + 12 * u), vec2(x - 18 * u, cy), F_PINK);
    hud.triangle(vec2(x + w + 30 * u, cy - 12 * u), vec2(x + w + 30 * u, cy + 12 * u), vec2(x + w + 18 * u, cy), F_PINK);
    hud.rectV(x - 12 * u, cy - rowH * 2.5f, w + 24 * u, rowH * 1.2f, vec4(0.03f, 0.01f, 0.03f, 0.95f), vec4(0.03f, 0.01f, 0.03f, 0));
    hud.rectV(x - 12 * u, cy + rowH * 1.3f, w + 24 * u, rowH * 1.2f, vec4(0.03f, 0.01f, 0.03f, 0), vec4(0.03f, 0.01f, 0.03f, 0.95f));
    if (spinReel.empty()) hud.text(hud.w * 0.5f, cy - 14 * u, 22 * u, wheelspins > 0 ? "Press ENTER to spin" : "Level up to earn wheelspins", F_WHITE, 1);
    // xp bar
    float bw = 420 * u, bx = hud.w * 0.5f - bw * 0.5f, by = hud.h - 110 * u;
    hud.text(bx, by - 22 * u, 13 * u, "LEVEL " + std::to_string(level), F_WHITE, 0, false);
    hud.text(bx + bw, by - 22 * u, 13 * u, fmtThousands(xp) + " / " + fmtThousands(xpToNext()) + " XP", F_DIM, 2, false);
    hud.rect(bx, by, bw, 8 * u, vec4(1, 1, 1, 0.12f));
    hud.rect(bx, by, bw * xp / float(xpToNext()), 8 * u, F_PINK);
    hud.text(hud.w * 0.5f, hud.h - 38 * u, 14 * u, pad ? "(A) spin   (B) back" : "ENTER spin   ESC back", vec4(1, 1, 1, 0.75f), 1, false);
    if (spinT >= 4.2f && spinResult >= 0 && spinResult < int(spinReel.size())) {
        const SpinPrize& p = spinReel[spinResult];
        float a = clampf((spinT - 4.2f) * 3.f, 0.f, 1.f);
        hud.text(x + w + 40 * u, cy - 30 * u, 14 * u, "YOU WON", vec4(1.f, 0.35f, 0.6f, a), 0, false);
        hud.text(x + w + 40 * u, cy - 10 * u, 24 * u, p.car >= 0 ? fname(kCars[p.car]) : "CR " + fmtThousands(p.credits), vec4(1, 1, 1, a), 0, true);
    }
}
