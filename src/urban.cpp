// Tokyo and Daikoku Wharf: procedural buildings, signs, street lights,
// container stacks, gantry cranes, warehouses and a container ship.
// Material ids (object shader): 0 plain, 7 steel, 14 facade, 15 neon/lamp, 16 corrugated, 17 glass tower.
#include "world.h"
#include "signs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace {
const vec4 kConcrete[] = {{0.62f, 0.60f, 0.56f, 14}, {0.55f, 0.52f, 0.48f, 14}, {0.70f, 0.66f, 0.58f, 14},
                          {0.48f, 0.30f, 0.24f, 14}, {0.58f, 0.36f, 0.27f, 14}, {0.40f, 0.42f, 0.44f, 14}, {0.78f, 0.76f, 0.70f, 14}};
const vec4 kGlass[] = {{0.30f, 0.45f, 0.58f, 17}, {0.22f, 0.36f, 0.40f, 17}, {0.42f, 0.38f, 0.30f, 17}, {0.30f, 0.33f, 0.40f, 17}};
const vec4 kNeon[] = {{1.0f, 0.18f, 0.55f, 15}, {0.15f, 0.85f, 1.0f, 15}, {1.0f, 0.85f, 0.2f, 15}, {0.35f, 1.0f, 0.4f, 15},
                      {1.0f, 0.3f, 0.15f, 15}, {0.7f, 0.35f, 1.0f, 15}};
const vec4 kBox[] = {{0.62f, 0.13f, 0.10f, 16}, {0.10f, 0.28f, 0.52f, 16}, {0.14f, 0.40f, 0.24f, 16}, {0.82f, 0.46f, 0.10f, 16},
                     {0.55f, 0.56f, 0.57f, 16}, {0.75f, 0.68f, 0.18f, 16}, {0.20f, 0.21f, 0.26f, 16}, {0.85f, 0.85f, 0.82f, 16}};
}  // namespace

