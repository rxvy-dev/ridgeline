#include <SDL3/SDL.h>
// Race furniture, Horizon style: inflatable checkpoint arches, a truss start/finish
// gantry with countdown lights, a chequered line and grid boxes painted on the road,
// festival flags and tyre stacks, checkpoint flashes and the racing line.
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
const vec4 kPink(0.95f, 0.10f, 0.48f, 19), kWhite(0.92f, 0.92f, 0.94f, 19);

// tube along a polyline (local coordinates), transformed by xf
void tube(MeshBuilder& b, const std::vector<vec3>& path, float r, int seg, const mat4& xf,
          const std::function<vec4(int, float)>& colAt, const std::function<float(float)>& radAt) {
    int n = int(path.size());
    std::vector<float> acc(n, 0.f);
    for (int i = 1; i < n; i++) acc[i] = acc[i - 1] + length(path[i] - path[i - 1]);
    uint32_t base = uint32_t(b.v.size());
    for (int i = 0; i < n; i++) {
        vec3 t = normalize(path[std::min(i + 1, n - 1)] - path[std::max(i - 1, 0)]);
        vec3 side = normalize(cross(t, vec3(0, 0, 1)));
        if (length(cross(t, vec3(0, 0, 1))) < 1e-3f) side = vec3(1, 0, 0);
        vec3 up = cross(side, t);
        float rr = r * radAt(acc[i]);
        vec4 c = colAt(i, acc[i]);
        for (int k = 0; k <= seg; k++) {
            float a = k / float(seg) * 2.f * PI;
            vec3 nn = side * std::cos(a) + up * std::sin(a);
            b.add(xf.transformPoint(path[i] + nn * rr), normalize(xf.transformDir(nn)), c);
        }
    }
    for (int i = 0; i + 1 < n; i++)
        for (int k = 0; k < seg; k++) {
            uint32_t a = base + i * (seg + 1) + k, c = a + seg + 1;
            b.tri(a, c, a + 1);
            b.tri(a + 1, c, c + 1);
        }
}

// arch centre line: up a leg, round the corner, across, down the other leg
std::vector<vec3> archPath(float hw, float H, float R, int cornerSeg) {
    std::vector<vec3> p;
    for (float y = 0; y < H - R; y += 0.6f) p.push_back(vec3(-hw, y, 0));
    for (int k = 0; k <= cornerSeg; k++) {
        float a = PI - k / float(cornerSeg) * PI * 0.5f;
        p.push_back(vec3(-hw + R + std::cos(a) * R, H - R + std::sin(a) * R, 0));
    }
    for (float x = -hw + R + 0.6f; x < hw - R; x += 0.6f) p.push_back(vec3(x, H, 0));
    for (int k = 0; k <= cornerSeg; k++) {
        float a = PI * 0.5f - k / float(cornerSeg) * PI * 0.5f;
        p.push_back(vec3(hw - R + std::cos(a) * R, H - R + std::sin(a) * R, 0));
    }
    for (float y = H - R - 0.6f; y >= -0.01f; y -= 0.6f) p.push_back(vec3(hw, y, 0));
    return p;
}

// square lattice truss between two points (local), chords + rungs + diagonals
void truss(MeshBuilder& b, vec3 a, vec3 c, float s, const mat4& xf, vec4 col) {
    vec3 d = c - a;
    float L = length(d);
    vec3 t = d / L;
    vec3 u = std::fabs(t.y) > 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    vec3 v = normalize(cross(t, u));
    u = cross(v, t);
    mat4 fr = xf * basis(u, v, t, a);  // local truss frame: z along the truss
    float h = s * 0.5f, rod = 0.035f;
    for (int i = 0; i < 4; i++) {
        float x = (i & 1) ? h : -h, y = (i & 2) ? h : -h;
        b.box(vec3(x, y, L * 0.5f), vec3(rod, rod, L * 0.5f), col, &fr);
    }
    int n = std::max(1, int(L / s));
    for (int k = 0; k <= n; k++) {
        float z = L * k / n;
        b.box(vec3(0, h, z), vec3(h, rod * 0.7f, rod * 0.7f), col, &fr);
        b.box(vec3(0, -h, z), vec3(h, rod * 0.7f, rod * 0.7f), col, &fr);
        b.box(vec3(h, 0, z), vec3(rod * 0.7f, h, rod * 0.7f), col, &fr);
        b.box(vec3(-h, 0, z), vec3(rod * 0.7f, h, rod * 0.7f), col, &fr);
        if (k == n) break;
        float z2 = L * (k + 0.5f) / n, seg = L / n;
        float ang = std::atan2(s, seg);
        for (int f = 0; f < 4; f++) {  // zig-zag diagonals on each face
            mat4 m = fr;
            vec3 off = f == 0 ? vec3(0, h, z2) : f == 1 ? vec3(0, -h, z2) : f == 2 ? vec3(h, 0, z2) : vec3(-h, 0, z2);
            if (f < 2) m = m * translate(off) * rotateY((k % 2 ? 1.f : -1.f) * ang);
            else m = m * translate(off) * rotateX((k % 2 ? 1.f : -1.f) * ang);
            float len = std::sqrt(s * s + seg * seg) * 0.5f;
            if (f < 2) b.box(vec3(0, 0, 0), vec3(rod * 0.6f, rod * 0.6f, len), col, &m);
            else b.box(vec3(0, 0, 0), vec3(rod * 0.6f, rod * 0.6f, len), col, &m);
        }
    }
}
}  // namespace

