// Procedural texture generation. Everything here is drawn with code at startup.
#include "textures.h"
#include "assets.h"
#include "mathx.h"
#include <SDL3/SDL.h>
#include <vector>

#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE

namespace {

struct Img {
    int w, h;
    std::vector<vec4> px;
    Img(int W, int H, vec4 fill = vec4(0, 0, 0, 0)) : w(W), h(H), px(size_t(W) * H, fill) {}
    vec4& at(int x, int y) {
        x = ((x % w) + w) % w;
        y = ((y % h) + h) % h;
        return px[size_t(y) * w + x];
    }
};

// Tileable value noise (period in cells).
float tnoise(float x, float y, int period, uint32_t seed) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - xi, fy = y - yi;
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    auto H = [&](int a, int b) { return hash2(((a % period) + period) % period, ((b % period) + period) % period, seed); };
    return lerpf(lerpf(H(xi, yi), H(xi + 1, yi), u), lerpf(H(xi, yi + 1), H(xi + 1, yi + 1), u), v);
}
// x, y in [0,1) texture space. Tileable fBm starting at `base` cells.
float tfbm(float x, float y, int base, int oct, uint32_t seed) {
    float s = 0, a = 0.5f, n = 0;
    int p = base;
    for (int i = 0; i < oct; i++) {
        s += tnoise(x * p, y * p, p, seed + i * 31) * a;
        n += a;
        a *= 0.5f;
        p *= 2;
    }
    return s / n;
}

void blend(Img& im, int x, int y, vec3 c, float a) {
    if (a <= 0) return;
    vec4& p = im.at(x, y);
    p.x = lerpf(p.x, c.x, a);
    p.y = lerpf(p.y, c.y, a);
    p.z = lerpf(p.z, c.z, a);
    p.w = std::max(p.w, a);
}

// Tapered anti-aliased stroke from (x0,y0) to (x1,y1).
void stroke(Img& im, float x0, float y0, float x1, float y1, float w0, float w1, vec3 c0, vec3 c1, float alpha, bool wrap = true) {
    float minx = std::min(x0, x1) - std::max(w0, w1) - 1, maxx = std::max(x0, x1) + std::max(w0, w1) + 1;
    float miny = std::min(y0, y1) - std::max(w0, w1) - 1, maxy = std::max(y0, y1) + std::max(w0, w1) + 1;
    float dx = x1 - x0, dy = y1 - y0, len2 = std::max(dx * dx + dy * dy, 1e-4f);
    for (int y = int(miny); y <= int(maxy); y++)
        for (int x = int(minx); x <= int(maxx); x++) {
            if (!wrap && (x < 0 || y < 0 || x >= im.w || y >= im.h)) continue;
            float t = clampf(((x + 0.5f - x0) * dx + (y + 0.5f - y0) * dy) / len2, 0.f, 1.f);
            float px = x0 + dx * t, py = y0 + dy * t;
            float d = std::hypot(x + 0.5f - px, y + 0.5f - py);
            float w = lerpf(w0, w1, t) * 0.5f;
            float cov = clampf(w + 0.5f - d, 0.f, 1.f);
            if (cov > 0) blend(im, x, y, lerp(c0, c1, t), cov * alpha);
        }
}

void ellipse(Img& im, float cx, float cy, float rx, float ry, float ang, vec3 c, float alpha, bool wrap = true, float shade = 0.f) {
    float r = std::max(rx, ry) + 1;
    float ca = std::cos(ang), sa = std::sin(ang);
    for (int y = int(cy - r); y <= int(cy + r); y++)
        for (int x = int(cx - r); x <= int(cx + r); x++) {
            if (!wrap && (x < 0 || y < 0 || x >= im.w || y >= im.h)) continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float u = (dx * ca + dy * sa) / rx, v = (-dx * sa + dy * ca) / ry;
            float d = std::sqrt(u * u + v * v);
            float cov = clampf((1.f - d) * std::min(rx, ry) + 0.5f, 0.f, 1.f);
            if (cov > 0) blend(im, x, y, c * (1.f - shade * (u * 0.5f + 0.5f)), cov * alpha);
        }
}

