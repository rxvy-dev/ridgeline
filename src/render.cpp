#include "render.h"
#include "shaders.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <map>

// ------------------------------------------------------------------ Shader
static GLuint compileStage(GLenum type, const std::string& src, const char* name) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        SDL_Log("Shader '%s' (%s) failed:\n%s", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log);
        return 0;
    }
    return s;
}

bool Shader::build(const char* name, const char* vs, const char* fs, bool common) {
    std::string head = "#version 330 core\n";
    std::string vsrc = head + vs;
    std::string fsrc = head + (common ? kCommonGLSL : "") + fs;
    GLuint v = compileStage(GL_VERTEX_SHADER, vsrc, name);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fsrc, name);
    if (!v || !f) return false;
    id = glCreateProgram();
    glAttachShader(id, v);
    glAttachShader(id, f);
    glLinkProgram(id);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(id, sizeof(log), nullptr, log);
        SDL_Log("Program '%s' link failed:\n%s", name, log);
        return false;
    }
    return true;
}

GLint Shader::loc(const char* n) {
    auto it = cache.find(n);
    if (it != cache.end()) return it->second;
    GLint l = glGetUniformLocation(id, n);
    cache[n] = l;
    return l;
}

// ------------------------------------------------------------------ Mesh
void Mesh::upload(const std::vector<Vertex>& v, const std::vector<uint32_t>& idx) {
    destroy();
    if (v.empty() || idx.empty()) return;
    bmin = bmax = v[0].p;
    for (auto& x : v) {
        bmin = vec3(std::min(bmin.x, x.p.x), std::min(bmin.y, x.p.y), std::min(bmin.z, x.p.z));
        bmax = vec3(std::max(bmax.x, x.p.x), std::max(bmax.y, x.p.y), std::max(bmax.z, x.p.z));
    }
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 4, idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)24);
    glBindVertexArray(0);
    count = int(idx.size());
}

void Mesh::draw() const {
    if (!vao) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

void Mesh::drawInstanced(int n) const {
    if (!vao || n <= 0) return;
    glBindVertexArray(vao);
    glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr, n);
}

void Mesh::destroy() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    vao = vbo = ibo = 0;
    count = 0;
}

// ------------------------------------------------------------------ InstanceField
void InstanceField::build(Mesh* m, std::vector<Instance>& inst, float worldHalf, float tile) {
    mesh = m;
    tileSize = tile;
    int nt = int(std::ceil(worldHalf * 2 / tile));
    auto tileOf = [&](const Instance& i) {
        int tx = std::clamp(int((i.pos.x + worldHalf) / tile), 0, nt - 1);
        int tz = std::clamp(int((i.pos.z + worldHalf) / tile), 0, nt - 1);
        return tz * nt + tx;
    };
    std::sort(inst.begin(), inst.end(), [&](const Instance& a, const Instance& b) { return tileOf(a) < tileOf(b); });
    tiles.clear();
    for (int i = 0; i < int(inst.size());) {
        int t = tileOf(inst[i]);
        int j = i;
        while (j < int(inst.size()) && tileOf(inst[j]) == t) j++;
        int tx = t % nt, tz = t / nt;
        tiles.push_back({vec3(-worldHalf + (tx + 0.5f) * tile, 0, -worldHalf + (tz + 0.5f) * tile), i, j - i});
        i = j;
    }
    glGenBuffers(1, &ibuf);
    glBindBuffer(GL_ARRAY_BUFFER, ibuf);
    glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(Instance), inst.data(), GL_STATIC_DRAW);
    glBindVertexArray(m->vao);
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);
    glBindVertexArray(0);
}

void InstanceField::draw(vec3 eye, float range) const {
    if (!mesh || !mesh->vao) return;
    glBindVertexArray(mesh->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ibuf);
    float r = range + tileSize * 0.75f;
    for (auto& t : tiles) {
        float dx = t.center.x - eye.x, dz = t.center.z - eye.z;
        if (dx * dx + dz * dz > r * r) continue;
        size_t off = size_t(t.first) * sizeof(Instance);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), (void*)off);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), (void*)(off + 16));
        glDrawElementsInstanced(GL_TRIANGLES, mesh->count, GL_UNSIGNED_INT, nullptr, t.count);
    }
}

