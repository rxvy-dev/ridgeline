// Hydraulic erosion: thousands of simulated rain droplets run down the mountains,
// picking up material on the steep bits and dropping it where they slow down. That
// carves gullies and sharp ridges and leaves fans of debris at the foot of slopes,
// the things that make a mountain read as a mountain instead of a smooth noise lump.
//
// Runs on a half-resolution copy (5 m cells) for speed; the change is scaled by a
// mask (mountains only: not the hub, the houses, the town or the lowland fields) and
// added back onto the full-resolution height field.
#include "world.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

void World::erode(uint32_t seed) {
    Uint64 t0 = SDL_GetTicks();
    const int M = (N - 1) / 2 + 1;        // 5 m cells
    const float SCALE = 250.f;            // metres per height unit for the simulation
    std::vector<float> hm(size_t(M) * M), orig;
    for (int z = 0; z < M; z++)
        for (int x = 0; x < M; x++) hm[size_t(z) * M + x] = height[size_t(z * 2) * N + x * 2] / SCALE;
    orig = hm;

    // erosion brush: radius 2 cells, weights fall off with distance
    struct Tap { int dx, dz; float w; };
    std::vector<Tap> brush;
    float wsum = 0;
    for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++) {
            float d = std::sqrt(float(dx * dx + dz * dz));
            if (d > 2.f) continue;
            float w = 1.f - d / 2.5f;
            brush.push_back({dx, dz, w});
            wsum += w;
        }
    for (auto& b : brush) b.w /= wsum;

    auto sample = [&](float px, float pz, float& gx, float& gz) {
        int x = int(px), z = int(pz);
        float u = px - x, v = pz - z;
        size_t i = size_t(z) * M + x;
        float h00 = hm[i], h10 = hm[i + 1], h01 = hm[i + M], h11 = hm[i + M + 1];
        gx = (h10 - h00) * (1 - v) + (h11 - h01) * v;
        gz = (h01 - h00) * (1 - u) + (h11 - h10) * u;
        return h00 * (1 - u) * (1 - v) + h10 * u * (1 - v) + h01 * (1 - u) * v + h11 * u * v;
    };

    const float inertia = 0.06f, capFactor = 5.f, minCap = 0.002f, depositRate = 0.3f, erodeRate = 0.35f;
    const float evaporate = 0.018f, gravity = 4.f;
    const int maxSteps = 64;
    Rng rng(seed * 2654435761u + 17u);
    int drops = 0, tries = 0;
    const int wantDrops = int(900000.0 * (double(N) * N) / (3201.0 * 3201.0));  // same rain per km2 as the old 8 km map
    while (drops < wantDrops && tries < wantDrops * 6) {
        tries++;
        float px = rng.range(2.f, M - 3.f), pz = rng.range(2.f, M - 3.f);
        float hstart = hm[size_t(int(pz)) * M + int(px)] * SCALE;
        if (hstart < 45.f) continue;  // only the hills and mountains get rained on
        drops++;
        float dx = 0, dz = 0, speed = 1.f, water = 1.f, sed = 0.f;
        for (int step = 0; step < maxSteps; step++) {
            int ix = int(px), iz = int(pz);
            float u = px - ix, v = pz - iz;
            float gx, gz;
            float h = sample(px, pz, gx, gz);
            dx = dx * inertia - gx * (1 - inertia);
            dz = dz * inertia - gz * (1 - inertia);
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 1e-7f) break;
            dx /= len; dz /= len;
            float nx = px + dx, nz = pz + dz;
            if (nx < 2 || nz < 2 || nx > M - 3 || nz > M - 3) break;
            float ngx, ngz;
            float nh = sample(nx, nz, ngx, ngz);
            float dh = nh - h;
            float cap = std::max(-dh * speed * water * capFactor, minCap);
            size_t i = size_t(iz) * M + ix;
            if (sed > cap || dh > 0) {
                float amt = dh > 0 ? std::min(dh, sed) : (sed - cap) * depositRate;
                sed -= amt;
                hm[i] += amt * (1 - u) * (1 - v);
                hm[i + 1] += amt * u * (1 - v);
                hm[i + M] += amt * (1 - u) * v;
                hm[i + M + 1] += amt * u * v;
            } else {
                float amt = std::min((cap - sed) * erodeRate, -dh);
                for (auto& b : brush) {
                    size_t j = size_t(iz + b.dz) * M + (ix + b.dx);
                    float take = std::min(amt * b.w, std::max(hm[j], 0.f));
                    hm[j] -= take;
                    sed += take;
                }
            }
            speed = std::sqrt(std::max(0.f, speed * speed - dh * gravity));
            water *= 1 - evaporate;
            px = nx; pz = nz;
        }
    }

    // add the change back at full resolution, only where the mask allows
    for (int iz = 0; iz < N; iz++)
        for (int ix = 0; ix < N; ix++) {
            float h = height[size_t(iz) * N + ix];
            float x = -HALF + ix * CELL, z = -HALF + iz * CELL;
            float mask = smoothstepf(35.f, 110.f, h);
            if (mask <= 0.f) continue;
            if (std::hypot(x - hubPos.x, z - hubPos.z) < 160.f || urban(x, z, 40.f) || nearEstate(x, z, 90.f)) continue;
            float fx = ix * 0.5f, fz = iz * 0.5f;
            int cx = std::min(int(fx), M - 2), cz = std::min(int(fz), M - 2);
            float u = fx - cx, v = fz - cz;
            size_t j = size_t(cz) * M + cx;
            auto d = [&](size_t k) { return hm[k] - orig[k]; };
            float delta = (d(j) * (1 - u) * (1 - v) + d(j + 1) * u * (1 - v) + d(j + M) * (1 - u) * v + d(j + M + 1) * u * v) * SCALE;
            height[size_t(iz) * N + ix] = h + clampf(delta, -14.f, 6.f) * mask;
        }
    SDL_Log("Erosion: %d droplets in %llu ms", drops, (unsigned long long)(SDL_GetTicks() - t0));
}
