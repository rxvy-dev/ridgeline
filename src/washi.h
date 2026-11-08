// Washi and ink: the look of every menu and HUD element. Cream paper, black sumi ink, vermilion
// seals, a brush serif for titles and a few Japanese characters for flavour.
#pragma once
#include "mathx.h"
#include <cstdint>

namespace wa {
// The palette is switchable: light (washi paper) or dark (sumi night). Everything reads these at draw
// time, so flipping setDark() re-skins every menu and the HUD at once.
inline vec4 PAPER(0.949f, 0.925f, 0.875f, 1);    // the sheet
inline vec4 CARD(0.975f, 0.962f, 0.93f, 1);      // a lighter card laid on it
inline vec4 PAPER2(0.894f, 0.863f, 0.796f, 1);   // darker, aged paper
inline vec4 INK(0.102f, 0.090f, 0.078f, 1);      // sumi ink
inline vec4 INK2(0.27f, 0.24f, 0.21f, 1);        // diluted ink
inline vec4 MUTED(0.45f, 0.41f, 0.36f, 1);       // faded ink for captions
inline vec4 VERM(0.784f, 0.196f, 0.165f, 1);     // vermilion (shrines, seals)
inline vec4 GOLD(0.80f, 0.60f, 0.18f, 1);        // leaf gold
inline vec4 SAKURA(0.91f, 0.58f, 0.67f, 1);
inline vec4 INDIGO(0.18f, 0.27f, 0.45f, 1);      // aizome
inline vec4 MOSS(0.38f, 0.47f, 0.25f, 1);
inline vec4 PLUM(0.47f, 0.23f, 0.43f, 1);
inline vec4 SHADOW(0.102f, 0.090f, 0.078f, 1);   // hard woodblock shadows under cards
inline vec4 COLUMN(0.102f, 0.090f, 0.078f, 1);   // the ink column down the left edge
inline vec4 COLTEXT(0.949f, 0.925f, 0.875f, 1);  // kanji in that column
inline bool dark = false;
inline void setDark(bool on) {
    dark = on;
    if (on) {   // sumi night: ink-black paper, cream ink, the reds and golds a touch brighter
        PAPER = vec4(0.105f, 0.098f, 0.090f, 1);  CARD = vec4(0.150f, 0.140f, 0.128f, 1);  PAPER2 = vec4(0.072f, 0.066f, 0.060f, 1);
        INK = vec4(0.930f, 0.900f, 0.835f, 1);    INK2 = vec4(0.780f, 0.745f, 0.680f, 1);  MUTED = vec4(0.580f, 0.545f, 0.490f, 1);
        VERM = vec4(0.900f, 0.300f, 0.230f, 1);   GOLD = vec4(0.880f, 0.700f, 0.300f, 1);  SAKURA = vec4(0.95f, 0.66f, 0.74f, 1);
        INDIGO = vec4(0.450f, 0.580f, 0.850f, 1); MOSS = vec4(0.580f, 0.700f, 0.400f, 1);  PLUM = vec4(0.760f, 0.480f, 0.720f, 1);
        SHADOW = vec4(0, 0, 0, 1);
        COLUMN = vec4(0.035f, 0.032f, 0.030f, 1); COLTEXT = INK;
    } else {
        PAPER = vec4(0.949f, 0.925f, 0.875f, 1);  CARD = vec4(0.975f, 0.962f, 0.93f, 1);   PAPER2 = vec4(0.894f, 0.863f, 0.796f, 1);
        INK = vec4(0.102f, 0.090f, 0.078f, 1);    INK2 = vec4(0.27f, 0.24f, 0.21f, 1);      MUTED = vec4(0.45f, 0.41f, 0.36f, 1);
        VERM = vec4(0.784f, 0.196f, 0.165f, 1);   GOLD = vec4(0.80f, 0.60f, 0.18f, 1);      SAKURA = vec4(0.91f, 0.58f, 0.67f, 1);
        INDIGO = vec4(0.18f, 0.27f, 0.45f, 1);    MOSS = vec4(0.38f, 0.47f, 0.25f, 1);      PLUM = vec4(0.47f, 0.23f, 0.43f, 1);
        SHADOW = INK;
        COLUMN = INK; COLTEXT = PAPER;
    }
}
inline vec4 A(vec4 c, float a) { return vec4(c.x, c.y, c.z, c.w * a); }
inline float hash(int seed, int i) {
    uint32_t h = uint32_t(seed) * 374761393u + uint32_t(i) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float(h & 0xFFFFFF) / float(0x1000000);
}
inline float ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
}
