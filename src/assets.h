// Optional photo-scanned assets (Poly Haven, CC0) dropped into assets/: ground textures,
// a sky HDRI and rock / plant props. Everything falls back to the procedural versions.
#pragma once
#include "gl.h"
#include "mathx.h"
#include <string>
#include <vector>

// Full path of assets/<rel> next to the game (installed, portable or source tree), or "".
std::string findAsset(const std::string& rel);
// Loads a JPG/PNG into a mip-mapped, repeating RGBA8 texture. 0 if missing.
GLuint loadTextureFile(const std::string& path, int maxSize = 2048);

struct SkyInfo {
    GLuint tex = 0;
    float exposure = 1.f;
    vec3 sunDir = normalize(vec3(-0.5f, 0.45f, -0.7f));
    vec3 sunColor = vec3(1, 1, 1);
    vec3 zenith, horizon, ambient;
    vec3 sh[9];  // diffuse sky light (irradiance / pi), 2nd-order spherical harmonics
};
// Loads an equirectangular .hdr sky, finds its sun and average colours.
bool loadSkyHdr(const std::string& path, SkyInfo& out);