float Game::groundAtRoute(const Route& rt, float d, vec3 p) const {
    // routes along roads carry the road height; off-road ones follow the terrain
    float rh = rt.pointAt(d).y;
    float th = world.heightAt(p.x, p.z);
    return std::fabs(rh - th) < 1.5f ? std::max(rh, th) : th;
}

void Game::buildGates(const EventDef& E) {
    const Route& rt = E.route;
    gates.clear();
    float L = rt.length;
    float spacing = E.type == EV_CROSS ? 260.f : 320.f;
    int n = int(L / spacing);
    // start line: ahead of the front row on a sprint, at distance 0 on a circuit
    float startD = rt.closed ? 0.f : 44.f;
    gates.push_back({startD, false, true});
    for (int i = 1; i <= n; i++) {
        float d = i * spacing;
        if (L - d < 120.f || std::fabs(d - startD) < 100.f) continue;
        gates.push_back({d, false, false});
    }
    if (!rt.closed) gates.push_back({L - 1.f, true, false});
    else gates[0].finish = true;
    gateFlash.assign(gates.size(), 0.f);
    gateIdx = 0;

    MeshBuilder lit, paint;
    for (auto& m : gateGlow) m.destroy();
    gateGlow.clear();
    for (size_t gi = 0; gi < gates.size(); gi++) {
        Gate& g = gates[gi];
        vec3 p = rt.pointAt(g.d), t = rt.tangentAt(g.d), right(-t.z, 0, t.x);
        p.y = groundAtRoute(rt, g.d, p);
        float hw = rt.width * 0.5f + 1.6f;
        mat4 xf = basis(right, vec3(0, 1, 0), t, p);
        g.p = p; g.t = t; g.hw = hw;
        if (g.start || g.finish) {
            // ---- truss gantry with a chequered banner and countdown lights
            vec4 steel(0.62f, 0.63f, 0.66f, 7), dark(0.05f, 0.05f, 0.06f, 21);
            float tx = hw + 0.7f, top = 8.2f;
            for (int s = -1; s <= 1; s += 2) {
                truss(lit, vec3(s * tx, 0, 0), vec3(s * tx, top + 0.3f, 0), 0.6f, xf, steel);
                lit.box(vec3(s * tx, 0.12f, 0), vec3(0.75f, 0.12f, 0.75f), dark, &xf);  // base plate
                // tyre stacks at the foot of each tower
                for (int k = 0; k < 3; k++) {
                    vec3 c = xf.transformPoint(vec3(s * (tx + 1.4f + k * 0.95f), 0, (k % 2) * 0.9f - 0.4f));
                    for (int h = 0; h < 4; h++) lit.cylinder(c + vec3(0, h * 0.27f, 0), 0.42f, 0.25f, 14, vec4(1, 1, 1, 8));
                }
            }
            truss(lit, vec3(-tx - 0.3f, top, 0), vec3(tx + 0.3f, top, 0), 0.6f, xf, steel);
            // banner: two rows of chequers between pink bands
            int cells = int(2 * tx / 0.9f);
            float cw = 2 * tx / cells;
            for (int row = 0; row < 2; row++)
                for (int c = 0; c < cells; c++) {
                    bool white = (c + row) % 2 == 0;
                    lit.box(vec3(-tx + cw * (c + 0.5f), top - 0.75f - row * 0.62f, 0), vec3(cw * 0.5f, 0.31f, 0.06f),
                            white ? vec4(0.95f, 0.95f, 0.95f, 21) : vec4(0.04f, 0.04f, 0.045f, 21), &xf);
                }
            lit.box(vec3(0, top - 0.38f, 0), vec3(tx, 0.08f, 0.07f), vec4(0.95f, 0.1f, 0.48f, 20), &xf);
            lit.box(vec3(0, top - 2.06f, 0), vec3(tx, 0.08f, 0.07f), vec4(0.95f, 0.1f, 0.48f, 20), &xf);
            if (g.start) {
                // light housing under the banner, five lamps facing the grid (-z)
                lit.box(vec3(0, top - 2.65f, 0), vec3(2.0f, 0.42f, 0.22f), dark, &xf);
                for (int k = 0; k < 5; k++) {
                    vec3 c = vec3(-1.6f + k * 0.8f, top - 2.65f, -0.24f);
                    lit.box(c, vec3(0.26f, 0.26f, 0.03f), vec4(0.08f, 0.02f, 0.02f, 21), &xf);
                    g.lamps[k] = xf.transformPoint(c + vec3(0, 0, -0.05f));
                }
            }
            // ---- chequered line painted across the road
            int pc = std::max(4, int(rt.width / 0.7f));
            float pw = rt.width / pc;
            for (int row = 0; row < 2; row++)
                for (int c = 0; c < pc; c++) {
                    float x0 = -rt.width * 0.5f + c * pw, z0 = -0.7f + row * 0.7f;
                    bool white = (c + row) % 2 == 0;
                    vec4 col = white ? vec4(0.9f, 0.9f, 0.9f, 21) : vec4(0.03f, 0.03f, 0.035f, 21);
                    auto P = [&](float x, float z) {
                        vec3 q = xf.transformPoint(vec3(x, 0, z));
                        q.y = groundAtRoute(rt, g.d + z, q) + 0.035f;
                        return q;
                    };
                    paint.flatQuad(P(x0, z0), P(x0, z0 + 0.7f), P(x0 + pw, z0 + 0.7f), P(x0 + pw, z0), col);
                }
            // ---- festival flags lining the run-in
            const vec4 flagCols[4] = {vec4(0.95f, 0.1f, 0.48f, 21), vec4(1.f, 0.55f, 0.08f, 21), vec4(0.15f, 0.7f, 0.95f, 21), vec4(0.98f, 0.85f, 0.15f, 21)};
            for (int s = -1; s <= 1; s += 2)
                for (int k = -5; k <= 5; k++) {
                    if (k == 0) continue;
                    float dz = k * 5.f;
                    vec3 base = rt.pointAt(g.d + dz), tt = rt.tangentAt(g.d + dz), rr(-tt.z, 0, tt.x);
                    base = base + rr * (s * (rt.width * 0.5f + 3.2f));
                    base.y = world.heightAt(base.x, base.z);
                    lit.cylinder(base, 0.05f, 4.6f, 6, vec4(0.85f, 0.85f, 0.88f, 7));
                    vec3 a = base + vec3(0, 4.5f, 0), bdir = tt * (k > 0 ? -1.f : 1.f);
                    vec4 fc = flagCols[(k + 8 + (s > 0 ? 1 : 0)) % 4];
                    // a tall feather flag: two triangles, both faces
                    vec3 q0 = a, q1 = a + bdir * 0.9f + vec3(0, -0.4f, 0), q2 = a + vec3(0, -2.6f, 0), q3 = a + bdir * 0.7f + vec3(0, -2.2f, 0);
                    lit.flatQuad(q0, q1, q3, q2, fc);
                    lit.flatQuad(q0, q2, q3, q1, fc);
                }
        } else {
            // ---- inflatable checkpoint arch: pink and white sections, weighted feet
            float H = 7.2f, R = std::min(hw * 0.8f, 3.0f), r = 0.62f;
            auto path = archPath(hw, H, R, 10);
            float total = 0;
            for (size_t i = 1; i < path.size(); i++) total += length(path[i] - path[i - 1]);
            float secLen = total / std::max(5.f, std::round(total / 2.6f));
            tube(lit, path, r, 14, xf,
                 [&](int, float s) { return int(s / secLen) % 2 ? kWhite : kPink; },
                 [&](float s) {  // pinched seams between the sections
                     float f = std::fmod(s, secLen) / secLen;
                     return 0.9f + 0.1f * std::sin(f * PI);
                 });
            for (int s = -1; s <= 1; s += 2) lit.box(vec3(s * hw, 0.25f, 0), vec3(0.85f, 0.25f, 0.85f), vec4(0.12f, 0.12f, 0.13f, 21), &xf);
            // banner under the crown: pink panel with glowing chevrons pointing the way
            float bw = std::min(hw * 0.75f, 4.5f);
            lit.box(vec3(0, H - 1.25f, 0), vec3(bw, 0.6f, 0.06f), vec4(0.95f, 0.1f, 0.48f, 21), &xf);
            for (int k = -2; k <= 2; k++) {
                for (int side = -1; side <= 1; side += 2) {
                    float zf = side * -0.09f;  // on both faces
                    vec3 c0(k * 1.1f, H - 1.25f, zf);
                    vec3 a = c0 + vec3(-0.35f, -0.28f, 0), bb = c0 + vec3(0, 0.3f, 0), cc = c0 + vec3(0.35f, -0.28f, 0);
                    vec3 a2 = c0 + vec3(-0.2f, -0.28f, 0), cc2 = c0 + vec3(0.2f, -0.28f, 0), bb2 = c0 + vec3(0, 0.08f, 0);
                    vec4 em(1.f, 0.95f, 0.98f, 20);
                    auto T = [&](vec3 q) { return xf.transformPoint(q); };
                    lit.flatQuad(T(a), T(bb), T(bb2), T(a2), em);
                    lit.flatQuad(T(bb), T(cc), T(cc2), T(bb2), em);
                    lit.flatQuad(T(a2), T(bb2), T(bb), T(a), em);
                    lit.flatQuad(T(bb2), T(cc2), T(cc), T(bb), em);
                }
            }
        }
        // glow overlay (drawn additively for the next gate and when you pass one)
        MeshBuilder gb;
        if (g.start || g.finish) {
            float tx = hw + 0.7f;
            gb.box(vec3(0, 8.2f - 1.22f, 0), vec3(tx + 0.2f, 0.95f, 0.2f), vec4(1, 1, 1, 1), &xf);
        } else {
            float H = 7.2f, R = std::min(hw * 0.8f, 3.0f);
            tube(gb, archPath(hw, H, R, 8), 0.78f, 10, xf, [](int, float) { return vec4(1, 1, 1, 1); }, [](float) { return 1.f; });
        }
        Mesh gm;
        gb.build(gm);
        gateGlow.push_back(gm);
    }
    // ---- grid boxes behind the start line
    for (Racer* r : racers) {
        vec3 c = r->car.pos, f = r->car.forward(), s = r->car.right();
        float hl = 2.6f, hwid = 1.25f, lw = 0.14f;
        auto P = [&](float x, float z) {
            vec3 q = c + s * x + f * z;
            q.y = world.heightAt(q.x, q.z);
            float rd = rt.pts[std::clamp(r->idx, 0, int(rt.pts.size()) - 1)].y;
            if (std::fabs(rd - q.y) < 1.5f) q.y = std::max(q.y, rd);
            q.y += 0.035f;
            return q;
        };
        vec4 w(0.88f, 0.88f, 0.86f, 21);
        paint.flatQuad(P(-hwid, hl), P(-hwid, hl - lw), P(hwid, hl - lw), P(hwid, hl), w);       // front bar
        paint.flatQuad(P(-hwid, hl), P(-hwid, -hl), P(-hwid + lw, -hl), P(-hwid + lw, hl), w);  // sides
        paint.flatQuad(P(hwid - lw, hl), P(hwid - lw, -hl), P(hwid, -hl), P(hwid, hl), w);
    }
    lit.build(gateLit);
    paint.build(gatePaint);
}