GLuint upload(const Img& im, bool repeat, bool alpha) {
    std::vector<uint8_t> d(size_t(im.w) * im.h * 4);
    for (size_t i = 0; i < im.px.size(); i++) {
        d[i * 4 + 0] = uint8_t(clampf(im.px[i].x, 0, 1) * 255);
        d[i * 4 + 1] = uint8_t(clampf(im.px[i].y, 0, 1) * 255);
        d[i * 4 + 2] = uint8_t(clampf(im.px[i].z, 0, 1) * 255);
        d[i * 4 + 3] = alpha ? uint8_t(clampf(im.px[i].w, 0, 1) * 255) : 255;
    }
    // Avoid dark fringes on alpha cards: bleed colour into transparent texels.
    if (alpha) {
        for (int pass = 0; pass < 6; pass++) {
            std::vector<uint8_t> o = d;
            for (int y = 0; y < im.h; y++)
                for (int x = 0; x < im.w; x++) {
                    size_t i = (size_t(y) * im.w + x) * 4;
                    if (d[i + 3] > 8) continue;
                    int n = 0, r = 0, g = 0, b = 0;
                    for (int k = 0; k < 4; k++) {
                        int nx = x + (k == 0) - (k == 1), ny = y + (k == 2) - (k == 3);
                        if (nx < 0 || ny < 0 || nx >= im.w || ny >= im.h) continue;
                        size_t j = (size_t(ny) * im.w + nx) * 4;
                        if (d[j + 3] > 8 || (pass > 0 && (d[j] | d[j + 1] | d[j + 2]))) { r += d[j]; g += d[j + 1]; b += d[j + 2]; n++; }
                    }
                    if (n) { o[i] = uint8_t(r / n); o[i + 1] = uint8_t(g / n); o[i + 2] = uint8_t(b / n); }
                }
            d = o;
        }
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, im.w, im.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data());
    if (!alpha) {
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        // Build mips by hand and rescale alpha so the alpha-tested coverage stays the same
        // at every level (otherwise foliage thins out and vanishes in the distance).
        const float cut = 0.42f * 255.f;
        auto coverage = [&](const std::vector<uint8_t>& px) {
            size_t n = 0;
            for (size_t i = 3; i < px.size(); i += 4) n += px[i] > cut;
            return float(n) / float(px.size() / 4);
        };
        float target = coverage(d);
        std::vector<uint8_t> cur = d;
        int w = im.w, h = im.h, level = 0;
        while (w > 1 || h > 1) {
            int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
            std::vector<uint8_t> nx(size_t(nw) * nh * 4);
            for (int y = 0; y < nh; y++)
                for (int x = 0; x < nw; x++)
                    for (int c = 0; c < 4; c++) {
                        int sum = 0;
                        for (int k = 0; k < 4; k++) {
                            int sx = std::min(x * 2 + (k & 1), w - 1), sy = std::min(y * 2 + (k >> 1), h - 1);
                            sum += cur[(size_t(sy) * w + sx) * 4 + c];
                        }
                        nx[(size_t(y) * nw + x) * 4 + c] = uint8_t(sum / 4);
                    }
            // binary search an alpha scale that matches the level-0 coverage
            float lo = 0.5f, hi = 8.f;
            std::vector<uint8_t> tmp = nx;
            for (int it = 0; it < 14; it++) {
                float m = (lo + hi) * 0.5f;
                for (size_t i = 3; i < tmp.size(); i += 4) tmp[i] = uint8_t(std::min(255.f, nx[i] * m));
                if (coverage(tmp) < target) lo = m; else hi = m;
            }
            for (size_t i = 3; i < nx.size(); i += 4) nx[i] = uint8_t(std::min(255.f, nx[i] * hi));
            level++;
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nx.data());
            cur.swap(nx);
            w = nw; h = nh;
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.f);
    glGetError();  // anisotropy may be unsupported; ignore
    return t;
}

// Normal map from a height function stored in .w of a height image.
Img normalsFrom(const Img& h, float strength) {
    Img n(h.w, h.h);
    for (int y = 0; y < h.h; y++)
        for (int x = 0; x < h.w; x++) {
            Img& hh = const_cast<Img&>(h);
            float l = hh.at(x - 1, y).w, r = hh.at(x + 1, y).w, d = hh.at(x, y - 1).w, u = hh.at(x, y + 1).w;
            vec3 nn = normalize(vec3((l - r) * strength, (d - u) * strength, 1.f));
            n.at(x, y) = vec4(nn.x * 0.5f + 0.5f, nn.y * 0.5f + 0.5f, nn.z * 0.5f + 0.5f, 1);
        }
    return n;
}

