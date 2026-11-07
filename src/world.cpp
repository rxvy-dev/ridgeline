#include "world.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------- helpers
static vec3 catmull(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
}

static float forestAt(float x, float z) {
    float f = fbm(x / 380.f, z / 380.f, 3, 77);
    return smoothstepf(-0.12f, 0.22f, f);
}

float World::baseHeight(float x, float z) const {
    float h = 42.f + fbm(x / 1100.f + 3.1f, z / 1100.f - 1.7f, 4, 11) * 85.f;
    h += fbm(x / 280.f, z / 280.f, 4, 23) * 24.f;
    h += fbm(x / 70.f, z / 70.f, 3, 37) * 2.5f;
    float edge = std::max(std::fabs(x), std::fabs(z));
    float ridge = 1.f - std::fabs(fbm(x / 420.f, z / 420.f, 4, 51));
    ridge *= ridge;
    h += smoothstepf(1000.f, 1430.f, edge) * (170.f + ridge * 170.f);
    h += smoothstepf(650.f, 1250.f, z) * (60.f + ridge * 120.f);
    // keep lowlands dry (only the lake goes below water)
    float floorH = waterLevel + 3.f;
    if (h < floorH) h = floorH - (floorH - h) * 0.15f;
    // lake bowls with irregular shorelines
    for (const Lake& L : lakes) {
        float dx = x - L.c.x, dz = z - L.c.z;
        float dl = std::sqrt(dx * dx + dz * dz);
        if (dl > L.r * 1.6f) continue;
        float ang = std::atan2(dz, dx);
        float rr = L.r * (1.f + 0.18f * std::sin(ang * 3.f + L.seed) + 0.08f * std::sin(ang * 7.f + L.seed * 2.f) + 0.05f * std::sin(ang * 13.f + L.seed));
        float lk = 1.f - smoothstepf(rr * 0.5f, rr * 1.15f, dl);
        float depth = 6.f + L.r * 0.04f;
        h = lerpf(h, waterLevel - depth, lk);
    }
    return h;
}

float World::heightAt(float x, float z) const {
    float fx = (x + HALF) / CELL, fz = (z + HALF) / CELL;
    fx = clampf(fx, 0.f, N - 1.001f);
    fz = clampf(fz, 0.f, N - 1.001f);
    int ix = int(fx), iz = int(fz);
    float tx = fx - ix, tz = fz - iz;
    const float* h = height.data();
    float a = h[iz * N + ix], b = h[iz * N + ix + 1], c = h[(iz + 1) * N + ix], d = h[(iz + 1) * N + ix + 1];
    // match the triangle split used by the terrain mesh (diagonal a-d)
    if (tx > tz) return a + (b - a) * tx + (d - b) * tz;
    return a + (d - c) * tx + (c - a) * tz;
}

vec3 World::normalAt(float x, float z) const {
    float e = CELL * 0.75f;
    float hl = heightAt(x - e, z), hr = heightAt(x + e, z);
    float hd = heightAt(x, z - e), hu = heightAt(x, z + e);
    return normalize(vec3(hl - hr, 2 * e, hd - hu));
}

float World::roadDistAt(float x, float z) const {
    float fx = clampf((x + HALF) / CELL, 0.f, N - 1.001f), fz = clampf((z + HALF) / CELL, 0.f, N - 1.001f);
    int ix = int(fx), iz = int(fz);
    float tx = fx - ix, tz = fz - iz;
    const float* r = roadDist.data();
    return lerpf(lerpf(r[iz * N + ix], r[iz * N + ix + 1], tx), lerpf(r[(iz + 1) * N + ix], r[(iz + 1) * N + ix + 1], tx), tz);
}

int World::surfaceAt(float x, float z) const {
    if (heightAt(x, z) < waterLevel - 0.3f) return SURF_WATER;
    if (roadDistAt(x, z) < 0.3f) {
        int ix = int(clampf((x + HALF) / CELL + 0.5f, 0.f, N - 1.f));
        int iz = int(clampf((z + HALF) / CELL + 0.5f, 0.f, N - 1.f));
        return roads[roadId[iz * N + ix]].surface;
    }
    return SURF_GRASS;
}

bool World::nearestRoad(vec3 p, int& road, int& sample, float maxDist) const {
    float best = 1e18f;
    road = sample = -1;
    int cx = int((p.x + HALF) / RGRID), cz = int((p.z + HALF) / RGRID);
    for (int ring = 0; ring < 6 && road < 0; ring++) {
        int r = ring * 2 + 1;
        for (int z = cz - r; z <= cz + r; z++)
            for (int x = cx - r; x <= cx + r; x++) {
                if (x < 0 || z < 0 || x >= rgN || z >= rgN) continue;
                for (auto& rs : roadGrid[z * rgN + x]) {
                    vec3 q = roads[rs.first].pts[rs.second];
                    float d = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
                    if (d < best) { best = d; road = rs.first; sample = rs.second; }
                }
            }
    }
    if (road < 0) {
        for (int r = 0; r < int(roads.size()); r++)
            for (int i = 0; i < int(roads[r].pts.size()); i++) {
                vec3 q = roads[r].pts[i];
                float d = (q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z);
                if (d < best) { best = d; road = r; sample = i; }
            }
    }
    return road >= 0 && std::sqrt(best) <= maxDist;
}

bool World::collideTrees(vec3& p, float r, vec3& normal) const {
    int cx = int((p.x + HALF) / TGRID), cz = int((p.z + HALF) / TGRID);
    bool hit = false;
    for (int z = cz - 1; z <= cz + 1; z++)
        for (int x = cx - 1; x <= cx + 1; x++) {
            if (x < 0 || z < 0 || x >= tgN || z >= tgN) continue;
            for (auto& t : trunkGrid[z * tgN + x]) {
                float dx = p.x - t.x, dz = p.z - t.z;
                float d2 = dx * dx + dz * dz, rr = r + t.r;
                if (d2 < rr * rr && d2 > 1e-6f) {
                    float d = std::sqrt(d2);
                    vec3 n(dx / d, 0, dz / d);
                    p += n * (rr - d);
                    normal = n;
                    hit = true;
                }
            }
        }
    return hit;
}