void World::buildUrban(uint32_t seed) {
    Rng rng(seed * 13 + 5);
    city = MeshBuilder();
    cityGl = MeshBuilder();
    boxes.clear();
    buildEstates();
    auto solid = [&](float x0, float z0, float x1, float z1, float top) { boxes.push_back({x0, z0, x1, z1, top}); };
    auto block = [&](MeshBuilder& b, vec3 lo, vec3 hi, vec4 col) {
        b.box((lo + hi) * 0.5f, (hi - lo) * 0.5f, col);
    };

    // ================================================================ Tokyo
    // A small Tokyo neighbourhood: narrow low buildings shoulder to shoulder along each street
    // frontage, a back alley behind, signs stacked up the shop fronts, Tokyo Tower over the rooftops.
    const float xs[] = {1800, 1950, 2100, 2250, 2400, 2550, 2700};
    const float zs[] = {-850, -700, -550, -400, -250, -100, 50, 200};
    const vec3 downtown(2240, 0, -260);
    const vec4 kTile[] = {{0.86f, 0.84f, 0.78f, 14}, {0.74f, 0.72f, 0.68f, 14}, {0.56f, 0.55f, 0.53f, 14}, {0.66f, 0.58f, 0.50f, 14},
                          {0.44f, 0.46f, 0.50f, 14}, {0.80f, 0.78f, 0.72f, 14}, {0.52f, 0.40f, 0.34f, 14}};
    int towers = 0, signs = 0;
    auto clearOfRoads = [&](float x0, float z0, float x1, float z1) {
        for (float x = x0; x <= x1 + 0.1f; x += std::max(2.f, (x1 - x0) / 4.f))
            for (float z = z0; z <= z1 + 0.1f; z += std::max(2.f, (z1 - z0) / 4.f))
                if (roadDistAt(x, z) < 1.0f) return false;
        return true;
    };
    // a vertical sign blade: lettering on both faces (n and -n), a tall one stacked from several cells
    const vec4 kSignFrame(0.07f, 0.07f, 0.08f, 0);
    auto vsign = [&](vec3 c, vec3 n, float off, float hh, vec4 nc) {
        (void)nc;
        bool neon = rng.f() < 0.6f;
        int base = neon ? SV_NEON0 : SV_SHOP0, cnt = neon ? SV_NEON_N : SV_SHOP_N;
        int cell = base + rng.irange(0, cnt - 1);
        const float hw = 0.62f;
        int segs = std::max(1, int(hh * 2.f / (hw * 2.f * 4.f) + 0.5f));
        float sh = hh / segs;
        for (int k = 0; k < segs; k++) {
            vec3 ck = c + vec3(0, -hh + sh * (2 * k + 1), 0);
            int ce = base + (cell - base + k) % cnt;
            signFace(city, ck + n * off, n, hw, sh * 0.98f, ce, true, neon ? 1.f : 0.3f);
            signFace(city, ck - n * off, -n, hw, sh * 0.98f, ce, true, neon ? 1.f : 0.3f);
        }
    };
    // a building: shaft, parapet, rooftop clutter, a lit shop floor and signs on its street face
    // face: 0 = -z, 1 = +z, 2 = -x, 3 = +x (the side that faces the street)
    auto building = [&](float x0, float z0, float x1, float z1, float H, int face, float down) {
        if (x1 - x0 < 6.f || z1 - z0 < 6.f || !clearOfRoads(x0, z0, x1, z1)) return;
        float g = std::min(std::min(heightAt(x0, z0), heightAt(x1, z1)), std::min(heightAt(x0, z1), heightAt(x1, z0))) - 0.3f;
        bool glassy = H > 22.f && rng.f() < 0.25f;
        vec4 col = glassy ? kGlass[rng.irange(0, 3)] : (rng.f() < 0.6f ? kTile[rng.irange(0, 6)] : kConcrete[rng.irange(0, 6)]);
        vec3 lo(x0, g, z0), hi(x1, g + H, z1);
        block(city, lo, hi, col);
        solid(x0, z0, x1, z1, hi.y);
        if (H > 70.f) {
            towers++;
            float in = std::min(x1 - x0, z1 - z0) * 0.16f;
            vec3 l2(x0 + in, hi.y, z0 + in), h2(x1 - in, hi.y + H * rng.range(0.08f, 0.22f), z1 - in);
            block(city, l2, h2, col);
            vec3 c = (l2 + h2) * 0.5f;
            block(city, vec3(c.x - 0.4f, h2.y, c.z - 0.4f), vec3(c.x + 0.4f, h2.y + 16.f, c.z + 0.4f), vec4(0.6f, 0.6f, 0.6f, 7));
            block(city, vec3(c.x - 0.6f, h2.y + 16.f, c.z - 0.6f), vec3(c.x + 0.6f, h2.y + 17.2f, c.z + 0.6f), vec4(1.f, 0.15f, 0.1f, 15));
        } else {
            vec4 dark(0.30f, 0.30f, 0.30f, 0);
            block(city, vec3(x0, hi.y, z0), vec3(x1, hi.y + 1.1f, z0 + 0.35f), dark);
            block(city, vec3(x0, hi.y, z1 - 0.35f), vec3(x1, hi.y + 1.1f, z1), dark);
            if (x1 - x0 > 10.f && z1 - z0 > 10.f && rng.f() < 0.7f) {
                float ax = rng.range(x0 + 2, x1 - 6), az = rng.range(z0 + 2, z1 - 6);
                block(city, vec3(ax, hi.y, az), vec3(ax + rng.range(2, 4), hi.y + rng.range(1.5f, 3.f), az + rng.range(2, 4)), vec4(0.55f, 0.56f, 0.56f, 7));
            }
            // rooftop billboard on some of the mid-rise ones
            if (H > 20.f && rng.f() < 0.25f) {
                vec4 nc = kNeon[rng.irange(0, 5)];
                (void)nc;
                // lettered neon billboard on a steel frame, readable from both sides
                bool neonB = rng.f() < 0.65f;
                int cell = neonB ? SH_NEON0 + rng.irange(0, SH_NEON_N - 1) : SH_SHOP0 + rng.irange(0, SH_SHOP_N - 1);
                vec4 board = signBoard(cell, false);
                if (face < 2) {
                    float z = face == 0 ? z0 + 0.6f : z1 - 0.6f, bh = std::min(8.f, (x1 - x0) * 0.4f);
                    block(city, vec3(x0 + 1, hi.y + 1.5f, z - 0.2f), vec3(x1 - 1, hi.y + 1.5f + bh, z + 0.2f), board);
                    vec3 c((x0 + x1) * 0.5f, hi.y + 1.5f + bh * 0.5f, z);
                    signFitted(city, c + vec3(0, 0, -0.215f), vec3(0, 0, -1), (x1 - x0 - 2) * 0.48f, bh * 0.48f, cell, false, neonB ? 1.f : 0.3f);
                    signFitted(city, c + vec3(0, 0, 0.215f), vec3(0, 0, 1), (x1 - x0 - 2) * 0.48f, bh * 0.48f, cell, false, neonB ? 1.f : 0.3f);
                } else {
                    float x = face == 2 ? x0 + 0.6f : x1 - 0.6f, bh = std::min(8.f, (z1 - z0) * 0.4f);
                    block(city, vec3(x - 0.2f, hi.y + 1.5f, z0 + 1), vec3(x + 0.2f, hi.y + 1.5f + bh, z1 - 1), board);
                    vec3 c(x, hi.y + 1.5f + bh * 0.5f, (z0 + z1) * 0.5f);
                    signFitted(city, c + vec3(-0.215f, 0, 0), vec3(-1, 0, 0), (z1 - z0 - 2) * 0.48f, bh * 0.48f, cell, false, neonB ? 1.f : 0.3f);
                    signFitted(city, c + vec3(0.215f, 0, 0), vec3(1, 0, 0), (z1 - z0 - 2) * 0.48f, bh * 0.48f, cell, false, neonB ? 1.f : 0.3f);
                }
                signs++;
            }
        }
        // glowing shop floor and a stack of vertical signs on the street face
        vec4 shop(1.0f, 0.86f, 0.62f, 20);
        int ns = H > 14.f ? rng.irange(1, 3) : 1;
        for (int k = 0; k < ns; k++) {
            vec4 nc = kNeon[rng.irange(0, 5)];
            float sh = rng.range(5.f, std::min(16.f, H * 0.7f + 4.f)), y0 = g + 3.6f + rng.range(0.f, 2.f);
            if (face < 2) {
                float z = face == 0 ? z0 : z1, out = face == 0 ? -1.f : 1.f;
                if (k == 0) block(city, vec3(x0 + 0.5f, g + 0.4f, std::min(z, z + out * 0.12f)), vec3(x1 - 0.5f, g + 3.2f, std::max(z, z + out * 0.12f)), shop);
                float x = rng.range(x0 + 1.f, x1 - 1.f);
                block(city, vec3(x - 0.12f, y0, std::min(z + out * 0.2f, z + out * 1.5f)), vec3(x + 0.12f, y0 + sh, std::max(z + out * 0.2f, z + out * 1.5f)), kSignFrame);
                vsign(vec3(x, y0 + sh * 0.5f, z + out * 0.85f), vec3(1, 0, 0), 0.125f, sh * 0.5f, nc);
            } else {
                float x = face == 2 ? x0 : x1, out = face == 2 ? -1.f : 1.f;
                if (k == 0) block(city, vec3(std::min(x, x + out * 0.12f), g + 0.4f, z0 + 0.5f), vec3(std::max(x, x + out * 0.12f), g + 3.2f, z1 - 0.5f), shop);
                float z = rng.range(z0 + 1.f, z1 - 1.f);
                block(city, vec3(std::min(x + out * 0.2f, x + out * 1.5f), y0, z - 0.12f), vec3(std::max(x + out * 0.2f, x + out * 1.5f), y0 + sh, z + 0.12f), kSignFrame);
                vsign(vec3(x + out * 0.85f, y0 + sh * 0.5f, z), vec3(0, 0, 1), 0.125f, sh * 0.5f, nc);
            }
            signs++;
        }
    };
    auto heightFor = [&](float cx, float cz, float& down) {
        float dd = std::hypot(cx - downtown.x, cz - downtown.z);
        down = std::exp(-dd * dd / (380.f * 380.f));
        // a small Tokyo neighbourhood: two to six storeys, a little taller round the station
        float H = 6.5f + 7.f * rng.f() * rng.f() + down * (5.f + 14.f * rng.f() * rng.f());
        if (rng.f() < 0.08f) H += 7.f;  // the odd narrow pencil building
        return H;
    };
    for (int bx = 0; bx < 6; bx++)
        for (int bz = 0; bz < 7; bz++) {
            float x0 = xs[bx] + 10.f, x1 = xs[bx + 1] - 10.f, z0 = zs[bz] + 10.f, z1 = zs[bz + 1] - 10.f;
            if (bx == 0 && bz == 0) continue;  // Tokyo Tower's park
            float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f, down;
            heightFor(cx, cz, down);
            // perimeter lots along the four frontages, an alley behind them
            float dS = rng.range(24.f, 36.f), dN = rng.range(24.f, 36.f), dW = rng.range(22.f, 32.f), dE = rng.range(22.f, 32.f);
            for (int side = 0; side < 2; side++) {
                float za = side == 0 ? z0 : z1 - dN, zb = side == 0 ? z0 + dS : z1;
                for (float x = x0; x < x1 - 4.f;) {
                    float w = std::min(rng.range(10.f, 26.f), x1 - x);
                    if (x1 - (x + w) < 8.f) w = x1 - x;
                    float H = heightFor(x + w * 0.5f, (za + zb) * 0.5f, down);
                    building(x + 0.4f, za, x + w - 0.4f, zb, H, side == 0 ? 0 : 1, down);
                    x += w;
                }
            }
            for (int side = 0; side < 2; side++) {
                float xa = side == 0 ? x0 : x1 - dE, xb = side == 0 ? x0 + dW : x1;
                float za = z0 + dS + 3.f, zb = z1 - dN - 3.f;
                for (float z = za; z < zb - 4.f;) {
                    float w = std::min(rng.range(10.f, 24.f), zb - z);
                    if (zb - (z + w) < 8.f) w = zb - z;
                    float H = heightFor((xa + xb) * 0.5f, z + w * 0.5f, down);
                    building(xa, z + 0.4f, xb, z + w - 0.4f, H, side == 0 ? 2 : 3, down);
                    z += w;
                }
            }
            // the middle of the block: low back-street buildings around a little courtyard
            float ix0 = x0 + dW + 4.f, ix1 = x1 - dE - 4.f, iz0 = z0 + dS + 4.f, iz1 = z1 - dN - 4.f;
            if (ix1 - ix0 > 20.f && iz1 - iz0 > 20.f) {
                float mx = (ix0 + ix1) * 0.5f;
                building(ix0, iz0, mx - 3.f, iz1, rng.range(7.f, 14.f), 0, 0.f);
                building(mx + 3.f, iz0, ix1, iz1, rng.range(7.f, 14.f), 1, 0.f);
            }
        }
    // ---- Tokyo Tower: a red and white lattice tower in its park
    {
        vec3 c(1875.f, 0, -775.f);
        float g = heightAt(c.x, c.z);
        const float Ht = 165.f;
        vec4 red(0.95f, 0.28f, 0.08f, 0), wht(0.95f, 0.95f, 0.92f, 0);
        auto half = [&](float y) { return 22.f * std::pow(1.f - y / Ht, 1.5f) + 1.2f; };
        for (int band = 0; band < 11; band++) {
            float y0 = band / 11.f * Ht, y1 = (band + 1) / 11.f * Ht;
            vec4 col = band % 2 ? wht : red;
            for (int k = 0; k < 4; k++) {
                float sx = (k & 1) ? 1.f : -1.f, sz = (k & 2) ? 1.f : -1.f;
                vec3 a(c.x + sx * half(y0), g + y0, c.z + sz * half(y0)), b(c.x + sx * half(y1), g + y1, c.z + sz * half(y1));
                city.tube(a, b, 0.9f * (1.f - y0 / Ht * 0.6f), 0.9f * (1.f - y1 / Ht * 0.6f), 6, col);
            }
            // cross bracing ring at the band's top
            float hy = half(y1);
            for (int k = 0; k < 4; k++) {
                vec3 p0(c.x + ((k == 0 || k == 3) ? -hy : hy), g + y1, c.z + (k < 2 ? -hy : hy));
                vec3 p1(c.x + ((k == 0 || k == 1) ? hy : -hy), g + y1, c.z + ((k == 0 || k == 3) ? -hy : hy));
                city.tube(p0, p1, 0.35f, 0.35f, 4, col);
            }
        }
        // observation decks
        block(city, vec3(c.x - 9, g + 58, c.z - 9), vec3(c.x + 9, g + 66, c.z + 9), vec4(0.85f, 0.85f, 0.82f, 0));
        block(city, vec3(c.x - 9.3f, g + 60, c.z - 9.3f), vec3(c.x + 9.3f, g + 63.5f, c.z + 9.3f), vec4(1.f, 0.82f, 0.55f, 20));
        block(city, vec3(c.x - 4.5f, g + 112, c.z - 4.5f), vec3(c.x + 4.5f, g + 117, c.z + 4.5f), vec4(1.f, 0.82f, 0.55f, 20));
        city.tube(vec3(c.x, g + Ht, c.z), vec3(c.x, g + Ht + 18.f, c.z), 0.8f, 0.3f, 6, wht);
        block(city, vec3(c.x - 0.7f, g + Ht + 18.f, c.z - 0.7f), vec3(c.x + 0.7f, g + Ht + 19.4f, c.z + 0.7f), vec4(1.f, 0.15f, 0.1f, 15));
        solid(c.x - 24, c.z - 24, c.x + 24, c.z + 24, g + 30.f);
    }
    // ---- a scramble crossing at Shibuya (Kabuki-dori x Akiba Street)
    {
        vec3 c(2250.f, 0, -100.f);
        float y = heightAt(c.x, c.z) + 0.09f;
        vec4 w(0.95f, 0.95f, 0.93f, 21);
        auto stripes = [&](vec3 a, vec3 b, float width) {
            vec3 d = b - a; float L = length(d); d = d / L;
            vec3 sd(-d.z, 0, d.x);
            for (float t = 0; t < L - 0.5f; t += 1.2f) {
                vec3 p0 = a + d * t, p1 = a + d * (t + 0.6f);
                p0.y = p1.y = y;
                city.flatQuad(p0 + sd * width, p1 + sd * width, p1 - sd * width, p0 - sd * width, w);
            }
        };
        stripes(c + vec3(-11, 0, -9), c + vec3(11, 0, -9), 1.6f);
        stripes(c + vec3(-11, 0, 9), c + vec3(11, 0, 9), 1.6f);
        stripes(c + vec3(-9, 0, -11), c + vec3(-9, 0, 11), 1.6f);
        stripes(c + vec3(9, 0, -11), c + vec3(9, 0, 11), 1.6f);
        stripes(c + vec3(-8, 0, -8), c + vec3(8, 0, 8), 1.4f);
        stripes(c + vec3(-8, 0, 8), c + vec3(8, 0, -8), 1.4f);
    }
    // street lights along every city street, both sides
    for (float x : xs)
        for (float z = -860; z < 330; z += 34) {
            for (int side = -1; side <= 1; side += 2) {
                float px = x + side * 7.6f, g = heightAt(px, z);
                if (std::fabs(z + 250) < 10) continue;
                block(city, vec3(px - 0.12f, g, z - 0.12f), vec3(px + 0.12f, g + 7.5f, z + 0.12f), vec4(0.25f, 0.26f, 0.27f, 7));
                block(city, vec3(std::min(px, px - side * 2.f), g + 7.3f, z - 0.1f), vec3(std::max(px, px - side * 2.f), g + 7.5f, z + 0.1f), vec4(0.25f, 0.26f, 0.27f, 7));
                block(city, vec3(px - side * 2.f - 0.35f, g + 7.1f, z - 0.2f), vec3(px - side * 2.f + 0.35f, g + 7.3f, z + 0.2f), vec4(1.0f, 0.85f, 0.6f, 15));
            }
        }
    for (float z : zs)
        for (float x = 1770; x < 2740; x += 34) {
            for (int side = -1; side <= 1; side += 2) {
                float pz = z + side * 7.6f, g = heightAt(x, pz);
                block(city, vec3(x - 0.12f, g, pz - 0.12f), vec3(x + 0.12f, g + 7.5f, pz + 0.12f), vec4(0.25f, 0.26f, 0.27f, 7));
                block(city, vec3(x - 0.1f, g + 7.3f, std::min(pz, pz - side * 2.f)), vec3(x + 0.1f, g + 7.5f, std::max(pz, pz - side * 2.f)), vec4(0.25f, 0.26f, 0.27f, 7));
                block(city, vec3(x - 0.2f, g + 7.1f, pz - side * 2.f - 0.35f), vec3(x + 0.2f, g + 7.3f, pz - side * 2.f + 0.35f), vec4(1.0f, 0.85f, 0.6f, 15));
            }
        }

    // ================================================================ docks
    const float quayZ = kDocks.z0;  // the quay wall (sea beyond)
    auto onRoad = [&](float x0, float z0, float x1, float z1) {
        for (float x = x0; x <= x1; x += 4)
            for (float z = z0; z <= z1; z += 4)
                if (roadDistAt(x, z) < 3.f) return true;
        return false;
    };
    // container stacks
    int containers = 0;
    for (float bx = 300; bx < 2150; bx += 92)
        for (float bz = -2290; bz < -1990; bz += 34) {
            float x0 = bx, x1 = bx + 12.2f * 6 + 2.5f, z0 = bz, z1 = bz + 2.5f * 8;
            if (onRoad(x0 - 3, z0 - 3, x1 + 3, z1 + 3) || rng.f() < 0.18f) continue;
            float g = heightAt((x0 + x1) * 0.5f, (z0 + z1) * 0.5f);
            float top = g;
            for (int i = 0; i < 6; i++)
                for (int j = 0; j < 8; j++) {
                    int stack = rng.irange(1, 4);
                    if (rng.f() < 0.12f) continue;
                    for (int k = 0; k < stack; k++) {
                        vec3 lo(x0 + i * 12.6f, g + k * 2.6f, z0 + j * 2.5f);
                        block(city, lo, lo + vec3(12.2f, 2.58f, 2.44f), kBox[rng.irange(0, 7)]);
                        top = std::max(top, lo.y + 2.6f);
                        containers++;
                    }
                }
            solid(x0, z0, x1, z1, top);
        }
    // gantry cranes on the quay
    for (float x = 420; x < 2150; x += 235) {
        float g = heightAt(x, quayZ + 20.f);
        vec4 orange(0.92f, 0.42f, 0.10f, 0), white(0.88f, 0.88f, 0.86f, 0);
        float zA = quayZ + 4.f, zB = quayZ + 34.f;
        for (float lx : {x - 9.f, x + 9.f})
            for (float lz : {zA, zB}) {
                block(city, vec3(lx - 0.8f, g, lz - 0.8f), vec3(lx + 0.8f, g + 46.f, lz + 0.8f), orange);
                solid(lx - 0.9f, lz - 0.9f, lx + 0.9f, lz + 0.9f, g + 46.f);
            }
        block(city, vec3(x - 10.f, g + 44.f, zA - 1.f), vec3(x + 10.f, g + 47.f, zB + 1.f), orange);
        block(city, vec3(x - 10.f, g + 18.f, zA - 0.6f), vec3(x + 10.f, g + 19.5f, zA + 0.6f), orange);
        block(city, vec3(x - 10.f, g + 18.f, zB - 0.6f), vec3(x + 10.f, g + 19.5f, zB + 0.6f), orange);
        // the boom reaches out over the water, the back-reach over the yard
        block(city, vec3(x - 2.f, g + 47.f, quayZ - 58.f), vec3(x + 2.f, g + 50.f, zB + 22.f), white);
        block(city, vec3(x - 3.f, g + 40.f, zA - 2.f), vec3(x + 3.f, g + 44.f, zA + 4.f), white);   // cab
        block(city, vec3(x - 1.f, g + 50.f, zA + 6.f), vec3(x + 1.f, g + 62.f, zA + 8.f), orange);    // A-frame
        block(city, vec3(x - 0.3f, g + 50.f, quayZ - 40.f), vec3(x + 0.3f, g + 61.f, quayZ - 39.f), white);
    }
    // quay edge, bollards
    for (float x = kDocks.x0 + 5; x < kDocks.x1 - 5; x += 18) {
        float g = heightAt(x, quayZ + 3.f);
        block(city, vec3(x - 0.35f, g, quayZ + 1.2f), vec3(x + 0.35f, g + 0.8f, quayZ + 1.9f), vec4(0.15f, 0.15f, 0.16f, 7));
    }
    block(city, vec3(kDocks.x0, waterLevel - 6.f, quayZ - 1.f), vec3(kDocks.x1, heightAt(kDocks.x0 + 50, quayZ + 4) + 0.05f, quayZ + 0.6f), vec4(0.5f, 0.49f, 0.47f, 14));
    // warehouses between the yard and the highway
    for (float x = 330; x < 2150; x += 150) {
        if (rng.f() < 0.3f) continue;
        float z0 = -1965.f, z1 = z0 + 42.f, x1 = x + rng.range(55, 95);
        if (onRoad(x - 3, -2000, x1 + 3, z1 + 3)) continue;
        float g = heightAt(x, z0) - 0.3f, H = rng.range(10, 16);
        vec4 c = kBox[rng.irange(0, 7)];
        c = vec4(c.x * 0.6f + 0.3f, c.y * 0.6f + 0.3f, c.z * 0.6f + 0.3f, 16);
        block(city, vec3(x, g, z0), vec3(x1, g + H, z1), c);
        block(city, vec3(x - 0.5f, g + H, z0 - 0.5f), vec3(x1 + 0.5f, g + H + 1.2f, z1 + 0.5f), vec4(0.35f, 0.36f, 0.37f, 7));
        solid(x, z0, x1, z1, g + H);
    }
    // a container ship alongside the quay
    {
        float sx0 = 820, sx1 = 1110, sz0 = quayZ - 62.f, sz1 = quayZ - 26.f;
        float w0 = waterLevel - 7.f;
        block(city, vec3(sx0, w0, sz0), vec3(sx1, waterLevel + 1.5f, sz1), vec4(0.55f, 0.10f, 0.08f, 0));      // red bottom
        block(city, vec3(sx0, waterLevel + 1.5f, sz0), vec3(sx1, waterLevel + 13.f, sz1), vec4(0.08f, 0.12f, 0.22f, 0));  // hull
        block(city, vec3(sx1, w0, sz0 + 6), vec3(sx1 + 18, waterLevel + 13.f, sz1 - 6), vec4(0.08f, 0.12f, 0.22f, 0));    // bow
        block(city, vec3(sx0 + 8, waterLevel + 13.f, sz0 + 4), vec3(sx0 + 30, waterLevel + 42.f, sz1 - 4), vec4(0.92f, 0.92f, 0.9f, 0));  // bridge
        block(city, vec3(sx0 + 10, waterLevel + 38.f, sz0 + 2), vec3(sx0 + 28, waterLevel + 41.f, sz1 - 2), vec4(0.2f, 0.25f, 0.3f, 17));
        for (float x = sx0 + 36; x < sx1 - 14; x += 12.7f)
            for (float z = sz0 + 2; z < sz1 - 2.4f; z += 2.5f) {
                int st = rng.irange(2, 6);
                for (int k = 0; k < st; k++) {
                    vec3 lo(x, waterLevel + 13.f + k * 2.6f, z);
                    block(city, lo, lo + vec3(12.2f, 2.58f, 2.44f), kBox[rng.irange(0, 7)]);
                }
            }
    }

    // ---- paved ground: nothing grows in town, and the grass shader stays away
    for (int iz = 0; iz < N; iz++)
        for (int ix = 0; ix < N; ix++) {
            float x = -HALF + ix * CELL, z = -HALF + iz * CELL;
            if (urban(x, z, 4.f) && height[iz * N + ix] > waterLevel + 0.5f) roadDist[iz * N + ix] = std::min(roadDist[iz * N + ix], -0.5f);
        }

    rebuildBoxGrid();
    SDL_Log("City: %zu solids, %d towers, %d signs, %d containers", boxes.size(), towers, signs, containers);
}

