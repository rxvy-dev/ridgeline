#include "hud.h"
#include "font_atlas.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

void Hud::init(Shader* s, GLuint map) {
    sh = s;
    mapTex = map;
    // decode RLE font atlas
    std::vector<uint8_t> px(FONT_ATLAS_W * FONT_ATLAS_H, 0);
    size_t o = 0;
    for (unsigned i = 0; i + 1 < kFontRLESize && o < px.size(); i += 2)
        for (int k = 0; k < kFontRLE[i] && o < px.size(); k++) px[o++] = kFontRLE[i + 1];
    glGenTextures(1, &fontTex);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_ATLAS_W, FONT_ATLAS_H, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(V), (void*)16);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(V), (void*)20);
    glBindVertexArray(0);
}

void Hud::begin(int width, int height) {
    w = float(width);
    h = float(height);
    ui = std::min(h / 720.f, w / 720.f);  // portrait screens scale by width
    verts.clear();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    sh->use();
    sh->set("uScreen", vec2(w, h));
    sh->set("uFont", 0);
    sh->set("uMap", 1);
    sh->set("uImg", 2);
    sh->set("uClip", vec3(0, 0, 0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mapTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex);
}

void Hud::flush() {
    if (verts.empty()) return;
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(V), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
    verts.clear();
}

void Hud::clip(float cx, float cy, float r) {
    flush();
    sh->set("uClip", vec3(cx, cy, r));
}

static uint8_t c8(float v) { return uint8_t(clampf(v, 0.f, 1.f) * 255.f + 0.5f); }

void Hud::quad(vec2 a, vec2 b, vec2 c, vec2 d, vec4 col, float mode, vec2 ua, vec2 ub, vec2 uc, vec2 ud) {
    V va{a.x, a.y, ua.x, ua.y, c8(col.x), c8(col.y), c8(col.z), c8(col.w), mode};
    V vb{b.x, b.y, ub.x, ub.y, va.r, va.g, va.b, va.a, mode};
    V vc{c.x, c.y, uc.x, uc.y, va.r, va.g, va.b, va.a, mode};
    V vd{d.x, d.y, ud.x, ud.y, va.r, va.g, va.b, va.a, mode};
    verts.insert(verts.end(), {va, vb, vc, va, vc, vd});
}

void Hud::rect(float x, float y, float rw, float rh, vec4 col) {
    quad({x, y}, {x + rw, y}, {x + rw, y + rh}, {x, y + rh}, col);
}

void Hud::rectV(float x, float y, float rw, float rh, vec4 top, vec4 bottom) {
    V a{x, y, 0, 0, c8(top.x), c8(top.y), c8(top.z), c8(top.w), 0};
    V b{x + rw, y, 0, 0, a.r, a.g, a.b, a.a, 0};
    V c{x + rw, y + rh, 0, 0, c8(bottom.x), c8(bottom.y), c8(bottom.z), c8(bottom.w), 0};
    V d{x, y + rh, 0, 0, c.r, c.g, c.b, c.a, 0};
    verts.insert(verts.end(), {a, b, c, a, c, d});
}

void Hud::line(vec2 a, vec2 b, float t, vec4 col) {
    vec2 d = b - a;
    float l = length(d);
    if (l < 1e-3f) return;
    vec2 n(-d.y / l * t * 0.5f, d.x / l * t * 0.5f);
    quad(a + n, b + n, b - n, a - n, col);
}

void Hud::triangle(vec2 a, vec2 b, vec2 c, vec4 col) {
    V va{a.x, a.y, 0, 0, c8(col.x), c8(col.y), c8(col.z), c8(col.w), 0};
    V vb{b.x, b.y, 0, 0, va.r, va.g, va.b, va.a, 0};
    V vc{c.x, c.y, 0, 0, va.r, va.g, va.b, va.a, 0};
    verts.insert(verts.end(), {va, vb, vc});
}

void Hud::circle(vec2 c, float r, vec4 col, int seg) {
    for (int i = 0; i < seg; i++) {
        float a0 = i / float(seg) * 2 * PI, a1 = (i + 1) / float(seg) * 2 * PI;
        triangle(c, c + vec2(std::cos(a0), std::sin(a0)) * r, c + vec2(std::cos(a1), std::sin(a1)) * r, col);
    }
}

void Hud::arc(vec2 c, float r0, float r1, float a0, float a1, vec4 col, int seg) {
    for (int i = 0; i < seg; i++) {
        float t0 = lerpf(a0, a1, i / float(seg)), t1 = lerpf(a0, a1, (i + 1) / float(seg));
        vec2 d0(std::cos(t0), std::sin(t0)), d1(std::cos(t1), std::sin(t1));
        quad(c + d0 * r0, c + d0 * r1, c + d1 * r1, c + d1 * r0, col);
    }
}

float Hud::textWidth(float px, const std::string& s) const {
    float sc = px / FONT_SIZE, x = 0;
    for (unsigned char ch : s) {
        if (ch < 32 || ch > 126) ch = '?';
        x += kFontGlyphs[ch - 32].adv * sc;
    }
    return x;
}

float Hud::text(float x, float y, float px, const std::string& s, vec4 col, int align, bool shadow) {
    float wdt = textWidth(px, s);
    if (align == 1) x -= wdt * 0.5f;
    else if (align == 2) x -= wdt;
    float sc = px / FONT_SIZE;
    auto emit = [&](float ox, float oy, vec4 c) {
        float cx = x + ox;
        for (unsigned char ch : s) {
            if (ch < 32 || ch > 126) ch = '?';
            const FontGlyph& g = kFontGlyphs[ch - 32];
            if (ch != ' ') {
                float gx = cx + g.ox * sc, gy = y + oy + g.oy * sc, gw = g.w * sc, gh = g.h * sc;
                float u0 = g.x / float(FONT_ATLAS_W), v0 = g.y / float(FONT_ATLAS_H);
                float u1 = (g.x + g.w) / float(FONT_ATLAS_W), v1 = (g.y + g.h) / float(FONT_ATLAS_H);
                quad({gx, gy}, {gx + gw, gy}, {gx + gw, gy + gh}, {gx, gy + gh}, c, 1, {u0, v0}, {u1, v0}, {u1, v1}, {u0, v1});
            }
            cx += g.adv * sc;
        }
    };
    if (shadow) emit(px * 0.05f, px * 0.06f, vec4(0, 0, 0, col.w * 0.55f));
    emit(0, 0, col);
    return wdt;
}

void Hud::mapQuad(vec2 p[4], vec2 uv[4], vec4 tint) {
    quad(p[0], p[1], p[2], p[3], tint, 2, uv[0], uv[1], uv[2], uv[3]);
}

void Hud::image(float x, float y, float iw, float ih, GLuint tex, vec4 tint, vec2 uv0, vec2 uv1) {
    flush();
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex);
    glActiveTexture(GL_TEXTURE0);
    quad({x, y}, {x + iw, y}, {x + iw, y + ih}, {x, y + ih}, tint, 3, {uv0.x, uv0.y}, {uv1.x, uv0.y}, {uv1.x, uv1.y}, {uv0.x, uv1.y});
    flush();
}