// ------------------------------------------------------------------ individual textures
Img makeGrass() {
    const int S = 1024;
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = x / float(S), v = y / float(S);
            float n = tfbm(u, v, 6, 5, 101);
            float m = tfbm(u, v, 24, 3, 202);
            vec3 soil(0.30f, 0.24f, 0.15f), dry(0.62f, 0.52f, 0.30f);
            vec3 c = lerp(soil, dry, smoothstepf(0.3f, 0.6f, n) * 0.8f + 0.2f);
            c = c * (0.8f + 0.35f * m);
            im.at(x, y) = vec4(c, 1);
        }
    Rng r(7);
    const vec3 pal[] = {{0.74f, 0.63f, 0.37f}, {0.66f, 0.55f, 0.30f}, {0.55f, 0.50f, 0.26f},
                        {0.46f, 0.45f, 0.22f}, {0.80f, 0.71f, 0.47f}, {0.40f, 0.33f, 0.19f}};
    for (int i = 0; i < 110000; i++) {
        float x = r.f() * S, y = r.f() * S;
        float a = r.range(0, 6.28f);
        float len = r.range(5, 16);
        vec3 c = pal[r.irange(0, 5)] * r.range(0.85f, 1.12f);
        float zone = tfbm(x / S, y / S, 8, 3, 303);
        if (zone < 0.4f && r.f() < 0.5f) c = lerp(c, vec3(0.42f, 0.44f, 0.22f), 0.6f);
        stroke(im, x, y, x + std::cos(a) * len, y + std::sin(a) * len, 1.6f, 0.4f, c * 0.75f, c, 0.85f);
    }
    return im;
}

Img makeDirt() {
    const int S = 512;
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = x / float(S), v = y / float(S);
            float n = tfbm(u, v, 4, 6, 404);
            float f = tfbm(u, v, 64, 2, 405);
            vec3 c = lerp(vec3(0.36f, 0.28f, 0.19f), vec3(0.56f, 0.45f, 0.31f), n);
            c = c * (0.85f + 0.25f * f);
            im.at(x, y) = vec4(c, n);
        }
    Rng r(9);
    for (int i = 0; i < 2200; i++) {
        float x = r.f() * S, y = r.f() * S, rad = r.range(0.8f, 3.2f);
        float g = r.range(0.45f, 0.75f);
        ellipse(im, x, y, rad, rad * r.range(0.6f, 1.f), r.range(0, 3), vec3(g, g * 0.92f, g * 0.82f), 0.9f, true, 0.5f);
    }
    return im;
}

// tileable cellular noise: distance to the nearest and second-nearest jittered cell centre
static void worley(float u, float v, int cells, unsigned seed, float& f1, float& f2, float& id) {
    float x = u * cells, y = v * cells;
    int cx = int(std::floor(x)), cy = int(std::floor(y));
    f1 = f2 = 9.f;
    id = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int gx = cx + i, gy = cy + j;
            int wx = ((gx % cells) + cells) % cells, wy = ((gy % cells) + cells) % cells;
            unsigned h = (unsigned(wx) * 73856093u) ^ (unsigned(wy) * 19349663u) ^ (seed * 83492791u);
            h = (h ^ (h >> 13)) * 1274126177u;
            float jx = float(h & 0xFFFF) / 65535.f, jy = float((h >> 16) & 0xFFFF) / 65535.f;
            float dx = gx + 0.15f + jx * 0.7f - x, dy = gy + 0.15f + jy * 0.7f - y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < f1) { f2 = f1; f1 = d; id = float((h >> 8) & 0xFF) / 255.f; }
            else if (d < f2) f2 = d;
        }
}

