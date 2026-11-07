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
    // additive glow: sparks, flames, electric arcs, burning tyre tracks
    struct Glow { vec3 p, v; float life, maxLife, size, grav; vec3 col; };
    struct Arc { std::vector<vec3> pts; float width; vec3 col; };
    struct Fire { vec3 a, b; float age; };
    std::vector<Glow> glows;
    std::vector<Arc> arcs;     // live for one frame
    std::vector<Fire> fires;
    Shader smoke, skid, glowSh;
    GLuint vao = 0, vbo = 0;
    uint32_t rng = 1234567;
    static constexpr size_t MAX_PARTS = 2600, MAX_SKIDS = 7000;

    bool init();
    void clear() { parts.clear(); skids.clear(); skidHead = 0; trails.clear(); glows.clear(); arcs.clear(); fires.clear(); }
    void glow(vec3 p, vec3 v, float life, float size, vec3 col, float grav = 0.f);
    void arc(vec3 a, vec3 b, float width, vec3 col, float jag);
    void fireLine(vec3 a, vec3 b) { fires.push_back({a, b, 0.f}); }
    void updateGlow(float dt);
    void drawGlow(Renderer& R, const Camera& c);
    void update(float dt, const std::vector<Racer*>& cars, const World& w);
    void drawSkids(Renderer& R, const Camera& c);
    void drawParticles(Renderer& R, const Camera& c);
private:
public:
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / float(0x1000000); }
private:
    void emit(vec3 p, vec3 v, float life, float size, float grow, float alpha, vec3 col);
    void stream(const std::vector<Vertex>& v);
};
