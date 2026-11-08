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

static float boxMask(float x, float z, const Region& r, float feather) {
    float dx = std::max(r.x0 - x, x - r.x1), dz = std::max(r.z0 - z, z - r.z1);
    float d = std::max(dx, dz);
    return 1.f - smoothstepf(0.f, feather, d);
}

static float segDist(float x, float z, vec3 a, vec3 b) {
    vec2 ab(b.x - a.x, b.z - a.z), ap(x - a.x, z - a.z);
    float t = clampf(dot(ap, ab) / std::max(dot(ab, ab), 1e-6f), 0.f, 1.f);
    return length(vec2(a.x + ab.x * t - x, a.z + ab.y * t - z));
}

// corridors where the highways leave the old valley: the ridge is cut down so the climb is drivable
static const vec3 kCorridors[][2] = {
    {vec3(860, 0, -60), vec3(1800, 0, -250)},     // east, to the city
    {vec3(-880, 0, 60), vec3(-2200, 0, 450)},     // west, to the farms
    {vec3(-260, 0, -880), vec3(-500, 0, -2000)},  // south, to the coast
};

bool World::urban(float x, float z, float pad) const {
    auto in = [&](const Region& r) { return x > r.x0 - pad && x < r.x1 + pad && z > r.z0 - pad && z < r.z1 + pad; };
    return in(kCity) || in(kDocks) || estatePaved(x, z, pad);
}