// ---------------------------------------------------------------- roads
void World::buildRoad(Road& r, const std::vector<vec3>& ctrl, bool closed) {
    r.closed = closed;
    int n = int(ctrl.size());
    auto P = [&](int i) {
        if (closed) return ctrl[((i % n) + n) % n];
        return ctrl[std::clamp(i, 0, n - 1)];
    };
    int segs = closed ? n : n - 1;
    r.pts.clear();
    for (int s = 0; s < segs; s++) {
        vec3 a = P(s - 1), b = P(s), c = P(s + 1), d = P(s + 2);
        float len = length(c - b);
        int steps = std::max(2, int(len / 4.f));
        for (int k = 0; k < steps; k++) r.pts.push_back(catmull(a, b, c, d, k / float(steps)));
    }
    if (!closed) r.pts.push_back(ctrl.back());
    int m = int(r.pts.size());
    r.tan.resize(m);
    r.dist.resize(m);
    float acc = 0;
    for (int i = 0; i < m; i++) {
        int ip = closed ? (i - 1 + m) % m : std::max(i - 1, 0);
        int in = closed ? (i + 1) % m : std::min(i + 1, m - 1);
        vec3 t = r.pts[in] - r.pts[ip];
        t.y = 0;
        r.tan[i] = normalize(t);
        if (i > 0) {
            vec3 d = r.pts[i] - r.pts[i - 1];
            acc += std::sqrt(d.x * d.x + d.z * d.z);
        }
        r.dist[i] = acc;
    }
    if (closed) {
        vec3 d = r.pts[0] - r.pts[m - 1];
        acc += std::sqrt(d.x * d.x + d.z * d.z);
    }
    r.length = acc;
}

void World::computeRoadHeights() {
    for (size_t ri = 0; ri < roads.size(); ri++) {
        Road& r = roads[ri];
        int m = int(r.pts.size());
        std::vector<float> hs(m), target(m, 0.f), w(m, 0.f);
        for (int i = 0; i < m; i++) hs[i] = baseHeight(r.pts[i].x, r.pts[i].z);
        // constraints from roads already processed (junctions/crossings)
        for (int i = 0; i < m; i++) {
            float best = 1e9f, bh = 0;
            for (size_t pj = 0; pj < ri; pj++)
                for (auto& q : roads[pj].pts) {
                    float d = std::hypot(q.x - r.pts[i].x, q.z - r.pts[i].z);
                    if (d < best) { best = d; bh = q.y; }
                }
            if (best < 40.f) { w[i] = 1.f - smoothstepf(6.f, 40.f, best); target[i] = bh; }
        }
        auto blur = [&](int rad) {
            std::vector<float> o(m);
            for (int i = 0; i < m; i++) {
                float s = 0; int c = 0;
                for (int k = -rad; k <= rad; k++) {
                    int j = i + k;
                    if (r.closed) j = (j % m + m) % m;
                    else if (j < 0 || j >= m) continue;
                    s += hs[j]; c++;
                }
                o[i] = s / c;
            }
            hs = o;
        };
        for (int it = 0; it < 4; it++) blur(12);
        for (int it = 0; it < 8; it++) {
            blur(6);
            for (int i = 0; i < m; i++) hs[i] = lerpf(hs[i], target[i], w[i]);
        }
        for (int i = 0; i < m; i++) r.pts[i].y = std::max(hs[i], waterLevel + 1.6f);
    }
}

void World::carveTerrain() {
    // spatial hash over road samples
    rgN = int(std::ceil(2 * HALF / RGRID));
    roadGrid.assign(rgN * rgN, {});
    for (int r = 0; r < int(roads.size()); r++)
        for (int i = 0; i < int(roads[r].pts.size()); i++) {
            vec3 p = roads[r].pts[i];
            int cx = std::clamp(int((p.x + HALF) / RGRID), 0, rgN - 1), cz = std::clamp(int((p.z + HALF) / RGRID), 0, rgN - 1);
            roadGrid[cz * rgN + cx].push_back({r, i});
        }
    roadDist.assign(N * N, 1000.f);
    roadId.assign(N * N, 0);
    for (int iz = 0; iz < N; iz++)
        for (int ix = 0; ix < N; ix++) {
            float x = -HALF + ix * CELL, z = -HALF + iz * CELL;
            int cx = int((x + HALF) / RGRID), cz = int((z + HALF) / RGRID);
            float best = 1e9f; int br = -1, bi = -1;
            for (int gz = cz - 2; gz <= cz + 2; gz++)
                for (int gx = cx - 2; gx <= cx + 2; gx++) {
                    if (gx < 0 || gz < 0 || gx >= rgN || gz >= rgN) continue;
                    for (auto& rs : roadGrid[gz * rgN + gx]) {
                        vec3 q = roads[rs.first].pts[rs.second];
                        float d = (q.x - x) * (q.x - x) + (q.z - z) * (q.z - z);
                        if (d < best) { best = d; br = rs.first; bi = rs.second; }
                    }
                }
            if (br < 0) continue;
            // refine against the two adjacent segments
            const Road& rd = roads[br];
            int m = int(rd.pts.size());
            float bestD = std::sqrt(best), bestH = rd.pts[bi].y;
            for (int side = -1; side <= 1; side += 2) {
                int j = bi + side;
                if (rd.closed) j = (j + m) % m;
                else if (j < 0 || j >= m) continue;
                vec3 a = rd.pts[bi], b = rd.pts[j];
                vec2 ab(b.x - a.x, b.z - a.z), ap(x - a.x, z - a.z);
                float t = clampf(dot(ap, ab) / std::max(dot(ab, ab), 1e-6f), 0.f, 1.f);
                vec2 c(a.x + ab.x * t - x, a.z + ab.y * t - z);
                float d = length(c);
                if (d < bestD) { bestD = d; bestH = lerpf(a.y, b.y, t); }
            }
            float hw = rd.width * 0.5f;
            roadDist[iz * N + ix] = bestD - hw;
            roadId[iz * N + ix] = uint8_t(br);
            float& h = height[iz * N + ix];
            float flat = hw + 3.f;
            if (bestD < flat) h = bestH - 0.03f;
            else if (bestD < flat + 32.f) h = lerpf(bestH - 0.03f, h, smoothstepf(0.f, 1.f, (bestD - flat) / 32.f));
        }
}

