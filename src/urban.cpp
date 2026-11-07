// Neon Bay City and the Harbour Docks: procedural buildings, signs, street lights,
// container stacks, gantry cranes, warehouses and a container ship.
// Material ids (object shader): 0 plain, 7 steel, 14 facade, 15 neon/lamp, 16 corrugated, 17 glass tower.
#include "world.h"
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

    // ================================================================ city
    const float xs[] = {1800, 1950, 2100, 2250, 2400, 2550, 2700};
    const float zs[] = {-850, -700, -550, -400, -250, -100, 50, 200};
    const vec3 downtown(2240, 0, -260);
    int towers = 0, signs = 0;
    for (int bx = 0; bx < 6; bx++)
        for (int bz = 0; bz < 7; bz++) {
            float x0 = xs[bx] + 10.f, x1 = xs[bx + 1] - 10.f, z0 = zs[bz] + 10.f, z1 = zs[bz + 1] - 10.f;
            // split the block into 1-4 lots
            int nx = rng.f() < 0.55f ? 2 : 1, nz = rng.f() < 0.55f ? 2 : 1;
            for (int ix = 0; ix < nx; ix++)
                for (int iz = 0; iz < nz; iz++) {
                    float lx0 = lerpf(x0, x1, ix / float(nx)) + 2.f, lx1 = lerpf(x0, x1, (ix + 1) / float(nx)) - 2.f;
                    float lz0 = lerpf(z0, z1, iz / float(nz)) + 2.f, lz1 = lerpf(z0, z1, (iz + 1) / float(nz)) - 2.f;
                    if (rng.f() < 0.07f) continue;  // the odd plaza / car park
                    float cxp = (lx0 + lx1) * 0.5f, czp = (lz0 + lz1) * 0.5f;
                    float dd = std::hypot(cxp - downtown.x, czp - downtown.z);
                    float down = std::exp(-dd * dd / (420.f * 420.f));
                    float H = 12.f + 26.f * rng.f() + down * (35.f + 175.f * rng.f() * rng.f());
                    float g = heightAt(cxp, czp) - 0.5f;
                    // shrink a little so blocks read as separate buildings
                    float sx = (lx1 - lx0) * rng.range(0.04f, 0.14f), sz = (lz1 - lz0) * rng.range(0.04f, 0.14f);
                    vec3 lo(lx0 + sx * 0.5f, g, lz0 + sz * 0.5f), hi(lx1 - sx * 0.5f, g + H, lz1 - sz * 0.5f);
                    bool glassy = H > 65.f || rng.f() < 0.15f;
                    vec4 col = glassy ? kGlass[rng.irange(0, 3)] : kConcrete[rng.irange(0, 6)];
                    block(city, lo, hi, col);
                    solid(lo.x, lo.z, hi.x, hi.z, hi.y);
                    // podium / setbacks / crown on the tall ones
                    if (H > 70.f) {
                        towers++;
                        float in = std::min(hi.x - lo.x, hi.z - lo.z) * 0.18f;
                        vec3 l2(lo.x + in, hi.y, lo.z + in), h2(hi.x - in, hi.y + H * rng.range(0.12f, 0.3f), hi.z - in);
                        block(city, l2, h2, col);
                        block(city, vec3(lo.x - 1.5f, g, lo.z - 1.5f), vec3(hi.x + 1.5f, g + 9.f, hi.z + 1.5f), kConcrete[rng.irange(0, 6)]);
                        if (rng.f() < 0.6f) {  // antenna
                            vec3 c = (l2 + h2) * 0.5f;
                            block(city, vec3(c.x - 0.4f, h2.y, c.z - 0.4f), vec3(c.x + 0.4f, h2.y + 18.f, c.z + 0.4f), vec4(0.6f, 0.6f, 0.6f, 7));
                            block(city, vec3(c.x - 0.6f, h2.y + 18.f, c.z - 0.6f), vec3(c.x + 0.6f, h2.y + 19.2f, c.z + 0.6f), vec4(1.f, 0.15f, 0.1f, 15));
                        }
                    } else {
                        // parapet and rooftop plant
                        vec4 dark(0.30f, 0.30f, 0.30f, 0);
                        block(city, vec3(lo.x, hi.y, lo.z), vec3(hi.x, hi.y + 1.1f, lo.z + 0.4f), dark);
                        block(city, vec3(lo.x, hi.y, hi.z - 0.4f), vec3(hi.x, hi.y + 1.1f, hi.z), dark);
                        for (int k = 0; k < 3; k++) {
                            float ax = rng.range(lo.x + 3, hi.x - 6), az = rng.range(lo.z + 3, hi.z - 6);
                            block(city, vec3(ax, hi.y, az), vec3(ax + rng.range(2, 5), hi.y + rng.range(1.5f, 3.f), az + rng.range(2, 5)), vec4(0.55f, 0.56f, 0.56f, 7));
                        }
                    }
                    // shop signs and neon on the street faces of the lower buildings
                    if (H < 80.f || rng.f() < 0.4f) {
                        for (int face = 0; face < 4; face++) {
                            if (rng.f() < 0.45f) continue;
                            vec4 nc = kNeon[rng.irange(0, 5)];
                            float y0 = g + rng.range(4.f, 9.f);
                            bool vertical = rng.f() < 0.35f;
                            float w = vertical ? 1.6f : rng.range(5.f, 11.f), hgt = vertical ? rng.range(6.f, 12.f) : rng.range(1.6f, 3.2f);
                            if (face < 2) {
                                float z = face == 0 ? lo.z - 0.25f : hi.z + 0.25f;
                                float x = rng.range(lo.x + w * 0.5f + 1, std::max(lo.x + w * 0.5f + 1.1f, hi.x - w * 0.5f - 1));
                                if (vertical) block(city, vec3(x - 0.15f, y0, z - (face == 0 ? 1.4f : -0.2f)), vec3(x + 0.15f, y0 + hgt, z + (face == 0 ? -0.2f : 1.4f)), nc);
                                else block(city, vec3(x - w * 0.5f, y0, std::min(z, z + (face == 0 ? -0.15f : 0.15f))), vec3(x + w * 0.5f, y0 + hgt, std::max(z, z + (face == 0 ? -0.15f : 0.15f))), nc);
                            } else {
                                float x = face == 2 ? lo.x - 0.25f : hi.x + 0.25f;
                                float z = rng.range(lo.z + w * 0.5f + 1, std::max(lo.z + w * 0.5f + 1.1f, hi.z - w * 0.5f - 1));
                                if (vertical) block(city, vec3(x - (face == 2 ? 1.4f : -0.2f), y0, z - 0.15f), vec3(x + (face == 2 ? -0.2f : 1.4f), y0 + hgt, z + 0.15f), nc);
                                else block(city, vec3(std::min(x, x + (face == 2 ? -0.15f : 0.15f)), y0, z - w * 0.5f), vec3(std::max(x, x + (face == 2 ? -0.15f : 0.15f)), y0 + hgt, z + w * 0.5f), nc);
                            }
                            signs++;
                        }
                    }
                }
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

    // ---- collision grid
    int gN = int(std::ceil(2 * HALF / TGRID));
    boxGrid.assign(gN * gN, {});
    for (int i = 0; i < int(boxes.size()); i++) {
        const Box& b = boxes[i];
        int ax = std::clamp(int((b.x0 + HALF) / TGRID), 0, gN - 1), bx = std::clamp(int((b.x1 + HALF) / TGRID), 0, gN - 1);
        int az = std::clamp(int((b.z0 + HALF) / TGRID), 0, gN - 1), bz = std::clamp(int((b.z1 + HALF) / TGRID), 0, gN - 1);
        for (int z = az; z <= bz; z++)
            for (int x = ax; x <= bx; x++) boxGrid[z * gN + x].push_back(i);
    }
    SDL_Log("City: %zu solids, %d towers, %d signs, %d containers", boxes.size(), towers, signs, containers);
}