float World::baseHeight(float x, float z) const {
    // ---- the original valley (inside), framed by a ridge that is tallest to the north
    float h = 42.f + fbm(x / 1100.f + 3.1f, z / 1100.f - 1.7f, 4, 11) * 85.f;
    h += fbm(x / 280.f, z / 280.f, 4, 23) * 24.f;
    h += fbm(x / 70.f, z / 70.f, 3, 37) * 2.5f;
    float edge = std::max(std::fabs(x), std::fabs(z));
    float ridge = 1.f - std::fabs(fbm(x / 420.f, z / 420.f, 4, 51));
    ridge *= ridge;
    float rimK = 0.35f + 0.65f * smoothstepf(400.f, 1400.f, z);
    float hIn = h + smoothstepf(1000.f, 1430.f, edge) * (170.f + ridge * 170.f) * rimK * (1.f - smoothstepf(1500.f, 1900.f, edge));
    hIn += smoothstepf(650.f, 1250.f, z) * (60.f + ridge * 120.f);
    // ---- the wider island: rolling farmland, big northern mountains
    float hOut = 30.f + fbm(x / 900.f, z / 900.f, 4, 61) * 55.f + fbm(x / 250.f, z / 250.f, 3, 63) * 14.f + fbm(x / 70.f, z / 70.f, 3, 37) * 2.5f;
    float ridge2 = 1.f - std::fabs(fbm(x / 600.f, z / 600.f, 4, 71));
    ridge2 *= ridge2;
    float north = smoothstepf(1450.f, 2500.f, z);
    hOut += north * (230.f + ridge2 * 380.f);
    float inner = 1.f - smoothstepf(1450.f, 1900.f, edge);
    h = lerpf(hOut, hIn, inner);
    // ---- Fuji: a lone stratovolcano. Long concave skirts, a steepening upper cone, a flat-topped
    // summit with a crater, and radial gullies scoring the upper flanks (the snow streaks follow them).
    {
        float dx = x - kFuji.x, dz = z - kFuji.z;
        float fd = std::sqrt(dx * dx + dz * dz);
        if (fd < kFujiR) {
            float t = 1.f - fd / kFujiR;
            float rc = 150.f;  // summit plateau radius
            float tt = std::min(t, 1.f - rc / kFujiR);
            float cone = kFujiBase + kFujiH * std::pow(tt / (1.f - rc / kFujiR), 2.2f);
            cone -= (1.f - smoothstepf(60.f, 125.f, fd)) * 45.f;  // the crater
            float rib = fujiRib(std::atan2(dz, dx), fd);
            cone -= 9.f * smoothstepf(0.25f, 0.8f, t) * (1.f - smoothstepf(0.93f, 0.97f, t)) * (0.5f + 0.5f * rib);
            float k = smoothstepf(kFujiR, kFujiR * 0.62f, fd);
            h = lerpf(h, std::max(h * (1.f - 0.4f * k), cone), k);
        }
    }
    // ---- valleys through the ridge for the highways
    for (auto& c : kCorridors) {
        float d = segDist(x, z, c[0], c[1]);
        float k = (1.f - smoothstepf(90.f, 330.f, d)) * (1.f - north);
        h = lerpf(h, std::min(h, 34.f + fbm(x / 300.f, z / 300.f, 2, 81) * 18.f), k);
    }
    // keep lowlands dry (only the lakes and the sea go below water)
    float floorH = waterLevel + 3.f;
    if (h < floorH) h = floorH - (floorH - h) * 0.15f;
    // ---- the city sits on a flat plateau, the docks on a quay just above the sea
    h = lerpf(h, 16.f, boxMask(x, z, kCity, 160.f));
    h = lerpf(h, waterLevel + 2.6f, boxMask(x, z, kDocks, 140.f));
    // ---- sea along the south and east coasts (a hard quay wall at the docks)
    bool quay = x > kDocks.x0 && x < kDocks.x1;
    float seaS = quay ? smoothstepf(kDocks.z0 - 2.f, kDocks.z0 - 22.f, z) : smoothstepf(-2330.f, -2620.f, z);
    float seaE = smoothstepf(2830.f, 2960.f, x);
    float seaW = smoothstepf(-3480.f, -3650.f, x);  // the Sea of Japan along the west coast
    float sea = std::max(std::max(seaS, seaE), seaW);
    h = lerpf(h, waterLevel - 16.f, sea);
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
    // buildings, containers, crane legs: circle against axis-aligned boxes
    if (!boxGrid.empty()) {
        std::vector<int> done;
        for (int z = cz - 2; z <= cz + 2; z++)
            for (int x = cx - 2; x <= cx + 2; x++) {
                if (x < 0 || z < 0 || x >= tgN || z >= tgN) continue;
                for (int bi : boxGrid[z * tgN + x]) {
                    if (std::find(done.begin(), done.end(), bi) != done.end()) continue;
                    done.push_back(bi);
                    const Box& b = boxes[bi];
                    if (p.y > b.top + 0.4f) continue;
                    float qx = clampf(p.x, b.x0, b.x1), qz = clampf(p.z, b.z0, b.z1);
                    float dx = p.x - qx, dz = p.z - qz;
                    float d2 = dx * dx + dz * dz;
                    if (d2 >= r * r) continue;
                    vec3 n;
                    if (d2 > 1e-8f) {
                        float d = std::sqrt(d2);
                        n = vec3(dx / d, 0, dz / d);
                        p += n * (r - d);
                    } else {
                        // centre inside: leave by the nearest face
                        float l = p.x - b.x0, rr = b.x1 - p.x, f = p.z - b.z0, k = b.z1 - p.z;
                        float m = std::min(std::min(l, rr), std::min(f, k));
                        n = m == l ? vec3(-1, 0, 0) : m == rr ? vec3(1, 0, 0) : m == f ? vec3(0, 0, -1) : vec3(0, 0, 1);
                        p += n * (m + r);
                    }
                    normal = n;
                    hit = true;
                }
            }
    }
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
        // never steeper than a real road: relax neighbouring points towards each other until
        // no step is steeper than the limit (converges, and keeps loops closed)
        float gmax = r.surface == SURF_ASPHALT ? 0.13f : 0.18f;
        {
            // a road pinned at both ends to very different heights may need more than the usual
            // limit: spread the climb evenly rather than leaving a cliff where the constraints fight
            int first = -1, last = -1;
            for (int i = 0; i < m; i++) if (w[i] > 0.6f) { if (first < 0) first = i; last = i; }
            if (!r.closed && first >= 0 && last > first) {
                float len = 0;
                for (int i = first; i < last; i++) len += std::hypot(r.pts[i + 1].x - r.pts[i].x, r.pts[i + 1].z - r.pts[i].z);
                float need = std::fabs(hs[last] - hs[first]) / std::max(len, 1.f);
                if (need * 1.15f > gmax) {
                    SDL_Log("Road '%s' needs a %.0f%% grade between its junctions", r.name.c_str(), need * 100.f);
                    gmax = need * 1.15f;
                }
            }
        }
        std::vector<float> lim(m, 0.f);
        for (int i = 0; i < m; i++) {
            int j = r.closed ? (i + 1) % m : std::min(i + 1, m - 1);
            lim[i] = gmax * std::hypot(r.pts[j].x - r.pts[i].x, r.pts[j].z - r.pts[i].z);
        }
        // points tied to a junction with an earlier road stay put
        std::vector<char> pin(m);
        for (int i = 0; i < m; i++) pin[i] = w[i] > 0.6f;
        if (!r.closed) {
            // open roads: sweep forwards and back (propagates long climbs quickly)
            for (int pass = 0; pass < 4; pass++) {
                for (int i = 1; i < m; i++) if (!pin[i]) hs[i] = clampf(hs[i], hs[i - 1] - lim[i - 1], hs[i - 1] + lim[i - 1]);
                for (int i = m - 2; i >= 0; i--) if (!pin[i]) hs[i] = clampf(hs[i], hs[i + 1] - lim[i], hs[i + 1] + lim[i]);
            }
        }
        for (int it = 0; it < 600; it++) {
            bool changed = false;
            int last = r.closed ? m : m - 1;
            for (int i = 0; i < last; i++) {
                int j = (i + 1) % m;
                float d = hs[j] - hs[i];
                float ex = std::fabs(d) - lim[i];
                if (ex > 0.001f && !(pin[i] && pin[j])) {
                    float sgn = d > 0 ? 1.f : -1.f;
                    float ki = pin[i] ? 0.f : pin[j] ? 1.f : 0.5f;
                    hs[j] -= ex * (1.f - ki) * sgn;
                    hs[i] += ex * ki * sgn;
                    changed = true;
                }
            }
            if (!changed) break;
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
    // a natural early-autumn mix: mostly gold and olive, some rust, a few still green, rare deep red
    const vec3 autumn[] = {{0.86f, 0.52f, 0.17f}, {0.90f, 0.70f, 0.24f}, {0.68f, 0.30f, 0.12f},
                           {0.62f, 0.62f, 0.26f}, {0.36f, 0.48f, 0.20f}, {0.80f, 0.42f, 0.15f}};
    const float autumnW[] = {0.20f, 0.24f, 0.10f, 0.18f, 0.18f, 0.10f};
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
            if (urban(x, z, 30.f) || nearEstate(x, z, 42.f)) continue;
            float hubD = std::hypot(x - hubPos.x, z - hubPos.z);
            if (hubD < 95.f) continue;
            if (std::hypot(x - festCenter.x, z - festCenter.z) < 265.f) continue;  // festival grounds
            if (inBox(x, z, 3.f)) continue;                                         // town buildings
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
            if (std::hypot(x - kFuji.x, z - kFuji.z) < kFujiR && h > kFujiBase + 260.f + fbm(x / 90.f, z / 90.f, 2, 17) * 60.f) continue;  // Fuji's treeline
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
                tint = lerp(tint, autumn[rng.irange(0, 5)], rng.range(0.f, 0.35f)) * rng.range(0.9f, 1.12f);  // blend neighbours
                // sakura: cherry blossom in drifts through the lowlands, thickest beside the roads
                float sak = smoothstepf(0.15f, 0.55f, fbm(x / 260.f, z / 260.f, 2, 133)) * (1.f - smoothstepf(90.f, 220.f, h));
                if (roadDistAt(x, z) < 40.f) sak = std::min(1.f, sak + 0.35f);
                if (rng.f() < sak * 0.7f) tint = vec3(rng.range(0.98f, 1.05f), rng.range(0.66f, 0.76f), rng.range(0.78f, 0.88f)) * rng.range(0.95f, 1.1f);
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
                if (std::hypot(rp.x - hubPos.x, rp.z - hubPos.z) < 80.f || nearEstate(rp.x, rp.z, 45.f)) need[i] = 0;
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
                if (std::hypot(p.x - hubPos.x, p.z - hubPos.z) < 70.f || nearEstate(p.x, p.z, 40.f)) continue;
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
    addRoad("Hakone Turnpike", loop, true, 13.f, SURF_ASPHALT);
    addRoad("Chichibu Road", valley, false, 12.f, SURF_ASPHALT);
    addRoad("Haruna Loop", summit, true, 11.f, SURF_ASPHALT);
    addRoad("Usui Pass Road", pass, false, 11.f, SURF_ASPHALT);
    addRoad("Okutama Trail", trail, true, 8.5f, SURF_DIRT);

    // ---- the wider island
    // Coast Highway: east through the city, down to the docks, along the south coast, up the west
    std::vector<vec3> coast = {vec3(880, 0, -40), vec3(1250, 0, -170), vec3(1600, 0, -250), vec3(2000, 0, -250),
                               vec3(2400, 0, -250), vec3(2700, 0, -250), vec3(2810, 0, -470), vec3(2810, 0, -1100),
                               vec3(2620, 0, -1600), vec3(2300, 0, -1890), vec3(1700, 0, -1900), vec3(1000, 0, -1890),
                               vec3(300, 0, -1880), vec3(-500, 0, -2000), vec3(-1300, 0, -1850), vec3(-2050, 0, -1500),
                               vec3(-2480, 0, -800), vec3(-2480, 0, -50), vec3(-2150, 0, 450), vec3(-1500, 0, 380),
                               vec3(-1000, 0, 120), vec3(-890, 0, 40)};
    addRoad("Wangan Highway", coast, false, 14.f, SURF_ASPHALT);
    // city grid (the Coast Highway is the main avenue at z = -250)
    {
        const float xs[] = {1800, 1950, 2100, 2250, 2400, 2550, 2700};
        const float zs[] = {-850, -700, -550, -400, -100, 50, 200};
        static const char* ns[] = {"Chuo-dori", "Meiji-dori", "Yasukuni-dori", "Kabuki-dori", "Aoyama-dori", "Omotesando", "Harumi-dori"};
        static const char* es[] = {"Kanda Street", "Ginza Street", "Shiba Street", "Ueno Street", "Akiba Street", "Roppongi Street", "Shibuya Street"};
        for (int i = 0; i < 7; i++) addRoad(ns[i], {vec3(xs[i], 0, -880), vec3(xs[i], 0, -500), vec3(xs[i], 0, -60), vec3(xs[i], 0, 320)}, false, 12.f, SURF_ASPHALT);
        for (int i = 0; i < 7; i++) addRoad(es[i], {vec3(1770, 0, zs[i]), vec3(2100, 0, zs[i]), vec3(2400, 0, zs[i]), vec3(2730, 0, zs[i])}, false, 12.f, SURF_ASPHALT);
    }
    // docks: the quay road and spurs up to the Coast Highway
    addRoad("Daikoku Quay", {vec3(260, 0, -2330), vec3(800, 0, -2330), vec3(1400, 0, -2330), vec3(2000, 0, -2330), vec3(2240, 0, -2330)}, false, 13.f, SURF_ASPHALT);
    for (float x : {450.f, 1150.f, 1750.f, 2200.f}) {
        // start each lane right on the Coast Highway so it joins it (no step at the junction)
        vec3 top(x, 0, -1905);
        float bd = 1e18f;
        for (auto& rd : roads)
            if (rd.name == "Wangan Highway")
                for (auto& q : rd.pts) {
                    float d = (q.x - x) * (q.x - x) + (q.z + 1905.f) * (q.z + 1905.f);
                    if (d < bd) { bd = d; top = vec3(q.x, 0, q.z); }
                }
        addRoad("Daikoku Lane", {top, vec3(x, 0, top.z - 40.f), vec3(x, 0, -2100), vec3(x, 0, -2320)}, false, 11.f, SURF_ASPHALT);
    }
    // Akagi Touge: hairpins from the Summit Loop up into the northern mountains
    {
        vec3 top = summit[3];
        std::vector<vec3> tg = {top, top + vec3(60, 0, 160), vec3(430, 0, 1390), vec3(120, 0, 1530), vec3(440, 0, 1670),
                                vec3(130, 0, 1810), vec3(430, 0, 1950), vec3(150, 0, 2090), vec3(390, 0, 2230), vec3(260, 0, 2380)};
        addRoad("Akagi Touge", tg, false, 9.5f, SURF_ASPHALT);
    }
    // farm lanes in the west
    {
        std::vector<vec3> farm;
        vec3 fc(-2050, 0, -350);
        for (int i = 0; i < 12; i++) {
            float a = i / 12.f * 2 * PI;
            farm.push_back(fc + vec3(std::cos(a) * 330.f * (1.f + rng.range(-0.15f, 0.15f)), 0, std::sin(a) * 520.f * (1.f + rng.range(-0.15f, 0.15f))));
        }
        addRoad("Inaka Rice Lanes", farm, true, 8.f, SURF_DIRT);
    }

    // ---- the north: Fuji and the Alps (added after the original roads so saved EventLab codes keep their road numbers)
    auto snapTo = [&](const char* name, vec3 near) {
        vec3 best = near;
        float bd = 1e18f;
        for (auto& rd : roads)
            if (rd.name == name)
                for (auto& q : rd.pts) {
                    float d = (q.x - near.x) * (q.x - near.x) + (q.z - near.z) * (q.z - near.z);
                    if (d < bd) { bd = d; best = vec3(q.x, 0, q.z); }
                }
        return best;
    };
    {
        // Fuji Skyline: a ring road round the volcano's lower flanks
        std::vector<vec3> sky;
        for (int i = 0; i < 22; i++) {
            float a = i / 22.f * 2 * PI;
            float wob = 1.f + 0.06f * std::sin(a * 3.f + 1.3f);
            sky.push_back(kFuji + vec3(std::cos(a) * 1100.f * wob, 0, std::sin(a) * 800.f * wob));
        }
        addRoad("Fuji Skyline", sky, true, 12.f, SURF_ASPHALT);
    }
    {
        // Fuji Road: up from the Wangan Highway past the lake to the Skyline
        vec3 a = snapTo("Wangan Highway", vec3(-1500, 0, 380));
        vec3 b = snapTo("Fuji Skyline", kFuji + vec3(150, 0, -800));
        // switchbacks on the steep upper part so the grade stays drivable
        addRoad("Fuji Road", {a, a + vec3(-10, 0, 90), vec3(-1560, 0, 1000), vec3(-1420, 0, 1500), vec3(-1200, 0, 1750), vec3(-1500, 0, 1900),
                              vec3(-1250, 0, 2050), b + vec3(60, 0, -140), b}, false, 12.f, SURF_ASPHALT);
    }
    {
        // Irohazaka: hairpins from the Skyline's east side across to the top of Akagi Touge
        vec3 a = snapTo("Fuji Skyline", kFuji + vec3(1100, 0, -100));
        vec3 b = snapTo("Akagi Touge", vec3(260, 0, 2380));
        addRoad("Irohazaka", {a, a + vec3(120, 0, -40), vec3(-500, 0, 2840), vec3(-260, 0, 2830), vec3(-330, 0, 2680), vec3(-90, 0, 2660),
                              vec3(-150, 0, 2520), vec3(90, 0, 2500), b + vec3(-60, 0, 10), b}, false, 10.f, SURF_ASPHALT);
    }
    {
        // Sea of Japan Road: up the west coast from the Wangan Highway to the Skyline
        vec3 a = snapTo("Wangan Highway", vec3(-2480, 0, -800));
        vec3 b = snapTo("Fuji Skyline", kFuji + vec3(-1100, 0, 0));
        addRoad("Sea of Japan Road", {a, a + vec3(-80, 0, 40), vec3(-3050, 0, -560), vec3(-3330, 0, 0), vec3(-3360, 0, 800), vec3(-3320, 0, 1600),
                                      vec3(-3200, 0, 2300), vec3(-2900, 0, 2780), b + vec3(-60, 0, -60), b}, false, 13.f, SURF_ASPHALT);
    }

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
        festCenter = hubPos + side * 130.f;
        festCenter.y = hubPos.y;
    }

    placeEstates();

    // ---- terrain heights
    for (int iz = 0; iz < N; iz++)
        for (int ix = 0; ix < N; ix++) {
            float x = -HALF + ix * CELL, z = -HALF + iz * CELL;
            float h = baseHeight(x, z);
            float dh = std::hypot(x - hubPos.x, z - hubPos.z);
            h = lerpf(h, hubPos.y - 0.03f, 1.f - smoothstepf(48.f, 85.f, dh));
            // the festival grounds: a broad, gently levelled field
            float df = std::hypot(x - festCenter.x, z - festCenter.z);
            h = lerpf(h, festCenter.y + (h - festCenter.y) * 0.08f, 1.f - smoothstepf(170.f, 290.f, df));
            for (const Estate& e : estates) {
                float de = std::hypot(x - e.pos.x, z - e.pos.z);
                if (de < 70.f) h = lerpf(h, e.pos.y - 0.04f, 1.f - smoothstepf(30.f, 62.f, de));
            }
            height[iz * N + ix] = h;
        }
    erode(seed);
    carveTerrain();
    for (auto& c : crossCountry) c.y = heightAt(c.x, c.z);
    hubSpawn.y = heightAt(hubSpawn.x, hubSpawn.z);
    buildUrban(seed);  // also builds the estates (shares the mesh and collision grid)
    buildJapan();
    scatter(seed);
    buildShrines();
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
// Broadleaf tree: a real branching skeleton (trunk, leader, limbs, side branches) with leaf
// sprays hung on the outer branches, so the crown is open and you see wood and sky through it.
static void buildLeafy(MeshBuilder& b, bool lod) {
    Rng r(lod ? 9 : 7);
    const vec4 bark(0, 0, 0.9f, 2);
    int sides = lod ? 5 : 8;
    // trunk with a slight lean, then a leader that tapers into the crown
    vec3 t0(0, -0.6f, 0), t1(0.18f, 4.2f, 0.08f), t2(0.05f, 8.6f, -0.1f), t3(0.12f, 11.2f, 0.05f);
    b.tube(t0, t1, 0.36f, 0.25f, sides + 2, bark);
    b.tube(t1, t2, 0.25f, 0.13f, sides, bark);
    if (!lod) b.tube(t2, t3, 0.13f, 0.04f, 5, bark);
    struct Tip { vec3 p, dir; float w; };
    std::vector<Tip> tips;
    vec3 crown(0.1f, 7.6f, 0.f);
    int limbs = lod ? 5 : 8;
    for (int i = 0; i < limbs; i++) {
        float f = (i + r.range(0.f, 0.6f)) / limbs;
        float hgt = lerpf(3.6f, 8.8f, f);
        vec3 base = hgt < 4.2f ? lerp(t0, t1, (hgt + 0.6f) / 4.8f) : lerp(t1, t2, (hgt - 4.2f) / 4.4f);
        float a = i * 2.39996f + r.range(-0.4f, 0.4f);           // golden-angle spiral
        float out = lerpf(4.2f, 2.4f, f) * r.range(0.85f, 1.15f); // lower limbs reach further
        float rise = lerpf(1.5f, 2.6f, f) * r.range(0.8f, 1.2f);
        vec3 d(std::cos(a), 0, std::sin(a));
        vec3 mid = base + d * (out * 0.5f) + vec3(0, rise * 0.62f, 0);
        vec3 end = base + d * out + vec3(0, rise, 0) + vec3(r.range(-0.4f, 0.4f), r.range(-0.3f, 0.3f), r.range(-0.4f, 0.4f));
        float rad0 = lerpf(0.15f, 0.08f, f);
        b.tube(base, mid, rad0, rad0 * 0.62f, sides - 1, bark);
        b.tube(mid, end, rad0 * 0.62f, 0.025f, sides - 2, bark);
        // side branches
        int nb = lod ? 2 : 4;
        for (int k = 0; k < nb; k++) {
            float s2 = r.range(0.35f, 0.95f);
            vec3 p = s2 < 0.5f ? lerp(base, mid, s2 * 2.f) : lerp(mid, end, (s2 - 0.5f) * 2.f);
            vec3 side = normalize(cross(d, vec3(0, 1, 0))) * (k % 2 ? 1.f : -1.f);
            vec3 bd = normalize(d * 0.6f + side * r.range(0.5f, 0.9f) + vec3(0, r.range(0.2f, 0.7f), 0));
            float bl = r.range(1.1f, 2.0f) * (1.2f - f * 0.4f);
            vec3 q = p + bd * bl;
            if (!lod) b.tube(p, q, rad0 * 0.32f, 0.012f, 4, bark);
            tips.push_back({q, bd, 1.f});
            tips.push_back({lerp(p, q, 0.55f), bd, 0.8f});
        }
        tips.push_back({end, normalize(d + vec3(0, 0.5f, 0)), 1.2f});
        tips.push_back({mid + vec3(0, 0.3f, 0), normalize(d + vec3(0, 0.8f, 0)), 0.9f});
    }
    tips.push_back({t3, vec3(0, 1, 0), 1.3f});
    tips.push_back({lerp(t2, t3, 0.5f), normalize(vec3(0.3f, 1, 0.2f)), 1.f});
    // leaf sprays: crossed cards around each tip, facing outwards, darker deep inside the crown
    for (const Tip& tp : tips) {
        int n = lod ? 2 : 5;
        for (int j = 0; j < n; j++) {
            vec3 off(r.range(-0.9f, 0.9f), r.range(-0.6f, 0.7f), r.range(-0.9f, 0.9f));
            vec3 p = tp.p + off * tp.w;
            float sz = (lod ? 2.9f : 1.75f) * r.range(0.8f, 1.2f) * tp.w;
            vec3 outv = normalize(p - crown + vec3(0, 0.6f, 0));
            vec3 nn = normalize(outv * 0.7f + tp.dir * 0.3f + vec3(r.range(-0.5f, 0.5f), r.range(-0.2f, 0.6f), r.range(-0.5f, 0.5f)));
            vec3 t1v = normalize(cross(nn, std::fabs(nn.y) < 0.9f ? vec3(0, 1, 0) : vec3(1, 0, 0)));
            vec3 t2v = cross(nn, t1v);
            float rot = r.range(0, 6.28f);
            vec3 u = t1v * std::cos(rot) + t2v * std::sin(rot), v = cross(nn, u);
            float depth = clampf(length(p - crown) / 5.f, 0.f, 1.f);
            float sh = 0.42f + 0.58f * depth + 0.1f * outv.y;
            float h = sz * 0.5f;
            vec3 ln = normalize(outv * 0.8f + vec3(0, 0.4f, 0));
            b.quad4(p - u * h - v * h, p + u * h - v * h, p + u * h + v * h, p - u * h + v * h, ln,
                    vec4(0, 0, sh, 1), vec4(1, 0, sh, 1), vec4(1, 1, sh, 1), vec4(0, 1, sh, 1));
        }
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
                    if (urban(wx, wz, 2.f)) col = vec4(0.50f, 0.49f, 0.47f, 2.f);  // paving (terrain shader)
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

    // ---- far terrain: every 8th vertex, sunk a little so the detailed chunks always win
    {
        const int S = 8, M = (N - 1) / S + 1;
        std::vector<Vertex> v;
        std::vector<uint32_t> idx;
        v.reserve(M * M);
        for (int z = 0; z < M; z++)
            for (int x = 0; x < M; x++) {
                int ix = x * S, iz = z * S;
                float wx = -HALF + ix * CELL, wz = -HALF + iz * CELL;
                vec4 col(0.30f, 0.25f, 0.15f, forestAt(wx, wz) * 0.45f);
                if (urban(wx, wz, 2.f)) col = vec4(0.50f, 0.49f, 0.47f, 2.f);
                v.push_back({vec3(wx, height[iz * N + ix] - 2.5f, wz), nrm[iz * N + ix], col});
            }
        for (int z = 0; z + 1 < M; z++)
            for (int x = 0; x + 1 < M; x++) {
                uint32_t a = z * M + x, b = a + 1, c = a + M, d = c + 1;
                idx.insert(idx.end(), {a, d, b, a, c, d});
            }
        farTerrain.upload(v, idx);
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
        // the sea: south and east coasts
        auto sea = [&](float x0, float z0, float x1, float z1, int gx, int gz) {
            uint32_t base = uint32_t(b.v.size());
            for (int z = 0; z <= gz; z++)
                for (int x = 0; x <= gx; x++)
                    b.add(vec3(lerpf(x0, x1, x / float(gx)), waterLevel, lerpf(z0, z1, z / float(gz))), vec3(0, 1, 0), vec4(1, 1, 1, 0));
            for (int z = 0; z < gz; z++)
                for (int x = 0; x < gx; x++) {
                    uint32_t a = base + z * (gx + 1) + x;
                    b.tri(a, a + gx + 1, a + 1);
                    b.tri(a + 1, a + gx + 1, a + gx + 2);
                }
        };
        sea(-HALF - 400, -HALF - 400, HALF + 400, -2280.f, 128, 24);
        sea(2760.f, -2280.f, HALF + 400, HALF + 400, 16, 128);
        sea(-HALF - 400, -2280.f, -3420.f, HALF + 400, 16, 128);
        b.build(waterMesh);
    }
    city.build(cityMesh);

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

// Torii gates over the mountain roads (red pillars, black kasagi with upturned ends) with a pair
// of stone lanterns: the way into the touge, the Fuji climb and the pass.
void World::buildShrines() {
    auto torii = [&](const char* road, float frac) {
        for (auto& r : roads) {
            if (r.name != road || r.pts.size() < 20) continue;
            int i = std::clamp(int(frac * (r.pts.size() - 1)), 2, int(r.pts.size()) - 3);
            vec3 p = r.pts[i], t = r.tan[i], side(-t.z, 0, t.x);
            float half = r.width * 0.5f + 1.4f, H = 8.5f;
            vec4 red(0.86f, 0.16f, 0.07f, 0), black(0.06f, 0.06f, 0.07f, 0), stone(0.55f, 0.54f, 0.5f, 0);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                vec3 b = p + side * (s2 * half);
                b.y = heightAt(b.x, b.z) - 0.3f;
                props.cylinder(b, 0.42f, H + 0.3f, 12, red);
                props.cylinder(b, 0.55f, 0.6f, 12, black);  // base ring
                int cx = std::clamp(int((b.x + HALF) / TGRID), 0, tgN - 1), cz = std::clamp(int((b.z + HALF) / TGRID), 0, tgN - 1);
                trunkGrid[cz * tgN + cx].push_back({b.x, b.z, 0.5f});
                // stone lantern in front of each pillar
                vec3 l = b + side * (s2 * 2.2f) - t * 3.f;
                l.y = heightAt(l.x, l.z);
                props.box(l + vec3(0, 0.5f, 0), vec3(0.35f, 0.5f, 0.35f), stone);
                props.box(l + vec3(0, 1.25f, 0), vec3(0.45f, 0.25f, 0.45f), vec4(1.f, 0.8f, 0.5f, 15));
                props.box(l + vec3(0, 1.6f, 0), vec3(0.6f, 0.12f, 0.6f), stone);
            }
            float y = std::max(heightAt((p - side * half).x, (p - side * half).z), heightAt((p + side * half).x, (p + side * half).z));
            // nuki (lower tie beam) and kasagi (top beam, black, ends swept up)
            mat4 xf = basis(side, vec3(0, 1, 0), t, vec3(p.x, y, p.z));
            props.box(vec3(0, H - 1.6f, 0), vec3(half + 0.9f, 0.28f, 0.3f), red, &xf);
            props.box(vec3(0, H + 0.05f, 0), vec3(half + 1.3f, 0.32f, 0.42f), red, &xf);
            props.box(vec3(0, H + 0.55f, 0), vec3(half + 1.8f, 0.22f, 0.55f), black, &xf);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                mat4 tip = xf * translate(vec3(s2 * (half + 1.9f), H + 0.75f, 0)) * rotateZ(s2 * -0.28f);
                props.box(vec3(0, 0, 0), vec3(0.6f, 0.16f, 0.55f), black, &tip);
            }
            props.box(vec3(0, H - 0.75f, 0), vec3(0.35f, 0.55f, 0.18f), black, &xf);  // gakuzuka (name plaque)
        }
    };
    torii("Akagi Touge", 0.04f);
    torii("Irohazaka", 0.05f);
    torii("Fuji Road", 0.93f);
    torii("Usui Pass Road", 0.08f);
    torii("Haruna Loop", 0.3f);
    torii("Chichibu Road", 0.5f);
}

// ---------------------------------------------------------------- Japan
// Little towns strung along the roads the way they are in Japan: two-storey shop houses and homes
// with grey-blue tiled hip roofs right up against the road, concrete utility poles with sagging
// wires, vending machines glowing by the doors; plus a five-storey pagoda looking across at Fuji.
void World::buildJapan() {
    Rng rng(4242);
    MeshBuilder& b = props;
    int houses = 0, poles = 0;
    const vec4 roofCols[] = {{0.20f, 0.23f, 0.28f, 0}, {0.17f, 0.18f, 0.20f, 0}, {0.30f, 0.20f, 0.16f, 0}, {0.24f, 0.30f, 0.34f, 0}};
    const vec4 wallCols[] = {{0.88f, 0.86f, 0.80f, 0}, {0.80f, 0.77f, 0.70f, 0}, {0.30f, 0.22f, 0.16f, 0}, {0.72f, 0.70f, 0.66f, 0}, {0.62f, 0.56f, 0.48f, 0}};
    const vec4 neon[] = {{1.f, 0.2f, 0.25f, 15}, {0.2f, 0.85f, 1.f, 15}, {1.f, 0.85f, 0.2f, 15}, {0.4f, 1.f, 0.5f, 15}};
    auto hipRoof = [&](vec3 c, vec3 f, vec3 r, float hw, float hd, float y, float H, vec4 col) {
        // eaves overhang the walls; ridge runs along the longer side
        float o = 0.75f;
        vec3 e0 = c + r * (-hw - o) + f * (-hd - o), e1 = c + r * (hw + o) + f * (-hd - o);
        vec3 e2 = c + r * (hw + o) + f * (hd + o), e3 = c + r * (-hw - o) + f * (hd + o);
        e0.y = e1.y = e2.y = e3.y = y;
        float rl = std::max(0.f, hw - hd);
        vec3 r0 = c + r * (-rl), r1 = c + r * rl;
        r0.y = r1.y = y + H;
        b.flatQuad(e1, e0, r0, r1, col);      // front slope (wound so the normals face out and up)
        b.flatQuad(e3, e2, r1, r0, col);      // back slope
        b.flatTri(e0, e3, r0, col);           // hip ends
        b.flatTri(e2, e1, r1, col);
        // dark ridge tiles
        b.tube(r0 + vec3(0, 0.08f, 0), r1 + vec3(0, 0.08f, 0), 0.18f, 0.18f, 5, vec4(0.1f, 0.1f, 0.11f, 0));
    };
    auto house = [&](vec3 c, vec3 f, float w, float d, int floors, bool shop) {
        vec3 r(f.z, 0, -f.x);
        float hw = w * 0.5f, hd = d * 0.5f;
        // stand it on the lowest corner so it never floats
        float g = 1e9f, gmax = -1e9f;
        for (int k = 0; k < 4; k++) {
            vec3 q = c + r * ((k & 1) ? hw : -hw) + f * ((k & 2) ? hd : -hd);
            float hh = heightAt(q.x, q.z);
            g = std::min(g, hh); gmax = std::max(gmax, hh);
        }
        if (gmax - g > 12.f) return false;
        // level a pad at road height (front edge), feathered into the slope around it
        {
            vec3 fe = c - f * hd;
            float py = heightAt(fe.x, fe.z);
            float rad = std::max(hw, hd) + 8.f;
            int x0 = std::max(0, int((c.x - rad + HALF) / CELL)), x1 = std::min(N - 1, int((c.x + rad + HALF) / CELL) + 1);
            int z0 = std::max(0, int((c.z - rad + HALF) / CELL)), z1 = std::min(N - 1, int((c.z + rad + HALF) / CELL) + 1);
            for (int iz = z0; iz <= z1; iz++)
                for (int ix = x0; ix <= x1; ix++) {
                    float wx = -HALF + ix * CELL, wz = -HALF + iz * CELL;
                    if (roadDist[iz * N + ix] < 1.5f) continue;  // never touch the road
                    vec3 d(wx - c.x, 0, wz - c.z);
                    float lx = std::fabs(dot(d, r)) - hw - 1.5f, lz = std::fabs(dot(d, f)) - hd - 1.5f;
                    float out = std::max(lx, lz);
                    float k = 1.f - smoothstepf(0.f, 6.f, out);
                    if (k > 0) height[iz * N + ix] = lerpf(height[iz * N + ix], py, k);
                }
            g = py;
        }
        g -= 0.25f;
        float H = floors * 3.1f + 0.4f;
        mat4 xf = basis(r, vec3(0, 1, 0), f, vec3(c.x, g, c.z));
        vec4 wall = wallCols[rng.irange(0, 4)];
        b.box(vec3(0, H * 0.5f, 0), vec3(hw, H * 0.5f, hd), wall, &xf);
        // timber base band and dark window strips on the road side
        b.box(vec3(0, 0.45f, 0), vec3(hw + 0.02f, 0.45f, hd + 0.02f), vec4(0.22f, 0.17f, 0.13f, 0), &xf);
        for (int fl = 0; fl < floors; fl++) {
            float wy = fl * 3.1f + 1.9f;
            b.box(vec3(0, wy, -hd - 0.02f), vec3(hw * 0.7f, 0.55f, 0.03f), vec4(0.10f, 0.12f, 0.15f, 2), &xf);
            b.box(vec3(0, wy, hd + 0.02f), vec3(hw * 0.6f, 0.5f, 0.03f), vec4(0.10f, 0.12f, 0.15f, 2), &xf);
        }
        if (floors == 2) {
            // the little skirt roof between the floors
            mat4 sx = xf * translate(vec3(0, 3.25f, -hd - 0.55f)) * rotateX(-0.35f);
            b.box(vec3(0, 0, 0), vec3(hw + 0.3f, 0.06f, 0.65f), roofCols[0], &sx);
        }
        vec4 rc = roofCols[rng.irange(0, 3)];
        vec3 cw(c.x, 0, c.z);
        hipRoof(cw, f, r, hw, hd, g + H, std::min(hw, hd) * 0.62f, rc);
        if (shop) {
            // lit shop front, a fabric awning, a vertical sign
            b.box(vec3(0, 1.35f, -hd - 0.04f), vec3(hw * 0.85f, 0.95f, 0.03f), vec4(1.f, 0.86f, 0.62f, 20), &xf);
            mat4 aw = xf * translate(vec3(0, 2.55f, -hd - 0.7f)) * rotateX(-0.28f);
            vec4 awc = rng.f() < 0.5f ? vec4(0.15f, 0.25f, 0.55f, 21) : vec4(0.7f, 0.12f, 0.1f, 21);
            b.box(vec3(0, 0, 0), vec3(hw * 0.9f, 0.04f, 0.75f), awc, &aw);
            float sx = rng.range(-hw + 0.6f, hw - 0.6f);
            b.box(vec3(sx, 4.2f + (floors - 1) * 1.5f, -hd - 0.5f), vec3(0.08f, 1.3f + floors * 0.4f, 0.45f), neon[rng.irange(0, 3)], &xf);
            if (rng.f() < 0.45f) {  // a pair of vending machines by the door
                for (int k = 0; k < 2; k++) {
                    vec3 vm(hw + 0.9f + k * 1.05f, 0.95f, -hd + 0.5f);
                    b.box(vm, vec3(0.48f, 0.95f, 0.38f), vec4(0.9f, 0.9f, 0.92f, 0), &xf);
                    b.box(vm + vec3(0, 0.25f, -0.39f), vec3(0.4f, 0.55f, 0.01f), vec4(1.f, 0.95f, 0.85f, 20), &xf);
                }
            }
        }
        // collision: axis-aligned box around the rotated footprint (slightly shrunk)
        float ex = std::fabs(r.x) * hw + std::fabs(f.x) * hd, ez = std::fabs(r.z) * hw + std::fabs(f.z) * hd;
        boxes.push_back({c.x - ex * 0.85f, c.z - ez * 0.85f, c.x + ex * 0.85f, c.z + ez * 0.85f, g + H});
        houses++;
        return true;
    };
    auto clearSpot = [&](vec3 c, float rad) {
        if (heightAt(c.x, c.z) < waterLevel + 1.5f) return false;
        if (urban(c.x, c.z, 20.f) || nearEstate(c.x, c.z, 50.f)) return false;
        if (std::hypot(c.x - hubPos.x, c.z - hubPos.z) < 110.f || std::hypot(c.x - festCenter.x, c.z - festCenter.z) < 300.f) return false;
        for (float a = 0; a < 6.28f; a += 0.785f)
            if (roadDistAt(c.x + std::cos(a) * rad, c.z + std::sin(a) * rad) < 1.5f) return false;
        return roadDistAt(c.x, c.z) > rad * 0.6f;
    };
    auto town = [&](const char* road, float f0, float f1, float density, bool farm) {
        for (auto& rd : roads) {
            if (rd.name != road || rd.pts.size() < 10) continue;
            int n = int(rd.pts.size());
            int i0 = int(f0 * (n - 1)), i1 = int(f1 * (n - 1));
            float along = 0, lastPole = -1e9f;
            vec3 prevPole[2];
            bool havePole = false;
            for (int i = i0; i < i1; i++) {
                along += length(rd.pts[i + 1] - rd.pts[i]);
                vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
                // utility poles on one side, wires sagging between them
                if (along - lastPole > 30.f) {
                    lastPole = along;
                    vec3 pp = p + side * (rd.width * 0.5f + 1.6f);
                    pp.y = heightAt(pp.x, pp.z);
                    if (roadDistAt(pp.x, pp.z) > 0.8f) {
                        b.cylinder(pp, 0.16f, 9.5f, 6, vec4(0.6f, 0.6f, 0.58f, 0));
                        mat4 ax = basis(t, vec3(0, 1, 0), side, pp);
                        b.box(vec3(0, 8.9f, 0), vec3(0.9f, 0.07f, 0.07f), vec4(0.3f, 0.3f, 0.3f, 7), &ax);
                        if (rng.f() < 0.3f) b.cylinder(pp + vec3(0, 6.8f, 0) + side * 0.35f, 0.32f, 0.9f, 8, vec4(0.55f, 0.56f, 0.55f, 7));  // transformer
                        vec3 tops[2] = {pp + vec3(0, 8.95f, 0) + t * 0.75f, pp + vec3(0, 8.95f, 0) - t * 0.75f};
                        if (havePole)
                            for (int k = 0; k < 2; k++) {
                                vec3 a = prevPole[k], c2 = tops[k];
                                vec3 prev = a;
                                for (int sgm = 1; sgm <= 4; sgm++) {
                                    float u = sgm / 4.f;
                                    vec3 q = lerp(a, c2, u) - vec3(0, 0.9f * 4.f * u * (1.f - u), 0);
                                    b.tube(prev, q, 0.025f, 0.025f, 3, vec4(0.05f, 0.05f, 0.05f, 0));
                                    prev = q;
                                }
                            }
                        prevPole[0] = tops[0]; prevPole[1] = tops[1];
                        havePole = true;
                        poles++;
                    }
                }
                // buildings, both sides, one every 11-16 m
                if (i % 3 != 0 || rng.f() > density) continue;
                for (int s = -1; s <= 1; s += 2) {
                    if (rng.f() > density) continue;
                    float w = farm ? rng.range(10.f, 14.f) : rng.range(7.5f, 11.f), d = farm ? rng.range(8.f, 10.f) : rng.range(7.f, 9.5f);
                    vec3 c = p + side * (s * (rd.width * 0.5f + 2.5f + d * 0.5f));
                    if (!clearSpot(c, std::max(w, d) * 0.6f)) continue;
                    if (inBox(c.x, c.z, 2.f)) continue;
                    vec3 f = side * (float)-s;  // front faces the road
                    house(c, f, w, d, farm ? 1 : (rng.f() < 0.65f ? 2 : 1), !farm && rng.f() < 0.45f);
                    rebuildBoxGrid();
                }
            }
        }
    };
    // towns go wherever a road runs through level ground for a while (not in cuttings or on
    // embankments), spaced out across the island; farm roads get scattered farmhouses
    {
        std::vector<vec3> placed;
        int towns = 0;
        for (auto& rd : roads) {
            bool skip = rd.name.find("-dori") != std::string::npos || rd.name.find("Street") != std::string::npos || rd.name == "Omotesando"
                     || rd.name.find("Daikoku") != std::string::npos || rd.name == "Akagi Touge" || rd.name == "Irohazaka" || rd.name == "Okutama Trail"
                     || rd.name == "Fuji Skyline";
            if (skip || rd.pts.size() < 40) continue;
            bool farm = rd.surface != SURF_ASPHALT;
            int n = int(rd.pts.size());
            std::vector<char> level(n, 0);
            for (int i = 0; i < n; i++) {
                vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
                bool ok = true;
                for (int sg = -1; sg <= 1 && ok; sg += 2)
                    for (float o : {10.f, 18.f}) {
                        vec3 q = p + side * (sg * (rd.width * 0.5f + o));
                        if (std::fabs(heightAt(q.x, q.z) - p.y) > 3.5f) ok = false;
                    }
                level[i] = ok;
            }
            for (int i = 0; i < n;) {
                if (!level[i]) { i++; continue; }
                int j = i;
                while (j < n && level[j]) j++;
                float len = 0;
                for (int k = i; k + 1 < j; k++) len += length(rd.pts[k + 1] - rd.pts[k]);
                vec3 mid = rd.pts[(i + j) / 2];
                bool far = true;
                for (auto& q : placed) far &= std::hypot(q.x - mid.x, q.z - mid.z) > 650.f;
                if (len > 140.f && far && std::hypot(mid.x - festCenter.x, mid.z - festCenter.z) > 400.f && !urban(mid.x, mid.z, 80.f)) {
                    int cut = std::min(j, i + int((j - i) * std::min(1.f, 420.f / len)));  // at most ~420 m of town
                    town(rd.name.c_str(), i / float(n - 1), cut / float(n - 1), farm ? 0.5f : 0.92f, farm);
                    placed.push_back(mid);
                    towns++;
                }
                i = j;
            }
        }
        SDL_Log("Japan: %d towns", towns);
    }

    // the five-storey pagoda on the hill above the lake, Fuji behind it
    {
        vec3 c(-1730.f, 0, 640.f);
        float g = heightAt(c.x, c.z) - 0.3f;
        vec4 red(0.82f, 0.16f, 0.08f, 0), roof(0.16f, 0.17f, 0.19f, 0), white(0.9f, 0.88f, 0.82f, 0), gold(0.85f, 0.65f, 0.2f, 7);
        b.box(vec3(c.x, g + 0.8f, c.z), vec3(8.f, 0.8f, 8.f), vec4(0.5f, 0.49f, 0.46f, 0));
        float y = g + 1.6f, s = 5.2f;
        for (int k = 0; k < 5; k++) {
            float hgt = k == 0 ? 4.f : 3.f;
            b.box(vec3(c.x, y + hgt * 0.5f, c.z), vec3(s, hgt * 0.5f, s), k % 2 ? white : red);
            for (int q = 0; q < 4; q++) {  // red corner posts
                float sx = (q & 1) ? s : -s, sz = (q & 2) ? s : -s;
                b.box(vec3(c.x + sx, y + hgt * 0.5f, c.z + sz), vec3(0.25f, hgt * 0.5f, 0.25f), red);
            }
            y += hgt;
            vec3 cc(c.x, 0, c.z);
            hipRoof(cc, vec3(0, 0, 1), vec3(1, 0, 0), s + 1.6f, s + 1.6f, y, 1.6f, roof);
            y += 0.9f;
            s *= 0.86f;
        }
        b.tube(vec3(c.x, y, c.z), vec3(c.x, y + 7.f, c.z), 0.3f, 0.12f, 6, gold);  // sorin spire
        for (int k = 0; k < 7; k++) b.cylinder(vec3(c.x, y + 1.f + k * 0.8f, c.z), 0.55f, 0.12f, 10, gold);
        boxes.push_back({c.x - 8, c.z - 8, c.x + 8, c.z + 8, y});
    }
    rebuildBoxGrid();
    SDL_Log("Japan: %d buildings, %d utility poles", houses, poles);
}