// ------------------------------------------------------------------ MeshBuilder
void MeshBuilder::flatTri(vec3 a, vec3 b, vec3 c, vec4 col) {
    vec3 n = normalize(cross(b - a, c - a));
    uint32_t i0 = add(a, n, col), i1 = add(b, n, col), i2 = add(c, n, col);
    tri(i0, i1, i2);
}
void MeshBuilder::flatQuad(vec3 a, vec3 b, vec3 c, vec3 d, vec4 col) {
    vec3 n = normalize(cross(b - a, c - a));
    uint32_t i0 = add(a, n, col), i1 = add(b, n, col), i2 = add(c, n, col), i3 = add(d, n, col);
    tri(i0, i1, i2);
    tri(i0, i2, i3);
}
void MeshBuilder::box(vec3 c, vec3 h, vec4 col, const mat4* xf) {
    vec3 p[8];
    for (int i = 0; i < 8; i++) {
        vec3 q(c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z));
        p[i] = xf ? xf->transformPoint(q) : q;
    }
    flatQuad(p[0], p[4], p[6], p[2], col);  // -x
    flatQuad(p[1], p[3], p[7], p[5], col);  // +x
    flatQuad(p[0], p[1], p[5], p[4], col);  // -y
    flatQuad(p[2], p[6], p[7], p[3], col);  // +y
    flatQuad(p[0], p[2], p[3], p[1], col);  // -z
    flatQuad(p[4], p[5], p[7], p[6], col);  // +z
}
void MeshBuilder::cylinder(vec3 base, float r, float h, int seg, vec4 col, bool caps) {
    uint32_t start = uint32_t(v.size());
    for (int i = 0; i <= seg; i++) {
        float a = i / float(seg) * 2 * PI;
        vec3 n(std::cos(a), 0, std::sin(a));
        add(base + n * r, n, col);
        add(base + n * r + vec3(0, h, 0), n, col);
    }
    for (int i = 0; i < seg; i++) {
        uint32_t a = start + i * 2;
        tri(a, a + 1, a + 3);
        tri(a, a + 3, a + 2);
    }
    if (caps) {
        uint32_t ct = add(base + vec3(0, h, 0), vec3(0, 1, 0), col);
        uint32_t first = uint32_t(v.size());
        for (int i = 0; i <= seg; i++) {
            float a = i / float(seg) * 2 * PI;
            add(base + vec3(std::cos(a) * r, h, std::sin(a) * r), vec3(0, 1, 0), col);
        }
        for (int i = 0; i < seg; i++) tri(ct, first + i + 1, first + i);
    }
}
void MeshBuilder::cone(vec3 base, float r, float h, int seg, vec4 col) {
    float slope = r / h;
    for (int i = 0; i < seg; i++) {
        float a0 = i / float(seg) * 2 * PI, a1 = (i + 1) / float(seg) * 2 * PI, am = (a0 + a1) * 0.5f;
        vec3 n0 = normalize(vec3(std::cos(a0), slope, std::sin(a0)));
        vec3 n1 = normalize(vec3(std::cos(a1), slope, std::sin(a1)));
        vec3 nm = normalize(vec3(std::cos(am), slope, std::sin(am)));
        uint32_t i0 = add(base + vec3(std::cos(a0) * r, 0, std::sin(a0) * r), n0, col);
        uint32_t i1 = add(base + vec3(std::cos(a1) * r, 0, std::sin(a1) * r), n1, col);
        uint32_t i2 = add(base + vec3(0, h, 0), nm, col);
        tri(i0, i2, i1);
        // underside
        uint32_t b0 = add(base + vec3(std::cos(a0) * r, 0, std::sin(a0) * r), vec3(0, -1, 0), col);
        uint32_t b1 = add(base + vec3(std::cos(a1) * r, 0, std::sin(a1) * r), vec3(0, -1, 0), col);
        uint32_t b2 = add(base, vec3(0, -1, 0), col);
        tri(b0, b1, b2);
    }
}
void MeshBuilder::sphere(vec3 c, float r, int detail, vec4 col, float jitter, uint32_t seed) {
    // icosphere
    const float t = (1.f + std::sqrt(5.f)) / 2.f;
    std::vector<vec3> pts = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                             {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<uint32_t> f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                               3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (auto& p : pts) p = normalize(p);
    for (int d = 0; d < detail; d++) {
        std::map<uint64_t, uint32_t> mid;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            uint64_t key = a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            pts.push_back(normalize((pts[a] + pts[b]) * 0.5f));
            mid[key] = uint32_t(pts.size() - 1);
            return uint32_t(pts.size() - 1);
        };
        std::vector<uint32_t> nf;
        for (size_t i = 0; i < f.size(); i += 3) {
            uint32_t a = f[i], b = f[i + 1], cc = f[i + 2];
            uint32_t ab = midpoint(a, b), bc = midpoint(b, cc), ca = midpoint(cc, a);
            uint32_t tris[12] = {a, ab, ca, b, bc, ab, cc, ca, bc, ab, bc, ca};
            nf.insert(nf.end(), tris, tris + 12);
        }
        f = nf;
    }
    uint32_t base = uint32_t(v.size());
    for (auto& p : pts) {
        float j = 1.f;
        if (jitter > 0) j = 1.f + (hash2(int(p.x * 97 + 300), int(p.y * 89 + p.z * 53 + 300), seed) - 0.5f) * 2 * jitter;
        vec4 cc = col;
        float shade = 0.9f + 0.2f * hash2(int(p.x * 41 + 100), int(p.z * 37 + 100), seed + 3);
        cc.x *= shade; cc.y *= shade; cc.z *= shade;
        add(c + p * (r * j), p, cc);
    }
    for (size_t i = 0; i < f.size(); i += 3) tri(base + f[i], base + f[i + 1], base + f[i + 2]);
}
void MeshBuilder::append(const MeshBuilder& o, const mat4& xf) {
    uint32_t base = uint32_t(v.size());
    for (auto& x : o.v) v.push_back({xf.transformPoint(x.p), normalize(xf.transformDir(x.n)), x.c});
    for (auto i : o.idx) idx.push_back(base + i);
}

