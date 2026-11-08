#include "world.h"
#include "signs.h"
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
    return in(kCity) || in(kDocks) || estatePaved(x, z, pad) || stripPaved(x, z, pad);
}

bool World::stripPaved(float x, float z, float pad) const {
    for (auto& s : pavedStrips) {
        float w = s.w + pad;
        if (x < std::min(s.a.x, s.b.x) - w || x > std::max(s.a.x, s.b.x) + w || z < std::min(s.a.z, s.b.z) - w || z > std::max(s.a.z, s.b.z) + w) continue;
        vec2 ab(s.b.x - s.a.x, s.b.z - s.a.z), ap(x - s.a.x, z - s.a.z);
        float t = clampf(dot(ap, ab) / std::max(dot(ab, ab), 1e-6f), 0.f, 1.f);
        if (length(vec2(s.a.x + ab.x * t - x, s.a.z + ab.y * t - z)) < w) return true;
    }
    return false;
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
            if (paddyAt(x, z) > 0.5f) continue;                                     // rice paddies
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
            {
                bool clear = false;
                for (auto& c : clearings) clear |= std::hypot(x - c.x, z - c.z) < c.y;
                if (clear) continue;
            }
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
    // (props already holds the towns from buildJapan: keep adding to it)
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
                    if (urban(wx, wz, 2.f)) col = stripPaved(wx, wz, 0.f) ? vec4(0.6f, 0.43f, 0.32f, 2.f) : vec4(0.50f, 0.49f, 0.47f, 2.f);  // paving (terrain shader); red brick on shopping streets
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
                if (urban(wx, wz, 2.f)) col = stripPaved(wx, wz, 0.f) ? vec4(0.6f, 0.43f, 0.32f, 2.f) : vec4(0.50f, 0.49f, 0.47f, 2.f);
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
        for (size_t q = 0; q + 3 < waterQuads.size(); q += 4) {
            uint32_t a = b.add(waterQuads[q], vec3(0, 1, 0), vec4(1, 1, 1, 0));
            b.add(waterQuads[q + 1], vec3(0, 1, 0), vec4(1, 1, 1, 0));
            b.add(waterQuads[q + 2], vec3(0, 1, 0), vec4(1, 1, 1, 0));
            b.add(waterQuads[q + 3], vec3(0, 1, 0), vec4(1, 1, 1, 0));
            b.tri(a, a + 1, a + 2);
            b.tri(a, a + 2, a + 3);
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
        // the grass reads this: paved shopping streets count as road (no grass on the pavement)
        std::vector<float> rdT = roadDist;
        for (auto& st : pavedStrips) {
            float w = st.w + 1.f;
            int x0 = std::max(0, int((std::min(st.a.x, st.b.x) - w + HALF) / CELL)), x1 = std::min(N - 1, int((std::max(st.a.x, st.b.x) + w + HALF) / CELL) + 1);
            int z0 = std::max(0, int((std::min(st.a.z, st.b.z) - w + HALF) / CELL)), z1 = std::min(N - 1, int((std::max(st.a.z, st.b.z) + w + HALF) / CELL) + 1);
            for (int iz = z0; iz <= z1; iz++)
                for (int ix = x0; ix <= x1; ix++)
                    if (stripPaved(-HALF + ix * CELL, -HALF + iz * CELL, 1.f)) rdT[iz * N + ix] = 0.f;
        }
        for (size_t k = 0; k < canalPts.size(); k++) {
            vec3 c = canalPts[k];
            int x0 = std::max(0, int((c.x - 8 + HALF) / CELL)), x1 = std::min(N - 1, int((c.x + 8 + HALF) / CELL) + 1);
            int z0 = std::max(0, int((c.z - 8 + HALF) / CELL)), z1 = std::min(N - 1, int((c.z + 8 + HALF) / CELL) + 1);
            for (int iz = z0; iz <= z1; iz++)
                for (int ix = x0; ix <= x1; ix++)
                    if (std::hypot(-HALF + ix * CELL - c.x, -HALF + iz * CELL - c.z) < 6.f) rdT[iz * N + ix] = 0.f;
        }
        roadTex = mk(rdT);
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
    auto torii = [&](const char* road, float frac, bool lanterns = true) {
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
                if (!lanterns) continue;
                for (int k = 0; k < 3; k++) sakuraSpots.push_back(b + side * (s2 * (5.f + k * 3.f)) - t * (7.f + k * 6.f));
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
            props.box(vec3(0, H - 0.8f, 0), vec3(0.3f, 0.62f, 0.18f), black, &xf);  // gakuzuka (name plaque)
            for (int s2 = -1; s2 <= 1; s2 += 2)
                signFitted(props, xf.transformPoint(vec3(0, H - 0.8f, s2 * 0.19f)), t * (float)s2, 0.22f, 0.56f, lanterns ? SV_SENGEN : SV_INARI, true);
        }
    };
    // Fushimi Inari: a tunnel of hundreds of vermilion gates along the forest trail
    for (float f = 0.02f; f < 0.13f; f += 0.0042f) torii("Okutama Trail", f, false);
    torii("Akagi Touge", 0.04f);
    torii("Irohazaka", 0.05f);
    torii("Fuji Road", 0.93f);
    torii("Usui Pass Road", 0.08f);
    torii("Haruna Loop", 0.3f);
    torii("Chichibu Road", 0.5f);

    // poles, lamps and gates built before scatter() reset the trunk grid
    auto addTrunk = [&](float x, float z, float r) {
        int cx = std::clamp(int((x + HALF) / TGRID), 0, tgN - 1), cz = std::clamp(int((z + HALF) / TGRID), 0, tgN - 1);
        trunkGrid[cz * tgN + cx].push_back({x, z, r});
    };
    for (auto& t : pendingTrunks) addTrunk(t.x, t.z, t.r);
    pendingTrunks.clear();

    // ---- sakura: cherry trees in full bloom by the shrines, the castle, the pagoda, the towns,
    // ringing the lakes and the festival grounds
    Rng sr(808);
    int sakuraN = 0;
    auto sakura = [&](vec3 q, float sc) {
        float h = heightAt(q.x, q.z);
        if (h < waterLevel + 0.8f || roadDistAt(q.x, q.z) < 4.5f || inBox(q.x, q.z, 2.5f) || paddyAt(q.x, q.z) > 0.05f) return;
        if (normalAt(q.x, q.z).y < 0.85f) return;
        vec3 tint = sr.f() < 0.25f ? vec3(1.f, sr.range(0.84f, 0.9f), sr.range(0.84f, 0.9f))          // somei-yoshino, nearly white
                                   : vec3(1.f, sr.range(0.58f, 0.68f), sr.range(0.66f, 0.76f));       // deeper pink
        leafInst.push_back({vec3(q.x, h - 0.3f, q.z), sc, tint * sr.range(1.02f, 1.14f), sr.range(0, 6.28f)});
        addTrunk(q.x, q.z, 0.32f * sc);
        sakuraN++;
    };
    for (auto& q : sakuraSpots) sakura(q, sr.range(0.95f, 1.35f));
    for (auto& lk : lakes) {
        int n = std::max(12, int(6.2832f * lk.r / 15.f));
        for (int k = 0; k < n; k++) {
            float a = k / float(n) * 6.2832f + sr.range(-0.05f, 0.05f);
            vec3 d(std::cos(a), 0, std::sin(a));
            for (float rr = lk.r * 0.5f; rr < lk.r * 1.8f; rr += 3.f) {
                vec3 q = lk.c + d * rr;
                if (heightAt(q.x, q.z) > waterLevel + 1.2f) { sakura(q + d * sr.range(5.f, 11.f), sr.range(1.f, 1.4f)); break; }
            }
        }
    }
    for (int k = 0; k < 64; k++) {
        float a = k / 64.f * 6.2832f;
        sakura(festCenter + vec3(std::cos(a), 0, std::sin(a)) * sr.range(252.f, 262.f), sr.range(1.1f, 1.45f));
    }
    SDL_Log("Japan: %d sakura", sakuraN);
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
            {
                float sh = 1.3f + floors * 0.4f;
                vec3 sc = xf.transformPoint(vec3(sx, 4.2f + (floors - 1) * 1.5f, -hd - 0.5f));
                signBoardBox(b, sc, r, 0.45f, sh, 0.08f, SV_SHOP0 + rng.irange(0, SV_SHOP_N - 1), true, true);
                signBoardBox(b, xf.transformPoint(vec3(0, 2.95f, -hd - 0.06f)), -f, std::min(hw * 0.8f, 2.4f), 0.3f, 0.04f,
                             SH_SHOP0 + rng.irange(0, SH_SHOP_N - 1), false, false);
            }
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
                    vec3 f = side * (float)s;  // points away from the road: the shop front (local -z) faces it
                    if (rng.f() < 0.22f) sakuraSpots.push_back(c + f * (d * 0.5f + 4.f) + side * 0.f + t * rng.range(-4.f, 4.f));
                    house(c, f, w, d, farm ? 1 : (rng.f() < 0.65f ? 2 : 1), !farm && rng.f() < 0.45f);
                    rebuildBoxGrid();
                }
            }
        }
    };
    // ---------------------------------------------------------------- street furniture
    const vec3 up(0, 1, 0);
    auto tri = [&](vec3 p0, vec3 p1, vec3 p2, vec3 want, vec4 col) {
        if (dot(cross(p1 - p0, p2 - p0), want) < 0) std::swap(p1, p2);
        b.flatTri(p0, p1, p2, col);
    };
    auto quad = [&](vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec3 want, vec4 col) {
        tri(p0, p1, p2, want, col);
        tri(p0, p2, p3, want, col);
    };
    auto wire = [&](vec3 a, vec3 c2, float sag) {
        vec3 prev = a;
        for (int sgm = 1; sgm <= 6; sgm++) {
            float u = sgm / 6.f;
            vec3 q = lerp(a, c2, u) - vec3(0, sag * 4.f * u * (1.f - u), 0);
            b.tube(prev, q, 0.022f, 0.022f, 3, vec4(0.04f, 0.04f, 0.045f, 0));
            prev = q;
        }
    };
    // concrete pole: three crossarms (six wires), yellow/black guard stripes, a blue address plate
    auto bigPole = [&](vec3 pp, vec3 t, vec3 side, vec3* tops) {
        pp.y = heightAt(pp.x, pp.z) - 0.1f;
        b.cylinder(pp, 0.17f, 11.2f, 7, vec4(0.62f, 0.62f, 0.6f, 0));
        for (int k = 0; k < 4; k++)
            b.cylinder(pp + vec3(0, 0.3f + k * 0.45f, 0), 0.18f, 0.22f, 7, k % 2 ? vec4(0.08f, 0.08f, 0.08f, 0) : vec4(0.95f, 0.78f, 0.1f, 0));
        mat4 ax = basis(t, up, side, pp);
        b.box(vec3(0, 2.6f, -0.19f), vec3(0.14f, 0.32f, 0.02f), vec4(0.12f, 0.25f, 0.62f, 0), &ax);  // address plate
        for (int k = 0; k < 3; k++) {
            float y = 10.7f - k * 1.05f;
            b.box(vec3(0, y, 0), vec3(1.0f, 0.07f, 0.07f), vec4(0.3f, 0.3f, 0.3f, 7), &ax);
            for (int s2 = -1; s2 <= 1; s2 += 2) b.box(vec3(s2 * 0.85f, y + 0.12f, 0), vec3(0.05f, 0.08f, 0.05f), vec4(0.85f, 0.85f, 0.82f, 0), &ax);
            tops[k * 2] = pp + up * (y + 0.2f) + t * 0.85f;
            tops[k * 2 + 1] = pp + up * (y + 0.2f) - t * 0.85f;
        }
        if (rng.f() < 0.35f) {  // pole transformer
            b.cylinder(pp + vec3(0, 7.4f, 0) + side * 0.42f, 0.34f, 1.0f, 8, vec4(0.55f, 0.57f, 0.56f, 7));
            b.tube(pp + vec3(0, 8.4f, 0) + side * 0.42f, tops[4], 0.02f, 0.02f, 3, vec4(0.04f, 0.04f, 0.04f, 0));
        }
        pendingTrunks.push_back({pp.x, pp.z, 0.24f});
        poles++;
    };
    // bell-shaped lantern lamp on a post, arm reaching out over the kerb
    auto lanternLamp = [&](vec3 base, vec3 toRoad) {
        base.y = heightAt(base.x, base.z) - 0.05f;
        vec4 post(0.15f, 0.19f, 0.17f, 7);
        b.cylinder(base, 0.075f, 4.7f, 8, post);
        b.cylinder(base, 0.15f, 0.55f, 8, post);
        vec3 a0 = base + vec3(0, 4.55f, 0), a1 = a0 + toRoad * 0.45f + vec3(0, 0.3f, 0), a2 = a1 + toRoad * 0.55f;
        b.tube(a0, a1, 0.04f, 0.04f, 5, post);
        b.tube(a1, a2, 0.04f, 0.04f, 5, post);
        vec3 l = a2 - vec3(0, 0.1f, 0);
        b.tube(l, l - vec3(0, 0.2f, 0), 0.05f, 0.21f, 10, post);                                // cap
        b.tube(l - vec3(0, 0.2f, 0), l - vec3(0, 0.72f, 0), 0.21f, 0.33f, 12, vec4(1.f, 0.86f, 0.58f, 15));  // glowing bell
        b.cylinder(l - vec3(0, 0.79f, 0), 0.35f, 0.07f, 12, post);                              // rim
        pendingTrunks.push_back({base.x, base.z, 0.18f});
    };
    // round regulatory sign: red ring, white face, blue "40" (seven-segment strokes)
    auto speedSign = [&](vec3 base, vec3 face, int extra = 0) {
        base.y = heightAt(base.x, base.z) - 0.05f;
        b.cylinder(base, 0.045f, 2.75f, 6, vec4(0.75f, 0.75f, 0.76f, 7));
        vec3 c = base + vec3(0, 2.6f, 0), rt = normalize(cross(up, face));
        auto disc = [&](float rad, float off, vec4 col, vec3 n) {
            for (int k = 0; k < 20; k++) {
                float a0 = k / 20.f * 6.2832f, a1 = (k + 1) / 20.f * 6.2832f;
                vec3 o = c + face * off;
                tri(o, o + (rt * std::cos(a0) + up * std::sin(a0)) * rad, o + (rt * std::cos(a1) + up * std::sin(a1)) * rad, n, col);
            }
        };
        disc(0.32f, 0.0f, vec4(0.85f, 0.08f, 0.08f, 0), face);
        disc(0.25f, 0.008f, vec4(0.95f, 0.95f, 0.95f, 0), face);
        disc(0.32f, -0.01f, vec4(0.6f, 0.6f, 0.62f, 0), -face);
        auto seg = [&](float x, float y, float hx, float hy) {
            vec3 o = c + face * 0.016f + rt * x + up * y;
            quad(o - rt * hx - up * hy, o + rt * hx - up * hy, o + rt * hx + up * hy, o - rt * hx + up * hy, face, vec4(0.1f, 0.2f, 0.65f, 0));
        };
        const float W = 0.055f, Hh = 0.1f, T = 0.016f;
        // "4": upper-left, middle, both right
        float x4 = -0.075f;
        seg(x4 - W, Hh * 0.5f, T, Hh * 0.5f); seg(x4, 0, W, T); seg(x4 + W, Hh * 0.5f, T, Hh * 0.5f); seg(x4 + W, -Hh * 0.5f, T, Hh * 0.5f);
        // "0"
        float x0 = 0.075f;
        seg(x0, Hh, W, T); seg(x0, -Hh, W, T);
        for (int s2 = -1; s2 <= 1; s2 += 2) { seg(x0 + s2 * W, Hh * 0.5f, T, Hh * 0.5f); seg(x0 + s2 * W, -Hh * 0.5f, T, Hh * 0.5f); }
        // below it: no parking (blue, red ring, one slash) and no stopping (two slashes)
        for (int e = 0; e < extra; e++) {
            vec3 cc = c - up * (0.72f * (e + 1));
            vec3 keep = c;
            c = cc;
            disc(0.3f, 0.0f, vec4(0.85f, 0.08f, 0.08f, 0), face);
            disc(0.24f, 0.008f, vec4(0.1f, 0.25f, 0.7f, 0), face);
            disc(0.3f, -0.01f, vec4(0.6f, 0.6f, 0.62f, 0), -face);
            for (int k = 0; k <= e; k++) {
                vec3 dgn = normalize(rt * (k ? -1.f : 1.f) + up);
                vec3 o = cc + face * 0.016f, pr = normalize(cross(face, dgn));
                quad(o - dgn * 0.24f - pr * 0.03f, o + dgn * 0.24f - pr * 0.03f, o + dgn * 0.24f + pr * 0.03f, o - dgn * 0.24f + pr * 0.03f, face, vec4(0.85f, 0.08f, 0.08f, 0));
            }
            c = keep;
        }
        pendingTrunks.push_back({base.x, base.z, 0.1f});
    };

    // ---------------------------------------------------------------- Honcho-dori
    // The Fujiyoshida shopping street: dead straight, climbing gently with Fuji filling the end of
    // it, two- and three-storey shop houses shoulder to shoulder, vertical signboards, bell lantern
    // lamps on both kerbs, a thicket of poles and wires, and the great bronze torii at its foot.
    for (auto& rd : roads) {
        if (rd.name != "Fuji Road") continue;
        int n = int(rd.pts.size()), i0 = -1, i1 = -1;
        for (int i = 0; i < n; i++) {
            if (rd.pts[i].z > 470.f && i0 < 0) i0 = i;
            if (rd.pts[i].z < 960.f) i1 = i;
            else if (i0 >= 0) break;
        }
        if (i0 < 0 || i1 - i0 < 20) break;
        float hwR = rd.width * 0.5f;
        const float walk = 3.2f;  // pavement between kerb and shop fronts
        // level a band either side to the road, so the houses sit flush along the pavement
        {
            float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
            for (int i = i0; i <= i1; i++) { x0 = std::min(x0, rd.pts[i].x); x1 = std::max(x1, rd.pts[i].x); z0 = std::min(z0, rd.pts[i].z); z1 = std::max(z1, rd.pts[i].z); }
            const float full = hwR + walk + 15.f, fade = 14.f;
            int ix0 = std::max(0, int((x0 - full - fade + HALF) / CELL)), ix1 = std::min(N - 1, int((x1 + full + fade + HALF) / CELL) + 1);
            int iz0 = std::max(0, int((z0 - 20 + HALF) / CELL)), iz1 = std::min(N - 1, int((z1 + 20 + HALF) / CELL) + 1);
            for (int iz = iz0; iz <= iz1; iz++)
                for (int ix = ix0; ix <= ix1; ix++) {
                    float wx = -HALF + ix * CELL, wz = -HALF + iz * CELL;
                    if (roadDist[iz * N + ix] < 0.5f) continue;
                    float best = 1e9f, by = 0, along = 0;
                    for (int i = i0; i < i1; i++) {
                        vec3 a = rd.pts[i], c2 = rd.pts[i + 1];
                        vec2 ab(c2.x - a.x, c2.z - a.z), ap(wx - a.x, wz - a.z);
                        float u = clampf(dot(ap, ab) / std::max(dot(ab, ab), 1e-6f), 0.f, 1.f);
                        float dd = length(vec2(a.x + ab.x * u - wx, a.z + ab.y * u - wz));
                        if (dd < best) { best = dd; by = lerpf(a.y, c2.y, u); along = (i - i0 + u) / float(i1 - i0); }
                    }
                    float k = 1.f - smoothstepf(full, full + fade, best);
                    k *= smoothstepf(0.f, 0.04f, along) * smoothstepf(1.f, 0.96f, along);
                    if (k > 0) height[iz * N + ix] = lerpf(height[iz * N + ix], by - 0.06f, k);
                }
        }
        for (int i = i0; i < i1; i += 6) pavedStrips.push_back({rd.pts[i], rd.pts[std::min(i + 6, i1)], hwR + walk + 0.5f});
        // shop houses, both sides, shoulder to shoulder with the odd side lane
        const vec4 walls[] = {{0.84f, 0.82f, 0.76f, 0}, {0.70f, 0.68f, 0.64f, 0}, {0.56f, 0.51f, 0.46f, 0}, {0.88f, 0.87f, 0.85f, 0},
                              {0.46f, 0.41f, 0.37f, 0}, {0.72f, 0.64f, 0.52f, 0}, {0.62f, 0.66f, 0.66f, 0}};
        const vec4 tins[] = {{0.55f, 0.20f, 0.14f, 0}, {0.22f, 0.36f, 0.38f, 0}, {0.30f, 0.30f, 0.32f, 0}, {0.45f, 0.30f, 0.22f, 0}};
        int shops = 0;
        for (int s = -1; s <= 1; s += 2) {
            float along = 0, nextAt = 0;
            int lane = 0;
            for (int i = i0; i < i1; i++) {
                float segL = length(rd.pts[i + 1] - rd.pts[i]);
                along += segL;
                if (along < nextAt) continue;
                vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
                float w = rng.range(5.5f, 9.f), d = rng.range(10.f, 13.f);
                if (++lane % 9 == 0) { nextAt = along + 7.f; continue; }  // a side lane between the shops
                vec3 c = p + t * (w * 0.5f) + side * (s * (hwR + walk + d * 0.5f));
                vec3 f = side * (float)s, r(f.z, 0, -f.x);
                nextAt = along + w + 0.15f;
                int floors = rng.f() < 0.4f ? 3 : 2;
                float hw = w * 0.5f, hd = d * 0.5f, fh = 3.2f, H = floors * fh + 0.35f;
                // the street climbs: set the floor at the uphill corner so no shop front is buried,
                // and run the walls down past the lowest corner (a plinth on the downhill side)
                float gMax = -1e9f, gMin = 1e9f;
                for (int k = 0; k < 4; k++) {
                    vec3 q = c + r * ((k & 1) ? hw : -hw) + f * ((k & 2) ? hd : -hd);
                    float hh = heightAt(q.x, q.z);
                    gMax = std::max(gMax, hh); gMin = std::min(gMin, hh);
                }
                float g = gMax + 0.32f;
                float drop = (g - 0.3f) - gMin + 0.6f;
                mat4 xf = basis(r, up, f, vec3(c.x, g - 0.3f, c.z));
                vec4 wall = walls[rng.irange(0, 6)];
                b.box(vec3(0, (H - drop) * 0.5f, 0), vec3(hw, (H + drop) * 0.5f, hd), wall, &xf);
                b.box(vec3(0, -drop * 0.5f + 0.02f, -hd - 0.05f), vec3(hw, drop * 0.5f, 0.06f), vec4(0.5f, 0.49f, 0.47f, 0), &xf);  // concrete plinth
                // ground floor: lit shop, shuttered, or dark glass behind a noren curtain
                int kind = rng.irange(0, 3);
                if (kind <= 1) b.box(vec3(0, 1.45f, -hd - 0.03f), vec3(hw * 0.86f, 1.05f, 0.03f), vec4(1.f, 0.87f, 0.64f, 20), &xf);
                else if (kind == 2) {
                    b.box(vec3(0, 1.45f, -hd - 0.03f), vec3(hw * 0.86f, 1.05f, 0.03f), vec4(0.62f, 0.63f, 0.64f, 7), &xf);
                    for (int k = 0; k < 9; k++) b.box(vec3(0, 0.5f + k * 0.22f, -hd - 0.07f), vec3(hw * 0.86f, 0.02f, 0.02f), vec4(0.42f, 0.43f, 0.44f, 7), &xf);
                } else {
                    b.box(vec3(0, 1.45f, -hd - 0.03f), vec3(hw * 0.86f, 1.05f, 0.03f), vec4(0.1f, 0.12f, 0.14f, 2), &xf);
                    vec4 nc = rng.f() < 0.5f ? vec4(0.12f, 0.16f, 0.35f, 21) : vec4(0.55f, 0.1f, 0.1f, 21);
                    for (int k = -1; k <= 1; k++) b.box(vec3(k * 0.42f, 2.05f, -hd - 0.12f), vec3(0.19f, 0.42f, 0.015f), nc, &xf);  // noren
                }
                // fascia signboard across the top of the shop front
                {
                    vec3 fc = xf.transformPoint(vec3(0, fh - 0.35f, -hd - 0.1f));
                    signBoardBox(b, fc, -f, hw * 0.95f, 0.42f, 0.07f, SH_SHOP0 + rng.irange(0, SH_SHOP_N - 1), false, false);
                }
                // upper floors: window bands with frames, some with a balcony rail
                for (int fl = 1; fl < floors; fl++) {
                    float wy = fl * fh + 1.55f;
                    int nw = std::max(1, int(w / 2.6f));
                    for (int k = 0; k < nw; k++) {
                        float wx = (k + 0.5f) / nw * w - hw;
                        b.box(vec3(wx, wy, -hd - 0.03f), vec3(w / nw * 0.36f, 0.62f, 0.03f), vec4(0.10f, 0.12f, 0.15f, 2), &xf);
                        b.box(vec3(wx, wy - 0.68f, -hd - 0.08f), vec3(w / nw * 0.4f, 0.05f, 0.08f), vec4(0.75f, 0.74f, 0.72f, 0), &xf);
                    }
                    if (rng.f() < 0.35f) {
                        b.box(vec3(0, fl * fh + 0.1f, -hd - 0.55f), vec3(hw * 0.9f, 0.08f, 0.55f), vec4(wall.x * 0.85f, wall.y * 0.85f, wall.z * 0.85f, 0), &xf);
                        b.box(vec3(0, fl * fh + 0.62f, -hd - 1.08f), vec3(hw * 0.9f, 0.45f, 0.025f), vec4(0.55f, 0.56f, 0.58f, 7), &xf);
                    }
                    if (rng.f() < 0.3f) b.box(vec3(hw - 0.6f, fl * fh + 0.45f, -hd - 0.3f), vec3(0.38f, 0.3f, 0.26f), vec4(0.85f, 0.85f, 0.83f, 0), &xf);  // AC unit
                }
                // roof: flat with a parapet, or an old tin gable facing the street
                if (rng.f() < 0.62f) {
                    b.box(vec3(0, H + 0.35f, -hd + 0.1f), vec3(hw, 0.35f, 0.1f), vec4(wall.x * 0.92f, wall.y * 0.92f, wall.z * 0.92f, 0), &xf);
                    b.box(vec3(-hw + 0.1f, H + 0.35f, 0), vec3(0.1f, 0.35f, hd), vec4(wall.x * 0.92f, wall.y * 0.92f, wall.z * 0.92f, 0), &xf);
                    b.box(vec3(hw - 0.1f, H + 0.35f, 0), vec3(0.1f, 0.35f, hd), vec4(wall.x * 0.92f, wall.y * 0.92f, wall.z * 0.92f, 0), &xf);
                    if (rng.f() < 0.45f) {  // water tank on legs
                        b.box(vec3(rng.range(-hw + 1.2f, hw - 1.2f), H + 1.9f, hd * 0.3f), vec3(0.8f, 0.6f, 0.8f), vec4(0.8f, 0.8f, 0.78f, 0), &xf);
                    }
                } else {
                    vec4 tin = tins[rng.irange(0, 3)];
                    float o = 0.35f, rh = hw * 0.55f;
                    vec3 base = vec3(c.x, g - 0.3f + H, c.z);
                    vec3 FL = base - r * (hw + o) - f * (hd + o), FR = base + r * (hw + o) - f * (hd + o);
                    vec3 BL = base - r * (hw + o) + f * (hd + o), BR = base + r * (hw + o) + f * (hd + o);
                    vec3 RF = base - f * (hd + o) + up * rh, RB = base + f * (hd + o) + up * rh;
                    quad(FL, BL, RB, RF, -r + up, tin);
                    quad(FR, RF, RB, BR, r + up, tin);
                    tri(FL + f * o, FR + f * o, RF + f * o, -f, wall);
                    tri(BL - f * o, BR - f * o, RB - f * o, f, wall);
                }
                // kanban-style false front rising past the roof: copper-green sheet or tiles, a cornice on top
                if (rng.f() < 0.3f) {
                    vec4 fc = rng.f() < 0.5f ? vec4(0.34f, 0.5f, 0.45f, 7) : vec4(wall.x * 0.9f, wall.y * 0.9f, wall.z * 0.9f, 0);
                    b.box(vec3(0, H + 0.85f, -hd + 0.15f), vec3(hw, 0.9f, 0.15f), fc, &xf);
                    b.box(vec3(0, H + 1.8f, -hd + 0.1f), vec3(hw + 0.1f, 0.09f, 0.32f), vec4(0.25f, 0.25f, 0.26f, 0), &xf);
                    for (int k = -1; k <= 1; k += 2) b.box(vec3(k * (hw - 0.15f), H * 0.5f + 0.5f, -hd - 0.05f), vec3(0.15f, H * 0.5f + 0.5f, 0.06f), fc, &xf);  // pilasters
                }
                // vertical projecting signboard
                if (rng.f() < 0.7f) {
                    float sx = (rng.f() < 0.5f ? -1.f : 1.f) * (hw - 0.35f);
                    float top = H - 0.2f, bot = fh + 0.15f;
                    int cell = SV_SHOP0 + rng.irange(0, SV_SHOP_N - 1);
                    b.box(vec3(sx, (top + bot) * 0.5f, -hd - 0.62f), vec3(0.07f, (top - bot) * 0.5f + 0.06f, 0.56f), vec4(0.12f, 0.12f, 0.13f, 7), &xf);
                    vec3 sc = xf.transformPoint(vec3(sx, (top + bot) * 0.5f, -hd - 0.62f));
                    signBoardBox(b, sc, r, 0.5f, (top - bot) * 0.5f, 0.09f, cell, true, true);
                    b.box(vec3(sx, top - 0.3f, -hd - 0.06f), vec3(0.05f, 0.05f, 0.1f), vec4(0.3f, 0.3f, 0.3f, 7), &xf);
                }
                // nobori: a cloth banner on a pole by the door
                if (rng.f() < 0.3f) {
                    vec3 q = xf.transformPoint(vec3((rng.f() < 0.5f ? -1.f : 1.f) * (hw - 0.4f), 0, -hd - 0.9f));
                    b.cylinder(q, 0.025f, 3.0f, 5, vec4(0.85f, 0.85f, 0.85f, 7));
                    b.tube(q + up * 2.85f, q + up * 2.85f + t * 0.62f, 0.015f, 0.015f, 3, vec4(0.85f, 0.85f, 0.85f, 7));
                    int cell = SV_SHOP0 + rng.irange(0, SV_SHOP_N - 1);
                    vec3 cc = q + up * 1.75f + t * 0.31f;
                    signFace(b, cc + f * 0.01f, -f, 0.29f, 1.1f, cell, true);
                    signFace(b, cc - f * 0.01f, f, 0.29f, 1.1f, cell, true);
                }
                // pavement clutter: vending machines, potted plants, a parked bicycle
                if (rng.f() < 0.3f) {
                    vec4 vmCol[] = {{0.9f, 0.9f, 0.92f, 0}, {0.75f, 0.1f, 0.1f, 0}, {0.1f, 0.3f, 0.7f, 0}};
                    float x0 = (rng.f() < 0.5f ? -1.f : 1.f) * (hw - 0.6f);
                    int cnt = rng.irange(1, 3);
                    for (int k = 0; k < cnt; k++) {
                        float x = x0 - (x0 > 0 ? 1.f : -1.f) * k * 1.0f;
                        vec4 vc = vmCol[rng.irange(0, 2)];
                        b.box(vec3(x, 0.95f, -hd - 0.42f), vec3(0.48f, 0.95f, 0.4f), vc, &xf);
                        b.box(vec3(x, 1.2f, -hd - 0.83f), vec3(0.4f, 0.5f, 0.01f), vec4(1.f, 0.96f, 0.86f, 20), &xf);
                        b.box(vec3(x, 0.45f, -hd - 0.83f), vec3(0.3f, 0.08f, 0.01f), vec4(0.1f, 0.1f, 0.1f, 0), &xf);
                    }
                }
                if (rng.f() < 0.3f)
                    for (int k = 0; k < 3; k++) {
                        vec3 q = xf.transformPoint(vec3(-hw * 0.5f + k * 0.55f, 0, -hd - 0.3f));
                        b.cylinder(q, 0.2f, 0.42f, 8, vec4(0.55f, 0.3f, 0.2f, 0));
                        b.sphere(q + up * 0.7f, 0.33f, 1, vec4(0.2f, 0.42f, 0.16f, 0), 0.1f, uint32_t(i * 7 + k));
                    }
                if (rng.f() < 0.15f) {
                    vec4 frame(rng.f() < 0.5f ? 0.7f : 0.1f, 0.15f, rng.f() < 0.5f ? 0.15f : 0.6f, 0), tyre(0.05f, 0.05f, 0.05f, 0);
                    float bx = rng.range(-hw + 1.f, hw - 1.f);
                    vec3 w0 = xf.transformPoint(vec3(bx - 0.52f, 0.34f, -hd - 0.5f)), w1 = xf.transformPoint(vec3(bx + 0.52f, 0.34f, -hd - 0.5f));
                    for (vec3 wc : {w0, w1})
                        for (int k = 0; k < 10; k++) {
                            float a0 = k / 10.f * 6.2832f, a1 = (k + 1) / 10.f * 6.2832f;
                            b.tube(wc + (r * std::cos(a0) + up * std::sin(a0)) * 0.33f, wc + (r * std::cos(a1) + up * std::sin(a1)) * 0.33f, 0.025f, 0.025f, 3, tyre);
                        }
                    vec3 seat = xf.transformPoint(vec3(bx - 0.2f, 0.9f, -hd - 0.5f)), bar = xf.transformPoint(vec3(bx + 0.42f, 1.0f, -hd - 0.5f));
                    b.tube(w0, seat, 0.025f, 0.025f, 3, frame);
                    b.tube(seat, w1 * 0.5f + bar * 0.5f, 0.025f, 0.025f, 3, frame);
                    b.tube(w1, bar, 0.025f, 0.025f, 3, frame);
                    b.tube(w0, xf.transformPoint(vec3(bx, 0.34f, -hd - 0.5f)), 0.025f, 0.025f, 3, frame);
                    b.tube(xf.transformPoint(vec3(bx, 0.34f, -hd - 0.5f)), seat, 0.025f, 0.025f, 3, frame);
                    b.box(seat + up * 0.04f, vec3(0.12f, 0.03f, 0.08f), tyre);
                    b.tube(bar - r * 0.0f + xf.transformPoint(vec3(0, 0, 0.25f)) - xf.transformPoint(vec3(0, 0, 0)), bar - xf.transformPoint(vec3(0, 0, 0.25f)) + xf.transformPoint(vec3(0, 0, 0)), 0.02f, 0.02f, 3, tyre);
                }
                if (shops == 40) {  // a red post box on the pavement
                    vec3 q = xf.transformPoint(vec3(0, 0, -hd - 1.6f));
                    b.cylinder(q, 0.28f, 1.15f, 12, vec4(0.85f, 0.1f, 0.08f, 0));
                    b.cylinder(q + up * 1.15f, 0.32f, 0.1f, 12, vec4(0.85f, 0.1f, 0.08f, 0));
                    b.box(q + up * 0.85f - f * 0.27f, vec3(0.12f, 0.03f, 0.01f), vec4(0.1f, 0.1f, 0.1f, 0));
                    pendingTrunks.push_back({q.x, q.z, 0.3f});
                }
                float ex = std::fabs(r.x) * hw + std::fabs(f.x) * hd, ez = std::fabs(r.z) * hw + std::fabs(f.z) * hd;
                boxes.push_back({c.x - ex * 0.95f, c.z - ez * 0.95f, c.x + ex * 0.95f, c.z + ez * 0.95f, g + H});
                houses++; shops++;
            }
        }
        // kerbside, as on the real street: lamp posts every 12 m both sides, each with a pair of
        // lanterns, a 本町名店街 plaque under a little roof and (every other one) a sky-blue banner;
        // concrete poles every 30 m with ads strapped on, six power lines, phone cables and drops
        // into the shops, wires across the street; signals at the crossing; sign stacks
        {
            auto honchoLamp = [&](vec3 base, vec3 t, vec3 toRoad, bool banner, int idx) {
                base.y = heightAt(base.x, base.z) - 0.05f;
                vec4 iron(0.12f, 0.11f, 0.10f, 7), glowC(1.f, 0.82f, 0.52f, 15), roofC(0.2f, 0.21f, 0.23f, 0);
                b.cylinder(base, 0.085f, 5.75f, 8, iron);
                b.cylinder(base, 0.16f, 0.7f, 8, iron);
                vec3 top = base + up * 5.55f;
                b.tube(top - t * 0.95f, top + t * 0.95f, 0.035f, 0.035f, 4, iron);
                for (int k = -1; k <= 1; k += 2) {  // hexagonal lanterns hanging off the bar ends
                    vec3 l = top + t * (k * 0.92f);
                    b.tube(l, l - up * 0.12f, 0.02f, 0.02f, 3, iron);
                    vec3 cap = l - up * 0.12f;
                    b.tube(cap, cap - up * 0.16f, 0.05f, 0.3f, 6, iron);
                    b.tube(cap - up * 0.16f, cap - up * 0.72f, 0.25f, 0.16f, 6, glowC);
                    b.cylinder(cap - up * 0.78f, 0.17f, 0.06f, 6, iron);
                    b.tube(cap, cap + up * 0.14f, 0.03f, 0.01f, 4, iron);  // finial
                }
                // plaque on an arm towards the road, under a tiny gable roof
                vec3 arm0 = base + up * 4.75f, arm1 = arm0 + toRoad * 1.05f;
                b.tube(arm0, arm1, 0.03f, 0.03f, 4, iron);
                vec3 pc = arm1 - toRoad * 0.38f - up * 0.48f;
                mat4 px = basis(toRoad, up, -t, pc);
                b.box(vec3(0, 0, 0), vec3(0.64f, 0.33f, 0.03f), signBoard(SG_HONCYO, false), &px);
                signFace(b, pc - t * 0.034f, -t, 0.62f, 0.31f, SG_HONCYO, false);
                signFace(b, pc + t * 0.034f, t, 0.62f, 0.31f, SG_HONCYO2, false);
                vec3 rL = pc - toRoad * 0.76f + up * 0.4f, rR = pc + toRoad * 0.76f + up * 0.4f, rT = pc + up * 0.62f;
                quad(rL - t * 0.12f, rL + t * 0.12f, rT + t * 0.12f, rT - t * 0.12f, -toRoad + up, roofC);
                quad(rR - t * 0.12f, rT - t * 0.12f, rT + t * 0.12f, rR + t * 0.12f, toRoad + up, roofC);
                b.tube(arm1 - toRoad * 0.38f, rT, 0.012f, 0.012f, 3, iron);
                if (banner) {  // sky-blue street banner on a bracket, readable up and down the street
                    vec3 bc = base + toRoad * 0.36f + up * 2.95f;
                    b.tube(base + up * 3.78f, base + up * 3.78f + toRoad * 0.6f, 0.018f, 0.018f, 3, iron);
                    int cell = SW_BANNER0 + idx % SW_BANNER_N;
                    signFace(b, bc - t * 0.006f, -t, 0.2f, 0.8f, cell, true);
                    signFace(b, bc + t * 0.006f, t, 0.2f, 0.8f, cell, true);
                }
                pendingTrunks.push_back({base.x, base.z, 0.18f});
            };
            float along = 0, nextLamp = 5, nextPole = 2, nextStack = 30;
            vec3 prevTops[2][6], prevLow[2][2];
            bool have = false;
            int lampN = 0, stackN = 0;
            for (int i = i0; i < i1; i++) {
                along += length(rd.pts[i + 1] - rd.pts[i]);
                vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
                if (along >= nextLamp) {
                    nextLamp += 12.f;
                    for (int s = -1; s <= 1; s += 2) honchoLamp(p + side * (s * (hwR + 0.65f)), t, side * (float)-s, lampN % 2 == 0, lampN + (s > 0 ? 3 : 0));
                    lampN++;
                }
                if (along >= nextStack && along < (i1 - i0) * 4.f - 30.f) {
                    nextStack += 85.f;
                    int s = stackN % 2 ? 1 : -1;
                    vec3 q = p + t * 6.f + side * (s * (hwR + 0.4f));
                    speedSign(q, s > 0 ? t : -t, 1 + stackN % 2);
                    stackN++;
                }
                if (along >= nextPole) {
                    nextPole += 30.f;
                    vec3 tops[2][6], low[2][2];
                    for (int s = 0; s < 2; s++) {
                        float sg = s ? 1.f : -1.f;
                        vec3 pp = p + t * 3.f + side * (sg * (hwR + walk - 0.5f));
                        bigPole(pp, t, side, tops[s]);
                        pp.y = heightAt(pp.x, pp.z);
                        // ads strapped to the pole, facing up and down the street
                        int cell = SW_AD0 + rng.irange(0, SW_AD_N - 1);
                        vec3 ac = pp + up * 2.75f - side * (sg * 0.19f);
                        b.box(ac, vec3(0.03f, 0.7f, 0.03f), vec4(0.3f, 0.3f, 0.3f, 7));
                        signFace(b, ac - t * 0.02f, -t, 0.17f, 0.68f, cell, true);
                        signFace(b, ac + t * 0.02f, t, 0.17f, 0.68f, cell, true);
                        // phone cables lower down, and service drops into the shops either side
                        low[s][0] = pp + up * 6.3f; low[s][1] = pp + up * 5.8f;
                        for (int k = -1; k <= 1; k += 2) {
                            vec3 fac = pp + side * (sg * 0.55f) + t * (k * rng.range(4.f, 8.f)) + up * rng.range(5.6f, 6.6f);
                            wire(tops[s][4], fac, 0.35f);
                        }
                        b.box(pp + up * 6.0f - side * (sg * 0.2f), vec3(0.12f, 0.35f, 0.12f), vec4(0.45f, 0.46f, 0.45f, 7));  // terminal box
                    }
                    if (have)
                        for (int s = 0; s < 2; s++) {
                            for (int k = 0; k < 6; k++) wire(prevTops[s][k], tops[s][k], 0.55f + 0.08f * k);
                            for (int k = 0; k < 2; k++) {
                                vec3 a = prevLow[s][k], c2 = low[s][k];
                                vec3 prev = a;
                                for (int sgm = 1; sgm <= 6; sgm++) {
                                    float u = sgm / 6.f;
                                    vec3 q = lerp(a, c2, u) - vec3(0, 0.8f * 4.f * u * (1.f - u), 0);
                                    b.tube(prev, q, 0.04f, 0.04f, 3, vec4(0.03f, 0.03f, 0.035f, 0));
                                    prev = q;
                                }
                            }
                        }
                    if (have) {  // across the street, two or three each time
                        wire(tops[0][rng.irange(0, 1)], tops[1][rng.irange(2, 3)], 0.9f);
                        if (rng.f() < 0.7f) wire(tops[1][rng.irange(0, 1)], tops[0][rng.irange(4, 5)], 1.1f);
                        if (rng.f() < 0.5f) wire(low[0][0], low[1][1], 0.7f);
                    }
                    std::memcpy(prevTops, tops, sizeof(tops));
                    std::memcpy(prevLow, low, sizeof(low));
                    have = true;
                }
            }
            for (int k = 0; k < 2; k++) {
                int i = k ? i1 - 8 : i0 + 8;
                vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
                float sg = k ? 1.f : -1.f;
                speedSign(p + side * (sg * (hwR + 0.45f)), k ? t : -t, 1);
            }
            // zebra crossings with traffic signals, halfway up and near the top
            for (int ci : {(i0 + i1) / 2, i1 - 30}) {
                vec3 p = rd.pts[ci], t = rd.tan[ci], side(-t.z, 0, t.x);
                for (int k = -4; k <= 4; k++) {
                    mat4 zx = basis(side, up, t, p + side * (k * 1.0f) + vec3(0, 0.06f, 0));
                    b.box(vec3(0, 0, 0), vec3(0.25f, 0.02f, 2.f), vec4(0.92f, 0.92f, 0.9f, 0), &zx);
                }
                for (int s = -1; s <= 1; s += 2) {
                    // stop line for the traffic coming towards this crossing
                    vec3 fwd = s > 0 ? t : -t;
                    vec3 right = normalize(cross(fwd, up));
                    mat4 sl = basis(right, up, fwd, p - fwd * 3.5f - right * (hwR * 0.5f) + up * 0.06f);
                    b.box(vec3(0, 0, 0), vec3(hwR * 0.48f, 0.015f, 0.2f), vec4(0.92f, 0.92f, 0.9f, 0), &sl);
                    // signal post on the left kerb, arm out over the lane, horizontal lamp housing
                    vec3 q = p - fwd * 3.f - right * (hwR + 0.5f);
                    q.y = heightAt(q.x, q.z) - 0.1f;
                    vec4 grey(0.55f, 0.56f, 0.58f, 7), housing(0.32f, 0.33f, 0.35f, 7);
                    b.cylinder(q, 0.13f, 5.6f, 8, grey);
                    vec3 a0 = q + up * 5.4f, a1 = a0 + right * (hwR * 0.9f);
                    b.tube(a0, a1, 0.07f, 0.06f, 6, grey);
                    vec3 hc = a1 - right * 0.5f - up * 0.05f;
                    mat4 hx = basis(right, up, -fwd, hc);
                    b.box(vec3(0, 0, 0), vec3(0.68f, 0.21f, 0.14f), housing, &hx);
                    const vec4 lampC[3] = {{0.1f, 0.35f, 0.25f, 0}, {0.35f, 0.3f, 0.1f, 0}, {1.f, 0.12f, 0.08f, 15}};
                    for (int k = 0; k < 3; k++) {
                        vec3 lc = hc + right * ((k - 1) * 0.43f) - fwd * 0.15f;
                        b.sphere(lc, 0.15f, 1, lampC[k]);
                        b.box(hx.transformPoint(vec3((k - 1) * 0.43f, 0.17f, 0.26f)), vec3(0.18f, 0.02f, 0.13f), housing);  // visor
                    }
                    // pedestrian signal across the road, on the near post
                    vec3 ped = q + up * 2.6f - right * 0.05f;
                    b.box(ped, vec3(0.2f, 0.32f, 0.2f), housing);
                    b.box(ped + right * 0.21f + up * 0.13f, vec3(0.01f, 0.12f, 0.15f), vec4(1.f, 0.15f, 0.1f, 15));
                    pendingTrunks.push_back({q.x, q.z, 0.18f});
                }
            }
        }
        // the great torii (Kanadorii) spanning the foot of the street, dark bronze
        {
            int i = i0 + 3;
            vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
            float half = hwR + walk + 0.4f, H = 13.f;
            vec4 bronze(0.30f, 0.27f, 0.23f, 7), dark(0.1f, 0.1f, 0.1f, 0);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                vec3 q = p + side * (s2 * half);
                q.y = heightAt(q.x, q.z) - 0.3f;
                b.cylinder(q, 0.62f, H + 0.3f, 14, bronze);
                b.cylinder(q, 0.8f, 1.0f, 14, dark);
                pendingTrunks.push_back({q.x, q.z, 0.75f});
            }
            mat4 xf = basis(side, up, t, vec3(p.x, p.y - 0.1f, p.z));
            b.box(vec3(0, H - 2.2f, 0), vec3(half + 1.2f, 0.4f, 0.42f), bronze, &xf);
            b.box(vec3(0, H + 0.1f, 0), vec3(half + 1.8f, 0.45f, 0.55f), bronze, &xf);
            b.box(vec3(0, H + 0.75f, 0), vec3(half + 2.4f, 0.32f, 0.72f), dark, &xf);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                mat4 tip = xf * translate(vec3(s2 * (half + 2.5f), H + 1.0f, 0)) * rotateZ(s2 * -0.3f);
                b.box(vec3(0, 0, 0), vec3(0.8f, 0.22f, 0.7f), dark, &tip);
            }
            b.box(vec3(0, H - 1.1f, 0), vec3(0.42f, 1.15f, 0.25f), dark, &xf);  // plaque: Sengen shrine
            for (int s2 = -1; s2 <= 1; s2 += 2)
                signFitted(b, xf.transformPoint(vec3(0, H - 1.1f, s2 * 0.27f)), t * (float)s2, 0.32f, 1.05f, SV_SENGEN, true);
            for (int s2 = -1; s2 <= 1; s2 += 2)
                for (int k = 0; k < 3; k++) sakuraSpots.push_back(p + side * (s2 * (half + 6.f + k * 7.f)) - t * (6.f + k * 5.f));
        }
        // the shopping-street arch near the top: "本町通り商店街" on the way up, "welcome" on the way down
        {
            int i = i1 - 14;
            vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
            float half = hwR + 1.7f;
            vec4 steelC(0.72f, 0.73f, 0.75f, 7);
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                vec3 q = p + side * (s2 * half);
                q.y = heightAt(q.x, q.z) - 0.2f;
                b.cylinder(q, 0.22f, 8.6f, 10, steelC);
                pendingTrunks.push_back({q.x, q.z, 0.3f});
            }
            vec3 sc = p + up * 6.9f;
            mat4 xf = basis(side, up, t, sc);
            b.box(vec3(0, 1.55f, 0), vec3(half + 0.3f, 0.12f, 0.2f), steelC, &xf);
            b.box(vec3(0, -1.55f, 0), vec3(half + 0.3f, 0.12f, 0.2f), steelC, &xf);
            b.box(vec3(0, 0, 0), vec3(5.2f, 1.4f, 0.14f), signBoard(SH_HONCHO, false), &xf);
            signFitted(b, sc - t * 0.16f, -t, 5.1f, 1.3f, SH_HONCHO, false, 0.15f);
            signFitted(b, sc + t * 0.16f, t, 5.1f, 1.3f, SH_WELCOME, false, 0.15f);
            for (int k = -3; k <= 3; k++) {  // a row of lanterns along the arch's lower beam
                vec3 l = xf.transformPoint(vec3(k * 1.5f, -2.0f, 0));
                b.sphere(l, 0.3f, 1, vec4(1.f, 0.25f, 0.12f, 15));
            }
        }
        rebuildBoxGrid();
        SDL_Log("Japan: Honcho-dori, %d shop houses (%.0f,%.0f)-(%.0f,%.0f)", shops, rd.pts[i0].x, rd.pts[i0].z, rd.pts[i1].x, rd.pts[i1].z);
        break;
    }

    // Tokyo's streets get the same clutter: poles and wires down one side, lantern lamps both
    for (auto& rd : roads) {
        bool street = rd.name.find("-dori") != std::string::npos || rd.name.find("Street") != std::string::npos || rd.name == "Omotesando";
        if (!street || rd.pts.size() < 10) continue;
        float along = 0, nextLamp = 0, nextPole = 0;
        vec3 prevTops[6];
        bool have = false;
        int sideSel = (rd.name.size() % 2) ? 1 : -1;
        for (size_t i = 0; i + 1 < rd.pts.size(); i++) {
            along += length(rd.pts[i + 1] - rd.pts[i]);
            vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
            if (!urban(p.x, p.z, 0.f)) { have = false; continue; }
            float hwR = rd.width * 0.5f;
            if (along >= nextLamp) {
                nextLamp = along + 26.f;
                for (int s = -1; s <= 1; s += 2) {
                    vec3 q = p + side * (s * (hwR + 0.8f));
                    if (!inBox(q.x, q.z, 0.4f) && roadDistAt(q.x, q.z) > 0.5f) lanternLamp(q, side * (float)-s);
                }
            }
            if (along >= nextPole) {
                nextPole = along + 32.f;
                vec3 q = p + side * (sideSel * (hwR + 1.6f));
                if (!inBox(q.x, q.z, 0.4f) && roadDistAt(q.x, q.z) > 0.8f) {
                    vec3 tops[6];
                    bigPole(q, t, side, tops);
                    if (have)
                        for (int k = 0; k < 6; k++) wire(prevTops[k], tops[k], 0.5f + 0.07f * k);
                    std::memcpy(prevTops, tops, sizeof(tops));
                    have = true;
                } else have = false;
            }
        }
    }
    rebuildBoxGrid();

    // ---------------------------------------------------------------- road signs
    // blue guide boards (and green ones for the toll roads), yellow warnings on the mountain passes,
    // gantries over the Wangan, and 止まれ painted where a road ends at another
    {
        Rng gr(77);
        int guides = 0, warns = 0, marks = 0;
        auto has = [](const std::string& a, const char* b) { return a.find(b) != std::string::npos; };
        auto board = [&](vec3 q, vec3 face, float hw, float hh, float y, int cell, bool twoPosts) {
            q.y = heightAt(q.x, q.z) - 0.1f;
            vec3 rt = normalize(cross(-face, up));
            vec4 post(0.62f, 0.63f, 0.65f, 7);
            if (twoPosts)
                for (int k = -1; k <= 1; k += 2) {
                    vec3 pp = q + rt * (k * hw * 0.62f);
                    pp.y = heightAt(pp.x, pp.z) - 0.1f;
                    b.cylinder(pp, 0.07f, y + hh - (pp.y - q.y), 6, post);
                    pendingTrunks.push_back({pp.x, pp.z, 0.12f});
                }
            else {
                b.cylinder(q, 0.05f, y + hh * 0.6f, 6, post);
                pendingTrunks.push_back({q.x, q.z, 0.1f});
            }
            vec3 c = q + up * y - face * 0.06f;
            mat4 xf = basis(rt, up, face, c);
            b.box(vec3(0, 0, -0.03f), vec3(hw + 0.04f, hh + 0.04f, 0.025f), vec4(0.55f, 0.56f, 0.58f, 7), &xf);  // back plate
            signFace(b, c + face * 0.002f, face, hw, hh, cell, false);
        };
        for (auto& rd : roads) {
            int n = int(rd.pts.size());
            if (n < 40) continue;
            bool street = has(rd.name, "-dori") || has(rd.name, "Street") || rd.name == "Omotesando";
            float hwR = rd.width * 0.5f;
            // which board this road gets
            std::vector<int> cells;
            if (rd.name == "Fuji Road") cells = {SG_WELCOMEFUJI, SG_FUJIYOSHIDA, SG_FUJI, SG_5TH};
            else if (rd.name == "Fuji Skyline") cells = {SG_SKYLINE, SG_5TH};
            else if (rd.name == "Irohazaka") cells = {SG_IROHA};
            else if (rd.name == "Sea of Japan Road") cells = {SG_SEA, SG_FUJI};
            else if (has(rd.name, "Chichibu") || has(rd.name, "Haruna")) cells = {SG_CHICHIBU, SG_FUJI};
            else if (has(rd.name, "Touge") || has(rd.name, "Pass")) cells = {SG_TOUGE, SG_FUJI};
            else if (has(rd.name, "Daikoku")) cells = {SG_DAIKOKU, SG_TOKYO};
            else if (has(rd.name, "Wangan")) cells = {};
            else cells = {SG_FUJI, SG_TOKYO, SG_FUJIYOSHIDA};
            bool pass = has(rd.name, "Touge") || has(rd.name, "Pass") || rd.name == "Irohazaka" || has(rd.name, "Haruna") || rd.name == "Fuji Skyline" ||
                        rd.name == "Fuji Road" || has(rd.name, "Okutama") || has(rd.name, "Chichibu");
            float along = 0, nextG = gr.range(250.f, 450.f), nextW = gr.range(150.f, 300.f), nextGantry = 400.f;
            int gi = 0;
            for (int i = 0; i + 1 < n; i++) {
                along += length(rd.pts[i + 1] - rd.pts[i]);
                vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);  // side is the driver's right
                if (street || urban(p.x, p.z, 10.f)) continue;
                if (!cells.empty() && along > nextG && i > 10 && i + 10 < n) {
                    nextG = along + gr.range(750.f, 1000.f);
                    bool back = gi % 2;  // alternate directions, always on the driver's left (Japan drives on the left)
                    vec3 fwd = back ? -t : t, right = back ? -side : side;
                    vec3 q = p - right * (hwR + 2.6f);
                    if (roadDistAt(q.x, q.z) > 1.8f && !inBox(q.x, q.z, 1.5f) && std::fabs(heightAt(q.x, q.z) - p.y) < 2.5f) {
                        board(q, -fwd, 2.1f, 1.05f, 3.4f, cells[gi % cells.size()], true);
                        guides++;
                    }
                    gi++;
                }
                if (pass && along > nextW && i > 6 && i + 6 < n) {
                    nextW = along + gr.range(380.f, 620.f);
                    bool back = gr.f() < 0.5f;
                    vec3 fwd = back ? -t : t, right = back ? -side : side;
                    vec3 q = p - right * (hwR + 1.4f);
                    if (roadDistAt(q.x, q.z) > 0.9f && !inBox(q.x, q.z, 1.f)) {
                        board(q, -fwd, 0.62f, 0.31f, 2.1f, SG_ROCKS + gr.irange(0, 2), false);
                        warns++;
                    }
                }
                if (has(rd.name, "Wangan") && along > nextGantry && i > 10 && i + 10 < n) {
                    nextGantry = along + 1100.f;
                    // overhead gantry: posts beyond both shoulders, boards over the lanes
                    float half = hwR + 2.2f;
                    vec4 steelC(0.66f, 0.67f, 0.69f, 7);
                    bool ok = true;
                    for (int k = -1; k <= 1; k += 2) { vec3 q = p + side * (k * half); ok &= roadDistAt(q.x, q.z) > 1.2f && !inBox(q.x, q.z, 1.f); }
                    if (!ok) continue;
                    for (int k = -1; k <= 1; k += 2) {
                        vec3 q = p + side * (k * half);
                        q.y = heightAt(q.x, q.z) - 0.2f;
                        b.cylinder(q, 0.28f, 9.2f + (p.y - q.y), 8, steelC);
                        pendingTrunks.push_back({q.x, q.z, 0.35f});
                    }
                    mat4 xf = basis(side, up, t, p + up * 9.f);
                    b.box(vec3(0, 0, 0), vec3(half, 0.22f, 0.22f), steelC, &xf);
                    b.box(vec3(0, -0.7f, 0), vec3(half, 0.12f, 0.12f), steelC, &xf);
                    int fwdCells[2] = {SG_TOKYO, SG_DAIKOKU}, backCells[2] = {SG_FUJI, SG_SEA};
                    for (int k = 0; k < 2; k++) {
                        float off = (k ? 1.f : -1.f) * hwR * 0.5f;
                        vec3 c = p + side * off + up * 7.6f;
                        b.box(vec3(off, -1.4f, 0), vec3(2.55f, 1.3f, 0.08f), signBoard(fwdCells[k], false), &xf);
                        signFace(b, c - t * 0.1f, -t, 2.5f, 1.25f, fwdCells[k], false);
                        signFace(b, c + t * 0.1f, t, 2.5f, 1.25f, backCells[k], false);
                    }
                    guides++;
                }
            }
            // 止まれ and a stop line where this road ends at another one
            if (!rd.closed && !street)
                for (int e = 0; e < 2; e++) {
                    vec3 pe = rd.pts[e ? n - 1 : 0];
                    bool joins = false;
                    for (auto& o : roads) {
                        if (&o == &rd || joins) continue;
                        for (size_t k = 0; k < o.pts.size(); k += 2)
                            if (std::fabs(o.pts[k].x - pe.x) < 14.f && std::fabs(o.pts[k].z - pe.z) < 14.f) { joins = true; break; }
                    }
                    if (!joins) continue;
                    int i = e ? n - 1 - 7 : 7, is = e ? n - 1 - 4 : 4;
                    vec3 fwd = e ? rd.tan[i] : -rd.tan[i];
                    vec3 right = normalize(cross(fwd, up));
                    vec3 lane = -right * (hwR * 0.5f);
                    roadMarking(b, rd.pts[i] + lane + up * 0.06f, fwd, std::min(hwR * 0.45f, 1.7f), 2.4f, SG_TOMARE);
                    mat4 sx = basis(right, up, fwd, rd.pts[is] + lane + up * 0.06f);
                    b.box(vec3(0, 0, 0), vec3(hwR * 0.48f, 0.015f, 0.22f), vec4(0.92f, 0.92f, 0.9f, 0), &sx);
                    marks++;
                }
        }
        SDL_Log("Japan: %d guide signs, %d warning signs, %d stop markings", guides, warns, marks);
    }

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

    // bamboo groves behind some of the towns' houses and along quiet roads
    {
        Rng br(515);
        int groves = 0;
        for (auto& rd : roads) {
            if (rd.surface != SURF_ASPHALT || rd.pts.size() < 60 || rd.name.find("-dori") != std::string::npos || rd.name.find("Street") != std::string::npos) continue;
            for (size_t i = 30; i + 30 < rd.pts.size(); i += 90) {
                if (br.f() > 0.35f) continue;
                vec3 p = rd.pts[i], t = rd.tan[i], side(-t.z, 0, t.x);
                float sg = br.f() < 0.5f ? -1.f : 1.f;
                vec3 c = p + side * (sg * (rd.width * 0.5f + 16.f));
                if (heightAt(c.x, c.z) < waterLevel + 2.f || urban(c.x, c.z, 30.f) || inBox(c.x, c.z, 6.f) || roadDistAt(c.x, c.z) < 9.f) continue;
                if (std::hypot(c.x - festCenter.x, c.z - festCenter.z) < 320.f) continue;
                for (int k = 0; k < 70; k++) {
                    float a = br.f() * 6.283f, r = std::sqrt(br.f()) * 11.f;
                    vec3 q = c + vec3(std::cos(a) * r, 0, std::sin(a) * r);
                    if (roadDistAt(q.x, q.z) < 3.f) continue;
                    q.y = heightAt(q.x, q.z) - 0.2f;
                    float H = br.range(8.f, 13.f), lean = br.range(-0.4f, 0.4f);
                    vec3 top = q + vec3(lean, H, lean * 0.6f);
                    vec4 culm(0.42f + br.range(-0.05f, 0.05f), 0.55f, 0.24f, 0);
                    props.tube(q, top, 0.07f, 0.045f, 4, culm);
                    // feathery leaf sprays near the top
                    for (int j = 0; j < 3; j++) {
                        vec3 lc = lerp(q, top, 0.7f + j * 0.12f);
                        float la = br.f() * 6.283f;
                        vec3 d(std::cos(la), -0.25f, std::sin(la));
                        vec4 leaf(0.30f, 0.46f, 0.16f, 0);
                        props.flatTri(lc, lc + d * 1.6f + vec3(0, 0.25f, 0), lc + d * 1.4f + vec3(-d.z * 0.5f, -0.2f, d.x * 0.5f), leaf);
                        props.flatTri(lc, lc + d * 1.4f + vec3(-d.z * 0.5f, -0.2f, d.x * 0.5f), lc + d * 1.6f + vec3(0, 0.25f, 0), leaf);
                    }
                }
                groves++;
            }
        }
        SDL_Log("Japan: %d bamboo groves", groves);
    }
    // a white castle (Himeji style) on its stone base, above the valley towns
    {
        vec3 c(1180.f, 0, 320.f);
        float best = -1e9f;
        for (float dx = -250; dx <= 250; dx += 50)
            for (float dz = -250; dz <= 250; dz += 50) {  // the highest knoll nearby that isn't on a road
                float hh = heightAt(1180 + dx, 320 + dz);
                if (roadDistAt(1180 + dx, 320 + dz) > 45.f && hh > best && std::fabs(heightAt(1180 + dx + 20, 320 + dz) - hh) < 6.f) { best = hh; c = vec3(1180 + dx, 0, 320 + dz); }
            }
        float g = heightAt(c.x, c.z);
        // level the top
        for (int iz = 0; iz < N; iz++) {
            float wz = -HALF + iz * CELL;
            if (std::fabs(wz - c.z) > 60) continue;
            for (int ix = 0; ix < N; ix++) {
                float wx = -HALF + ix * CELL;
                if (std::fabs(wx - c.x) > 60) continue;
                if (roadDist[iz * N + ix] < 3.f) continue;
                float d = std::max(std::fabs(wx - c.x), std::fabs(wz - c.z));
                height[iz * N + ix] = lerpf(height[iz * N + ix], g, 1.f - smoothstepf(30.f, 55.f, d));
            }
        }
        vec4 stone(0.55f, 0.53f, 0.49f, 0), white(0.93f, 0.92f, 0.89f, 0), roof(0.24f, 0.28f, 0.32f, 0), dark(0.14f, 0.14f, 0.16f, 0);
        // battered stone base: stacked slabs stepping in
        for (int k = 0; k < 6; k++) {
            float s2 = 24.f - k * 1.1f;
            props.box(vec3(c.x, g + k * 1.5f + 0.75f, c.z), vec3(s2, 0.75f, s2 * 0.8f), stone);
        }
        float y = g + 9.f;
        float sx = 17.f, sz = 13.f;
        for (int k = 0; k < 5; k++) {
            float hgt = k == 0 ? 6.f : 4.2f;
            props.box(vec3(c.x, y + hgt * 0.5f, c.z), vec3(sx, hgt * 0.5f, sz), white);
            for (int w2 = -2; w2 <= 2; w2++)  // dark windows
                props.box(vec3(c.x + w2 * sx * 0.35f, y + hgt * 0.55f, c.z - sz - 0.02f), vec3(0.6f, 0.5f, 0.04f), dark);
            y += hgt;
            vec3 cc(c.x, 0, c.z);
            // roof: hip roof plus a gable (chidori-hafu) on the front
            float ro = 1.6f;
            {
                vec3 f(0, 0, 1), r(1, 0, 0);
                float hw = sx + ro, hd = sz + ro;
                vec3 e0 = cc + r * -hw + f * -hd, e1 = cc + r * hw + f * -hd, e2 = cc + r * hw + f * hd, e3 = cc + r * -hw + f * hd;
                e0.y = e1.y = e2.y = e3.y = y;
                float rl = std::max(0.f, hw - hd);
                vec3 r0 = cc + r * -rl, r1 = cc + r * rl;
                r0.y = r1.y = y + 3.2f;
                props.flatQuad(e1, e0, r0, r1, roof); props.flatQuad(e3, e2, r1, r0, roof);
                props.flatTri(e0, e3, r0, roof); props.flatTri(e2, e1, r1, roof);
                vec3 gA = cc + vec3(-4.f, y, -hd), gB = cc + vec3(4.f, y, -hd), gT = cc + vec3(0, y + 3.6f, -hd + 1.5f);
                props.flatTri(gB, gA, gT, roof);
                props.flatTri(gA + vec3(0.3f, 0.4f, 0.05f), gB + vec3(-0.3f, 0.4f, 0.05f), gT + vec3(0, -0.4f, -0.05f), white);
            }
            y += 1.8f;
            sx *= 0.78f; sz *= 0.78f;
        }
        props.box(vec3(c.x, y + 1.2f, c.z), vec3(sx * 0.9f, 1.2f, sz * 0.5f), roof);
        for (int s2 = -1; s2 <= 1; s2 += 2) props.box(vec3(c.x + s2 * sx * 0.9f, y + 2.6f, c.z), vec3(0.25f, 0.6f, 0.15f), vec4(0.85f, 0.65f, 0.2f, 7));  // golden shachihoko
        boxes.push_back({c.x - 24, c.z - 19, c.x + 24, c.z + 19, y});
        clearings.push_back(vec3(c.x, 75.f, c.z));
        for (int k = 0; k < 22; k++) { float a = k / 22.f * 6.2832f; sakuraSpots.push_back(c + vec3(std::cos(a) * 34.f, 0, std::sin(a) * 29.f)); }
        SDL_Log("Japan: castle at (%.0f, %.0f)", c.x, c.z);
    }
    // ---------------------------------------------------------------- set pieces
    auto levelDisc = [&](vec3 c, float r0, float r1, float y) {
        int x0 = std::max(0, int((c.x - r1 + HALF) / CELL)), x1 = std::min(N - 1, int((c.x + r1 + HALF) / CELL) + 1);
        int z0 = std::max(0, int((c.z - r1 + HALF) / CELL)), z1 = std::min(N - 1, int((c.z + r1 + HALF) / CELL) + 1);
        for (int iz = z0; iz <= z1; iz++)
            for (int ix = x0; ix <= x1; ix++) {
                if (roadDist[iz * N + ix] < 2.f) continue;
                float d = std::hypot(-HALF + ix * CELL - c.x, -HALF + iz * CELL - c.z);
                float k = 1.f - smoothstepf(r0, r1, d);
                if (k > 0) height[iz * N + ix] = lerpf(height[iz * N + ix], y, k);
            }
    };
    // ground along a line a -> e (heights included), for stairs and paths
    auto carveLine = [&](vec3 a, vec3 e, float hwid, float fade) {
        float x0f = std::min(a.x, e.x) - hwid - fade, x1f = std::max(a.x, e.x) + hwid + fade;
        float z0f = std::min(a.z, e.z) - hwid - fade, z1f = std::max(a.z, e.z) + hwid + fade;
        int x0 = std::max(0, int((x0f + HALF) / CELL)), x1 = std::min(N - 1, int((x1f + HALF) / CELL) + 1);
        int z0 = std::max(0, int((z0f + HALF) / CELL)), z1 = std::min(N - 1, int((z1f + HALF) / CELL) + 1);
        vec2 ab(e.x - a.x, e.z - a.z);
        for (int iz = z0; iz <= z1; iz++)
            for (int ix = x0; ix <= x1; ix++) {
                if (roadDist[iz * N + ix] < 2.f) continue;
                float wx = -HALF + ix * CELL, wz = -HALF + iz * CELL;
                vec2 ap(wx - a.x, wz - a.z);
                float u = clampf(dot(ap, ab) / std::max(dot(ab, ab), 1e-6f), 0.f, 1.f);
                float d = length(vec2(a.x + ab.x * u - wx, a.z + ab.y * u - wz));
                float k = 1.f - smoothstepf(hwid, hwid + fade, d);
                if (k > 0) height[iz * N + ix] = lerpf(height[iz * N + ix], lerpf(a.y, e.y, u) - 0.15f, k);
            }
    };
    auto toro = [&](vec3 q) {  // stone lantern
        q.y = heightAt(q.x, q.z) - 0.05f;
        vec4 stone(0.58f, 0.57f, 0.53f, 0);
        b.box(q + up * 0.15f, vec3(0.4f, 0.15f, 0.4f), stone);
        b.cylinder(q + up * 0.3f, 0.13f, 0.95f, 8, stone);
        b.box(q + up * 1.32f, vec3(0.33f, 0.08f, 0.33f), stone);
        b.box(q + up * 1.62f, vec3(0.22f, 0.22f, 0.22f), vec4(1.f, 0.8f, 0.5f, 15));
        b.cone(q + up * 1.84f, 0.52f, 0.36f, 6, stone);
        b.sphere(q + up * 2.24f, 0.08f, 1, stone);
        pendingTrunks.push_back({q.x, q.z, 0.4f});
    };
    auto redLamp = [&](vec3 q) {  // the little red lanterns on posts along temple paths
        q.y = heightAt(q.x, q.z) - 0.05f;
        vec4 dark(0.12f, 0.1f, 0.09f, 0);
        b.cylinder(q, 0.05f, 1.15f, 6, dark);
        b.box(q + up * 1.33f, vec3(0.16f, 0.2f, 0.16f), vec4(1.f, 0.22f, 0.1f, 15));
        b.box(q + up * 1.57f, vec3(0.21f, 0.04f, 0.21f), dark);
    };
    int people = 0;
    auto person = [&](vec3 q, float yaw, int style, bool parasol) {  // someone in a kimono
        q.y = heightAt(q.x, q.z) - 0.02f;
        const vec4 kimonos[] = {{0.85f, 0.22f, 0.32f, 0}, {0.95f, 0.62f, 0.72f, 0}, {0.18f, 0.28f, 0.58f, 0},
                                {0.9f, 0.72f, 0.3f, 0}, {0.32f, 0.52f, 0.38f, 0}, {0.94f, 0.93f, 0.88f, 0}, {0.55f, 0.2f, 0.45f, 0}};
        vec4 km = kimonos[style % 7], obi(0.88f, 0.72f, 0.3f, 0), hair(0.05f, 0.04f, 0.04f, 0), skin(0.93f, 0.79f, 0.67f, 0);
        vec3 fw(std::sin(yaw), 0, std::cos(yaw)), rt(fw.z, 0, -fw.x);
        b.cylinder(q, 0.1f, 0.06f, 6, vec4(0.95f, 0.95f, 0.93f, 0));          // tabi and geta
        b.tube(q + up * 0.04f, q + up * 1.3f, 0.21f, 0.18f, 8, km);
        b.cylinder(q + up * 0.92f, 0.205f, 0.22f, 8, obi);
        b.box(q + up * 1.02f - fw * 0.22f, vec3(0.16f, 0.13f, 0.05f), obi);   // obi bow at the back
        b.tube(q + up * 1.3f, q + up * 1.4f, 0.18f, 0.07f, 8, km);
        b.tube(q + up * 1.2f + rt * 0.17f, q + up * 0.85f + rt * 0.2f + fw * 0.12f, 0.07f, 0.07f, 5, km);  // sleeves
        b.tube(q + up * 1.2f - rt * 0.17f, q + up * 0.85f - rt * 0.2f + fw * 0.12f, 0.07f, 0.07f, 5, km);
        b.sphere(q + up * 1.52f, 0.105f, 1, skin);
        b.sphere(q + up * 1.56f - fw * 0.03f, 0.102f, 1, hair);
        b.sphere(q + up * 1.66f - fw * 0.07f, 0.065f, 1, hair);                 // bun
        if (parasol) {
            vec3 hnd = q + up * 1.05f + rt * 0.2f + fw * 0.15f, top = hnd + up * 1.0f - rt * 0.12f;
            b.tube(hnd, top, 0.012f, 0.012f, 3, vec4(0.3f, 0.2f, 0.12f, 0));
            vec4 pc = style % 2 ? vec4(0.96f, 0.5f, 0.62f, 19) : vec4(0.86f, 0.18f, 0.16f, 19);
            b.cone(top - up * 0.22f, 0.62f, 0.24f, 12, pc);
        }
        pendingTrunks.push_back({q.x, q.z, 0.3f});
        people++;
    };
    // a big two-storey vermilion temple gate (Kiyomizu-dera's niomon); walk through along fwd
    auto niomon = [&](vec3 c, vec3 fwd) {
        vec3 rt = normalize(cross(fwd, up));
        c.y = heightAt(c.x, c.z);
        vec4 red(0.86f, 0.24f, 0.1f, 0), white(0.94f, 0.92f, 0.87f, 0), roofC(0.17f, 0.18f, 0.2f, 0), stone(0.55f, 0.54f, 0.5f, 0), gold(0.85f, 0.65f, 0.2f, 7);
        mat4 xf = basis(rt, up, fwd, c);
        b.box(vec3(0, -0.2f, 0), vec3(7.4f, 0.5f, 3.6f), stone, &xf);
        for (float x : {-5.6f, -2.7f, 2.7f, 5.6f})
            for (float z : {-2.3f, 2.3f}) {
                vec3 q = xf.transformPoint(vec3(x, 0.3f, z));
                b.cylinder(q, 0.33f, 6.6f, 10, red);
                b.cylinder(q - up * 0.05f, 0.45f, 0.3f, 10, stone);
                pendingTrunks.push_back({q.x, q.z, 0.4f});
            }
        b.box(vec3(0, 6.2f, 0), vec3(6.2f, 0.32f, 2.7f), red, &xf);
        b.box(vec3(0, 6.75f, 0), vec3(6.5f, 0.22f, 3.0f), vec4(0.55f, 0.13f, 0.06f, 0), &xf);       // bracket band
        for (int k = -1; k <= 1; k += 2) b.box(vec3(k * 4.2f, 3.6f, 0), vec3(1.2f, 2.8f, 2.1f), white, &xf);  // side bays (guardian niches)
        vec3 cc(c.x, 0, c.z);
        hipRoof(cc, fwd, rt, 6.6f, 3.4f, c.y + 7.0f, 1.3f, roofC);
        // upper storey with a railed balcony, then the big top roof
        b.box(vec3(0, 9.0f, 0), vec3(4.6f, 1.3f, 2.2f), red, &xf);
        b.box(vec3(0, 9.1f, -2.22f), vec3(3.4f, 0.8f, 0.03f), white, &xf);
        b.box(vec3(0, 9.1f, 2.22f), vec3(3.4f, 0.8f, 0.03f), white, &xf);
        b.box(vec3(0, 7.95f, 0), vec3(5.2f, 0.08f, 2.8f), red, &xf);
        for (int k = -1; k <= 1; k += 2) {
            b.box(vec3(0, 8.4f, k * 2.8f), vec3(5.2f, 0.05f, 0.05f), red, &xf);
            b.box(vec3(k * 5.2f, 8.4f, 0), vec3(0.05f, 0.05f, 2.8f), red, &xf);
        }
        b.box(vec3(0, 10.45f, 0), vec3(5.0f, 0.22f, 2.6f), vec4(0.55f, 0.13f, 0.06f, 0), &xf);
        hipRoof(cc, fwd, rt, 6.4f, 3.6f, c.y + 10.6f, 2.4f, roofC);
        for (int k = -1; k <= 1; k += 2) b.box(xf.transformPoint(vec3(k * 3.0f, 13.25f, 0)), vec3(0.25f, 0.35f, 0.25f), gold);
        signFitted(b, xf.transformPoint(vec3(0, 9.2f, -2.26f)), -fwd, 0.6f, 0.75f, SV_SENGEN, true);
    };

    // ---- Chureito: the five-storey pagoda on the hill above the town, cherry trees all round it,
    // a long stone stair up from a vermilion gate, stone and red lanterns, Fuji behind
    {
        vec3 c(-1730.f, 0, 640.f);
        float g0 = heightAt(c.x, c.z);
        levelDisc(c, 15.f, 30.f, g0);
        float g = g0 - 0.2f;
        vec4 red(0.84f, 0.2f, 0.09f, 0), deep(0.55f, 0.12f, 0.06f, 0), white(0.93f, 0.91f, 0.86f, 0), roofG(0.27f, 0.35f, 0.34f, 0),
             gold(0.85f, 0.65f, 0.2f, 7), stone(0.55f, 0.54f, 0.5f, 0);
        b.box(vec3(c.x, g + 0.5f, c.z), vec3(7.5f, 0.6f, 7.5f), stone);
        b.box(vec3(c.x, g + 1.2f, c.z), vec3(6.6f, 0.15f, 6.6f), stone);
        float y = g + 1.35f, s = 4.4f;
        vec3 cc(c.x, 0, c.z);
        for (int k = 0; k < 5; k++) {
            float hgt = k == 0 ? 4.0f : 3.1f;
            b.box(vec3(c.x, y + hgt * 0.5f, c.z), vec3(s, hgt * 0.5f, s), red);
            for (int q = 0; q < 4; q++) {
                vec3 n = q == 0 ? vec3(1, 0, 0) : q == 1 ? vec3(-1, 0, 0) : q == 2 ? vec3(0, 0, 1) : vec3(0, 0, -1);
                vec3 tn(n.z, 0, -n.x);
                if (k > 0) {
                    for (int w2 = -1; w2 <= 1; w2 += 2)
                        b.box(vec3(c.x, y + hgt * 0.5f, c.z) + n * (s + 0.02f) + tn * (w2 * s * 0.55f), vec3(std::fabs(tn.x) * s * 0.3f + 0.03f, hgt * 0.32f, std::fabs(tn.z) * s * 0.3f + 0.03f), white);
                }
                b.box(vec3(c.x, y + hgt * 0.5f, c.z) + n * (s + 0.04f), vec3(std::fabs(tn.x) * 0.5f + 0.02f, hgt * 0.36f, std::fabs(tn.z) * 0.5f + 0.02f), deep);  // door
                float sx = (q & 1) ? s : -s, sz = (q & 2) ? s : -s;
                b.box(vec3(c.x + sx, y + hgt * 0.5f, c.z + sz), vec3(0.22f, hgt * 0.5f, 0.22f), red);
            }
            if (k > 0) {  // balcony with a rail
                b.box(vec3(c.x, y + 0.05f, c.z), vec3(s + 0.75f, 0.07f, s + 0.75f), deep);
                for (int q = 0; q < 4; q++) {
                    vec3 n = q == 0 ? vec3(1, 0, 0) : q == 1 ? vec3(-1, 0, 0) : q == 2 ? vec3(0, 0, 1) : vec3(0, 0, -1);
                    vec3 tn(n.z, 0, -n.x);
                    b.box(vec3(c.x, y + 0.55f, c.z) + n * (s + 0.72f), vec3(std::fabs(tn.x) * (s + 0.75f) + 0.03f, 0.04f, std::fabs(tn.z) * (s + 0.75f) + 0.03f), red);
                }
            }
            b.box(vec3(c.x, y + hgt + 0.12f, c.z), vec3(s + 0.55f, 0.2f, s + 0.55f), deep);   // bracket band
            float er = s + 1.8f, ey = y + hgt + 0.25f;
            // deep eaves: a thick red rafter slab under the roof, so they read from the ground
            b.box(vec3(c.x, ey - 0.12f, c.z), vec3(er + 0.55f, 0.22f, er + 0.55f), deep);
            b.box(vec3(c.x, ey - 0.42f, c.z), vec3(er - 0.3f, 0.12f, er - 0.3f), red);
            hipRoof(cc, vec3(0, 0, 1), vec3(1, 0, 0), er, er, ey + 0.1f, 1.25f, roofG);
            for (int q = 0; q < 4; q++) {  // corners sweep up, a bronze bell under each
                vec3 dq(((q & 1) ? 1.f : -1.f), 0, ((q & 2) ? 1.f : -1.f));
                vec3 cin = vec3(c.x, ey + 0.12f, c.z) + dq * (er * 0.85f), cout = vec3(c.x, ey + 0.55f, c.z) + dq * (er + 1.0f);
                b.tube(cin, cout, 0.16f, 0.07f, 4, roofG);
                vec3 bell = vec3(c.x, ey - 0.35f, c.z) + dq * (er * 0.95f);
                b.tube(bell + up * 0.35f, bell + up * 0.12f, 0.01f, 0.01f, 3, gold);
                b.tube(bell + up * 0.12f, bell - up * 0.15f, 0.05f, 0.11f, 6, vec4(0.42f, 0.38f, 0.2f, 7));
            }
            y += hgt + 1.05f;
            s *= 0.88f;
        }
        b.tube(vec3(c.x, y, c.z), vec3(c.x, y + 8.f, c.z), 0.26f, 0.1f, 6, gold);  // sorin spire
        for (int k = 0; k < 9; k++) b.cylinder(vec3(c.x, y + 1.2f + k * 0.62f, c.z), 0.5f - k * 0.025f, 0.1f, 10, gold);
        b.sphere(vec3(c.x, y + 8.2f, c.z), 0.3f, 1, gold);
        boxes.push_back({c.x - 7.5f, c.z - 7.5f, c.x + 7.5f, c.z + 7.5f, y});
        clearings.push_back(vec3(c.x, 52.f, c.z));
        for (int k = 0; k < 16; k++) {
            float a = k / 16.f * 6.2832f;
            sakuraSpots.push_back(c + vec3(std::cos(a), 0, std::sin(a)) * (17.f + (k % 3) * 5.f));
            if (k % 2 == 0) redLamp(c + vec3(std::cos(a + 0.2f), 0, std::sin(a + 0.2f)) * 12.5f);
        }
        // the stair: down the hill towards the town, a gate at the bottom
        vec3 dir = normalize(vec3(1, 0, -0.15f));
        vec3 top = c + dir * 15.f;
        top.y = g0;
        float L = 72.f;
        vec3 foot = c + dir * (15.f + L);
        while (L > 30.f && roadDistAt(foot.x, foot.z) < 12.f) { L -= 6.f; foot = c + dir * (15.f + L); }
        foot.y = heightAt(foot.x, foot.z);
        carveLine(top, foot, 3.2f, 7.f);
        levelDisc(foot + dir * 6.f, 8.f, 16.f, foot.y);
        int steps = std::clamp(int(std::fabs(top.y - foot.y) / 0.17f), 14, 260);
        vec3 rt = normalize(cross(dir, up));
        for (int k = 0; k < steps; k++) {
            float u0 = k / float(steps), u1 = (k + 1) / float(steps);
            vec3 a = lerp(top, foot, u0), e = lerp(top, foot, u1);
            float yTop = std::max(a.y, e.y);
            vec3 m = (a + e) * 0.5f;
            mat4 sx = basis(rt, up, dir, vec3(m.x, yTop, m.z));
            float run = length(vec3(e.x - a.x, 0, e.z - a.z)) * 0.5f + 0.02f;
            float tone = 0.9f + 0.1f * ((k * 37) % 7) / 6.f;
            b.box(vec3(0, -0.35f, 0), vec3(2.3f, 0.35f, run), vec4(0.6f * tone, 0.59f * tone, 0.55f * tone, 0), &sx);
        }
        for (int k = -1; k <= 1; k += 2) {  // stone kerbs either side
            vec3 a = top + rt * (k * 2.5f), e = foot + rt * (k * 2.5f);
            b.tube(a + up * 0.15f, e + up * 0.15f, 0.22f, 0.22f, 4, stone);
        }
        for (float u = 0.08f; u < 0.95f; u += 0.14f) {
            vec3 q = lerp(top, foot, u);
            for (int k = -1; k <= 1; k += 2) {
                toro(q + rt * (k * 3.4f));
                sakuraSpots.push_back(q + rt * (k * 7.5f) + dir * 3.f);
            }
        }
        niomon(foot + dir * 4.f, -dir);
        for (int k = -1; k <= 1; k += 2) toro(foot + dir * 9.f + rt * (k * 4.2f));
        // visitors in kimono, a couple with parasols
        Rng pr(321);
        for (int k = 0; k < 7; k++) {
            float u = pr.range(0.1f, 0.9f);
            person(lerp(top, foot, u) + rt * pr.range(-1.4f, 1.4f), std::atan2(-dir.x, -dir.z), k, k % 2 == 0);
        }
        person(foot + dir * 7.f + rt * 1.2f, std::atan2(-dir.x, -dir.z), 1, true);
        person(foot + dir * 7.2f - rt * 1.3f, std::atan2(-dir.x, -dir.z), 0, false);
        for (float u = 0.f; u <= 1.f; u += 0.04f) canalPts.push_back(lerp(top, foot, u));  // no grass on the stair
        for (int k = -2; k <= 2; k++) canalPts.push_back(foot + dir * 4.f + rt * (k * 2.5f));
        clearings.push_back(vec3(foot.x, 22.f, foot.z));
        clearings.push_back(vec3((top.x + foot.x) * 0.5f, 18.f, (top.z + foot.z) * 0.5f));
    }

    // ---- a canal lined with cherry trees (Kyoto's Fushimi style): stone walls, a footpath, a red
    // arched bridge and two covered riverboats. It goes on the flattest stretch of open lowland near
    // the town that no road crosses.
    for (int once = 0; once < 1; once++) {
        const float W = 4.6f, LEN = 360.f;
        float bestCost = 1e9f;
        vec3 bestA, bestD;
        for (float cx = -3200.f; cx <= 2600.f; cx += 80.f)
            for (float cz = -2100.f; cz <= 2300.f; cz += 80.f) {
                if (cx < -1250.f && cz < 520.f && cx > -3350.f && cz > -1450.f) continue;  // rice paddy country
                for (int ai = 0; ai < 8; ai++) {
                    float ang = ai / 8.f * PI;
                    vec3 d(std::cos(ang), 0, std::sin(ang)), a0 = vec3(cx, 0, cz) - d * (LEN * 0.5f);
                    float lo = 1e9f, hi = -1e9f;
                    bool ok = true;
                    for (int k = 0; k <= 30 && ok; k++) {
                        vec3 q = a0 + d * (LEN * k / 30.f);
                        float hh = heightAt(q.x, q.z);
                        lo = std::min(lo, hh); hi = std::max(hi, hh);
                        ok = hh > waterLevel + 4.f && hh < 140.f && roadDistAt(q.x, q.z) > 18.f && !urban(q.x, q.z, 40.f) && !inBox(q.x, q.z, 12.f) &&
                             std::hypot(q.x - festCenter.x, q.z - festCenter.z) > 320.f && std::hypot(q.x - hubPos.x, q.z - hubPos.z) > 150.f;
                        for (auto& cg : clearings) ok = ok && std::hypot(q.x - cg.x, q.z - cg.z) > cg.y + 10.f;
                    }
                    if (!ok) continue;
                    float cost = (hi - lo) + std::hypot(cx + 1650.f, cz - 650.f) / 500.f;
                    if (cost < bestCost) { bestCost = cost; bestA = a0; bestD = d; }
                }
            }
        if (bestCost > 1e8f) { SDL_Log("Japan: no room for the canal"); break; }
        std::vector<vec3> cl;
        std::vector<float> bank;
        float lo = 1e9f;
        for (int k = 0; k <= 30; k++) {
            vec3 q = bestA + bestD * (LEN * k / 30.f);
            // a gentle meander
            q = q + vec3(-bestD.z, 0, bestD.x) * (std::sin(k * 0.35f) * 6.f);
            cl.push_back(q);
            lo = std::min(lo, heightAt(q.x, q.z));
        }
        for (size_t k = 0; k < cl.size(); k++) bank.push_back(lo + 0.6f);
        float sg = 1.f;
        int m = int(cl.size());
        if (m < 20) { SDL_Log("Japan: no room for the canal"); break; }
        // water steps up in pools of ~60 m
        std::vector<float> water(m), bed(m);
        for (int j0 = 0; j0 < m; j0 += 15) {
            int j1 = std::min(m, j0 + 15);
            float mn = 1e9f;
            for (int j = j0; j < j1; j++) mn = std::min(mn, bank[j]);
            for (int j = j0; j < j1; j++) { water[j] = mn - 0.85f; bed[j] = mn - 1.9f; }
        }
        std::vector<vec3> nrm(m);
        for (int j = 0; j < m; j++) {
            vec3 d = cl[std::min(m - 1, j + 1)] - cl[std::max(0, j - 1)];
            d.y = 0;
            d = normalize(d);
            nrm[j] = vec3(-d.z, 0, d.x);
        }
        // carve the channel and level the banks
        {
            float x0f = 1e9f, x1f = -1e9f, z0f = 1e9f, z1f = -1e9f;
            for (auto& q : cl) { x0f = std::min(x0f, q.x); x1f = std::max(x1f, q.x); z0f = std::min(z0f, q.z); z1f = std::max(z1f, q.z); }
            const float R = 30.f;
            int ix0 = std::max(0, int((x0f - R + HALF) / CELL)), ix1 = std::min(N - 1, int((x1f + R + HALF) / CELL) + 1);
            int iz0 = std::max(0, int((z0f - R + HALF) / CELL)), iz1 = std::min(N - 1, int((z1f + R + HALF) / CELL) + 1);
            for (int iz = iz0; iz <= iz1; iz++)
                for (int ix = ix0; ix <= ix1; ix++) {
                    if (roadDist[iz * N + ix] < 3.f) continue;
                    float wx = -HALF + ix * CELL, wz = -HALF + iz * CELL;
                    float best = 1e9f, bk = 0, bd = 0, endK = 1.f;
                    for (int j = 0; j + 1 < m; j++) {
                        vec3 a = cl[j], e = cl[j + 1];
                        vec2 ab(e.x - a.x, e.z - a.z), ap(wx - a.x, wz - a.z);
                        float uu = dot(ap, ab) / std::max(dot(ab, ab), 1e-6f), u = clampf(uu, 0.f, 1.f);
                        float dd = length(vec2(a.x + ab.x * u - wx, a.z + ab.y * u - wz));
                        if (dd < best) {
                            best = dd; bk = lerpf(bank[j], bank[j + 1], u); bd = u < 0.5f ? bed[j] : bed[j + 1];
                            endK = (j == 0 && uu < 0.f) || (j + 2 == m && uu > 1.f) ? 0.f : 1.f;
                        }
                    }
                    if (best > R) continue;
                    float& h = height[iz * N + ix];
                    if (best < W + 1.6f && endK > 0.f) h = bd;  // the walls hide the step; a capstone strip covers the top
                    else {
                        float k = 1.f - smoothstepf(14.f, R, best);
                        h = lerpf(h, bk, k);
                    }
                }
        }
        vec4 stone(0.56f, 0.55f, 0.5f, 0), coping(0.66f, 0.65f, 0.6f, 0), foam(0.9f, 0.94f, 0.96f, 0);
        for (int j = 0; j + 1 < m; j++) {
            vec3 a = cl[j], e = cl[j + 1];
            // water surface
            vec3 wa(a.x, water[j], a.z), we(e.x, water[j], e.z);
            waterQuads.push_back(wa - nrm[j] * (W + 0.1f)); waterQuads.push_back(we - nrm[j + 1] * (W + 0.1f));
            waterQuads.push_back(we + nrm[j + 1] * (W + 0.1f)); waterQuads.push_back(wa + nrm[j] * (W + 0.1f));
            if (water[j + 1] != water[j]) {  // a little weir where the next pool starts
                float lo = std::min(water[j], water[j + 1]), hi = std::max(water[j], water[j + 1]);
                vec3 l0 = e - nrm[j + 1] * W, l1 = e + nrm[j + 1] * W;
                quad(vec3(l0.x, lo - 0.05f, l0.z), vec3(l1.x, lo - 0.05f, l1.z), vec3(l1.x, hi, l1.z), vec3(l0.x, hi, l0.z), water[j] < water[j + 1] ? -(e - a) : (e - a), foam);
                b.box(vec3(e.x, hi - 0.1f, e.z), vec3(0.3f, 0.12f, 0.3f), stone);
            }
            // stone walls and coping on both sides
            for (int k = -1; k <= 1; k += 2) {
                vec3 pa = a + nrm[j] * (k * W), pe = e + nrm[j + 1] * (k * W);
                float bl = std::min(bed[j], bed[j + 1]) - 0.2f;
                quad(vec3(pa.x, bl, pa.z), vec3(pe.x, bl, pe.z), vec3(pe.x, bank[j + 1] + 0.05f, pe.z), vec3(pa.x, bank[j] + 0.05f, pa.z), nrm[j] * (float)-k, stone);
                b.tube(vec3(pa.x, bank[j] + 0.1f, pa.z) + nrm[j] * (k * 0.2f), vec3(pe.x, bank[j + 1] + 0.1f, pe.z) + nrm[j + 1] * (k * 0.2f), 0.2f, 0.2f, 4, coping);
                vec3 qa = pa + nrm[j] * (k * 2.2f), qe = pe + nrm[j + 1] * (k * 2.2f);
                quad(vec3(pa.x, bank[j] + 0.06f, pa.z), vec3(pe.x, bank[j + 1] + 0.06f, pe.z), vec3(qe.x, bank[j + 1] + 0.06f, qe.z), vec3(qa.x, bank[j] + 0.06f, qa.z), up, coping);
            }
            if (j % 2 == 0)
                for (int k = -1; k <= 1; k += 2) sakuraSpots.push_back(a + nrm[j] * (k * (W + 4.f + (j % 4 ? 1.5f : 0.f))));
            if (j % 3 == 0) clearings.push_back(vec3(a.x, 17.f, a.z));
            canalPts.push_back(a);
        }
        canalPts.push_back(cl[m - 1]);
        // end walls
        for (int j : {0, m - 1}) {
            vec3 a = cl[j], d = vec3(nrm[j].z, 0, -nrm[j].x);
            mat4 ex = basis(nrm[j], up, d, vec3(a.x, (bed[j] + bank[j]) * 0.5f, a.z));
            b.box(vec3(0, 0, 0), vec3(W + 0.3f, (bank[j] - bed[j]) * 0.5f + 0.2f, 0.3f), stone, &ex);
        }
        // footpath along the town-side bank
        for (int j = 0; j + 4 < m; j += 4)
            pavedStrips.push_back({cl[j] - nrm[j] * (sg * -1.f) * 0.f + nrm[j] * (-sg * (W + 7.5f)), cl[j + 4] + nrm[j + 4] * (-sg * (W + 7.5f)), 1.3f});
        // red arched bridge (taikobashi) halfway along
        {
            int j = m / 2;
            vec3 a = cl[j], d = normalize(vec3(nrm[j].z, 0, -nrm[j].x));
            vec4 red(0.84f, 0.2f, 0.09f, 0), wood(0.42f, 0.3f, 0.2f, 0);
            const int K = 14;
            vec3 prevL, prevR;
            for (int k = 0; k <= K; k++) {
                float u = k / float(K), x = (u - 0.5f) * 2.f * (W + 2.5f);
                float yy = bank[j] + 0.15f + 1.5f * std::sin(u * PI);
                vec3 q = vec3(a.x, yy, a.z) + nrm[j] * x;
                mat4 px = basis(nrm[j], up, d, q);
                if (k < K) {
                    float ny = bank[j] + 0.15f + 1.5f * std::sin((k + 1) / float(K) * PI);
                    mat4 tx = px * rotateZ(std::atan2(ny - yy, 2.f * (W + 2.5f) / K));
                    b.box(vec3((W + 2.5f) / K, 0, 0), vec3((W + 2.5f) / K + 0.05f, 0.08f, 1.3f), wood, &tx);
                }
                vec3 L = q - d * 1.3f + up * 0.9f, Rr = q + d * 1.3f + up * 0.9f;
                if (k % 2 == 0) { b.tube(q - d * 1.3f, L, 0.06f, 0.06f, 5, red); b.tube(q + d * 1.3f, Rr, 0.06f, 0.06f, 5, red); }
                if (k > 0) { b.tube(prevL, L, 0.05f, 0.05f, 5, red); b.tube(prevR, Rr, 0.05f, 0.05f, 5, red); }
                prevL = L; prevR = Rr;
            }
        }
        // two covered riverboats moored in the canal
        for (float fu : {0.27f, 0.72f}) {
            int j = int(fu * (m - 1));
            vec3 a = cl[j], d = normalize(vec3(nrm[j].z, 0, -nrm[j].x));
            vec3 q(a.x, water[j] - 0.15f, a.z);
            q = q + nrm[j] * 0.8f;
            mat4 bx = basis(nrm[j], up, d, q);
            vec4 hull(0.36f, 0.25f, 0.16f, 0), deck(0.5f, 0.38f, 0.26f, 0), roofC(0.34f, 0.27f, 0.2f, 0), slat(0.22f, 0.17f, 0.12f, 0);
            b.box(vec3(0, 0.15f, 0), vec3(1.0f, 0.38f, 4.3f), hull, &bx);
            b.box(vec3(0, 0.55f, 0), vec3(0.95f, 0.04f, 4.2f), deck, &bx);
            // pointed bow
            vec3 b0 = bx.transformPoint(vec3(-1.0f, 0.53f, 4.3f)), b1 = bx.transformPoint(vec3(1.0f, 0.53f, 4.3f)), bt = bx.transformPoint(vec3(0, 0.62f, 5.6f));
            vec3 b2 = bx.transformPoint(vec3(-1.0f, -0.23f, 4.3f)), b3 = bx.transformPoint(vec3(1.0f, -0.23f, 4.3f)), bb = bx.transformPoint(vec3(0, -0.1f, 5.4f));
            tri(b0, b1, bt, up, deck);
            quad(b0, bt, bb, b2, -nrm[j] + d, hull);
            quad(b1, b3, bb, bt, nrm[j] + d, hull);
            // low cabin roof on posts with slats, an opening at the stern
            for (float zz : {-2.6f, 0.f, 2.6f})
                for (int k = -1; k <= 1; k += 2) b.box(vec3(k * 0.85f, 1.25f, zz), vec3(0.05f, 0.68f, 0.05f), slat, &bx);
            mat4 r1 = bx * translate(vec3(-0.5f, 2.02f, 0)) * rotateZ(0.32f), r2 = bx * translate(vec3(0.5f, 2.02f, 0)) * rotateZ(-0.32f);
            b.box(vec3(0, 0, 0), vec3(0.62f, 0.04f, 3.0f), roofC, &r1);
            b.box(vec3(0, 0, 0), vec3(0.62f, 0.04f, 3.0f), roofC, &r2);
            for (int k = -5; k <= 5; k++) {
                b.box(vec3(0, 0.05f, k * 0.55f), vec3(0.62f, 0.03f, 0.04f), slat, &r1);
                b.box(vec3(0, 0.05f, k * 0.55f), vec3(0.62f, 0.03f, 0.04f), slat, &r2);
            }
            b.box(vec3(0, 2.25f, 0), vec3(0.08f, 0.06f, 3.05f), slat, &bx);  // ridge
            for (int k = -1; k <= 1; k += 2) b.box(vec3(k * 0.95f, 0.9f, 0), vec3(0.03f, 0.3f, 2.7f), vec4(0.85f, 0.82f, 0.74f, 0), &bx);  // shoji screens
            b.box(vec3(0, 1.05f, -3.6f), vec3(0.1f, 0.5f, 0.1f), slat, &bx);  // boatman's pole stand
            b.tube(bx.transformPoint(vec3(0.3f, 0.6f, -3.8f)), bx.transformPoint(vec3(0.6f, 4.2f, -4.6f)), 0.035f, 0.035f, 4, vec4(0.55f, 0.45f, 0.3f, 0));
            person(bx.transformPoint(vec3(0, 0.58f, -3.9f)) + up * 0.0f, std::atan2(d.x, d.z), 2, false);
        }
        SDL_Log("Japan: canal, %d sections, middle (%.0f, %.0f) side %.0f", m, cl[m / 2].x, cl[m / 2].z, sg);
        break;
    }

    // ---- torii standing in the water: out in the lake below the pagoda, and off the west coast
    auto waterTorii = [&](vec3 c, vec3 fwd, float half, float H) {
        vec3 side = normalize(cross(fwd, up));
        vec4 red(0.82f, 0.17f, 0.08f, 0), black(0.06f, 0.06f, 0.07f, 0);
        float top = waterLevel + H;
        for (int k = -1; k <= 1; k += 2) {
            vec3 q = c + side * (k * half);
            float bedY = heightAt(q.x, q.z) - 0.6f;
            q.y = bedY;
            b.cylinder(q, 0.55f, top - bedY + 0.3f, 12, red);
            b.cylinder(vec3(q.x, waterLevel - 0.3f, q.z), 0.68f, 0.5f, 12, black);
            pendingTrunks.push_back({q.x, q.z, 0.65f});
        }
        mat4 xf = basis(side, up, fwd, vec3(c.x, top, c.z));
        b.box(vec3(0, -1.9f, 0), vec3(half + 1.1f, 0.34f, 0.36f), red, &xf);
        b.box(vec3(0, 0.05f, 0), vec3(half + 1.5f, 0.4f, 0.5f), red, &xf);
        b.box(vec3(0, 0.62f, 0), vec3(half + 2.1f, 0.26f, 0.66f), black, &xf);
        for (int k = -1; k <= 1; k += 2) {
            mat4 tip = xf * translate(vec3(k * (half + 2.2f), 0.85f, 0)) * rotateZ(k * -0.28f);
            b.box(vec3(0, 0, 0), vec3(0.75f, 0.2f, 0.65f), black, &tip);
        }
        b.box(vec3(0, -0.95f, 0), vec3(0.42f, 0.62f, 0.22f), black, &xf);
        for (int k = -1; k <= 1; k += 2)
            signFitted(b, xf.transformPoint(vec3(0, -0.95f, k * 0.23f)), fwd * (float)k, 0.3f, 0.58f, SV_SENGEN, true);
    };
    {
        const Lake& lk = lakes.back();  // the lake below the pagoda
        vec3 toward = normalize(vec3(-1730.f - lk.c.x, 0, 640.f - lk.c.z));
        vec3 shore = lk.c;
        for (float rr = 0; rr < lk.r * 1.6f; rr += 2.f) {
            vec3 q = lk.c + toward * rr;
            if (heightAt(q.x, q.z) > waterLevel) { shore = q; break; }
        }
        vec3 tp = shore - toward * 42.f;
        waterTorii(tp, toward, 4.2f, 11.f);
        for (int k = -1; k <= 1; k += 2) toro(shore + toward * 6.f + normalize(cross(toward, up)) * (k * 4.f));
        sakuraSpots.push_back(shore + toward * 12.f + normalize(cross(toward, up)) * 9.f);
        sakuraSpots.push_back(shore + toward * 12.f - normalize(cross(toward, up)) * 9.f);
        SDL_Log("Japan: lake torii at (%.0f, %.0f)", tp.x, tp.z);
    }
    {
        for (float z : {300.f, -300.f}) {
            vec3 q(-3250.f, 0, z), found(0, -1e9f, 0);
            for (float x = -3250.f; x > -3900.f; x -= 3.f)
                if (heightAt(x, z) < waterLevel - 1.0f) { found = vec3(x - 25.f, 0, z); break; }
            if (found.y < -1e8f) continue;
            found.y = 0;
            waterTorii(found, vec3(1, 0, 0), 4.6f, 12.f);
            SDL_Log("Japan: sea torii at (%.0f, %.0f)", found.x, found.z);
            break;
        }
    }

    // people in kimono strolling on Honcho-dori's pavements
    for (auto& rd : roads) {
        if (rd.name != "Fuji Road") continue;
        Rng pr(555);
        int n = int(rd.pts.size());
        for (int k = 0; k < 14; k++) {
            int i = pr.irange(0, n - 1);
            if (rd.pts[i].z < 480.f || rd.pts[i].z > 950.f) { k--; if (pr.f() < 0.02f) break; continue; }
            vec3 t = rd.tan[i], side(-t.z, 0, t.x);
            float s2 = pr.f() < 0.5f ? -1.f : 1.f;
            vec3 q = rd.pts[i] + side * (s2 * (rd.width * 0.5f + pr.range(1.4f, 2.4f)));
            if (inBox(q.x, q.z, 0.3f)) continue;
            person(q, std::atan2(t.x, t.z) + (pr.f() < 0.5f ? 0.f : PI), pr.irange(0, 6), pr.f() < 0.35f);
        }
        break;
    }
    SDL_Log("Japan: %d people", people);
    rebuildBoxGrid();
    SDL_Log("Japan: %d buildings, %d utility poles", houses, poles);
}

// rice paddies: flat low ground in the western countryside (the terrain shader draws the plots)
float World::paddyAt(float x, float z) const {
    if (x > -1250.f || x < -3350.f || z < -1450.f || z > 470.f) return 0.f;
    float h = heightAt(x, z);
    if (h > 150.f || h < waterLevel + 1.f) return 0.f;
    if (normalAt(x, z).y < 0.985f || roadDistAt(x, z) < 6.f) return 0.f;
    float edge = std::min(std::min(x + 3350.f, -1250.f - x), std::min(z + 1450.f, 470.f - z));
    return smoothstepf(0.f, 60.f, edge);
}