// ---------------------------------------------------------------- scatter
void World::scatter(uint32_t seed) {
    Rng rng(seed * 7 + 3);
    tgN = int(std::ceil(2 * HALF / TGRID));
    trunkGrid.assign(tgN * tgN, {});
    auto addTrunk = [&](float x, float z, float r) {
        int cx = std::clamp(int((x + HALF) / TGRID), 0, tgN - 1), cz = std::clamp(int((z + HALF) / TGRID), 0, tgN - 1);
        trunkGrid[cz * tgN + cx].push_back({x, z, r});
    };
    // distance to the cross-country path
    auto ccDist = [&](float x, float z) {
        float best = 1e9f;
        for (size_t i = 0; i + 1 < crossCountry.size(); i++) {
            vec3 a = crossCountry[i], b = crossCountry[i + 1];
            vec2 ab(b.x - a.x, b.z - a.z), ap(x - a.x, z - a.z);
            float t = clampf(dot(ap, ab) / std::max(dot(ab, ab), 1e-6f), 0.f, 1.f);
            best = std::min(best, length(vec2(a.x + ab.x * t - x, a.z + ab.y * t - z)));
        }
        return best;
    };
    const vec3 autumn[] = {{0.98f, 0.50f, 0.12f}, {0.97f, 0.72f, 0.18f}, {0.82f, 0.24f, 0.08f},
                           {0.70f, 0.66f, 0.20f}, {0.38f, 0.50f, 0.18f}, {0.93f, 0.40f, 0.10f}};
    const float autumnW[] = {0.26f, 0.22f, 0.14f, 0.14f, 0.12f, 0.12f};
    const float step = 6.0f;
    for (float gz = -HALF + 10; gz < HALF - 10; gz += step)
        for (float gx = -HALF + 10; gx < HALF - 10; gx += step) {
            float x = gx + rng.range(-step * 0.45f, step * 0.45f);
            float z = gz + rng.range(-step * 0.45f, step * 0.45f);
            float roll = rng.f();
            float h = heightAt(x, z);
            if (h < waterLevel + 1.2f) continue;
            float rd = roadDistAt(x, z);
            if (rd < 5.5f) continue;
            float hubD = std::hypot(x - hubPos.x, z - hubPos.z);
            if (hubD < 95.f) continue;
            if (ccDist(x, z) < 20.f) continue;
            vec3 n = normalAt(x, z);
            float forest = forestAt(x, z);
            float dens = forest * 0.55f + 0.03f;
            dens += smoothstepf(1050.f, 1350.f, std::max(std::fabs(x), std::fabs(z))) * 0.25f;
            dens *= 1.f - smoothstepf(230.f, 290.f, h);         // tree line
            dens *= smoothstepf(5.5f, 18.f, rd);                // thin out near roads
            if (ccDist(x, z) < 45.f) dens *= 0.3f;
            if (n.y < 0.72f) {  // steep: rocks and the odd hardy pine
                if (roll < 0.05f) {
                    float sc = rng.range(0.8f, 2.6f);
                    rockInst.push_back({vec3(x, h - 0.3f * sc, z), sc, vec3(1, 1, 1) * rng.range(0.85f, 1.1f), rng.range(0, 6.28f)});
                    addTrunk(x, z, 1.0f * sc);
                } else if (roll < 0.05f + dens * 0.4f) {
                    float sc = rng.range(0.55f, 1.0f);
                    pineInst.push_back({vec3(x, h - 0.4f, z), sc, vec3(0.95f, 1.0f, 0.92f), rng.range(0, 6.28f)});
                    addTrunk(x, z, 0.35f * sc);
                }
                continue;
            }
            if (roll < 0.0025f) {
                float sc = rng.range(0.6f, 1.9f);
                rockInst.push_back({vec3(x, h - 0.3f * sc, z), sc, vec3(1, 1, 1) * rng.range(0.85f, 1.1f), rng.range(0, 6.28f)});
                addTrunk(x, z, 1.0f * sc);
                continue;
            }
            if (roll > dens) continue;
            float pineP = clampf(0.30f + (h - 50.f) / 140.f + fbm(x / 200.f, z / 200.f, 2, 91) * 0.5f, 0.12f, 0.96f);
            if (rng.f() < pineP) {
                float sc = rng.range(0.65f, 1.35f);
                float v = rng.range(0.85f, 1.12f);
                vec3 tint = vec3(rng.range(0.92f, 1.05f), 1.0f, rng.range(0.85f, 1.0f)) * v;
                pineInst.push_back({vec3(x, h - 0.3f, z), sc, tint, rng.range(0, 6.28f)});
                addTrunk(x, z, 0.38f * sc);
            } else {
                float sc = rng.range(0.7f, 1.25f);
                float pick = rng.f(), acc = 0;
                vec3 tint = autumn[0];
                for (int k = 0; k < 6; k++) { acc += autumnW[k]; if (pick <= acc) { tint = autumn[k]; break; } }
                tint = tint * rng.range(0.95f, 1.2f);
                leafInst.push_back({vec3(x, h - 0.3f, z), sc, tint, rng.range(0, 6.28f)});
                addTrunk(x, z, 0.32f * sc);
            }
        }

    // ---- roadside props: marker posts and guardrails (built straight into a static mesh)
    props = MeshBuilder();
    vec4 white(0.92f, 0.92f, 0.88f, 0), red(0.9f, 0.18f, 0.08f, 3), wood(0.30f, 0.24f, 0.18f, 0), steel(1, 1, 1, 7);
    int railSegs = 0;
    for (int ri = 0; ri < int(roads.size()); ri++) {
        const Road& r = roads[ri];
        if (r.surface != SURF_ASPHALT) continue;
        int m = int(r.pts.size());
        float hw = r.width * 0.5f;
        // which samples need a rail on each side?
        for (int side = -1; side <= 1; side += 2) {
            std::vector<char> need(m, 0);
            for (int i = 0; i < m; i++) {
                vec3 t = r.tan[i], right(-t.z, 0, t.x);
                vec3 q = r.pts[i] + right * (side * (hw + 8.f));
                float drop = r.pts[i].y - heightAt(q.x, q.z);
                int ip = r.closed ? (i - 4 + m) % m : std::max(i - 4, 0);
                int in = r.closed ? (i + 4) % m : std::min(i + 4, m - 1);
                float turn = wrapAngle(std::atan2(r.tan[in].x, r.tan[in].z) - std::atan2(r.tan[ip].x, r.tan[ip].z));
                bool outside = (turn > 0.18f && side < 0) || (turn < -0.18f && side > 0);
                need[i] = drop > 2.0f || outside;
                vec3 rp = r.pts[i] + right * (side * (hw + 1.5f));
                if (std::hypot(rp.x - hubPos.x, rp.z - hubPos.z) < 80.f) need[i] = 0;
                // keep junctions open
                int cx = int((rp.x + HALF) / RGRID), cz = int((rp.z + HALF) / RGRID);
                for (int gz = cz - 1; gz <= cz + 1 && need[i]; gz++)
                    for (int gx = cx - 1; gx <= cx + 1 && need[i]; gx++) {
                        if (gx < 0 || gz < 0 || gx >= rgN || gz >= rgN) continue;
                        for (auto& rs : roadGrid[gz * rgN + gx])
                            if (rs.first != ri && std::hypot(roads[rs.first].pts[rs.second].x - rp.x, roads[rs.first].pts[rs.second].z - rp.z) < 22.f) { need[i] = 0; break; }
                    }
            }
            // remove short runs, then emit
            for (int i = 0; i < m;) {
                if (!need[i]) { i++; continue; }
                int j = i;
                while (j < m && need[j]) j++;
                if (j - i >= 6) {
                    railSegs++;
                    for (int k = i; k < j; k++) {
                        int k2 = k + 1;
                        if (k2 >= m) { if (!r.closed) break; k2 = 0; }
                        if (k2 >= j && !(r.closed && j == m && k2 == 0)) break;
                        vec3 t0 = r.tan[k], t1 = r.tan[k2];
                        vec3 r0(-t0.z, 0, t0.x), r1(-t1.z, 0, t1.x);
                        vec3 a = r.pts[k] + r0 * (side * (hw + 1.5f)), bb = r.pts[k2] + r1 * (side * (hw + 1.5f));
                        const float prof[5][2] = {{0.f, 0.52f}, {0.07f, 0.60f}, {0.f, 0.68f}, {0.07f, 0.76f}, {0.f, 0.84f}};
                        for (int q = 0; q < 4; q++) {
                            vec3 a0 = a + r0 * (side * prof[q][0]) + vec3(0, prof[q][1], 0);
                            vec3 a1 = a + r0 * (side * prof[q + 1][0]) + vec3(0, prof[q + 1][1], 0);
                            vec3 b0 = bb + r1 * (side * prof[q][0]) + vec3(0, prof[q][1], 0);
                            vec3 b1 = bb + r1 * (side * prof[q + 1][0]) + vec3(0, prof[q + 1][1], 0);
                            props.flatQuad(a0, b0, b1, a1, steel);
                        }
                        // post every sample (~4 m), sunk into the ground
                        vec3 pp = a + r0 * (side * 0.22f);
                        float gh = heightAt(pp.x, pp.z);
                        mat4 xf = basis(r0, vec3(0, 1, 0), t0, vec3(pp.x, 0, pp.z));
                        float top = r.pts[k].y + 0.82f;
                        props.box(vec3(0, (gh - 0.4f + top) * 0.5f, 0), vec3(0.07f, (top - gh + 0.4f) * 0.5f, 0.09f), wood, &xf);
                        addTrunk(a.x, a.z, 0.35f);
                    }
                }
                i = j;
            }
        }
        // white marker posts every 50 m where there is no rail
        float next = 0;
        for (int i = 0; i < m; i++) {
            if (r.dist[i] < next) continue;
            next = r.dist[i] + 50.f;
            vec3 t = r.tan[i], right(-t.z, 0, t.x);
            for (int side = -1; side <= 1; side += 2) {
                vec3 p = r.pts[i] + right * (side * (hw + 1.4f));
                if (std::hypot(p.x - hubPos.x, p.z - hubPos.z) < 70.f) continue;
                float gh = heightAt(p.x, p.z);
                mat4 xf = basis(right, vec3(0, 1, 0), t, vec3(p.x, gh - 0.1f, p.z));
                props.box(vec3(0, 0.6f, 0), vec3(0.06f, 0.6f, 0.06f), white, &xf);
                props.box(vec3(0, 1.0f, side * -0.065f), vec3(0.045f, 0.07f, 0.008f), red, &xf);
            }
        }
    }
    float roadKm = 0;
    for (auto& r : roads) roadKm += r.length / 1000.f;
    SDL_Log("World: %.1f km of road, %d guardrail runs", roadKm, railSegs);
    SDL_Log("World: %zu pines, %zu autumn trees, %zu rocks", pineInst.size(), leafInst.size(), rockInst.size());
}