// ------------------------------------------------------------------ Renderer
bool Renderer::init() {
    bool ok = true;
    ok &= terrain.build("terrain", kLitVS, kTerrainFS, true);
    ok &= road.build("road", kLitVS, kRoadFS, true);
    ok &= object.build("object", kLitVS, kObjectFS, true);
    ok &= tree.build("tree", kTreeVS, kTreeFS, true);
    ok &= water.build("water", kLitVS, kWaterFS, true);
    ok &= unlit.build("unlit", kLitVS, kUnlitFS, true);
    ok &= depth.build("depth", kDepthVS, kDepthFS, false);
    ok &= depthTree.build("depthTree", kDepthTreeVS, kDepthFS, false);
    ok &= sky.build("sky", kFullscreenVS, kSkyFS, true);
    ok &= post.build("post", kFullscreenVS, kPostFS, false);
    ok &= hud.build("hud", kHudVS, kHudFS, false);
    if (!ok) return false;
    glGenVertexArrays(1, &emptyVao);

    // shadow map
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(0);
    glReadBuffer(0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("Shadow framebuffer incomplete, shadows disabled");
        shadows = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Renderer::destroyTargets() {
    if (sceneFbo) glDeleteFramebuffers(1, &sceneFbo);
    if (sceneTex) glDeleteTextures(1, &sceneTex);
    if (sceneDepth) glDeleteRenderbuffers(1, &sceneDepth);
    if (msFbo) glDeleteFramebuffers(1, &msFbo);
    if (msColor) glDeleteRenderbuffers(1, &msColor);
    if (msDepth) glDeleteRenderbuffers(1, &msDepth);
    sceneFbo = sceneTex = sceneDepth = msFbo = msColor = msDepth = 0;
}

void Renderer::resize(int w, int h) {
    width = std::max(w, 16);
    height = std::max(h, 16);
    destroyTargets();
    usePost = true;
    glGenTextures(1, &sceneTex);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &sceneDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH_COMPONENT24, width, height);
    glGenFramebuffers(1, &sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("Scene framebuffer incomplete, post-processing disabled");
        usePost = false;
    }
    if (msaa > 1 && usePost) {
        GLint maxS = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxS);
        int s = std::min(msaa, int(maxS));
        if (s > 1) {
            glGenRenderbuffers(1, &msColor);
            glBindRenderbuffer(GL_RENDERBUFFER, msColor);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_RGBA16F, width, height);
            glGenRenderbuffers(1, &msDepth);
            glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, s, GL_DEPTH_COMPONENT24, width, height);
            glGenFramebuffers(1, &msFbo);
            glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                SDL_Log("MSAA framebuffer incomplete, MSAA disabled");
                glDeleteFramebuffers(1, &msFbo);
                msFbo = 0;
            }
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setCommon(Shader& s, const Camera& cam) {
    s.use();
    s.set("uViewProj", cam.viewProj);
    s.set("uSunDir", atmo.sunDir);
    s.set("uSunColor", atmo.sunColor);
    s.set("uSkyAmb", atmo.skyAmb);
    s.set("uGroundAmb", atmo.groundAmb);
    s.set("uSkyTop", atmo.skyTop);
    s.set("uSkyHorizon", atmo.skyHorizon);
    s.set("uCamPos", cam.pos);
    s.set("uFogColor", atmo.fogColor);
    s.set("uFogDensity", atmo.fogDensity);
    s.set("uTime", time);
    s.set("uShadows", shadows ? 1 : 0);
    s.set("uShadowMat", shadowMat);
    s.set("uShadowTexel", 1.f / shadowSize);
    s.set("uShadowMap", 3);
    s.set("uModel", mat4());
}