void Hud::triangleC(vec2 a, vec2 b, vec2 c, vec4 ca, vec4 cb, vec4 cc) {
    V va{a.x, a.y, 0, 0, c8(ca.x), c8(ca.y), c8(ca.z), c8(ca.w), 0};
    V vb{b.x, b.y, 0, 0, c8(cb.x), c8(cb.y), c8(cb.z), c8(cb.w), 0};
    V vc{c.x, c.y, 0, 0, c8(cc.x), c8(cc.y), c8(cc.z), c8(cc.w), 0};
    verts.insert(verts.end(), {va, vb, vc});
}

void Hud::disc(vec2 c, float r, vec4 inner, vec4 outer, int seg) {
    for (int i = 0; i < seg; i++) {
        float a0 = i / float(seg) * 2 * PI, a1 = (i + 1) / float(seg) * 2 * PI;
        triangleC(c, c + vec2(std::cos(a0), std::sin(a0)) * r, c + vec2(std::cos(a1), std::sin(a1)) * r, inner, outer, outer);
    }
}

void Hud::roundRect(float x, float y, float rw, float rh, float r, vec4 col) {
    r = std::min(r, std::min(rw, rh) * 0.5f);
    rect(x + r, y, rw - 2 * r, rh, col);
    rect(x, y + r, r, rh - 2 * r, col);
    rect(x + rw - r, y + r, r, rh - 2 * r, col);
    vec2 cs[4] = {{x + r, y + r}, {x + rw - r, y + r}, {x + rw - r, y + rh - r}, {x + r, y + rh - r}};
    for (int k = 0; k < 4; k++) arc(cs[k], 0, r, PI + k * PI * 0.5f, PI + (k + 1) * PI * 0.5f, col, 6);
}

void Hud::rectH(float x, float y, float rw, float rh, vec4 l, vec4 r) {
    V a{x, y, 0, 0, c8(l.x), c8(l.y), c8(l.z), c8(l.w), 0};
    V b{x + rw, y, 0, 0, c8(r.x), c8(r.y), c8(r.z), c8(r.w), 0};
    V c{x + rw, y + rh, 0, 0, b.r, b.g, b.b, b.a, 0};
    V d{x, y + rh, 0, 0, a.r, a.g, a.b, a.a, 0};
    verts.insert(verts.end(), {a, b, c, a, c, d});
}

std::string fmtThousands(long long v) {
    bool neg = v < 0;
    unsigned long long u = neg ? -v : v;
    std::string s = std::to_string(u);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return neg ? "-" + s : s;
}

std::string fmtTime(float t) {
    if (t < 0) t = 0;
    int m = int(t / 60);
    float s = t - m * 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%05.2f", m, s);
    return buf;
}