Img makeRock(Img& height) {
    // granite-like cliff rock: fractured blocks (cellular edges), weathered faces, faint bedding
    const int S = 1024;
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = x / float(S), v = y / float(S);
            float wu = tfbm(u, v, 4, 3, 501) - 0.5f, wv = tfbm(u, v, 4, 3, 511) - 0.5f;
            float f1, f2, id, g1, g2, gid;
            worley(u + wu * 0.16f, v + wv * 0.16f, 4, 7, f1, f2, id);       // big blocks
            worley(u + wu * 0.09f, v + wv * 0.09f, 13, 13, g1, g2, gid);    // smaller fractures
            float edge = (1.f - smoothstepf(0.0f, 0.035f, f2 - f1)) * smoothstepf(0.35f, 0.6f, tfbm(u, v, 6, 3, 507));
            float edge2 = (1.f - smoothstepf(0.0f, 0.03f, g2 - g1)) * smoothstepf(0.45f, 0.7f, tfbm(u, v, 9, 3, 508));
            float n = tfbm(u, v, 8, 5, 502);
            float fine = tfbm(u, v, 48, 3, 505);
            float bedding = std::sin((v * 9.f + wu * 1.6f) * 6.2831853f) * 0.5f + 0.5f;
            // each block leans a little lighter/darker and warmer/cooler
            float tone = 0.82f + 0.32f * id + 0.18f * (n - 0.5f);
            vec3 base = lerp(vec3(0.43f, 0.42f, 0.40f), vec3(0.60f, 0.57f, 0.52f), clampf(0.35f + (n - 0.5f) * 0.9f + (gid - 0.5f) * 0.2f, 0.f, 1.f));
            base = lerp(base, vec3(0.56f, 0.48f, 0.39f), smoothstepf(0.55f, 0.8f, tfbm(u, v, 3, 3, 504)) * 0.45f);  // iron stains
            base = lerp(base, vec3(0.33f, 0.36f, 0.27f), smoothstepf(0.62f, 0.85f, tfbm(u, v, 5, 4, 506)) * 0.35f);  // lichen
            base = base * tone * (0.9f + 0.2f * fine) * (0.95f + 0.05f * bedding);
            base = base * (1.f - edge * 0.38f) * (1.f - edge2 * 0.2f);
            float hgt = 0.55f + (f1 * 0.35f) + n * 0.35f + fine * 0.1f - edge * 0.55f - edge2 * 0.25f + bedding * 0.04f;
            im.at(x, y) = vec4(base, 1);
            height.at(x, y) = vec4(0, 0, 0, hgt);
        }
    return im;
}

Img makeAsphalt(Img& height) {
    // 1024px covers 4 m: dense aggregate in a dark binder, a few larger stones, sealed cracks
    const int S = 1024;
    Img im(S, S);
    Rng r(11);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = x / float(S), v = y / float(S);
            float n = tfbm(u, v, 3, 4, 601);
            float g = tfbm(u, v, 160, 2, 602);
            float g2 = tfbm(u, v, 48, 3, 603);
            float base = 0.17f + 0.045f * n + 0.05f * (g - 0.5f) + 0.03f * (g2 - 0.5f);
            im.at(x, y) = vec4(base, base * 0.995f, base * 1.02f, 1);
            height.at(x, y) = vec4(0, 0, 0, g * 0.4f + g2 * 0.3f);
        }
    // aggregate: tens of thousands of small stones with soft edges and a lit top
    for (int i = 0; i < 52000; i++) {
        float cx = r.f() * S, cy = r.f() * S;
        float rad = r.f() < 0.9f ? r.range(0.8f, 2.0f) : r.range(2.2f, 4.2f);
        float tone = r.f();
        float g1 = r.range(0.30f, 0.46f), g2 = r.range(0.09f, 0.14f), g3 = r.range(0.8f, 1.15f);
        vec3 col = tone < 0.55f ? vec3(g1, g1, g1) : tone < 0.85f ? vec3(g2, g2, g2) : vec3(0.42f, 0.36f, 0.30f) * g3;
        int x0 = int(cx - rad - 1), x1 = int(cx + rad + 1), y0 = int(cy - rad - 1), y1 = int(cy + rad + 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / rad;
                if (d > 1.f) continue;
                float a = smoothstepf(1.f, 0.6f, d);
                float lit = 1.f + 0.25f * ((cy - y) / rad);  // light from the top
                vec4& p = im.at(x, y);
                vec3 c = col * lit;
                p = vec4(lerpf(p.x, c.x, a), lerpf(p.y, c.y, a), lerpf(p.z, c.z, a), 1);
                height.at(x, y).w += a * (0.35f + rad * 0.05f) * (1.f - d * d);
            }
    }
    // tar-sealed cracks
    for (int i = 0; i < 14; i++) {
        float x = r.f() * S, y = r.f() * S, a = r.range(0, 6.28f);
        for (int k = 0; k < 26; k++) {
            float nx = x + std::cos(a) * 14, ny = y + std::sin(a) * 14;
            stroke(im, x, y, nx, ny, 4.f, 3.f, vec3(0.055f, 0.055f, 0.06f), vec3(0.06f, 0.06f, 0.065f), 0.85f);
            x = nx; y = ny; a += r.range(-0.55f, 0.55f);
        }
    }
    return im;
}