// ---------------------------------------------------------------- generate
void World::generate(uint32_t seed) {
    Rng rng(seed);
    height.resize(N * N);

    // ---- road layout (control points)
    roads.clear();
    std::vector<vec3> loop;
    for (int i = 0; i < 18; i++) {
        float a = i / 18.f * 2 * PI;
        float r = 870.f + rng.range(-90.f, 90.f);
        loop.push_back(vec3(std::cos(a) * r, 0, std::sin(a) * r * 0.82f));
    }
    std::vector<vec3> valley;
    valley.push_back(loop[9]);
    for (float x = -640.f; x <= 640.f; x += 128.f)
        valley.push_back(vec3(x, 0, 200.f * std::sin(x / 380.f + 0.3f) + rng.range(-35.f, 35.f)));
    valley.push_back(loop[0]);

    std::vector<vec3> summit;
    vec3 sc(250, 0, 1060);
    for (int i = 0; i < 12; i++) {
        float a = i / 12.f * 2 * PI;
        float rx = 300.f + rng.range(-40.f, 40.f), rz = 170.f + rng.range(-35.f, 35.f);
        summit.push_back(sc + vec3(std::cos(a) * rx, 0, std::sin(a) * rz));
    }
    std::vector<vec3> pass = {loop[4], loop[4] + vec3(-90, 0, 90), loop[4] + vec3(60, 0, 160), summit[9] + vec3(-70, 0, -60), summit[9]};

    std::vector<vec3> trail;
    vec3 tc(-430, 0, -360);
    for (int i = 0; i < 14; i++) {
        float a = -i / 14.f * 2 * PI;
        float rr = 1.f + rng.range(-0.28f, 0.28f);
        trail.push_back(tc + vec3(std::cos(a) * 215.f * rr, 0, std::sin(a) * 170.f * rr));
    }

    auto addRoad = [&](const char* name, const std::vector<vec3>& c, bool closed, float w, int surf) {
        Road r;
        r.name = name;
        r.width = w;
        r.surface = surf;
        buildRoad(r, c, closed);
        roads.push_back(r);
    };
    addRoad("Ridge Highway", loop, true, 13.f, SURF_ASPHALT);
    addRoad("Valley Road", valley, false, 12.f, SURF_ASPHALT);
    addRoad("Summit Loop", summit, true, 11.f, SURF_ASPHALT);
    addRoad("Pass Road", pass, false, 11.f, SURF_ASPHALT);
    addRoad("Timberline Trail", trail, true, 8.5f, SURF_DIRT);

    // cross-country checkpoints through the north-west meadows
    crossCountry = {vec3(-140, 0, 290), vec3(-300, 0, 360), vec3(-470, 0, 300), vec3(-610, 0, 400),
                    vec3(-540, 0, 540), vec3(-360, 0, 520), vec3(-210, 0, 600), vec3(-80, 0, 520)};

    computeRoadHeights();

    // ---- festival hub beside Valley Road
    {
        Road& v = roads[1];
        int bi = 0; float bd = 1e9f;
        for (int i = 0; i < int(v.pts.size()); i++) {
            float d = std::fabs(v.pts[i].x + 430.f);
            if (d < bd) { bd = d; bi = i; }
        }
        vec3 t = v.tan[bi], right(-t.z, 0, t.x);
        vec3 side = right.z > 0 ? right : -right;  // northern side
        hubPos = v.pts[bi] + side * 62.f;
        hubPos.y = v.pts[bi].y;
        vec3 toRoad = normalize(-side);
        hubSpawn = hubPos + toRoad * 18.f;
        hubYaw = std::atan2(toRoad.x, toRoad.z);
    }

    // ---- terrain heights
    for (int iz = 0; iz < N; iz++)
        for (int ix = 0; ix < N; ix++) {
            float x = -HALF + ix * CELL, z = -HALF + iz * CELL;
            float h = baseHeight(x, z);
            float dh = std::hypot(x - hubPos.x, z - hubPos.z);
            h = lerpf(h, hubPos.y - 0.03f, 1.f - smoothstepf(48.f, 85.f, dh));
            height[iz * N + ix] = h;
        }
    carveTerrain();
    for (auto& c : crossCountry) c.y = heightAt(c.x, c.z);
    hubSpawn.y = heightAt(hubSpawn.x, hubSpawn.z);
    scatter(seed);
}

