// Washi and ink: the look of every menu and HUD element. Cream paper, black sumi ink, vermilion
// seals, a brush serif for titles and a few Japanese characters for flavour.
#pragma once
#include "mathx.h"
#include <cstdint>

namespace wa {
const vec4 PAPER(0.949f, 0.925f, 0.875f, 1);    // the sheet
const vec4 CARD(0.975f, 0.962f, 0.93f, 1);      // a lighter card laid on it
const vec4 PAPER2(0.894f, 0.863f, 0.796f, 1);   // darker, aged paper
const vec4 INK(0.102f, 0.090f, 0.078f, 1);      // sumi ink
const vec4 INK2(0.27f, 0.24f, 0.21f, 1);        // diluted ink
const vec4 MUTED(0.45f, 0.41f, 0.36f, 1);       // faded ink for captions
const vec4 VERM(0.784f, 0.196f, 0.165f, 1);     // vermilion (shrines, seals)
const vec4 GOLD(0.80f, 0.60f, 0.18f, 1);        // leaf gold
const vec4 SAKURA(0.91f, 0.58f, 0.67f, 1);
const vec4 INDIGO(0.18f, 0.27f, 0.45f, 1);      // aizome
const vec4 MOSS(0.38f, 0.47f, 0.25f, 1);
const vec4 PLUM(0.47f, 0.23f, 0.43f, 1);
inline vec4 A(vec4 c, float a) { return vec4(c.x, c.y, c.z, c.w * a); }
inline float hash(int seed, int i) {
    uint32_t h = uint32_t(seed) * 374761393u + uint32_t(i) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float(h & 0xFFFFFF) / float(0x1000000);
}
inline float ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
}
