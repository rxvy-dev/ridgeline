// Loading of imported cars (model.rcar + car.ini written by tools/import_car.py).
#pragma once
#include "cars.h"
#include <string>
#include <vector>

enum ImpFlags { IF_PAINT = 1, IF_GLASS = 2, IF_LIGHT = 4, IF_ALPHA = 8, IF_CHROME = 16, IF_TIRE = 32, IF_EMISSIVE = 64, IF_BLEND = 128 };

struct ImpMaterial {
    vec4 color;
    int base = -1, normal = -1, emissive = -1;
    vec3 emis;
    uint32_t flags = 0;
};
struct ImpPart {
    int role = 0;  // 0 body, 1-4 wheel FL FR RL RR, 5-8 caliper FL FR RL RR
    int mat = -1;
    Mesh mesh;
};
struct ImportedModel {
    std::string dir;
    std::vector<GLuint> tex;
    std::vector<ImpMaterial> mats;
    std::vector<ImpPart> parts;
    float L = 4.4f, W = 1.9f, H = 1.3f;
    vec3 wc[4];
    float wr[4], ww[4];
    vec3 eye, hood, bumper;
    bool paintable = false;
    bool load(const std::string& dir, std::string& err);
};

// Scans each directory for sub-folders containing car.ini + model.rcar, appends
// their specs to kCars and returns the loaded models (same order).
std::vector<ImportedModel*> loadImportedCars(const std::vector<std::string>& dirs);
