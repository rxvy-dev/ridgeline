// Tyre smoke, dust, water spray and skid marks.
#pragma once
#include "race.h"
#include "render.h"
#include <vector>
#include <unordered_map>
#include <array>

struct Effects {
    struct Particle { vec3 p, v; float life, maxLife, size, grow, alpha, seed; vec3 col; };
    struct Skid { vec3 a0, a1, b0, b1; float alpha; float age; };
    struct Trail { vec3 last[4]; bool active[4] = {false, false, false, false}; };
    std::vector<Particle> parts;
    std::vector<Skid> skids;
    size_t skidHead = 0;
    std::unordered_map<const void*, Trail> trails;
    Shader smoke, skid;
    GLuint vao = 0, vbo = 0;
    uint32_t rng = 1234567;
    static constexpr size_t MAX_PARTS = 2600, MAX_SKIDS = 7000;

    bool init();
    void clear() { parts.clear(); skids.clear(); skidHead = 0; trails.clear(); }
    void update(float dt, const std::vector<Racer*>& cars, const World& w);
    void drawSkids(Renderer& R, const Camera& c);
    void drawParticles(Renderer& R, const Camera& c);
private:
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / float(0x1000000); }
    void emit(vec3 p, vec3 v, float life, float size, float grow, float alpha, vec3 col);
    void stream(const std::vector<Vertex>& v);
};