Img makeBark() {
    const int W = 256, H = 512;
    Img im(W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = x / float(W), v = y / float(H);
            float n = tfbm(u, v * 0.25f, 16, 4, 701);
            float f = std::fabs(std::sin((u * 10.f + tfbm(u, v, 4, 3, 702) * 1.5f) * 6.2831853f));
            float ridge = smoothstepf(0.0f, 0.35f, f);
            vec3 c = lerp(vec3(0.13f, 0.09f, 0.07f), vec3(0.42f, 0.33f, 0.26f), ridge * 0.7f + n * 0.3f);
            im.at(x, y) = vec4(c, 1);
        }
    return im;
}

// Conifer branch card: u across, v from trunk (v=0) to tip (v=1).
Img makePine() {
    const int W = 256, H = 512;
    Img im(W, H);
    Rng r(13);
    float cx = W * 0.5f;
    auto mainX = [&](float t) { return cx + std::sin(t * 2.2f) * 5.f; };
    struct Twig { float x0, y0, x1, y1, t; };
    std::vector<Twig> twigs;
    for (int i = 0; i < 34; i++) {
        float t = (i + 0.5f) / 34.f;
        float y0 = t * (H - 24);
        float side = (i % 2) ? 1.f : -1.f;
        float len = (1.f - t * 0.6f) * W * 0.47f * r.range(0.8f, 1.05f);
        float ang = side * r.range(0.7f, 1.1f);
        float x0 = mainX(t);
        twigs.push_back({x0, y0, x0 + std::sin(ang) * len, y0 + std::cos(ang) * len * 0.7f, t});
    }
    // twigs
    for (auto& tw : twigs) stroke(im, tw.x0, tw.y0, tw.x1, tw.y1, 3.f, 1.2f, vec3(0.20f, 0.15f, 0.10f), vec3(0.22f, 0.17f, 0.11f), 1.f, false);
    for (int y = 0; y < H - 24; y += 2)
        stroke(im, mainX(y / float(H)), float(y), mainX((y + 2) / float(H)), float(y + 2), 6.f * (1.f - y / float(H)) + 2.f, 2.f, vec3(0.18f, 0.13f, 0.09f), vec3(0.18f, 0.13f, 0.09f), 1.f, false);
    // dense needles: darker underneath, lighter sunlit tips
    for (int layer = 0; layer < 2; layer++)
        for (auto& tw : twigs) {
            float len = std::hypot(tw.x1 - tw.x0, tw.y1 - tw.y0);
            int nn = int(len / 1.4f);
            float ang = std::atan2(tw.x1 - tw.x0, tw.y1 - tw.y0);
            for (int k = 0; k < nn; k++) {
                float s2 = k / float(nn);
                float px = lerpf(tw.x0, tw.x1, s2), py = lerpf(tw.y0, tw.y1, s2);
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    float na = ang + sgn * r.range(0.45f, 1.1f);
                    float nl = r.range(11, 19) * (1.f - s2 * 0.35f);
                    float shade = r.range(0.7f, 1.15f) * (layer == 0 ? 0.7f : 1.f);
                    vec3 cb = vec3(0.09f, 0.17f, 0.09f) * shade, ct = vec3(0.22f, 0.34f, 0.15f) * shade;
                    stroke(im, px, py, px + std::sin(na) * nl, py + std::cos(na) * nl * 0.85f, 2.2f, 1.0f, cb, ct, 1.f, false);
                }
            }
        }
    return im;
}

