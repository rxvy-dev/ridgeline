// Rendering primitives: shaders, meshes, mesh builder, frame buffers.
#pragma once
#include "gl.h"
#include "mathx.h"
#include "textures.h"
#include <vector>
#include <string>
#include <unordered_map>

struct Vertex {
    vec3 p;
    vec3 n;
    vec4 c;  // rgb + material id (or uv/shade/mode data for roads and trees)
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

// Instances grouped into square tiles so far-away tiles can be skipped or drawn with a LOD mesh.
struct InstanceField {
    Mesh* mesh = nullptr;
    Mesh* lod = nullptr;
    GLuint ibuf = 0;
    float tileSize = 150.f;
    struct Tile { vec3 center; int first, count; };
    std::vector<Tile> tiles;
    size_t total = 0;
    void build(Mesh* m, Mesh* lodMesh, std::vector<Instance>& inst, float worldHalf, float tile);
    // Tiles within `lodDist` use the full mesh, the rest (up to `range`) the LOD mesh.
    void draw(vec3 eye, float range, float lodDist) const;
};

struct MeshBuilder {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    uint32_t add(vec3 p, vec3 n, vec4 c) { v.push_back({p, n, c}); return uint32_t(v.size() - 1); }
    void tri(uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); }
    void flatTri(vec3 a, vec3 b, vec3 c, vec4 col);
    void flatQuad(vec3 a, vec3 b, vec3 c, vec3 d, vec4 col);
    // quad with explicit per-vertex colour data (used for uv-mapped cards)
    void quad4(vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, vec4 ca, vec4 cb, vec4 cc, vec4 cd);
    void box(vec3 center, vec3 half, vec4 col, const mat4* xf = nullptr);
    void cylinder(vec3 base, float r, float h, int seg, vec4 col, bool caps = true);
    void cone(vec3 base, float r, float h, int seg, vec4 col);
    void sphere(vec3 c, float r, int detail, vec4 col, float jitter = 0.f, uint32_t seed = 1);
    // tube along an arbitrary axis, smooth normals
    void tube(vec3 a, vec3 b, float ra, float rb, int seg, vec4 col);
    void append(const MeshBuilder& o, const mat4& xf);
    void build(Mesh& m) const { m.upload(v, idx); }
};

struct Camera {
    vec3 pos, target;
    float fov = 1.1f, aspect = 1.77f, nearZ = 0.25f, farZ = 7000.f;  // far enough for Fuji from across the island
    float roll = 0;  // radians, banks the horizon
    mat4 view, proj, viewProj;
    void update() {
        vec3 up(0, 1, 0);
        if (roll != 0.f) {
            vec3 f = normalize(target - pos);
            vec3 r = normalize(cross(f, up));
            up = cross(r, f) * std::cos(roll) + r * std::sin(roll);
        }
        view = lookAt(pos, target, up);
        proj = perspective(fov, aspect, nearZ, farZ);
        viewProj = proj * view;
    }
};

struct Atmosphere {
    vec3 sunDir = normalize(vec3(-0.52f, 0.40f, -0.75f));
    vec3 sunColor = vec3(1.75f, 1.42f, 1.02f);
    vec3 skyAmb = vec3(0.42f, 0.46f, 0.54f);
    vec3 groundAmb = vec3(0.30f, 0.25f, 0.17f);
    vec3 skyTop = vec3(0.17f, 0.38f, 0.72f);
    vec3 skyHorizon = vec3(0.70f, 0.78f, 0.84f);
    vec3 fogColor = vec3(0.68f, 0.74f, 0.78f);
    float fogDensity = 0.0011f;
};

struct Renderer {
    Shader terrain, road, object, car, tree, grass, water, unlit, depth, depthTree, sky, post, hud, bright, blur, ssao, rainSh;
    Textures tex;
    GLuint skyTex = 0;   // HDRI sky (assets/sky/sky.hdr), 0 = procedural
    bool useSky = false; // only in the normal daytime atmosphere
    vec3 skySH[9];
    float motionBlur = 0;  // camera motion blur strength (0 = off)
    float wet = 0;         // road / ground wetness 0..1
    float rain = 0;        // rain amount 0..1 (overcast sky, rain streaks)
    float hdriW = 1, night = 0;   // time of day: HDRI sky weight, night amount
    vec3 hlPos, hlDir = vec3(0, 0, 1);
    float hlOn = 0;              // player headlights
    float gradeExposure = 1, gradeContrast = 1, gradeSat = 1, gradeWarmth = 0;
    int colorBlind = 0;
    Mesh rainMesh;
    void drawRain(const Camera& cam, vec3 wind);
    mat4 prevViewProj;
    bool havePrev = false;
    int width = 1280, height = 720;
    int msaa = 4;
    bool usePost = true;
    bool shadows = true;
    int shadowSize[2] = {2048, 2048};
    GLuint shadowFbo[2] = {0, 0}, shadowTex[2] = {0, 0};
    mat4 shadowMat[2];
    GLuint sceneFbo = 0, sceneTex = 0, sceneDepth = 0;
    GLuint msFbo = 0, msColor = 0, msDepth = 0;
    GLuint bloomFbo[2] = {0, 0}, bloomTex[2] = {0, 0};
    GLuint reflFbo = 0, reflTex = 0, reflDepth = 0;
    // environment probe: a small cubemap captured around the player's car so the paint,
    // chrome and glass reflect the real world (trees, road, buildings) instead of just sky
    GLuint probeTex = 0, probeFbo = 0, probeDepth = 0;
    int probeSize = 128;
    vec3 probePos;
    int probeFrames = 0;   // faces rendered since the probe moved somewhere new
    bool probeOn = true;
    void initProbe();
    Camera beginProbeFace(int face, vec3 pos);
    void endProbe();
    GLuint aoFbo = 0, aoTex = 0;
    int aw = 0, ah = 0;
    bool useAO = true;
    int rw = 0, rh = 0;
    float clipY = -1e9f;
    void beginReflection();
    int bw = 0, bh = 0;
    GLuint emptyVao = 0;
    Atmosphere atmo;
    float time = 0.f;
    float bloomAmount = 0.22f;
    vec2 sunUV;
    float sunAmt = 0.f;

    bool init();
    void resize(int w, int h);
    void destroyTargets();
    void setShadowQuality(int nearSize);
    // Uniforms shared by every lit shader.
    void setCommon(Shader& s, const Camera& cam);
    void beginShadowPass(int cascade, vec3 focus, float extent, float depthRange);
    void endShadowPasses();
    void beginScene();
    void drawSky(const Camera& cam);
    void endScene(float blur, float flash, const Camera* cam = nullptr, bool mainView = false);
};
