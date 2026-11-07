#include "assets.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_HDR
#include "stb_image.h"

static bool exists(const std::string& p) {
    SDL_PathInfo i;
    return SDL_GetPathInfo(p.c_str(), &i) && i.type == SDL_PATHTYPE_FILE;
}

std::string findAsset(const std::string& rel) {
    std::vector<std::string> roots;
    if (const char* bp = SDL_GetBasePath()) {
        std::string b = bp;
        roots = {b + "../share/ridgeline/assets/", b + "assets/", b + "../assets/"};
    }
    roots.push_back("assets/");
    for (auto& r : roots)
        if (exists(r + rel)) return r + rel;
    return "";
}

GLuint loadTextureFile(const std::string& path, int maxSize) {
    if (path.empty()) return 0;
    int w, h, n;
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) { SDL_Log("Could not load %s: %s", path.c_str(), stbi_failure_reason()); return 0; }
    // halve until it fits the size budget
    std::vector<uint8_t> img(px, px + size_t(w) * h * 4);
    stbi_image_free(px);
    while (std::max(w, h) > maxSize && w > 1 && h > 1) {
        int nw = w / 2, nh = h / 2;
        std::vector<uint8_t> o(size_t(nw) * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++) {
                    int s = img[(size_t(y * 2) * w + x * 2) * 4 + c] + img[(size_t(y * 2) * w + x * 2 + 1) * 4 + c] +
                            img[(size_t(y * 2 + 1) * w + x * 2) * 4 + c] + img[(size_t(y * 2 + 1) * w + x * 2 + 1) * 4 + c];
                    o[(size_t(y) * nw + x) * 4 + c] = uint8_t(s / 4);
                }
        img.swap(o);
        w = nw;
        h = nh;
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY*/, 16.f);
    SDL_Log("Loaded %s (%dx%d)", path.c_str(), w, h);
    return t;
}

bool loadSkyHdr(const std::string& path, SkyInfo& out) {
    if (path.empty()) return false;
    int w, h, n;
    float* px = stbi_loadf(path.c_str(), &w, &h, &n, 3);
    if (!px) { SDL_Log("Could not load sky %s: %s", path.c_str(), stbi_failure_reason()); return false; }
    auto dirOf = [&](int x, int y) {
        float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
        float phi = (u - 0.5f) * 2.f * PI, th = v * PI;
        return vec3(std::sin(th) * std::sin(phi), std::cos(th), -std::sin(th) * std::cos(phi));
    };
    // sun: the brightest texel in the upper half
    float best = -1;
    int bx = 0, by = 0;
    vec3 zen(0, 0, 0), hor(0, 0, 0), amb(0, 0, 0);
    float zw = 0, hw = 0, aw = 0;
    for (int y = 0; y < h / 2; y++)
        for (int x = 0; x < w; x++) {
            const float* p = px + (size_t(y) * w + x) * 3;
            float l = p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f;
            if (l > best) { best = l; bx = x; by = y; }
            vec3 c(std::min(p[0], 4.f), std::min(p[1], 4.f), std::min(p[2], 4.f));
            float s = std::sin((y + 0.5f) / h * PI);
            amb += c * s;
            aw += s;
            if (y < h / 8) { zen += c; zw += 1; }
            if (y > h * 0.44f) { hor += c; hw += 1; }
        }
    out.sunDir = dirOf(bx, by);
    if (out.sunDir.y < 0.12f) out.sunDir = normalize(vec3(out.sunDir.x, 0.12f, out.sunDir.z));
    // sun colour: average around the hot spot, normalised
    vec3 sc(0, 0, 0);
    int r = std::max(2, w / 512);
    for (int y = std::max(0, by - r); y <= std::min(h - 1, by + r); y++)
        for (int x = std::max(0, bx - r); x <= std::min(w - 1, bx + r); x++) {
            const float* p = px + (size_t(y) * w + x) * 3;
            sc += vec3(p[0], p[1], p[2]);
        }
    float m = std::max(sc.x, std::max(sc.y, sc.z));
    out.sunColor = m > 0 ? sc / m : vec3(1, 1, 1);
    out.zenith = zw > 0 ? zen / zw : vec3(0.3f, 0.45f, 0.7f);
    out.horizon = hw > 0 ? hor / hw : vec3(0.7f, 0.75f, 0.8f);
    out.ambient = aw > 0 ? amb / aw : vec3(0.5f, 0.55f, 0.6f);
    // bring the sky to the brightness our procedural lighting expects
    float zl = out.horizon.x * 0.2126f + out.horizon.y * 0.7152f + out.horizon.z * 0.0722f;
    out.exposure = zl > 1e-4f ? 0.75f / zl : 1.f;
    out.zenith = out.zenith * out.exposure;
    out.horizon = out.horizon * out.exposure;
    out.ambient = out.ambient * out.exposure;
    // diffuse sky light: project the (sun-clamped) sky onto 9 SH coefficients; below the
    // horizon use a dim, warm "ground bounce" instead of the HDRI's bottom half
    {
        for (auto& c : out.sh) c = vec3(0, 0, 0);
        vec3 ground = out.ambient * 0.3f * vec3(1.0f, 0.9f, 0.7f);
        int step = std::max(1, w / 256);
        double wsum = 0;
        for (int y = 0; y < h; y += step)
            for (int x = 0; x < w; x += step) {
                vec3 d = dirOf(x, y);
                float th = (y + 0.5f) / h * PI;
                float dw = std::sin(th);
                vec3 L;
                if (d.y >= 0) {
                    const float* p = px + (size_t(y) * w + x) * 3;
                    L = vec3(std::min(p[0], 6.f), std::min(p[1], 6.f), std::min(p[2], 6.f)) * out.exposure;
                } else L = ground;
                float Y[9] = {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x, 1.092548f * d.x * d.y,
                              1.092548f * d.y * d.z, 0.315392f * (3.f * d.z * d.z - 1.f), 1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y)};
                for (int k = 0; k < 9; k++) out.sh[k] += L * (Y[k] * dw);
                wsum += dw;
            }
        float norm = float(4.0 * PI / wsum);
        for (auto& c : out.sh) c = c * norm;
        // convolve with the cosine lobe and divide by pi -> ready to multiply by albedo
        const float A[9] = {1.f, 2.f / 3.f, 2.f / 3.f, 2.f / 3.f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
        for (int k = 0; k < 9; k++) out.sh[k] = out.sh[k] * A[k];
    }
    // clamp the sun disc so reflections don't sparkle, then upload as half floats
    for (size_t i = 0; i < size_t(w) * h * 3; i++) px[i] = std::min(px[i] * out.exposure, 10.f);
    glGenTextures(1, &out.tex);
    glBindTexture(GL_TEXTURE_2D, out.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(px);
    SDL_Log("Sky %s: %dx%d, sun (%.2f %.2f %.2f), exposure %.3f", path.c_str(), w, h, out.sunDir.x, out.sunDir.y, out.sunDir.z, out.exposure);
    return true;
}