void Game::clearGates() {
    gateLit.destroy();
    gatePaint.destroy();
    for (auto& m : gateGlow) m.destroy();
    gateGlow.clear();
    gates.clear();
}

// Absolute route distance (progress units) of the n-th gate a racer has to pass:
// on a circuit the sequence repeats every lap, starting with the line itself.
float Game::gateAbs(int n) const {
    const EventDef& E = events[raceEvent];
    int G = int(gates.size());
    if (!E.route.closed) return n < G ? gates[n].d : 1e9f;
    return (n / G) * E.route.length + gates[n % G].d;
}

void Game::updateGates(float dt) {
    for (float& f : gateFlash) f = std::max(0.f, f - dt * 1.6f);
    if (gateBannerT > 0) gateBannerT -= dt;
    if (raceEvent < 0 || gates.empty() || state != ST_RACE) return;
    const EventDef& E = events[raceEvent];
    int G = int(gates.size());
    while (player.progress >= gateAbs(gateIdx) - 1.f) {
        int gi = E.route.closed ? gateIdx % G : gateIdx;
        gateFlash[gi] = 1.f;
        const Gate& g = gates[gi];
        int lap = E.route.closed ? gateIdx / G : 0;
        char buf[64];
        int ms = int(raceTime * 1000.f);
        buf[0] = 0;
        if (!g.start && !g.finish) std::snprintf(buf, sizeof(buf), "CHECKPOINT   %d:%02d.%d", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
        else if (E.route.closed && lap > 0 && lap < E.laps) std::snprintf(buf, sizeof(buf), "LAP %d / %d   %d:%02d.%d", lap + 1, E.laps, ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
        if (buf[0]) {
            gateBanner = buf;
            gateBannerT = 1.8f;
            audio.beep(1046, 0.09f);
        }
        gateIdx++;
        if (!E.route.closed && gateIdx >= G) break;
    }
}

// additive pass: next-gate glow, pass flashes, countdown lamps, racing line
void Game::drawGateFx() {
    if (raceEvent < 0 || gates.empty()) return;
    const EventDef& E = events[raceEvent];
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    R.unlit.set("uFog", 1);
    R.unlit.set("uModel", mat4());
    int ng = E.route.closed ? gateIdx % int(gates.size()) : gateIdx;
    for (size_t i = 0; i < gates.size() && i < gateGlow.size(); i++) {
        float a = gateFlash[i] * 0.9f;
        if (int(i) == ng && state == ST_RACE) a = std::max(a, 0.16f + 0.12f * std::sin(R.time * 5.f));
        if (a <= 0.01f) continue;
        vec4 c = gates[i].start || gates[i].finish ? vec4(1.f, 0.85f, 0.95f, a) : vec4(1.f, 0.3f, 0.65f, a);
        R.unlit.set("uTint", c);
        gateGlow[i].draw();
    }
    // countdown lamps on the start gantry
    const Gate& sg = gates[0];
    if (sg.start) {
        int lit = 0;
        bool green = false;
        if (state == ST_RACE_INTRO && stateTime > 2.5f) lit = std::clamp(int((3.f - countdown) / 3.f * 5.f) + 1, 1, 5);
        if (state == ST_RACE && raceTime < 2.5f) { lit = 5; green = true; }
        for (int k = 0; k < lit; k++) {
            R.unlit.set("uTint", green ? vec4(0.2f, 1.f, 0.35f, 1) : vec4(1.f, 0.12f, 0.08f, 1));
            R.unlit.set("uModel", translate(sg.lamps[k]) * basis(vec3(-sg.t.z, 0, sg.t.x), vec3(0, 1, 0), sg.t, vec3(0, 0, 0)) * scale(vec3(0.3f, 0.3f, 0.3f)));
            lampMesh.draw();
        }
    }
    if (settings.racingLine && state == ST_RACE) {
        // roads are drawn pulled towards the camera; the line needs a stronger pull to sit on top
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-4.f, -12.f);
        drawRacingLine(E);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// Forza-style driving line: chevrons along the ideal line, blue when you're fine,
// yellow then red where you need to brake for the corner ahead.
void Game::drawRacingLine(const EventDef& E) {
    const Route& rt = E.route;
    float L = rt.length;
    // where the car actually is along the route (progress can run ahead of it): nearest point nearby
    float guess = rt.closed ? std::fmod(std::max(player.progress, 0.f), L) : player.progress;
    float base = guess, bestD = 1e30f;
    for (float o = -80.f; o <= 80.f; o += 2.f) {
        float d = guess + o;
        if (!rt.closed && (d < 0.f || d > L)) continue;
        vec3 q = rt.pointAt(rt.closed ? std::fmod(d + L, L) : d) - player.car.pos;
        float dd = q.x * q.x + q.z * q.z;
        if (dd < bestD) { bestD = dd; base = d; }
    }
    float v = player.car.speed();
    float mu = std::max(0.8f, player.car.spec->grip) * 9.81f * 0.95f;
    auto wrap = [&](float d) { return rt.closed ? std::fmod(d + L, L) : clampf(d, 0.f, L); };
    // ideal line: drift to the inside of upcoming bends (smoothed so the arrows flow)
    auto lineOff = [&](float dd) {
        float k = 0;
        for (float o = -20.f; o <= 20.f; o += 10.f) k += rt.curvatureAt(wrap(dd + o));
        return clampf(-k * 0.2f * 900.f, -rt.width * 0.3f, rt.width * 0.3f);
    };
    // a point on the line: a metres along, x metres across
    auto at = [&](float d, float x) {
        float dd = wrap(d);
        vec3 p = rt.pointAt(dd), t = rt.tangentAt(dd), side(-t.z, 0, t.x);
        p = p + side * (lineOff(dd) + x);
        p.y = groundAtRoute(rt, dd, p) + 0.1f;
        return p;
    };
    // solid chevrons laid along the line, bending with it, coloured by how hard you need to brake
    std::vector<Vertex> vs;
    std::vector<uint32_t> ix;
    const float w = 0.95f, len = 1.1f, thick = 1.3f, step = 3.2f;
    for (float s = 4.f; s < 160.f; s += step) {
        float d = base + s;
        if (!rt.closed && d + len + thick > L - 2.f) break;
        float dd = wrap(d);
        float need = 0;
        for (float a = 0; a < 70.f; a += 7.f) {
            float kk = std::fabs(rt.curvatureAt(wrap(dd + a)));
            if (kk < 1e-4f) continue;
            float vmax = std::sqrt(mu / kk);
            float allowed = std::sqrt(vmax * vmax + 2.f * 8.5f * (s + a));
            need = std::max(need, (v - allowed) / 8.f);
        }
        need = clampf(need + 0.25f, 0.f, 1.f);
        // blue -> yellow -> red, a touch over 1 so it holds up against bright tarmac
        vec3 col = need < 0.35f ? vec3(0.25f, 0.75f, 1.4f) : need < 0.7f ? vec3(1.4f, 1.1f, 0.15f) : vec3(1.5f, 0.18f, 0.12f);
        float alpha = 0.9f * smoothstepf(2.f, 9.f, s) * (1.f - smoothstepf(115.f, 160.f, s));
        vec4 c(col.x, col.y, col.z, alpha);
        vec3 n(0, 1, 0);
        uint32_t b0 = uint32_t(vs.size());
        vs.push_back({at(d, -w), n, c});                 // 0 back left
        vs.push_back({at(d + len, 0.f), n, c});          // 1 back tip
        vs.push_back({at(d, w), n, c});                  // 2 back right
        vs.push_back({at(d + thick, w), n, c});          // 3 front right
        vs.push_back({at(d + len + thick, 0.f), n, c});  // 4 front tip
        vs.push_back({at(d + thick, -w), n, c});         // 5 front left
        const uint32_t t[] = {0, 1, 4, 0, 4, 5, 2, 3, 4, 2, 4, 1};
        for (uint32_t k : t) ix.push_back(b0 + k);
    }
    if (ix.empty()) return;
    lineMesh.upload(vs, ix);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R.unlit.set("uTint", vec4(1, 1, 1, 1));
    R.unlit.set("uModel", mat4());
    lineMesh.draw();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
}

void Game::drawGateHud() {
    if (gateBannerT <= 0 || state != ST_RACE) return;
    float u = hud.ui;
    float a = std::min(1.f, gateBannerT * 3.f);
    float y = hud.h * 0.17f;
    float w = hud.textWidth(20 * u, gateBanner) + 60 * u;
    hud.rect(hud.w * 0.5f - w * 0.5f, y, w, 40 * u, vec4(0.05f, 0.05f, 0.06f, 0.75f * a));
    hud.rect(hud.w * 0.5f - w * 0.5f, y, w, 4 * u, vec4(0.95f, 0.1f, 0.48f, a));
    hud.text(hud.w * 0.5f, y + 9 * u, 20 * u, gateBanner, vec4(1, 1, 1, a), 1, false);
}