void World::rebuildBoxGrid() {
    int gN = int(std::ceil(2 * HALF / TGRID));
    boxGrid.assign(gN * gN, {});
    for (int i = 0; i < int(boxes.size()); i++) {
        const Box& b = boxes[i];
        int ax = std::clamp(int((b.x0 + HALF) / TGRID), 0, gN - 1), bx = std::clamp(int((b.x1 + HALF) / TGRID), 0, gN - 1);
        int az = std::clamp(int((b.z0 + HALF) / TGRID), 0, gN - 1), bz = std::clamp(int((b.z1 + HALF) / TGRID), 0, gN - 1);
        for (int z = az; z <= bz; z++)
            for (int x = ax; x <= bx; x++) boxGrid[z * gN + x].push_back(i);
    }
}

bool World::inBox(float x, float z, float pad) const {
    int gN = int(std::ceil(2 * HALF / TGRID));
    if (boxGrid.empty()) return false;
    int cx = std::clamp(int((x + HALF) / TGRID), 0, gN - 1), cz = std::clamp(int((z + HALF) / TGRID), 0, gN - 1);
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            int gx = cx + dx, gz = cz + dz;
            if (gx < 0 || gz < 0 || gx >= gN || gz >= gN) continue;
            for (int i : boxGrid[gz * gN + gx]) {
                const Box& b = boxes[i];
                if (x > b.x0 - pad && x < b.x1 + pad && z > b.z0 - pad && z < b.z1 + pad) return true;
            }
        }
    return false;
}
