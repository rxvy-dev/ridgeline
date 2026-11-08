// Horizon-style systems: XP & levels, wheelspins, skill chains, speed traps / zones,
// rewind, photo mode, car upgrades.
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static const vec4 F_ACCENT(0.96f, 0.10f, 0.50f, 1.f);
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
    const float camberGrip[4] = {0.f, 0.015f, 0.025f, 0.012f};  // stance looks the part but gives some back
    s.grip += camberGrip[std::clamp(oc.camber, 0, 3)];
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
    float x0 = 48 * u;
    hud.rectV(0, hud.h - 160 * u, hud.w, 160 * u, vec4(0.07f, 0.34f, 0.32f, 0), vec4(0.07f, 0.34f, 0.32f, 0.85f));
    drawMenuHeader(x0, 30 * u, hud.w - 2 * x0, "UPGRADE SHOP");
    float slide = 1.f - clampf(stateTime / 0.4f, 0.f, 1.f);
    slide = slide * slide * slide;

    float pw = 520 * u, px = x0 - slide * 140 * u, py = 140 * u;
    themePanel(px - 12 * u, py - 12 * u, pw + 24 * u, 3 * 104 * u + 12 * u, 1.f, true);
    const char* names[3] = {"ENGINE", "WEIGHT REDUCTION", "TYRES & SUSPENSION"};
    const char* desc[3] = {"Intake, exhaust, ECU and forced induction", "Lightweight panels, seats and glass", "Stickier compound, stiffer springs"};
    for (int i = 0; i < 3; i++) {
        float ry = py + i * 104 * u;
        bool sel = i == upSel;
        themeRow(px, ry, pw, 92 * u, sel);
        hud.text(px + 22 * u, ry + 12 * u, 20 * u, names[i], F_WHITE, 0, false);
        hud.text(px + 22 * u, ry + 38 * u, 13 * u, desc[i], F_DIM, 0, false);
        for (int k = 0; k < 3; k++) {
            float bx = px + 22 * u + k * 46 * u;
            hud.rect(bx, ry + 64 * u, 40 * u, 8 * u, k < oc.up[i] ? vec4(0.80f, 0.94f, 0.16f, 1) : vec4(0, 0, 0, 0.3f));
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
    float qx = hud.w - 430 * u + slide * 140 * u, qy = 140 * u, qw = 382 * u;
    themePanel(qx, qy, qw, 300 * u, 1.f, true);
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
        hud.rect(bx, by, bw, 7 * u, vec4(0, 0, 0, 0.3f));
        hud.rect(bx, by, bw * vx[i] / 10.f, 7 * u, vec4(0.80f, 0.94f, 0.16f, 0.95f));
        hud.rect(bx, by, bw * vn[i] / 10.f, 7 * u, vec4(0.98f, 0.86f, 0.2f, 1));
        hud.rect(bx, by, bw * vb[i] / 10.f, 7 * u, F_WHITE);
    }
    std::snprintf(buf, sizeof(buf), "%d HP   %d KG   grip %.2f", int(now.powerKW * 1.341f), int(now.massKg), now.grip);
    hud.text(qx + 22 * u, qy + 252 * u, 14 * u, buf, F_WHITE, 0, false);
    drawThemePrompts(pad ? "D-pad select   (A) buy upgrade   (B) back" : "UP/DOWN select   ENTER buy upgrade   ESC back");
    drawMenuToasts(hud.h * 0.72f);
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
    if (stars > 0 && title != "CHALLENGE COMPLETE") festEvent(FS_STARS, float(stars));
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
        themePanel(x, y, w, 58 * u);
        {
            float tw = hud.textWidth(12 * u, "SPEED ZONE") + 26 * u, tx = hud.w * 0.5f - tw * 0.5f;
            hud.quad(vec2(tx + 6 * u, y - 10 * u), vec2(tx + tw + 6 * u, y - 10 * u), vec2(tx + tw, y + 10 * u), vec2(tx, y + 10 * u), vec4(0.04f, 0.04f, 0.04f, 0.95f));
            hud.text(hud.w * 0.5f + 3 * u, y - 6 * u, 12 * u, "SPEED ZONE", F_ZONE, 1, false);
        }
        hud.text(hud.w * 0.5f, y + 16 * u, 28 * u, speedStr(avg / 3.6f) + (settings.mph ? " MPH" : " KM/H"), F_WHITE, 1);
    }
    if (popup.t <= 0) return;
    float a = clampf(popup.t * 3.f, 0.f, 1.f) * clampf((4.5f - popup.t) * 4.f, 0.f, 1.f);
    float slide = (1.f - clampf((4.5f - popup.t) * 4.f, 0.f, 1.f)) * 40 * u;
    float w = 460 * u, h = popup.stars > 0 || popup.title == "SPEED TRAP" || popup.title == "SPEED ZONE" ? 132 * u : 104 * u;
    float x = hud.w - w - 10 * u + slide, y = hud.h * 0.30f;
    vec4 col = popup.col;
    w = 400 * u; x = hud.w - w - 24 * u + slide;
    themePanel(x, y, w, h, a);
    hud.rect(x, y, w, 4 * u, vec4(col.x, col.y, col.z, a));
    float cx = x + w * 0.5f;
    {   // the title on a black slanted tab poking out of the top
        float tw = hud.textWidth(14 * u, popup.title) + 34 * u, tx = cx - tw * 0.5f;
        hud.quad(vec2(tx + 8 * u, y - 12 * u), vec2(tx + tw + 8 * u, y - 12 * u), vec2(tx + tw, y + 14 * u), vec2(tx, y + 14 * u), vec4(0.04f, 0.04f, 0.04f, 0.95f * a));
        hud.text(cx + 4 * u, y - 7 * u, 14 * u, popup.title, vec4(col.x, col.y, col.z, a), 1, false);
    }
    hud.text(cx, y + 24 * u, 15 * u, popup.name, vec4(0.85f, 0.95f, 0.93f, a), 1, false);
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
            vec4 sc = on ? vec4(0.98f, 0.86f, 0.2f, a) : vec4(0, 0, 0, 0.35f * a);
            for (int k = 0; k < 10; k++) hud.triangle(c, pts[k], pts[(k + 1) % 10], sc);
        }
        if (popup.best) {
            float bw2 = hud.textWidth(12 * u, "NEW BEST") + 16 * u;
            hud.rect(cx + 70 * u, y + 94 * u, bw2, 20 * u, vec4(0.80f, 0.94f, 0.16f, a));
            hud.text(cx + 70 * u + bw2 * 0.5f, y + 97 * u, 12 * u, "NEW BEST", vec4(0.06f, 0.06f, 0.06f, a), 1, false);
        }
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
        festEvent(FS_SKILL, chainScore);
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
    if (drifting) {
        float pts = v * std::fabs(c.slipAngle) * dt * 14.f;
        chainAdd("DRIFT", int(pts + 0.5f));
        if (state == ST_RACE && raceEvent >= 0 && events[raceEvent].type == EV_DRIFT) driftTotal += pts * chainMult;
        any = true;
    }
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
    {
        float tw = hud.textWidth(12 * u, "SKILL CHAIN") + 26 * u, tx = hud.w * 0.5f - tw * 0.5f;
        hud.quad(vec2(tx + 6 * u, y - 4 * u), vec2(tx + tw + 6 * u, y - 4 * u), vec2(tx + tw, y + 15 * u), vec2(tx, y + 15 * u), vec4(0.04f, 0.04f, 0.04f, 0.9f));
        hud.text(hud.w * 0.5f + 3 * u, y - 1 * u, 12 * u, "SKILL CHAIN", vec4(0.80f, 0.94f, 0.16f, 1), 1, false);
    }
    char m[16];
    std::snprintf(m, sizeof(m), "x%.2f", chainMult);
    std::string sc = fmtThousands(int(chainScore));
    float sw = hud.textWidth(34 * u * pulse, sc);
    hud.text(hud.w * 0.5f, y + 18 * u, 34 * u * pulse, sc, F_WHITE, 1);
    {   // multiplier badge
        float mw = hud.textWidth(16 * u, m) + 16 * u, mx = hud.w * 0.5f + sw * 0.5f + 12 * u;
        hud.rect(mx, y + 28 * u, mw, 24 * u, vec4(0.80f, 0.94f, 0.16f, 1));
        hud.text(mx + mw * 0.5f, y + 31 * u, 16 * u, m, vec4(0.06f, 0.06f, 0.06f, 1), 1, false);
    }
    float bw = 200 * u;
    hud.rect(hud.w * 0.5f - bw * 0.5f, y + 62 * u, bw, 5 * u, vec4(0.07f, 0.34f, 0.32f, 0.8f));
    hud.rect(hud.w * 0.5f - bw * 0.5f, y + 62 * u, bw * t, 5 * u, vec4(0.96f, 0.10f, 0.50f, 1));
    float ly = y + 74 * u;
    for (int i = int(chainSkills.size()) - 1; i >= 0; i--) {
        auto& sk = chainSkills[i];
        float al = i == int(chainSkills.size()) - 1 ? 1.f : 0.55f;
        hud.text(hud.w * 0.5f, ly, 14 * u, sk.first + "  +" + fmtThousands(sk.second), vec4(1, 1, 1, al), 1, true);
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
    photoYaw += yr * dt * 1.4f + opt.orbit * dt;
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
    hud.rectV(0, 0, hud.w, 64 * u, vec4(0.14f, 0.48f, 0.44f, 0.8f), vec4(0.11f, 0.42f, 0.39f, 0.55f));
    hud.rect(0, 64 * u, hud.w, 3 * u, vec4(0.80f, 0.94f, 0.16f, 0.9f));
    hud.rectV(0, hud.h - 72 * u, hud.w, 72 * u, vec4(0.11f, 0.42f, 0.39f, 0.0f), vec4(0.11f, 0.42f, 0.39f, 0.7f));
    {
        float tw = hud.textWidth(22 * u, "PHOTO MODE") + 44 * u;
        hud.quad(vec2(40 * u + 12 * u, 12 * u), vec2(40 * u + tw + 12 * u, 12 * u), vec2(40 * u + tw, 52 * u), vec2(40 * u, 52 * u), vec4(0.04f, 0.04f, 0.04f, 0.95f));
        hud.text(40 * u + 6 * u + tw * 0.5f, 19 * u, 22 * u, "PHOTO MODE", F_WHITE, 1, false);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f mm", 18.f / std::tan(photoFov * 0.5f));
    float lw2 = hud.textWidth(16 * u, buf) + 24 * u;
    hud.rect(hud.w - 40 * u - lw2, 18 * u, lw2, 28 * u, vec4(0.92f, 0.92f, 0.92f, 0.95f));
    hud.text(hud.w - 40 * u - lw2 * 0.5f, 23 * u, 16 * u, buf, vec4(0.08f, 0.08f, 0.08f, 1), 1, false);
    hud.text(hud.w - 52 * u - lw2, 24 * u, 12 * u, "LENS", vec4(0.8f, 0.92f, 0.9f, 1), 2, false);
    // rule-of-thirds guides
    for (int i = 1; i < 3; i++) {
        hud.rect(hud.w * i / 3.f, 67 * u, 1, hud.h - 139 * u, vec4(1, 1, 1, 0.12f));
        hud.rect(0, hud.h * i / 3.f, hud.w, 1, vec4(1, 1, 1, 0.12f));
    }
    drawThemePrompts(pad ? "L-stick orbit   R-stick zoom / lens   (A) take photo   (Y) hide UI   (B) exit"
                         : "WASD orbit   Q/E zoom   Z/X lens   ENTER take photo   H hide UI   ESC exit");
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
                if (!owned && listed(kCars[k]) && kCars[k].price < 400000) pool.push_back(k);
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
    drawThemeBackdrop(0.94f);
    drawPetals(0.6f, R.time);
    float x0 = 48 * u;
    drawMenuHeader(x0, 30 * u, hud.w - 2 * x0, "WHEELSPIN");
    {
        std::string sr = std::to_string(wheelspins) + (wheelspins == 1 ? " SPIN READY" : " SPINS READY");
        float sw2 = hud.textWidth(14 * u, sr) + 28 * u;
        hud.rect(x0, 122 * u, sw2, 30 * u, vec4(0.96f, 0.10f, 0.50f, 0.95f));
        hud.text(x0 + 14 * u, 129 * u, 14 * u, sr, F_WHITE, 0, false);
    }
    // rarity: what colour a prize card is and what it's called
    auto rarity = [](const SpinPrize& p, std::string& tier) {
        if (p.car >= 0) { tier = "LEGENDARY CAR"; return vec4(0.98f, 0.70f, 0.10f, 1); }
        if (p.credits >= 40000) { tier = "EPIC"; return vec4(0.66f, 0.30f, 0.96f, 1); }
        if (p.credits >= 15000) { tier = "RARE"; return vec4(0.20f, 0.55f, 0.98f, 1); }
        tier = "COMMON";
        return vec4(0.50f, 0.56f, 0.62f, 1);
    };
    float cy = hud.h * 0.5f + 6 * u, rowH = 96 * u, w = 600 * u, x = hud.w * 0.5f - w * 0.5f;
    float top = cy - rowH * 2.5f, bot = cy + rowH * 2.5f;
    themePanel(x - 16 * u, top - 10 * u, w + 32 * u, bot - top + 20 * u, 1.f, true);
    bool landed = spinT >= 4.2f && spinResult >= 0 && spinResult < int(spinReel.size());
    if (!spinReel.empty()) {
        int first = int(spinPos) - 3;
        for (int i = first; i <= first + 7; i++) {
            if (i < 0 || i >= int(spinReel.size())) continue;
            float yy = cy + (i - spinPos) * rowH - rowH * 0.5f;
            if (yy < top - 1 || yy + rowH > bot + 1) continue;
            const SpinPrize& p = spinReel[i];
            std::string tier;
            vec4 col = rarity(p, tier);
            float d = std::fabs((yy + rowH * 0.5f) - cy) / (rowH * 2.5f);  // 0 at the selector, 1 at the edge
            float fade = 1.f - 0.65f * d;
            bool win = landed && i == spinResult;
            float cx0 = x + d * 18 * u, cw = w - d * 36 * u;
            hud.rectH(cx0, yy + 5 * u, cw, rowH - 10 * u, vec4(col.x * 0.85f, col.y * 0.85f, col.z * 0.85f, 0.95f * fade), vec4(col.x * 0.45f, col.y * 0.45f, col.z * 0.45f, 0.95f * fade));
            // diagonal light slab on each card
            hud.quad(vec2(cx0 + cw * 0.55f, yy + 5 * u), vec2(cx0 + cw * 0.72f, yy + 5 * u), vec2(cx0 + cw * 0.62f, yy + rowH - 5 * u), vec2(cx0 + cw * 0.45f, yy + rowH - 5 * u), vec4(1, 1, 1, 0.08f * fade));
            if (p.car >= 0 && carThumb[p.car]) hud.image(cx0 + cw - 170 * u, yy + 10 * u, 156 * u, rowH - 20 * u, carThumb[p.car], vec4(1, 1, 1, fade), vec2(0.05f, 0.95f), vec2(0.95f, 0.05f));
            float tw = hud.textWidth(11 * u, tier) + 16 * u;
            hud.rect(cx0 + 22 * u, yy + 16 * u, tw, 18 * u, vec4(0.04f, 0.04f, 0.04f, 0.85f * fade));
            hud.text(cx0 + 30 * u, yy + 19 * u, 11 * u, tier, vec4(col.x, col.y, col.z, fade), 0, false);
            hud.textFit(cx0 + 22 * u, yy + 40 * u, 30 * u, p.car >= 0 ? fname(kCars[p.car]) : "CR " + fmtThousands(p.credits), vec4(1, 1, 1, fade), 0, cw - (p.car >= 0 ? 210 : 44) * u, true);
            if (win) {
                float pulse = 0.6f + 0.4f * std::sin(R.time * 6.f), bb = 4 * u;
                vec4 fc(0.80f, 0.94f, 0.16f, pulse);
                hud.rect(cx0 - bb, yy + 5 * u - bb, cw + 2 * bb, bb, fc);
                hud.rect(cx0 - bb, yy + rowH - 5 * u, cw + 2 * bb, bb, fc);
                hud.rect(cx0 - bb, yy + 5 * u, bb, rowH - 10 * u, fc);
                hud.rect(cx0 + cw, yy + 5 * u, bb, rowH - 10 * u, fc);
            }
        }
    }
    // selector: yellow-green brackets with arrows either side of the centre row
    {
        vec4 sc(0.80f, 0.94f, 0.16f, 1);
        float sy0 = cy - rowH * 0.5f, sy1 = cy + rowH * 0.5f, L = 22 * u, t = 4 * u;
        for (int sx : {0, 1}) {
            float ex = sx ? x + w + 8 * u : x - 8 * u, dir = sx ? -1.f : 1.f;
            hud.rect(sx ? ex - t : ex, sy0, t, sy1 - sy0, sc);
            hud.rect(sx ? ex - L : ex, sy0, L, t, sc);
            hud.rect(sx ? ex - L : ex, sy1 - t, L, t, sc);
            float ax = sx ? x + w + 22 * u : x - 22 * u;
            hud.triangle(vec2(ax - dir * 12 * u, cy - 12 * u), vec2(ax - dir * 12 * u, cy + 12 * u), vec2(ax, cy), sc);
        }
    }
    if (spinReel.empty()) {
        std::string msg = wheelspins > 0 ? (pad ? "PRESS (A) TO SPIN" : "PRESS ENTER TO SPIN") : "LEVEL UP TO EARN WHEELSPINS";
        float mw = hud.textWidth(22 * u, msg) + 60 * u, mx = hud.w * 0.5f - mw * 0.5f;
        float pulse = 0.75f + 0.25f * std::sin(R.time * 3.f);
        hud.quad(vec2(mx + 14 * u, cy - 24 * u), vec2(mx + mw + 14 * u, cy - 24 * u), vec2(mx + mw, cy + 24 * u), vec2(mx, cy + 24 * u),
                 wheelspins > 0 ? vec4(0.80f, 0.94f, 0.16f, pulse) : vec4(0.04f, 0.04f, 0.04f, 0.9f));
        hud.text(mx + 7 * u + mw * 0.5f, cy - 12 * u, 22 * u, msg, wheelspins > 0 ? vec4(0.06f, 0.06f, 0.06f, 1) : F_WHITE, 1, false);
    }
    // level and XP under the reel
    float bw = 420 * u, bx = hud.w * 0.5f - bw * 0.5f, by = bot + 40 * u;
    std::string lv = std::to_string(level);
    float lvw = hud.textWidth(16 * u, lv) + 26 * u;
    hud.rect(bx, by - 10 * u, lvw, 26 * u, vec4(0.92f, 0.92f, 0.92f, 0.95f));
    hud.text(bx + lvw * 0.5f, by - 6 * u, 16 * u, lv, vec4(0.1f, 0.1f, 0.1f, 1), 1, false);
    hud.rect(bx + lvw, by - 10 * u, bw - lvw, 26 * u, vec4(0.07f, 0.34f, 0.32f, 0.85f));
    hud.rect(bx + lvw + 8 * u, by + 1 * u, (bw - lvw - 16 * u) * clampf(xp / float(xpToNext()), 0.f, 1.f), 4 * u, F_PINK);
    hud.text(bx + bw, by + 22 * u, 12 * u, fmtThousands(xp) + " / " + fmtThousands(xpToNext()) + " XP", vec4(0.8f, 0.92f, 0.9f, 1), 2, false);
    drawThemePrompts(pad ? "(A) spin   (B) back" : "ENTER spin   ESC back");
    // the prize, revealed on a card that pops out to the right
    if (landed) {
        const SpinPrize& p = spinReel[spinResult];
        std::string tier;
        vec4 col = rarity(p, tier);
        float t = clampf((spinT - 4.2f) / 0.35f, 0.f, 1.f), e = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
        float pw = std::min(330 * u, hud.w - (x + w + 60 * u) - x0), ph = 210 * u;
        if (pw > 160 * u) {
            float px = x + w + 50 * u + (1.f - e) * 60 * u, py = cy - ph * 0.5f;
            hud.rect(px + 7 * u, py + 9 * u, pw, ph, vec4(0, 0, 0, 0.3f * e));
            hud.rectV(px, py, pw, ph, vec4(col.x, col.y, col.z, e), vec4(col.x * 0.6f, col.y * 0.6f, col.z * 0.6f, e));
            hud.rect(px, py, pw, 6 * u, vec4(0.80f, 0.94f, 0.16f, e));
            for (int k = 0; k < 10; k++) {  // a burst of rays behind the prize
                float ang = R.time * 0.6f + k * PI / 5.f;
                vec2 c(px + pw * 0.5f, py + ph * 0.48f);
                hud.triangle(c, c + vec2(std::cos(ang), std::sin(ang)) * pw * 0.45f, c + vec2(std::cos(ang + 0.12f), std::sin(ang + 0.12f)) * pw * 0.45f, vec4(1, 1, 1, 0.10f * e));
            }
            hud.text(px + pw * 0.5f, py + 18 * u, 15 * u, "YOU WON", vec4(1, 1, 1, e), 1, false);
            hud.text(px + pw * 0.5f, py + 38 * u, 12 * u, tier, vec4(0.06f, 0.06f, 0.06f, 0.8f * e), 1, false);
            if (p.car >= 0 && carThumb[p.car]) hud.image(px + 20 * u, py + 58 * u, pw - 40 * u, 96 * u, carThumb[p.car], vec4(1, 1, 1, e), vec2(0.05f, 0.95f), vec2(0.95f, 0.05f));
            hud.textFit(px + pw * 0.5f, py + (p.car >= 0 ? 164 : 92) * u, p.car >= 0 ? 22 * u : 40 * u, p.car >= 0 ? fname(kCars[p.car]) : "CR " + fmtThousands(p.credits), vec4(1, 1, 1, e), 1, pw - 30 * u, true);
        }
    }
}

// ====================================================================== weather
// Dynamic weather: dry spells and showers roll in and out. Roads soak while it rains
// (puddles grow) and dry out slowly afterwards.
void Game::updateWeather(float dt) {
    int mode = settings.weather;
    if (opt.wet) mode = 3;
    if (raceEvent >= 0 && raceEvent < int(events.size()) && events[raceEvent].wet) mode = 2;
    if (opt.scene == "rain") mode = 2;
    switch (mode) {
        case 1: wxTarget = 0; break;
        case 2: wxTarget = 1; break;
        case 3: wxTarget = 0; wxWet = 1; break;
        default:
            wxTimer -= dt;
            if (wxTimer <= 0) {
                static uint32_t s = SDL_GetTicks() | 1u;
                s = s * 1664525u + 1013904223u;
                float r = (s >> 8) / 16777216.f;
                if (wxTarget > 0.5f) { wxTarget = 0; wxTimer = 240 + r * 300; }        // dry spell
                else if (r < 0.35f) { wxTarget = 0.6f + r; wxTimer = 120 + r * 360; }  // a shower
                else wxTimer = 120 + r * 200;
                if (wxTarget > 0.5f) toast("Rain is rolling in", vec4(0.6f, 0.75f, 1.f, 1));
            }
    }
    wxRain += (wxTarget - wxRain) * std::min(1.f, dt / 20.f);
    if (mode != 3) {
        if (wxRain > 0.25f) wxWet = std::min(1.f, wxWet + dt * wxRain / 30.f);
        else wxWet = std::max(0.f, wxWet - dt / 150.f);
    }
    if (!opt.scene.empty() && mode == 2) { wxRain = wxTarget; wxWet = 1; }
}

// ====================================================================== time of day
// A day lasts 24 real minutes. By day the photographed HDRI sky shows; towards evening
// it hands over to a procedural sky so the sun can set, then night (moon and stars)
// and a pink dawn.
static Atmosphere lerpAtmo(const Atmosphere& a, const Atmosphere& b, float t) {
    auto L = [t](vec3 x, vec3 y) { return x + (y - x) * t; };
    Atmosphere r;
    r.sunDir = normalize(L(a.sunDir, b.sunDir));
    r.sunColor = L(a.sunColor, b.sunColor);
    r.skyAmb = L(a.skyAmb, b.skyAmb);
    r.groundAmb = L(a.groundAmb, b.groundAmb);
    r.skyTop = L(a.skyTop, b.skyTop);
    r.skyHorizon = L(a.skyHorizon, b.skyHorizon);
    r.fogColor = L(a.fogColor, b.fogColor);
    r.fogDensity = a.fogDensity + (b.fogDensity - a.fogDensity) * t;
    return r;
}

Atmosphere Game::timeOfDayAtmo(float h, float& hdriW, float& night) const {
    const Atmosphere& day = eraAtmo[0];
    float az0 = std::atan2(day.sunDir.x, day.sunDir.z);
    auto sunAt = [&](float hr) {
        float el = std::sin((hr - 6.f) / 12.f * PI) * 0.9f;
        float az = az0 + (hr - 13.f) / 12.f * PI;
        float c = std::cos(el);
        return normalize(vec3(std::sin(az) * c, std::max(el, 0.03f), std::cos(az) * c));
    };
    const vec3 moon = normalize(vec3(0.35f, 0.62f, 0.55f));
    struct Key { float h; Atmosphere a; float w, n; };
    Atmosphere nightA = day;
    nightA.sunDir = moon;
    nightA.sunColor = vec3(0.22f, 0.28f, 0.45f);
    nightA.skyTop = vec3(0.012f, 0.02f, 0.055f);
    nightA.skyHorizon = vec3(0.05f, 0.065f, 0.12f);
    nightA.fogColor = vec3(0.04f, 0.05f, 0.09f);
    nightA.fogDensity = day.fogDensity * 0.9f;
    nightA.skyAmb = vec3(0.09f, 0.11f, 0.18f);
    nightA.groundAmb = vec3(0.035f, 0.04f, 0.06f);
    Atmosphere dawn = day;
    dawn.sunDir = sunAt(6.4f);
    dawn.sunColor = vec3(1.7f, 1.0f, 0.68f);
    dawn.skyTop = vec3(0.16f, 0.24f, 0.48f);
    dawn.skyHorizon = vec3(0.96f, 0.66f, 0.58f);
    dawn.fogColor = vec3(0.80f, 0.64f, 0.58f);
    dawn.skyAmb = vec3(0.36f, 0.34f, 0.42f);
    dawn.groundAmb = vec3(0.20f, 0.16f, 0.13f);
    Atmosphere morning = day;  // day colours with a moving sun, under the procedural sky
    morning.sunDir = sunAt(7.4f);
    Atmosphere afternoon = day;
    afternoon.sunDir = sunAt(16.8f);
    Atmosphere golden = day;
    golden.sunDir = sunAt(17.7f);
    golden.sunColor = vec3(2.1f, 1.3f, 0.68f);
    golden.skyTop = vec3(0.13f, 0.25f, 0.52f);
    golden.skyHorizon = vec3(0.98f, 0.74f, 0.50f);
    golden.fogColor = vec3(0.86f, 0.68f, 0.50f);
    golden.skyAmb = vec3(0.44f, 0.38f, 0.40f);
    golden.groundAmb = vec3(0.28f, 0.20f, 0.13f);
    Atmosphere sunset = golden;
    sunset.sunDir = sunAt(18.55f);
    sunset.sunColor = vec3(1.9f, 0.78f, 0.34f);
    sunset.skyTop = vec3(0.10f, 0.14f, 0.36f);
    sunset.skyHorizon = vec3(1.0f, 0.46f, 0.26f);
    sunset.fogColor = vec3(0.72f, 0.42f, 0.32f);
    sunset.skyAmb = vec3(0.30f, 0.22f, 0.28f);
    sunset.groundAmb = vec3(0.17f, 0.11f, 0.08f);
    Atmosphere dusk = nightA;
    dusk.sunDir = normalize(sunAt(19.2f) + moon);
    dusk.sunColor = vec3(0.35f, 0.25f, 0.35f);
    dusk.skyTop = vec3(0.04f, 0.05f, 0.14f);
    dusk.skyHorizon = vec3(0.32f, 0.17f, 0.20f);
    dusk.fogColor = vec3(0.14f, 0.10f, 0.14f);
    dusk.skyAmb = vec3(0.12f, 0.11f, 0.17f);
    dusk.groundAmb = vec3(0.05f, 0.04f, 0.05f);
    const Key keys[] = {
        {0.f, nightA, 0, 1}, {4.9f, nightA, 0, 1}, {6.1f, dawn, 0, 0.2f}, {7.3f, morning, 0, 0},
        {8.2f, day, 1, 0}, {16.4f, day, 1, 0}, {17.0f, afternoon, 0, 0}, {17.7f, golden, 0, 0},
        {18.55f, sunset, 0, 0}, {19.3f, dusk, 0, 0.6f}, {20.2f, nightA, 0, 1}, {24.f, nightA, 0, 1}};
    h = std::fmod(h + 24.f, 24.f);
    int n = int(sizeof(keys) / sizeof(keys[0]));
    for (int i = 0; i + 1 < n; i++)
        if (h >= keys[i].h && h <= keys[i + 1].h) {
            float t = (h - keys[i].h) / std::max(keys[i + 1].h - keys[i].h, 1e-3f);
            t = t * t * (3.f - 2.f * t);
            hdriW = keys[i].w + (keys[i + 1].w - keys[i].w) * t;
            night = keys[i].n + (keys[i + 1].n - keys[i].n) * t;
            return lerpAtmo(keys[i].a, keys[i + 1].a, t);
        }
    hdriW = 1; night = 0;
    return day;
}

void Game::updateTimeOfDay(float dt) {
    const float fixedH[5] = {0.f, 9.0f, 18.4f, 23.0f, 13.0f};
    int m = std::clamp(settings.timeMode, 0, 4);
    if (m == 0) todHours = std::fmod(todHours + dt / 60.f, 24.f);  // one game hour per real minute
    else todHours = fixedH[m];
    if (opt.scene == "night") todHours = 22.5f;
    else if (opt.scene == "sunset") todHours = 18.4f;
}