// ---------------------------------------------------------------- GPU
// Conifer: tapered bark trunk + whorls of drooping needle-branch cards. Nominal height ~16 m.
static void buildPine(MeshBuilder& b, bool lod) {
    Rng r(lod ? 5 : 3);
    b.tube(vec3(0, -0.6f, 0), vec3(0, 15.8f, 0), 0.40f, 0.05f, lod ? 5 : 10, vec4(0, 0, 0.95f, 2));
    int whorls = lod ? 9 : 24;
    for (int k = 0; k < whorls; k++) {
        float t = k / float(whorls - 1);
        float y = lerpf(2.2f, 15.4f, t);
        float L = lerpf(4.8f, 0.8f, std::pow(t, 0.85f)) * r.range(0.85f, 1.1f);
        int nb = lod ? 3 : (t < 0.5f ? 6 : 5);
        for (int i = 0; i < nb; i++) {
            float a = k * 2.4f + i * 2 * PI / nb + r.range(-0.3f, 0.3f);
            vec3 dir(std::cos(a), 0, std::sin(a));
            float droop = r.range(0.22f, 0.42f);
            vec3 base = vec3(0, y, 0) + dir * 0.1f;
            vec3 tip = base + dir * L + vec3(0, -L * droop, 0);
            vec3 side = normalize(vec3(-dir.z, 0.22f, dir.x));
            float w = L * (lod ? 0.85f : 0.62f) + 0.5f;
            vec3 n = normalize(dir * 0.7f + vec3(0, 0.75f, 0));
            float sh = clampf(0.6f + 0.4f * t + r.range(-0.05f, 0.05f), 0.f, 1.2f);
            b.quad4(base - side * (w * 0.2f), base + side * (w * 0.2f), tip + side * (w * 0.5f), tip - side * (w * 0.5f), n,
                    vec4(0, 0, sh, 1), vec4(1, 0, sh, 1), vec4(1, 1, sh, 1), vec4(0, 1, sh, 1));
            if (!lod) {
                vec3 bd = normalize(tip - base);
                vec3 side2 = normalize(cross(bd, side));
                float w2 = w * 0.42f;
                b.quad4(base - side2 * (w2 * 0.2f), base + side2 * (w2 * 0.2f), tip + side2 * (w2 * 0.5f), tip - side2 * (w2 * 0.5f), n,
                        vec4(0, 0, sh * 0.85f, 1), vec4(1, 0, sh * 0.85f, 1), vec4(1, 1, sh * 0.85f, 1), vec4(0, 1, sh * 0.85f, 1));
            }
        }
    }
    // spire
    for (int i = 0; i < 2; i++) {
        float a = i * PI * 0.5f;
        vec3 sd(std::cos(a) * 0.8f, 0, std::sin(a) * 0.8f);
        vec3 lo(0, 14.2f, 0), hi(0, 17.6f, 0);
        b.quad4(lo - sd, lo + sd, hi + sd * 0.15f, hi - sd * 0.15f, vec3(0, 1, 0), vec4(0, 1, 1.1f, 1), vec4(1, 1, 1.1f, 1), vec4(1, 0, 1.1f, 1), vec4(0, 0, 1.1f, 1));
    }
}

// Broadleaf: trunk, a few limbs and a crown made of leaf-cluster cards. Nominal height ~11 m.
static void buildLeafy(MeshBuilder& b, bool lod) {
    Rng r(lod ? 9 : 7);
    b.tube(vec3(0, -0.6f, 0), vec3(0.15f, 5.6f, 0.1f), 0.34f, 0.17f, lod ? 5 : 9, vec4(0, 0, 0.9f, 2));
    int limbs = lod ? 3 : 6;
    for (int i = 0; i < limbs; i++) {
        float a = i * 2 * PI / limbs + r.range(-0.3f, 0.3f);
        vec3 from(0.1f, r.range(3.2f, 5.0f), 0.05f);
        vec3 to = from + vec3(std::cos(a) * 2.6f, r.range(1.6f, 2.6f), std::sin(a) * 2.6f);
        b.tube(from, to, 0.13f, 0.04f, 5, vec4(0, 0, 0.85f, 2));
    }
    vec3 c(0, 6.8f, 0), rad(3.9f, 3.1f, 3.9f);
    int cards = lod ? 28 : 95;
    for (int i = 0; i < cards; i++) {
        vec3 d;
        do { d = vec3(r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)); } while (dot(d, d) > 1.f || dot(d, d) < 0.01f);
        d = normalize(d);
        float rr = std::pow(r.f(), 0.35f);
        vec3 p = c + d * rad * rr;
        float sz = (lod ? 3.6f : 2.5f) * r.range(0.8f, 1.2f);
        vec3 nn = normalize(d + vec3(r.range(-0.8f, 0.8f), r.range(-0.4f, 0.8f), r.range(-0.8f, 0.8f)));
        vec3 t1 = normalize(cross(nn, std::fabs(nn.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0)));
        vec3 t2 = cross(nn, t1);
        float rot = r.range(0, 6.28f);
        vec3 u = t1 * std::cos(rot) + t2 * std::sin(rot), v = cross(nn, u);
        vec3 ln = normalize(d * 0.85f + vec3(0, 0.35f, 0));
        float sh = 0.5f + 0.5f * rr + 0.12f * d.y;
        float h = sz * 0.5f;
        b.quad4(p - u * h - v * h, p + u * h - v * h, p + u * h + v * h, p - u * h + v * h, ln,
                vec4(0, 0, sh, 1), vec4(1, 0, sh, 1), vec4(1, 1, sh, 1), vec4(0, 1, sh, 1));
    }
}

