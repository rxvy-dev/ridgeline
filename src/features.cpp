// Horizon-style systems: XP & levels, wheelspins, skill chains, speed traps / zones,
// rewind, photo mode, car upgrades.
#include "game.h"
#include "washi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// washi and ink: text is sumi on paper, accents vermilion
static const vec4& F_ACCENT = wa::VERM;
static const vec4& F_WHITE = wa::INK;
static const vec4& F_DIM = wa::MUTED;
static const vec4& F_PINK = wa::VERM;
static const vec4& F_TRAP = wa::GOLD;
static const vec4& F_ZONE = wa::INDIGO;

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
    hud.rectV(0, hud.h - 160 * u, hud.w, 160 * u, wa::A(wa::PAPER, 0), wa::A(wa::PAPER, 0.85f));
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
        hud.text(px + 22 * u, ry + 12 * u, 20 * u, names[i], sel ? wa::PAPER : wa::INK, 0, false);
        hud.text(px + 22 * u, ry + 38 * u, 13 * u, desc[i], sel ? wa::PAPER2 : wa::MUTED, 0, false);
        for (int k = 0; k < 3; k++) {
            float bx = px + 22 * u + k * 46 * u;
            hud.rect(bx, ry + 64 * u, 40 * u, 8 * u, k < oc.up[i] ? wa::VERM : wa::A(sel ? wa::PAPER : wa::INK, 0.25f));
        }
        std::string price = oc.up[i] >= 3 ? "MAXED" : "CR " + fmtThousands(upgradeCost(oc, i));
        vec4 pc = oc.up[i] >= 3 ? (sel ? wa::GOLD : wa::MOSS) : (credits >= upgradeCost(oc, i) ? (sel ? wa::PAPER : wa::INK) : wa::VERM);
        hud.text(px + pw - 22 * u, ry + 14 * u, 20 * u, price, pc, 2, false);
        if (oc.up[i] < 3) hud.text(px + pw - 22 * u, ry + 44 * u, 12 * u, "Stage " + std::to_string(oc.up[i] + 1), sel ? wa::PAPER2 : wa::MUTED, 2, false);
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
        hud.text(qx + 160 * u, qy + 44 * u, 20 * u, buf, wa::MOSS, 0, false);
    }
    const char* labels[] = {"SPEED", "ACCELERATION", "HANDLING", "OFF-ROAD"};
    float vb[] = {rb.speed, rb.accel, rb.handling, rb.offroad};
    float vn[] = {rn.speed, rn.accel, rn.handling, rn.offroad};
    float vx[] = {rx.speed, rx.accel, rx.handling, rx.offroad};
    for (int i = 0; i < 4; i++) {
        float ry = qy + 96 * u + i * 36 * u;
        hud.text(qx + 22 * u, ry, 12 * u, labels[i], F_DIM, 0, false);
        float bx = qx + 22 * u, by = ry + 18 * u, bw = qw - 44 * u;
        hud.rect(bx, by + 3 * u, bw, 1.5f * u, wa::A(wa::INK, 0.25f));
        hud.rect(bx, by, bw * vx[i] / 10.f, 7 * u, wa::VERM);
        hud.rect(bx, by, bw * vn[i] / 10.f, 7 * u, wa::GOLD);
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
            hud.text(hud.w * 0.5f + 3 * u, y - 6 * u, 12 * u, "SPEED ZONE", wa::PAPER, 1, false);
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
    hud.text(cx, y + 24 * u, 15 * u, popup.name, wa::A(wa::INK2, a), 1, false);
    hud.text(cx, y + 48 * u, 32 * u, popup.value, wa::A(wa::INK, a), 1, false);
    {   // a seal stamped onto the card just after it lands
        float age = 4.5f - popup.t, st = wa::ease((age - 0.3f) / 0.16f);
        if (st > 0) hanko(vec2(x + w - 34 * u, y + h - 30 * u), 46 * u * (1.6f - 0.6f * st), popup.title == "LEVEL UP" ? "段" : popup.title == "WHEELSPIN" ? "福" : "速",
                          wa::VERM, 0.18f, st * a);
    }
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
            vec4 sc = on ? wa::A(wa::VERM, a) : wa::A(wa::INK, 0.2f * a);
            for (int k = 0; k < 10; k++) hud.triangle(c, pts[k], pts[(k + 1) % 10], sc);
        }
        if (popup.best) {
            float bw2 = hud.textWidth(12 * u, "NEW BEST") + 16 * u;
            hud.rect(cx + 70 * u, y + 94 * u, bw2, 20 * u, wa::A(wa::VERM, a));
            hud.text(cx + 70 * u + bw2 * 0.5f, y + 97 * u, 12 * u, "NEW BEST", wa::A(wa::PAPER, a), 1, false);
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
    // the skill chain: one small slip under the top of the screen - the score, the multiplier on a
    // little seal, the latest skill and a brush line for the time left
    using namespace wa;
    static float shownT = -10, multT = -10, lastMult = 1;
    static bool shown = false;
    if (chainScore < 1.f) { shown = false; return; }
    if (!shown) { shown = true; shownT = R.time; }
    if (chainMult > lastMult + 0.01f) multT = R.time;
    lastMult = chainMult;
    float u = hud.ui;
    float pulse = 1.f + std::max(0.f, chainFlash) * 0.25f;
    float t = clampf(1.f - chainTimer / 2.2f, 0.f, 1.f);
    char m[16];
    std::snprintf(m, sizeof(m), "x%.1f", chainMult);
    std::string sc = fmtThousands(int(chainScore));
    std::string last = chainSkills.empty() ? "" : chainSkills.back().first + " +" + fmtThousands(chainSkills.back().second);
    float sw = hud.textWidth(22 * u, sc), lw = hud.textWidth(12 * u, last);
    float w = std::max(sw, lw) + 96 * u, h = 52 * u;
    float drop = ease((R.time - shownT) / 0.3f);
    float x = hud.w * 0.5f - w * 0.5f, y = 18 * u - (1.f - drop) * 70 * u;   // drops in from above
    card(x, y, w, h, 0.95f * drop, false, 131);
    float thump = 1.f + 0.5f * (1.f - ease((R.time - multT) / 0.25f));        // the seal thumps when the multiplier climbs
    hanko(vec2(x + 24 * u, y + h * 0.5f), 30 * u * thump, "技", VERM, -0.1f + 0.2f * (thump - 1.f), drop);
    hud.text(x + 46 * u, y + 5 * u, 22 * u * pulse, sc, INK, 0, false);
    hud.text(x + w - 10 * u, y + 9 * u, 14 * u, m, VERM, 2, false);
    if (!last.empty()) hud.text(x + 46 * u, y + 31 * u, 12 * u, last, MUTED, 0, false);
    brush(vec2(x + 6 * u, y + h - 3 * u), vec2(x + 6 * u + (w - 12 * u) * (1.f - t), y + h - 3 * u), 4 * u, VERM, 33, 1.f);
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
    using namespace wa;
    if (photoHideUi) return;
    float u = hud.ui;
    // a strip of paper across the top like a print's margin, the lens on an ink tag
    paper(0, 0, hud.w, 58 * u, 0.94f, 133);
    brush(vec2(0, 58 * u), vec2(hud.w, 60 * u), 5 * u, INK, 13, 1.f);
    hanko(vec2(46 * u, 29 * u), 38 * u, "写", VERM, -0.1f, 1.f);
    hud.text(76 * u, 14 * u, 26 * u, "Photo mode", INK, 0, false);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f mm", 18.f / std::tan(photoFov * 0.5f));
    float lw2 = hud.textWidth(16 * u, buf) + 24 * u;
    hud.rect(hud.w - 40 * u - lw2, 15 * u, lw2, 28 * u, INK);
    hud.text(hud.w - 40 * u - lw2 * 0.5f, 20 * u, 16 * u, buf, PAPER, 1, false);
    hud.text(hud.w - 52 * u - lw2, 21 * u, 12 * u, "LENS", MUTED, 2, false);
    for (int i = 1; i < 3; i++) {   // rule-of-thirds guides
        hud.rect(hud.w * i / 3.f, 62 * u, 1, hud.h - 130 * u, vec4(1, 1, 1, 0.14f));
        hud.rect(0, hud.h * i / 3.f, hud.w, 1, vec4(1, 1, 1, 0.14f));
    }
    drawThemePrompts(pad ? "L-stick orbit   R-stick zoom / lens   (A) take photo   (Y) hide UI   (B) exit"
                         : "WASD orbit   Q/E zoom   Z/X lens   ENTER take photo   H hide UI   ESC exit", 40 * u);
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
    // fukubiki, the festival lottery: prize slips run past a vermilion frame, the winner gets stamped
    using namespace wa;
    float u = hud.ui;
    washiSide = "福引";
    drawThemeBackdrop(1.f);
    float x0 = 100 * u;
    drawMenuHeader(x0, 30 * u, hud.w - x0 - 50 * u, "WHEELSPIN");
    {
        std::string sr = std::to_string(wheelspins) + (wheelspins == 1 ? " spin ready" : " spins ready");
        float sw2 = hud.textWidth(15 * u, sr) + 28 * u;
        hud.rect(x0, 124 * u, sw2, 30 * u, VERM);
        hud.text(x0 + 14 * u, 130 * u, 15 * u, sr, PAPER, 0, false);
    }
    auto rarity = [](const SpinPrize& p, std::string& tier, std::string& kj) {
        if (p.car >= 0) { tier = "LEGENDARY CAR"; kj = "伝説"; return GOLD; }
        if (p.credits >= 40000) { tier = "EPIC"; kj = "極"; return PLUM; }
        if (p.credits >= 15000) { tier = "RARE"; kj = "珍"; return INDIGO; }
        tier = "COMMON"; kj = "並";
        return MUTED;
    };
    float cy = hud.h * 0.5f + 10 * u, rowH = 92 * u, w = 560 * u, x = hud.w * 0.5f - w * 0.5f + 30 * u;
    float top = cy - rowH * 2.5f, bot = cy + rowH * 2.5f;
    bool landed = spinT >= 4.2f && spinResult >= 0 && spinResult < int(spinReel.size());
    if (!spinReel.empty()) {
        int first = int(spinPos) - 3;
        for (int i = first; i <= first + 7; i++) {
            if (i < 0 || i >= int(spinReel.size())) continue;
            float yy = cy + (i - spinPos) * rowH - rowH * 0.5f;
            if (yy < top - 1 || yy + rowH > bot + 1) continue;
            const SpinPrize& p = spinReel[i];
            std::string tier, kj;
            vec4 col = rarity(p, tier, kj);
            float d = std::fabs((yy + rowH * 0.5f) - cy) / (rowH * 2.5f);
            float fade = 1.f - 0.6f * d;
            float cx0 = x + d * 24 * u, cw = w - d * 48 * u;
            card(cx0, yy + 6 * u, cw, rowH - 12 * u, fade, false, 400 + i);
            hud.rect(cx0 + 2 * u, yy + 8 * u, 12 * u, rowH - 16 * u, A(col, fade));
            if (p.car >= 0 && carThumb[p.car]) hud.image(cx0 + cw - 180 * u, yy + 8 * u, 170 * u, rowH - 16 * u, carThumb[p.car], vec4(1, 1, 1, fade), vec2(0.1f, 0.84f), vec2(0.9f, 0.2f));
            hud.text(cx0 + 30 * u, yy + 16 * u, 12 * u, tier, A(col, fade), 0, false);
            hud.textFit(cx0 + 30 * u, yy + 34 * u, 30 * u, p.car >= 0 ? fname(kCars[p.car]) : "CR " + fmtThousands(p.credits), A(INK, fade), 0, cw - (p.car >= 0 ? 230 : 120) * u, false);
            if (p.car < 0) hud.text(cx0 + cw - 40 * u, yy + 24 * u, 28 * u, kj, A(col, 0.5f * fade), 1, false);
            if (landed && i == spinResult) hanko(vec2(cx0 + cw - 50 * u, yy + rowH * 0.5f), 60 * u, "当", VERM, -0.15f, ease((spinT - 4.2f) / 0.15f));
        }
    }
    // the frame the winning slip stops in
    {
        float sy0 = cy - rowH * 0.5f + 2 * u, sy1 = cy + rowH * 0.5f - 2 * u, L = 26 * u, t = 5 * u;
        for (int sx = 0; sx < 2; sx++) {
            float ex = sx ? x + w + 10 * u : x - 10 * u;
            hud.rect(sx ? ex - t : ex, sy0, t, sy1 - sy0, VERM);
            hud.rect(sx ? ex - L : ex, sy0, L, t, VERM);
            hud.rect(sx ? ex - L : ex, sy1 - t, L, t, VERM);
        }
    }
    if (spinReel.empty()) {
        std::string msg = wheelspins > 0 ? (pad ? "Press (A) to draw" : "Press ENTER to draw") : "Level up to earn wheelspins";
        float mw = hud.textWidth(24 * u, msg) + 80 * u, mx = hud.w * 0.5f - mw * 0.5f + 30 * u;
        if (wheelspins > 0) brushSwipe(mx, cy - 26 * u, mw, 52 * u, INK, 141, ease(std::fmod(R.time, 2.5f) / 0.5f));
        else card(mx, cy - 26 * u, mw, 52 * u, 1.f, false, 143);
        hud.text(mx + mw * 0.5f, cy - 14 * u, 24 * u, msg, wheelspins > 0 ? PAPER : INK, 1, false);
        enso(vec2(hud.w * 0.5f + 30 * u, cy), 150 * u, 16 * u, A(INK, 0.12f), 1.f, 7);
    }
    // level and XP under it all
    float bw = 420 * u, bx = hud.w * 0.5f - bw * 0.5f + 30 * u, by = bot + 36 * u;
    std::string lv = "段 " + std::to_string(level);
    hud.text(bx, by - 8 * u, 18 * u, lv, VERM, 0, false);
    float lvw = hud.textWidth(18 * u, lv) + 14 * u;
    hud.rect(bx + lvw, by + 4 * u, bw - lvw, 2 * u, A(INK, 0.2f));
    brush(vec2(bx + lvw, by + 5 * u), vec2(bx + lvw + (bw - lvw) * clampf(xp / float(xpToNext()), 0.f, 1.f), by + 5 * u), 8 * u, VERM, 17, 1.f);
    hud.text(bx + bw, by + 16 * u, 12 * u, fmtThousands(xp) + " / " + fmtThousands(xpToNext()) + " XP", MUTED, 2, false);
    drawThemePrompts(pad ? "(A) spin   (B) back" : "ENTER spin   ESC back");
    // the prize, on its own card to the right with a big seal
    if (landed) {
        const SpinPrize& p = spinReel[spinResult];
        std::string tier, kj;
        vec4 col = rarity(p, tier, kj);
        float e = ease((spinT - 4.2f) / 0.35f);
        float pw = std::min(320 * u, hud.w - (x + w + 50 * u) - 30 * u), ph = 230 * u;
        if (pw > 160 * u) {
            float px = x + w + 40 * u + (1.f - e) * 60 * u, py = cy - ph * 0.5f;
            card(px, py, pw, ph, e, true, 145);
            hud.rect(px + 2 * u, py + 2 * u, pw - 4 * u, 8 * u, A(col, e));
            hud.text(px + pw * 0.5f, py + 22 * u, 15 * u, "YOU WON", A(VERM, e), 1, false);
            hud.text(px + pw * 0.5f, py + 42 * u, 12 * u, tier, A(MUTED, e), 1, false);
            if (p.car >= 0 && carThumb[p.car]) hud.image(px + 16 * u, py + 62 * u, pw - 32 * u, 100 * u, carThumb[p.car], vec4(1, 1, 1, e), vec2(0.1f, 0.84f), vec2(0.9f, 0.2f));
            else hud.text(px + pw * 0.5f, py + 70 * u, 64 * u, kj, A(col, e), 1, false);
            hud.textFit(px + pw * 0.5f, py + 170 * u, p.car >= 0 ? 22 * u : 34 * u, p.car >= 0 ? fname(kCars[p.car]) : "CR " + fmtThousands(p.credits), A(INK, e), 1, pw - 30 * u, false);
            hanko(vec2(px + pw - 26 * u, py + ph - 26 * u), 52 * u, "大吉", VERM, 0.2f, ease((spinT - 4.5f) / 0.15f));
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
