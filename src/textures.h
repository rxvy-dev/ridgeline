// Procedurally generated textures (no image files needed).
#pragma once
#include "gl.h"

struct Textures {
    GLuint grass = 0, dirt = 0, rock = 0, rockN = 0, asphalt = 0, asphaltN = 0, bark = 0;
    GLuint pine = 0, leaves = 0, blades = 0, flakes = 0;
    // photo ground textures (assets/textures), 0 when not installed
    GLuint sand = 0, leafGround = 0, gravel = 0, grassN = 0, forestN = 0;
    GLuint signs = 0;  // Japanese signboard atlas (assets/signs)
    bool photo = false;
    void loadPhoto();
    void generate();
    // Bind every texture to its fixed unit (see kUnit* below).
    void bindAll() const;
};

// Fixed texture units shared by all shaders.
enum TexUnit {
    U_FONT = 0, U_MAP = 1, U_SHADOW0 = 2, U_SHADOW1 = 3, U_SCENE = 4, U_BLOOM = 5,
    U_GRASS = 6, U_DIRT = 7, U_ROCK = 8, U_ROCKN = 9, U_ASPHALT = 10, U_ASPHALTN = 11,
    U_BARK = 12, U_PINE = 13, U_LEAVES = 14, U_BLADES = 15, U_HEIGHT = 16, U_ROADDIST = 17, U_FLAKES = 18, U_CARBASE = 19, U_CARNORMAL = 20, U_CAREMIS = 21, U_REFL = 22, U_DEPTH = 23, U_AO = 24,
    U_SAND = 25, U_LEAFGROUND = 26, U_GRAVEL = 27, U_GRASSN = 28, U_SKY = 29, U_FORESTN = 30, U_PROBE = 31, U_SIGNS = 32, U_PETALS = 33,
};