void World::buildGPU() {
    // ---- terrain chunks
    const int CH = 20, CS = (N - 1) / CH;
    std::vector<vec3> nrm(N * N);
    for (int iz = 0; iz < N; iz++)
        for (int ix = 0; ix < N; ix++) {
            float hl = height[iz * N + std::max(ix - 1, 0)], hr = height[iz * N + std::min(ix + 1, N - 1)];
            float hd = height[std::max(iz - 1, 0) * N + ix], hu = height[std::min(iz + 1, N - 1) * N + ix];
            nrm[iz * N + ix] = normalize(vec3(hl - hr, 2 * CELL, hd - hu));
        }
    chunks.resize(CH * CH);
    for (int cz = 0; cz < CH; cz++)
        for (int cx = 0; cx < CH; cx++) {
            std::vector<Vertex> v;
            std::vector<uint32_t> idx;
            for (int z = 0; z <= CS; z++)
                for (int x = 0; x <= CS; x++) {
                    int ix = cx * CS + x, iz = cz * CS + z;
                    float wx = -HALF + ix * CELL, wz = -HALF + iz * CELL;
                    float f = forestAt(wx, wz);
                    float rd = roadDist[iz * N + ix];
                    vec4 col(0.30f, 0.25f, 0.15f, f * 0.45f);
                    // dusty verge beside roads
                    if (rd < 4.f) col = vec4(0.52f, 0.46f, 0.33f, 0.55f * (1.f - smoothstepf(1.f, 4.f, rd)));
                    v.push_back({vec3(wx, height[iz * N + ix], wz), nrm[iz * N + ix], col});
                }
            for (int z = 0; z < CS; z++)
                for (int x = 0; x < CS; x++) {
                    uint32_t a = z * (CS + 1) + x, b = a + 1, c = a + (CS + 1), d = c + 1;
                    // diagonal a-d (matches heightAt)
                    idx.insert(idx.end(), {a, d, b, a, c, d});
                }
            Chunk& ch = chunks[cz * CH + cx];
            ch.mesh.upload(v, idx);
            ch.center = vec3(-HALF + (cx + 0.5f) * CS * CELL, 0, -HALF + (cz + 0.5f) * CS * CELL);
        }

    // ---- roads
    {
        MeshBuilder b;
        for (auto& r : roads) {
            int m = int(r.pts.size());
            float hw = r.width * 0.5f;
            float mat = r.surface == SURF_DIRT ? 11.f : 10.f;
            uint32_t base = uint32_t(b.v.size());
            int rows = r.closed ? m + 1 : m;
            for (int row = 0; row < rows; row++) {
                int i = row % m;
                vec3 c = r.pts[i];
                vec3 t = r.tan[i], right(-t.z, 0, t.x);
                int ip = r.closed ? (i - 1 + m) % m : std::max(i - 1, 0);
                int in = r.closed ? (i + 1) % m : std::min(i + 1, m - 1);
                vec3 t3 = normalize(r.pts[in] - r.pts[ip]);
                vec3 n = normalize(cross(right, t3));
                float v = row == m ? r.length : r.dist[i];
                float sh = r.surface == SURF_DIRT ? 0.8f : 1.7f;
                b.add(c - right * (hw + sh) + vec3(0, -0.02f, 0), n, vec4(1, v, r.width, 12));
                b.add(c - right * hw + vec3(0, 0.04f, 0), n, vec4(0, v, r.width, 12));
                b.add(c - right * hw + vec3(0, 0.04f, 0), n, vec4(0, v, r.width, mat));
                b.add(c + right * hw + vec3(0, 0.04f, 0), n, vec4(1, v, r.width, mat));
                b.add(c + right * hw + vec3(0, 0.04f, 0), n, vec4(0, v, r.width, 12));
                b.add(c + right * (hw + sh) + vec3(0, -0.02f, 0), n, vec4(1, v, r.width, 12));
            }
            for (int i = 0; i + 1 < rows; i++) {
                uint32_t a = base + i * 6, nb = base + (i + 1) * 6;
                for (int k = 0; k < 6; k += 2) {
                    // quad between column k..k+1 at sample i and i+1
                    b.tri(a + k, nb + k + 1, nb + k);
                    b.tri(a + k, a + k + 1, nb + k + 1);
                }
            }
        }
        b.build(roadMesh);
    }

    // ---- water
    {
        MeshBuilder b;
        const int G = 32;
        for (const Lake& L : lakes) {
            float ext = L.r * 1.3f;
            uint32_t base = uint32_t(b.v.size());
            for (int z = 0; z <= G; z++)
                for (int x = 0; x <= G; x++)
                    b.add(vec3(L.c.x - ext + x * 2 * ext / G, waterLevel, L.c.z - ext + z * 2 * ext / G), vec3(0, 1, 0), vec4(1, 1, 1, 0));
            for (int z = 0; z < G; z++)
                for (int x = 0; x < G; x++) {
                    uint32_t a = base + z * (G + 1) + x;
                    b.tri(a, a + G + 1, a + 1);
                    b.tri(a + 1, a + G + 1, a + G + 2);
                }
        }
        b.build(waterMesh);
    }

    // ---- festival hub
    {
        MeshBuilder b;
        vec3 c = hubPos;
        vec3 fwd(std::sin(hubYaw), 0, std::cos(hubYaw)), right(-fwd.z, 0, fwd.x);
        // plaza
        const int S = 40;
        uint32_t ctr = b.add(c + vec3(0, 0.06f, 0), vec3(0, 1, 0), vec4(0.46f, 0.44f, 0.41f, 13));
        for (int i = 0; i <= S; i++) {
            float a = i / float(S) * 2 * PI;
            b.add(c + vec3(std::cos(a) * 44.f, 0.06f, std::sin(a) * 44.f), vec3(0, 1, 0), vec4(0.42f, 0.40f, 0.38f, 13));
        }
        for (int i = 0; i < S; i++) b.tri(ctr, ctr + 2 + i, ctr + 1 + i);
        // painted ring
        for (int i = 0; i < S; i++) {
            float a0 = i / float(S) * 2 * PI, a1 = (i + 1) / float(S) * 2 * PI;
            vec4 col = (i % 2) ? vec4(0.92f, 0.25f, 0.18f, 0) : vec4(0.95f, 0.95f, 0.93f, 0);
            auto P = [&](float a, float r) { return c + vec3(std::cos(a) * r, 0.08f, std::sin(a) * r); };
            b.flatQuad(P(a0, 41.f), P(a1, 41.f), P(a1, 43.f), P(a0, 43.f), col);
        }
        // tents behind the plaza (away from the road)
        const vec4 tentCols[] = {{0.85f, 0.15f, 0.12f, 0}, {0.95f, 0.75f, 0.15f, 0}, {0.15f, 0.35f, 0.75f, 0},
                                 {0.95f, 0.45f, 0.10f, 0}, {0.92f, 0.92f, 0.9f, 0}};
        for (int i = 0; i < 5; i++) {
            float a = (i - 2) * 0.42f;
            vec3 dir = fwd * -std::cos(a) + right * std::sin(a);
            vec3 p = c + dir * 34.f;
            b.cylinder(p, 7.f, 2.6f, 14, vec4(0.94f, 0.93f, 0.9f, 0), false);
            b.cone(p + vec3(0, 2.6f, 0), 7.6f, 5.5f, 14, tentCols[i]);
            b.cylinder(p + vec3(0, 7.5f, 0), 0.08f, 3.f, 5, vec4(0.8f, 0.8f, 0.8f, 0));
            b.flatTri(p + vec3(0, 10.4f, 0), p + vec3(0, 9.4f, 0), p + vec3(0, 9.9f, 0) + right * 1.6f, tentCols[(i + 2) % 5]);
            b.flatTri(p + vec3(0, 10.4f, 0), p + vec3(0, 9.9f, 0) + right * 1.6f, p + vec3(0, 9.4f, 0), tentCols[(i + 2) % 5]);
        }
        // entrance arch facing the road
        {
            vec3 g = c + fwd * 40.f;
            mat4 xf = basis(right, vec3(0, 1, 0), fwd, g);
            vec4 pillar(0.12f, 0.12f, 0.13f, 0), banner(0.92f, 0.28f, 0.12f, 0), stripe(0.98f, 0.78f, 0.2f, 0);
            b.box(vec3(-11, 5.5f, 0), vec3(0.9f, 5.5f, 0.9f), pillar, &xf);
            b.box(vec3(11, 5.5f, 0), vec3(0.9f, 5.5f, 0.9f), pillar, &xf);
            b.box(vec3(0, 11.5f, 0), vec3(12.5f, 1.5f, 0.8f), banner, &xf);
            b.box(vec3(0, 10.2f, 0), vec3(12.5f, 0.25f, 0.85f), stripe, &xf);
            b.box(vec3(0, 12.9f, 0), vec3(12.5f, 0.15f, 0.85f), stripe, &xf);
        }
        // display podiums (cars are drawn on top by the game)
        for (int i = 0; i < 3; i++) {
            vec3 p = c + right * ((i - 1) * 12.f) - fwd * 12.f;
            b.cylinder(p, 3.6f, 0.45f, 24, vec4(0.15f, 0.15f, 0.16f, 0));
            b.cylinder(p + vec3(0, 0.45f, 0), 3.65f, 0.06f, 24, vec4(0.9f, 0.3f, 0.15f, 3), false);
        }
        // flag poles around the edge
        const vec4 flagCols[] = {{0.9f, 0.2f, 0.15f, 0}, {0.95f, 0.8f, 0.2f, 0}, {0.2f, 0.5f, 0.85f, 0}, {0.95f, 0.5f, 0.1f, 0}};
        for (int i = 0; i < 12; i++) {
            float a = i / 12.f * 2 * PI + 0.13f;
            vec3 p = c + vec3(std::cos(a) * 46.f, 0, std::sin(a) * 46.f);
            if (dot(normalize(p - c), fwd) > 0.75f) continue;  // keep the entrance open
            b.cylinder(p, 0.1f, 8.f, 5, vec4(0.85f, 0.85f, 0.85f, 0));
            vec3 t(-std::sin(a), 0, std::cos(a));
            b.flatQuad(p + vec3(0, 7.8f, 0), p + vec3(0, 7.8f, 0) + t * 1.2f, p + vec3(0, 4.6f, 0) + t * 1.2f, p + vec3(0, 4.6f, 0), flagCols[i % 4]);
            b.flatQuad(p + vec3(0, 4.6f, 0), p + vec3(0, 4.6f, 0) + t * 1.2f, p + vec3(0, 7.8f, 0) + t * 1.2f, p + vec3(0, 7.8f, 0), flagCols[i % 4]);
        }
        b.build(hubMesh);
    }

    // ---- foliage / rocks / props
    {
        MeshBuilder pb; buildPine(pb, false); pb.build(pineMesh);
        MeshBuilder pl; buildPine(pl, true); pl.build(pineLod);
        MeshBuilder lb; buildLeafy(lb, false); lb.build(leafMesh);
        MeshBuilder ll; buildLeafy(ll, true); ll.build(leafLod);
        MeshBuilder rb, tmp;
        tmp.sphere(vec3(0, 0, 0), 1.2f, 2, vec4(0, 0, 1, 3), 0.28f, 5);
        rb.append(tmp, scale(vec3(1.25f, 0.72f, 1.0f)));
        rb.build(rockMesh);
        pines.build(&pineMesh, &pineLod, pineInst, HALF, 125.f);
        leafy.build(&leafMesh, &leafLod, leafInst, HALF, 125.f);
        rocks.build(&rockMesh, nullptr, rockInst, HALF, 125.f);
        props.build(propsMesh);
        props = MeshBuilder();
    }
    // ---- height / road-distance textures for GPU grass placement
    {
        auto mk = [&](const std::vector<float>& data) {
            GLuint t;
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, N, N, 0, GL_RED, GL_FLOAT, data.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return t;
        };
        heightTex = mk(height);
        roadTex = mk(roadDist);
    }
    buildGrass();
    buildMapTexture();
}

