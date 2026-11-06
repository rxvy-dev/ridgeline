// Rendering primitives: shaders, meshes, mesh builder, frame buffers.
#pragma once
#include "gl.h"
#include "mathx.h"
#include <vector>
#include <string>
#include <unordered_map>

struct Vertex {
    vec3 p;
    vec3 n;
    vec4 c;  // rgb + material id (or uv data for roads)
};

struct Shader {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> cache;
    bool build(const char* name, const char* vs, const char* fs, bool common);
    void use() const { glUseProgram(id); }
    GLint loc(const char* n);
    void set(const char* n, int v) { glUniform1i(loc(n), v); }
    void set(const char* n, float v) { glUniform1f(loc(n), v); }
    void set(const char* n, vec2 v) { glUniform2f(loc(n), v.x, v.y); }
    void set(const char* n, vec3 v) { glUniform3f(loc(n), v.x, v.y, v.z); }
    void set(const char* n, vec4 v) { glUniform4f(loc(n), v.x, v.y, v.z, v.w); }
    void set(const char* n, const mat4& m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m.m); }
};

struct Mesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    int count = 0;
    vec3 bmin, bmax;
    void upload(const std::vector<Vertex>& v, const std::vector<uint32_t>& idx);
    void draw() const;
    void drawInstanced(int n) const;
    void destroy();
};

// Per-instance data for trees / rocks (matches kTreeVS attributes 3 and 4).
struct Instance {
    vec3 pos; float scale;
    vec3 tint; float rot;
};

// Instances grouped into square tiles so far-away tiles can be skipped.
struct InstanceField {
    Mesh* mesh = nullptr;
    GLuint ibuf = 0;
    float tileSize = 200.f;
    struct Tile { vec3 center; int first, count; };
    std::vector<Tile> tiles;
    void build(Mesh* m, std::vector<Instance>& inst, float worldHalf, float tile);
    // Draw every tile whose center lies within `range` of `eye`.
    void draw(vec3 eye, float range) const;
};

struct MeshBuilder {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    uint32_t add(vec3 p, vec3 n, vec4 c) { v.push_back({p, n, c}); return uint32_t(v.size() - 1); }
    void tri(uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); }
    // Flat-shaded triangle/quad (counter-clockwise when viewed from the front).
    void flatTri(vec3 a, vec3 b, vec3 c, vec4 col);
    void flatQuad(vec3 a, vec3 b, vec3 c, vec3 d, vec4 col);
    void box(vec3 center, vec3 half, vec4 col, const mat4* xf = nullptr);
    void cylinder(vec3 base, float r, float h, int seg, vec4 col, bool caps = true);
    void cone(vec3 base, float r, float h, int seg, vec4 col);
    void sphere(vec3 c, float r, int detail, vec4 col, float jitter = 0.f, uint32_t seed = 1);
    void append(const MeshBuilder& o, const mat4& xf);
    void build(Mesh& m) const { m.upload(v, idx); }
};

struct Camera {
    vec3 pos, target;
    float fov = 1.1f, aspect = 1.77f, nearZ = 0.3f, farZ = 4000.f;
    mat4 view, proj, viewProj;
    void update() {
        view = lookAt(pos, target, vec3(0, 1, 0));
        proj = perspective(fov, aspect, nearZ, farZ);
        viewProj = proj * view;
    }
};

struct Atmosphere {
    vec3 sunDir = normalize(vec3(-0.45f, 0.55f, -0.62f));
    vec3 sunColor = vec3(1.45f, 1.27f, 1.02f);
    vec3 skyAmb = vec3(0.40f, 0.47f, 0.60f);
    vec3 groundAmb = vec3(0.34f, 0.29f, 0.22f);
    vec3 skyTop = vec3(0.24f, 0.45f, 0.80f);
    vec3 skyHorizon = vec3(0.72f, 0.80f, 0.88f);
    vec3 fogColor = vec3(0.70f, 0.77f, 0.84f);
    float fogDensity = 0.0011f;
};

struct Renderer {
    Shader terrain, road, object, tree, water, unlit, depth, depthTree, sky, post, hud;
    int width = 1280, height = 720;
    int msaa = 4;
    bool usePost = true;
    bool shadows = true;
    int shadowSize = 2048;
    GLuint shadowFbo = 0, shadowTex = 0;
    GLuint sceneFbo = 0, sceneTex = 0, sceneDepth = 0;
    GLuint msFbo = 0, msColor = 0, msDepth = 0;
    GLuint emptyVao = 0;
    mat4 shadowMat;
    Atmosphere atmo;
    float time = 0.f;

    bool init();
    void resize(int w, int h);
    void destroyTargets();
    // Uniforms shared by every lit shader.
    void setCommon(Shader& s, const Camera& cam);
    void beginShadowPass(vec3 focus, float extent);
    void endShadowPass();
    void beginScene();
    void drawSky(const Camera& cam);
    void endScene(float blur, float flash);
};
