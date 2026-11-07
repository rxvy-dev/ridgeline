// Immediate-mode 2D drawing: rectangles, arcs, lines, text, textured map quads.
#pragma once
#include "render.h"
#include <string>
#include <vector>

struct Hud {
    struct V { float x, y, u, v; uint8_t r, g, b, a; float mode; };
    Shader* sh = nullptr;
    GLuint vao = 0, vbo = 0, fontTex = 0, mapTex = 0;
    std::vector<V> verts;
    float w = 1280, h = 720;
    float ui = 1.f;  // UI scale relative to 720p

    void init(Shader* s, GLuint map);
    void begin(int width, int height);
    void flush();
    void end() { flush(); }
    void clip(float cx, float cy, float r);
    void noClip() { clip(0, 0, 0); }

    void quad(vec2 a, vec2 b, vec2 c, vec2 d, vec4 col, float mode = 0, vec2 ua = {}, vec2 ub = {}, vec2 uc = {}, vec2 ud = {});
    void rect(float x, float y, float rw, float rh, vec4 col);
    void rectV(float x, float y, float rw, float rh, vec4 top, vec4 bottom);
    void line(vec2 a, vec2 b, float thick, vec4 col);
    void circle(vec2 c, float r, vec4 col, int seg = 32);
    void arc(vec2 c, float r0, float r1, float a0, float a1, vec4 col, int seg = 24);
    void triangle(vec2 a, vec2 b, vec2 c, vec4 col);
    // align: 0 left, 1 centre, 2 right. Returns width.
    float text(float x, float y, float px, const std::string& s, vec4 col, int align = 0, bool shadow = true);
    float textWidth(float px, const std::string& s) const;
    int fontFor(float px) const;
    int forceFont = -1;  // -1 = pick by size
    void mapQuad(vec2 p[4], vec2 uv[4], vec4 tint);
    // textured image from any GL texture (drawn immediately, keeps batch order)
    void image(float x, float y, float iw, float ih, GLuint tex, vec4 tint = vec4(1, 1, 1, 1), vec2 uv0 = vec2(0, 1), vec2 uv1 = vec2(1, 0));
    void triangleC(vec2 a, vec2 b, vec2 c, vec4 ca, vec4 cb, vec4 cc);
    void disc(vec2 c, float r, vec4 inner, vec4 outer, int seg = 48);   // radial gradient
    void roundRect(float x, float y, float rw, float rh, float r, vec4 col);
    void rectH(float x, float y, float rw, float rh, vec4 left, vec4 right);
    // text squeezed (then cut with "...") to fit maxW
    float textFit(float x, float y, float px, const std::string& s, vec4 col, int align, float maxW, bool shadow = false);
    // button prompts: "ENTER select   ESC back" or "(A) select   (B) back" drawn as key caps / pad buttons
    float prompts(float x, float y, float px, const std::string& spec, int align = 1, float alpha = 1.f, bool measureOnly = false);
};

std::string fmtThousands(long long v);
std::string fmtTime(float seconds);