void World::buildGrass() {
    MeshBuilder b;
    const float w = 0.95f, h = 0.62f;
    for (int i = 0; i < 3; i++) {
        float a = i * PI / 3.f;
        vec3 d(std::cos(a) * w * 0.5f, 0, std::sin(a) * w * 0.5f);
        b.quad4(-d, d, d + vec3(0, h, 0), -d + vec3(0, h, 0), vec3(0, 1, 0), vec4(0, 1, 1, 1), vec4(1, 1, 1, 1), vec4(1, 0, 1, 1), vec4(0, 0, 1, 1));
    }
    b.build(grassMesh);
    // concentric rings: dense small tufts near the camera, fewer and larger ones far away
    const float spec[4][4] = {{0.55f, 0.f, 52.f, 1.0f}, {1.1f, 40.f, 135.f, 1.5f}, {2.3f, 118.f, 300.f, 2.1f}, {4.4f, 270.f, 580.f, 2.9f}};
    grassRings.clear();
    for (auto& sp : spec) {
        GrassRing r;
        r.spacing = sp[0]; r.inner = sp[1]; r.outer = sp[2]; r.scale = sp[3];
        std::vector<float> offs;
        int n = int(r.outer / r.spacing) + 1;
        float in2 = std::max(0.f, r.inner - r.spacing * 2) , out2 = r.outer + r.spacing * 2;
        for (int z = -n; z <= n; z++)
            for (int x = -n; x <= n; x++) {
                float d = std::sqrt(float(x * x + z * z)) * r.spacing;
                if (d < in2 || d > out2) continue;
                offs.push_back(x * r.spacing);
                offs.push_back(z * r.spacing);
            }
        r.count = int(offs.size() / 2);
        glGenBuffers(1, &r.buf);
        glBindBuffer(GL_ARRAY_BUFFER, r.buf);
        glBufferData(GL_ARRAY_BUFFER, offs.size() * 4, offs.data(), GL_STATIC_DRAW);
        grassRings.push_back(r);
    }
    glBindVertexArray(grassMesh.vao);
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);
    glBindVertexArray(0);
    size_t total = 0;
    for (auto& r : grassRings) total += r.count;
    SDL_Log("Grass: %zu tufts in %zu rings", total, grassRings.size());
}