void Renderer::beginShadowPass(vec3 focus, float extent) {
    vec3 eye = atmo.sunDir * 1000.f;
    mat4 lv = lookAt(eye, vec3(0, 0, 0), vec3(0, 1, 0));
    vec3 f = lv.transformPoint(focus);
    float texel = 2 * extent / shadowSize;
    f.x = std::floor(f.x / texel) * texel;
    f.y = std::floor(f.y / texel) * texel;
    mat4 lp = ortho(f.x - extent, f.x + extent, f.y - extent, f.y + extent, -f.z - 900.f, -f.z + 900.f);
    mat4 lvp = lp * lv;
    mat4 bias;  // identity; shader maps [-1,1] -> [0,1] itself
    shadowMat = bias * lvp;
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glViewport(0, 0, shadowSize, shadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    depth.use();
    depth.set("uViewProj", lvp);
    depth.set("uModel", mat4());
    depthTree.use();
    depthTree.set("uViewProj", lvp);
}

void Renderer::endShadowPass() {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::beginScene() {
    GLuint target = usePost ? (msFbo ? msFbo : sceneFbo) : 0;
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, width, height);
    glClearColor(atmo.fogColor.x, atmo.fogColor.y, atmo.fogColor.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::drawSky(const Camera& cam) {
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    setCommon(sky, cam);
    mat4 rotOnly = cam.view;
    rotOnly.m[12] = rotOnly.m[13] = rotOnly.m[14] = 0;
    sky.set("uInvViewProj", inverse(cam.proj * rotOnly));
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::endScene(float blur, float flash) {
    if (!usePost) return;
    if (msFbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    post.use();
    post.set("uScene", 0);
    post.set("uBlur", blur);
    post.set("uFlash", flash);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glBindVertexArray(emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