// Deciduous leaf cluster (greyscale-ish; tinted per tree in the shader).
Img makeLeaves() {
    // a leaf spray: twigs carrying individual pointed leaves with midribs, gaps between them
    const int S = 512;
    Img im(S, S);
    Rng r(17);
    struct Tw { float x0, y0, x1, y1; };
    std::vector<Tw> tw;
    for (int i = 0; i < 6; i++) {
        float a = r.range(0, 6.28f);
        float x0 = S * 0.5f + r.range(-30.f, 30.f), y0 = S * 0.5f + r.range(-30.f, 30.f);
        float len = r.range(0.28f, 0.44f) * S;
        tw.push_back({x0, y0, x0 + std::cos(a) * len, y0 + std::sin(a) * len});
    }
    for (auto& t : tw) stroke(im, t.x0, t.y0, t.x1, t.y1, 3.2f, 1.2f, vec3(0.22f, 0.17f, 0.12f), vec3(0.27f, 0.21f, 0.15f), 1.f, false);
    for (int pass = 0; pass < 2; pass++)
        for (auto& t : tw) {
            int n = 9;
            float ta = std::atan2(t.y1 - t.y0, t.x1 - t.x0);
            for (int k = 0; k < n; k++) {
                float s2 = r.range(0.15f, 1.f);
                float bx = lerpf(t.x0, t.x1, s2), by = lerpf(t.y0, t.y1, s2);
                float side = (k % 2) ? 1.f : -1.f;
                float a = ta + side * r.range(0.5f, 1.2f);
                float len = r.range(30.f, 46.f) * (pass ? 0.85f : 1.f);
                float stem = 6.f;
                float cx = bx + std::cos(a) * (stem + len * 0.5f), cy = by + std::sin(a) * (stem + len * 0.5f);
                float g = r.range(0.7f, 1.05f) * (pass ? 1.f : 0.78f);
                vec3 c(g, g * r.range(0.86f, 1.0f), g * r.range(0.72f, 0.9f));
                stroke(im, bx, by, bx + std::cos(a) * stem, by + std::sin(a) * stem, 1.4f, 1.2f, c * 0.5f, c * 0.5f, 1.f, false);
                // leaf blade: a pointed oval built from a few overlapping ellipses
                for (int e = 0; e < 4; e++) {
                    float f = (e + 0.5f) / 4.f - 0.5f;
                    float w = len * 0.22f * (1.f - std::fabs(f) * 1.4f);
                    ellipse(im, cx + std::cos(a) * f * len * 0.8f, cy + std::sin(a) * f * len * 0.8f, len * 0.24f, w, a, c * (0.92f + 0.12f * e / 4.f), 1.f, false, 0.25f);
                }
                // midrib
                stroke(im, cx - std::cos(a) * len * 0.42f, cy - std::sin(a) * len * 0.42f, cx + std::cos(a) * len * 0.42f, cy + std::sin(a) * len * 0.42f,
                       1.1f, 0.4f, c * 0.62f, c * 0.75f, 0.8f, false);
            }
        }
    return im;
}

// Grass tuft card: blades rising from the bottom edge (v=1 bottom, v=0 top).
Img makeBlades() {
    // many thin, tapering blades in mixed late-summer colours, darker at the roots
    const int W = 512, H = 512;
    Img im(W, H);
    Rng r(19);
    for (int i = 0; i < 260; i++) {
        float x0 = r.range(12, W - 12.f), h = std::pow(r.range(0.25f, 1.f), 0.7f) * (H - 8);
        float lean = r.range(-0.55f, 0.55f), curl = r.range(-0.25f, 0.25f);
        float g = r.range(0.78f, 1.12f);
        float kind = r.f();
        vec3 base, tip;
        if (kind < 0.42f) { base = vec3(0.16f, 0.22f, 0.08f); tip = vec3(0.48f, 0.58f, 0.24f); }       // green
        else if (kind < 0.78f) { base = vec3(0.26f, 0.25f, 0.11f); tip = vec3(0.74f, 0.66f, 0.36f); }  // straw
        else { base = vec3(0.20f, 0.20f, 0.09f); tip = vec3(0.60f, 0.56f, 0.28f); }                     // dry olive
        base = base * g; tip = tip * g;
        float wdt = r.range(2.5f, 5.5f);
        float px = x0, py = H - 1.f;
        const int SEG = 10;
        for (int k = 0; k < SEG; k++) {
            float t0 = k / float(SEG), t1 = (k + 1) / float(SEG);
            float nx = x0 + lean * h * t1 * t1 + curl * h * t1 * t1 * t1, ny = H - 1.f - h * t1;
            stroke(im, px, py, nx, ny, wdt * (1 - t0) + 0.6f, wdt * (1 - t1) + 0.6f, lerp(base, tip, std::pow(t0, 0.7f)), lerp(base, tip, std::pow(t1, 0.7f)), 1.f, false);
            px = nx; py = ny;
        }
    }
    return im;
}

