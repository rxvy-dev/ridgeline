// Japanese signboards: textured faces from the sign atlas (assets/signs/signs.png, object
// material 22: uv in colour.rg, glow in colour.b). Cell groups mirror tools/make_signs.py.
#pragma once
#include "render.h"
#include "signs_gen.h"

// vertical cells (128 x 512)
enum { SV_SHOP0 = 0, SV_SHOP_N = 24, SV_NEON0 = 24, SV_NEON_N = 6, SV_SENGEN = 30, SV_INARI = 31 };
// horizontal cells (512 x 128)
enum { SH_HONCHO = 0, SH_SHOP0 = 1, SH_SHOP_N = 19, SH_FEST0 = 20, SH_FEST_N = 6, SH_NEON0 = 26, SH_NEON_N = 4,
       SH_FESTIVAL = 30, SH_WELCOME = 31 };
// road sign cells (512 x 256); cell numbers are passed with SG added
enum { SG = 64, SG_FUJI = SG + 0, SG_FUJIYOSHIDA = SG + 1, SG_TOKYO = SG + 2, SG_SKYLINE = SG + 3, SG_IROHA = SG + 4, SG_SEA = SG + 5,
       SG_5TH = SG + 6, SG_DAIKOKU = SG + 7, SG_TOMARE = SG + 8, SG_SLOW = SG + 9, SG_ROCKS = SG + 10, SG_CURVE = SG + 11,
       SG_ANIMALS = SG + 12, SG_TOUGE = SG + 13, SG_CHICHIBU = SG + 14, SG_WELCOMEFUJI = SG + 15,
       SG_HONCYO = SG + 16, SG_HONCYO2 = SG + 17, SG_PARKING = SG + 18, SG_TABACCO = SG + 19, SG_SNACK = SG + 20,
       SG_CLEANING = SG + 21, SG_STATION = SG + 22, SG_KOBAN = SG + 23 };
// second row of vertical cells: hanging street banners and utility-pole ads (pass with vert = true)
enum { SW = 96, SW_BANNER0 = SW + 0, SW_BANNER_N = 8, SW_AD0 = SW + 8, SW_AD_N = 8 };

// colour of a cell's board, for the box the lettering sits on
inline vec4 signBoard(int cell, bool vert, float mat = 0.f) {
    const float* c = cell >= SW ? kSignBgW[(cell - SW) & 15] : cell >= SG ? kSignBgG[(cell - SG) % 24] : vert ? kSignBgV[cell & 31] : kSignBgH[cell & 31];
    return vec4(c[0], c[1], c[2], mat);
}

// One lettered face. n: the (horizontal) direction it faces; hw/hh: half width and height.
inline void signFace(MeshBuilder& b, vec3 c, vec3 n, float hw, float hh, int cell, bool vert, float glow = 0.f) {
    const vec3 up(0, 1, 0);
    vec3 rt = normalize(cross(-n, up));
    float u0, v0, u1, v1;
    if (cell >= SW) { int w = (cell - SW) & 15; u0 = w * 128.f; v0 = 3072.f; u1 = u0 + 128.f; v1 = v0 + 512.f; }
    else if (cell >= SG) { int g = (cell - SG) % 24; u0 = (g % 4) * 512.f; v0 = (g < 16 ? 2048.f : 3584.f - 1024.f) + (g / 4) * 256.f; u1 = u0 + 512.f; v1 = v0 + 256.f; }
    else if (vert) { cell &= 31; u0 = (cell % 16) * 128.f; v0 = (cell / 16) * 512.f; u1 = u0 + 128.f; v1 = v0 + 512.f; }
    else { cell &= 31; u0 = (cell % 4) * 512.f; v0 = 1024.f + (cell / 4) * 128.f; u1 = u0 + 512.f; v1 = v0 + 128.f; }
    const float in = 3.f, TW = 2048.f, TH = 4096.f;
    u0 = (u0 + in) / TW; u1 = (u1 - in) / TW; v0 = (v0 + in) / TH; v1 = (v1 - in) / TH;
    b.quad4(c - rt * hw - up * hh, c + rt * hw - up * hh, c + rt * hw + up * hh, c - rt * hw + up * hh, n,
            vec4(u0, v1, glow, 22), vec4(u1, v1, glow, 22), vec4(u1, v0, glow, 22), vec4(u0, v0, glow, 22));
}

// Lettering fitted onto a board of half size bw x bh, keeping the cell's proportions.
inline void signFitted(MeshBuilder& b, vec3 c, vec3 n, float bw, float bh, int cell, bool vert, float glow = 0.f) {
    float aspect = cell >= SW ? 0.25f : cell >= SG ? 2.f : vert ? 0.25f : 4.f;  // width / height of a cell
    float hw = bw, hh = bw / aspect;
    if (hh > bh) { hh = bh; hw = bh * aspect; }
    signFace(b, c, n, hw, hh, cell, vert, glow);
}

// A signboard: a thin box in the board colour with lettering on its front (and back when two-sided).
inline void signBoardBox(MeshBuilder& b, vec3 c, vec3 n, float hw, float hh, float thick, int cell, bool vert, bool twoSided, float glow = 0.f) {
    const vec3 up(0, 1, 0);
    vec3 rt = normalize(cross(-n, up));
    mat4 xf = basis(rt, up, n, c);
    b.box(vec3(0, 0, 0), vec3(hw, hh, thick), signBoard(cell, vert), &xf);
    signFitted(b, c + n * (thick + 0.012f), n, hw, hh, cell, vert, glow);
    if (twoSided) signFitted(b, c - n * (thick + 0.012f), -n, hw, hh, cell, vert, glow);
}

// A marking painted on the road (止まれ...): lies flat, reads for a driver heading along `fwd`.
inline void roadMarking(MeshBuilder& b, vec3 c, vec3 fwd, float hw, float hl, int cell) {
    const vec3 up(0, 1, 0);
    vec3 rt = normalize(cross(fwd, up));
    int g = (cell - SG) & 15;
    float u0 = (g % 4) * 512.f, v0 = 2048.f + (g / 4) * 256.f;
    float u1 = (u0 + 509.f) / 2048.f, v1 = (v0 + 253.f) / 4096.f;
    u0 = (u0 + 3.f) / 2048.f; v0 = (v0 + 3.f) / 4096.f;
    // the near edge (bottom of the lettering) is towards the driver
    b.quad4(c - rt * hw - fwd * hl, c + rt * hw - fwd * hl, c + rt * hw + fwd * hl, c - rt * hw + fwd * hl, up,
            vec4(u0, v1, 0, 22), vec4(u1, v1, 0, 22), vec4(u1, v0, 0, 22), vec4(u0, v0, 0, 22));
}
