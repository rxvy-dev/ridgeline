// Car database (all makes and models are fictional) and procedural car meshes.
#pragma once
#include "render.h"
#include <vector>

enum Drive { FWD = 0, RWD = 1, AWD = 2 };
enum Body { BODY_HATCH, BODY_MUSCLE, BODY_COUPE, BODY_MIDENGINE, BODY_TRUCK, BODY_GT, BODY_HYPER, BODY_RALLY };

struct CarSpec {
    const char* make;
    const char* model;
    char cls;          // D C B A S
    int price;         // credits
    float powerKW;
    float massKg;
    float topKmh;
    int drive;
    float grip;        // tyre friction coefficient on asphalt
    float offroad;     // 0..1, how well it keeps grip on grass/dirt
    float redline;     // rpm
    int body;
    vec3 color;        // default paint
    bool stripes;
    bool spoiler;
    float length, width, height, wheelbase, wheelR;
};

extern const std::vector<CarSpec> kCars;
extern const vec3 kPaints[];
extern const int kPaintCount;
extern const char* kPaintNames[];

// Rating bars shown in the dealership (0..10).
struct CarRatings { float speed, accel, handling, offroad; int pi; };
CarRatings rateCar(const CarSpec& s);

struct CarModel {
    Mesh body;
    Mesh wheel;     // spins
    Mesh caliper;   // steers but does not spin
    vec3 wheelPos[4];   // FL, FR, RL, RR in car space (+z forward, +x left)
    float wheelR, wheelW;
    void build(const CarSpec& s);
};