void World::drawGrass(Shader& sh, vec3 eye, int rings) const {
    glBindVertexArray(grassMesh.vao);
    for (int i = 0; i < int(grassRings.size()) && i < rings; i++) {
        const GrassRing& r = grassRings[i];
        sh.set("uOrigin", vec2(std::floor(eye.x / r.spacing) * r.spacing, std::floor(eye.z / r.spacing) * r.spacing));
        sh.set("uSpacing", r.spacing);
        sh.set("uInner", r.inner);
        sh.set("uOuter", i + 1 == rings ? -r.outer : r.outer);
        sh.set("uScaleMul", r.scale);
        glBindBuffer(GL_ARRAY_BUFFER, r.buf);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
        glDrawElementsInstanced(GL_TRIANGLES, grassMesh.count, GL_UNSIGNED_INT, nullptr, r.count);
    }
}

void World::buildMapTexture() {
    std::vector<uint8_t> img(MAP_RES * MAP_RES * 4);
    const float mpp = 2 * HALF / MAP_RES;
    vec3 light = normalize(vec3(-1, 1.4f, 1));
    for (int py = 0; py < MAP_RES; py++)
        for (int px = 0; px < MAP_RES; px++) {
            float x = -HALF + (px + 0.5f) * mpp;
            float z = HALF - (py + 0.5f) * mpp;
            float h = heightAt(x, z);
            vec3 c;
            if (h < waterLevel) {
                float d = clampf((waterLevel - h) / 10.f, 0.f, 1.f);
                c = lerp(vec3(0.30f, 0.52f, 0.60f), vec3(0.14f, 0.30f, 0.42f), d);
            } else {
                vec3 n = normalAt(x, z);
                c = lerp(vec3(0.62f, 0.58f, 0.36f), vec3(0.50f, 0.47f, 0.40f), smoothstepf(60.f, 200.f, h));
                c = c * (1.f - 0.4f * forestAt(x, z));
                if (n.y < 0.75f) c = lerp(c, vec3(0.5f, 0.48f, 0.45f), 0.6f);
                if (h > 285.f) c = lerp(c, vec3(0.92f, 0.93f, 0.95f), smoothstepf(285.f, 320.f, h));
                float shade = 0.72f + 0.42f * std::max(dot(n, light), 0.f);
                c = c * shade;
            }
            uint8_t* o = &img[(py * MAP_RES + px) * 4];
            o[0] = uint8_t(clampf(c.x, 0, 1) * 255);
            o[1] = uint8_t(clampf(c.y, 0, 1) * 255);
            o[2] = uint8_t(clampf(c.z, 0, 1) * 255);
            o[3] = 255;
        }
    for (auto& r : roads) {
        vec3 col = r.surface == SURF_DIRT ? vec3(0.80f, 0.64f, 0.44f) : vec3(0.96f, 0.95f, 0.92f);
        float rad = std::max(1.2f, r.width * 0.5f / mpp + 0.4f);
        for (auto& p : r.pts) {
            int cx = int((p.x + HALF) / mpp), cy = int((HALF - p.z) / mpp);
            int ir = int(std::ceil(rad));
            for (int dy = -ir; dy <= ir; dy++)
                for (int dx = -ir; dx <= ir; dx++) {
                    if (dx * dx + dy * dy > rad * rad) continue;
                    int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= MAP_RES || y >= MAP_RES) continue;
                    uint8_t* o = &img[(y * MAP_RES + x) * 4];
                    o[0] = uint8_t(col.x * 255); o[1] = uint8_t(col.y * 255); o[2] = uint8_t(col.z * 255);
                }
        }
    }
    glGenTextures(1, &mapTex);
    glBindTexture(GL_TEXTURE_2D, mapTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, MAP_RES, MAP_RES, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