Img makeFlakes() {
    const int S = 256;
    Img im(S, S);
    Rng r(23);
    for (auto& p : im.px) { float v = r.f(); p = vec4(v, r.f(), r.f(), 1); }
    return im;
}

}  // namespace

void Textures::generate() {
    Uint64 t0 = SDL_GetTicks();
    grass = upload(makeGrass(), true, false);
    Img dh = makeDirt();
    dirt = upload(dh, true, false);
    Img rh(1024, 1024);
    rock = upload(makeRock(rh), true, false);
    rockN = upload(normalsFrom(rh, 6.f), true, false);
    Img ah(1024, 1024);
    asphalt = upload(makeAsphalt(ah), true, false);
    asphaltN = upload(normalsFrom(ah, 3.5f), true, false);
    bark = upload(makeBark(), true, false);
    pine = upload(makePine(), false, true);
    leaves = upload(makeLeaves(), false, true);
    blades = upload(makeBlades(), false, true);
    flakes = upload(makeFlakes(), true, false);
    SDL_Log("Textures generated in %llu ms", (unsigned long long)(SDL_GetTicks() - t0));
    loadPhoto();
}

// photo textures from assets/textures replace the generated ones they match
void Textures::loadPhoto() {
    auto get = [](const char* name) { return loadTextureFile(findAsset(std::string("textures/") + name), 2048); };
    signs = loadTextureFile(findAsset("signs/signs.png"), 4096);
    if (GLuint t = get("grass_diff.jpg")) { grass = t; photo = true; }
    if (GLuint t = get("grass_nor.jpg")) grassN = t;
    if (GLuint t = get("forest_diff.jpg")) dirt = t;
    if (GLuint t = get("forest_nor.jpg")) forestN = t;
    if (GLuint t = get("cliff_diff.jpg")) rock = t;
    if (GLuint t = get("cliff_nor.jpg")) rockN = t;
    if (GLuint t = get("asphalt_diff.jpg")) asphalt = t;
    if (GLuint t = get("asphalt_nor.jpg")) asphaltN = t;
    sand = get("sand_diff.jpg");
    leafGround = get("leaves_diff.jpg");
    gravel = get("gravel_diff.jpg");
}

void Textures::bindAll() const {
    const std::pair<int, GLuint> b[] = {{U_GRASS, grass}, {U_DIRT, dirt}, {U_ROCK, rock}, {U_ROCKN, rockN}, {U_ASPHALT, asphalt},
                                        {U_ASPHALTN, asphaltN}, {U_BARK, bark}, {U_PINE, pine}, {U_LEAVES, leaves}, {U_BLADES, blades}, {U_FLAKES, flakes},
                                        {U_SAND, sand ? sand : dirt}, {U_LEAFGROUND, leafGround ? leafGround : dirt},
                                        {U_GRAVEL, gravel ? gravel : dirt}, {U_GRASSN, grassN ? grassN : rockN}, {U_FORESTN, forestN ? forestN : rockN},
                                        {U_SIGNS, signs ? signs : flakes}};
    for (auto& p : b) {
        glActiveTexture(GL_TEXTURE0 + p.first);
        glBindTexture(GL_TEXTURE_2D, p.second);
    }
    glActiveTexture(GL_TEXTURE0);
}
