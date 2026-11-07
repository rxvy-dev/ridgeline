#include "effects.h"
#include "shaders.h"
#include <algorithm>
#include <cmath>

static const char* kSmokeFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
out vec4 FragColor;
void main(){
    vec2 q = vNrm.xy * 2.0 - 1.0;
    float r = length(q);
    float n = vnoise(vNrm.xy * 3.5 + vNrm.z * 17.0) * 0.6 + vnoise(vNrm.xy * 8.0 - vNrm.z * 5.0) * 0.4;
    float a = smoothstep(1.0, 0.15, r + (n - 0.5) * 0.6) * vCol.a;
    if (a < 0.004) discard;
    float sh = shadowFactor(vPos, vec3(0, 1, 0));
    // light from the top and from the sun side of the puff
    float side = clamp(dot(normalize(vec3(q.x, -q.y, 0.6)), normalize(uSunDir + vec3(0, 0.3, 0))) * 0.5 + 0.5, 0.0, 1.0);
    vec3 lit = vCol.rgb * (uSkyAmb * 0.95 + uSunColor * (0.25 + 0.45 * side) * (0.45 + 0.55 * sh));
    FragColor = vec4(applyFog(lit, vPos), a);
}
)";
static const char* kSkidFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
out vec4 FragColor;
void main(){
    float edge = smoothstep(0.0, 0.25, vNrm.x) * smoothstep(1.0, 0.75, vNrm.x);
    float grain = 0.75 + 0.25 * vnoise(vPos.xz * 9.0);
    float a = vCol.a * edge * grain;
    if (a < 0.004) discard;
    FragColor = vec4(applyFog(vec3(0.015, 0.015, 0.017), vPos), a);
}
)";

static const char* kGlowFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
out vec4 FragColor;
void main(){
    // vNrm.xy: quad coords, vNrm.z: 0 = round sprite, 1 = streak (soft across, hard along)
    vec2 q = vNrm.xy * 2.0 - 1.0;
    float r = vNrm.z > 0.5 ? abs(q.y) : length(q);
    float a = pow(clamp(1.0 - r, 0.0, 1.0), 1.6) * vCol.a;
    if (a < 0.002) discard;
    float fogK = clamp(exp(-uFogDensity * length(vPos - uCamPos) * 0.6), 0.0, 1.0);
    FragColor = vec4(vCol.rgb * a * fogK, 1.0);
}
)";

void Effects::glow(vec3 p, vec3 v, float life, float size, vec3 col, float grav) {
    if (glows.size() >= 4000) return;
    glows.push_back({p, v, 0.f, life, size, grav, col});
}

void Effects::arc(vec3 a, vec3 b, float width, vec3 col, float jag) {
    Arc A;
    A.width = width;
    A.col = col;
    int n = 7;
    vec3 d = b - a;
    vec3 side = normalize(cross(d, vec3(0.3f, 1, 0.2f)));
    vec3 side2 = normalize(cross(d, side));
    for (int i = 0; i <= n; i++) {
        float t = float(i) / n;
        float k = (i == 0 || i == n) ? 0.f : jag * std::sin(t * 3.14159f);
        A.pts.push_back(a + d * t + side * ((rnd() - 0.5f) * k) + side2 * ((rnd() - 0.5f) * k));
    }
    arcs.push_back(A);
}

void Effects::updateGlow(float dt) {
    for (auto& g : glows) {
        g.life += dt;
        g.v = g.v * (1.f - std::min(1.f, dt * 1.2f));
        g.v.y -= g.grav * dt;
        g.p += g.v * dt;
    }
    glows.erase(std::remove_if(glows.begin(), glows.end(), [](const Glow& g) { return g.life >= g.maxLife; }), glows.end());
    // burning tracks: flames licking up along each segment, dying down over a few seconds
    for (auto& f : fires) {
        f.age += dt;
        float heat = 1.f - smoothstepf(2.5f, 7.f, f.age);
        float n = 26.f * dt * heat * std::max(0.3f, length(f.b - f.a));
        while (n > 0) {
            if (n >= 1 || rnd() < n) {
                vec3 p = f.a + (f.b - f.a) * rnd() + vec3(rnd() - 0.5f, 0, rnd() - 0.5f) * 0.12f;
                float hot = rnd();
                glow(p + vec3(0, 0.05f, 0), vec3(0, 1.1f + rnd() * 1.6f * heat, 0), 0.35f + rnd() * 0.45f, 0.10f + 0.16f * heat * rnd(),
                     vec3(3.2f, 1.1f + hot * 0.9f, 0.18f) * (1.5f + heat * 2.f), -0.6f);
            }
            n -= 1;
        }
    }
    fires.erase(std::remove_if(fires.begin(), fires.end(), [](const Fire& f) { return f.age > 7.f; }), fires.end());
}

void Effects::drawGlow(Renderer& R, const Camera& c) {
    if (glows.empty() && arcs.empty() && fires.empty()) return;
    vec3 fwd = normalize(c.target - c.pos);
    vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
    vec3 up = cross(right, fwd);
    static std::vector<Vertex> v;
    v.clear();
    for (auto& g : glows) {
        float t = g.life / g.maxLife;
        float a = std::min(1.f, g.life * 20.f) * (1.f - t);
        float s = g.size * (1.f - 0.4f * t);
        vec4 col(g.col.x, g.col.y, g.col.z, a);
        Vertex A{g.p - right * s - up * s, vec3(0, 0, 0), col}, B{g.p + right * s - up * s, vec3(1, 0, 0), col};
        Vertex C{g.p + right * s + up * s, vec3(1, 1, 0), col}, D{g.p - right * s + up * s, vec3(0, 1, 0), col};
        v.insert(v.end(), {A, B, C, A, C, D});
    }
    auto streak = [&](vec3 a, vec3 b, float w, vec4 col) {
        vec3 d = b - a;
        vec3 mid = (a + b) * 0.5f;
        vec3 sd = cross(d, mid - c.pos);
        float l = length(sd);
        if (l < 1e-5f) return;
        sd = sd / l * w;
        Vertex A{a - sd, vec3(0, 0, 1), col}, B{b - sd, vec3(1, 0, 1), col}, C{b + sd, vec3(1, 1, 1), col}, D{a + sd, vec3(0, 1, 1), col};
        v.insert(v.end(), {A, B, C, A, C, D});
    };
    for (auto& A : arcs) {
        for (size_t i = 0; i + 1 < A.pts.size(); i++) {
            streak(A.pts[i], A.pts[i + 1], A.width * 4.f, vec4(A.col.x, A.col.y, A.col.z, 0.25f));
            streak(A.pts[i], A.pts[i + 1], A.width, vec4(A.col.x * 2.f, A.col.y * 2.f, A.col.z * 2.f, 1.f));
        }
    }
    // the burning tracks themselves: a glowing line on the road
    for (auto& f : fires) {
        float heat = 1.f - smoothstepf(1.5f, 7.f, f.age);
        streak(f.a + vec3(0, 0.04f, 0), f.b + vec3(0, 0.04f, 0), 0.16f, vec4(3.5f, 1.2f, 0.2f, heat));
    }
    arcs.clear();
    if (v.empty()) return;
    R.setCommon(glowSh, c);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    stream(v);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

bool Effects::init() {
    bool ok = smoke.build("smoke", kLitVS, kSmokeFS, true);
    ok &= skid.build("skid", kLitVS, kSkidFS, true);
    ok &= glowSh.build("glow", kLitVS, kGlowFS, true);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)24);
    glBindVertexArray(0);
    parts.reserve(MAX_PARTS);
    skids.reserve(MAX_SKIDS);
    return ok;
}

void Effects::emit(vec3 p, vec3 v, float life, float size, float grow, float alpha, vec3 col) {
    if (parts.size() >= MAX_PARTS) return;
    parts.push_back({p, v, 0.f, life, size, grow, alpha, rnd(), col});
}

void Effects::update(float dt, const std::vector<Racer*>& cars, const World& w) {
    // particles: drift, rise, slow down, grow
    for (auto& p : parts) {
        p.life += dt;
        p.v = p.v * (1.f - std::min(1.f, dt * 1.6f));
        p.v.y += dt * 0.35f;
        p.p += p.v * dt;
        p.size += p.grow * dt;
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life >= p.maxLife; }), parts.end());
    for (auto& s : skids) s.age += dt;
    updateGlow(dt);

    for (Racer* r : cars) {
        if (!r || !r->model) continue;
        const Vehicle& c = r->car;
        Trail& tr = trails[r];
        mat4 M = c.chassisMatrix();
        float v = c.speed();
        bool asphalt = c.surface == SURF_ASPHALT;
        for (int k = 0; k < 4; k++) {
            vec3 wp = r->model->wheelPos[k];
            vec4 q = M * vec4(wp.x, 0.f, wp.z, 1.f);
            vec3 g(q.x, w.heightAt(q.x, q.z), q.z);
            if (asphalt) g.y = std::max(g.y, q.y - 0.02f);
            bool rear = k >= 2;
            float slip = c.tyreSlip * (rear ? 1.f : (c.brake > 0.6f ? 0.8f : 0.35f));
            // smoke on tarmac
            if (c.grounded && asphalt && slip > 0.28f) {
                float rate = (slip - 0.2f) * 70.f;
                float n = rate * dt;
                while (n > 0) {
                    if (n >= 1 || rnd() < n) {
                        vec3 vel = c.vel * 0.12f + vec3(rnd() - 0.5f, 0.3f + rnd() * 0.5f, rnd() - 0.5f) * 1.6f;
                        emit(g + vec3(0, 0.25f, 0), vel, 2.2f + rnd() * 1.8f, 0.55f + rnd() * 0.3f, 1.4f + rnd() * 0.8f, std::min(0.42f, slip * 0.45f), vec3(0.86f, 0.86f, 0.88f));
                    }
                    n -= 1;
                }
            }
            // dust off-road
            if (c.grounded && !asphalt && c.surface != SURF_WATER && v > 5.f && rear) {
                float rate = (c.surface == SURF_DIRT ? 1.6f : 0.7f) * v * (0.6f + c.tyreSlip);
                float n = rate * dt;
                vec3 col = c.surface == SURF_DIRT ? vec3(0.62f, 0.50f, 0.36f) : vec3(0.56f, 0.52f, 0.38f);
                while (n > 0) {
                    if (n >= 1 || rnd() < n) {
                        vec3 vel = c.vel * 0.25f + vec3(rnd() - 0.5f, 0.4f + rnd() * 0.6f, rnd() - 0.5f) * 2.2f;
                        emit(g + vec3(0, 0.2f, 0), vel, 1.8f + rnd() * 1.6f, 0.6f + rnd() * 0.4f, 2.2f + rnd() * 1.2f, 0.28f, col);
                    }
                    n -= 1;
                }
            }
            // spray in water
            if (c.waterTime > 0 && v > 3.f) {
                float n = v * 1.4f * dt;
                while (n > 0) {
                    if (n >= 1 || rnd() < n) {
                        vec3 vel = c.vel * 0.4f + vec3(rnd() - 0.5f, 1.5f + rnd() * 2.f, rnd() - 0.5f) * 2.5f;
                        emit(vec3(g.x, w.waterLevel + 0.1f, g.z), vel, 0.8f + rnd() * 0.6f, 0.4f, 1.6f, 0.45f, vec3(0.92f, 0.95f, 0.97f));
                    }
                    n -= 1;
                }
            }
            // skid marks
            float sk = c.tyreSlip * (rear ? 1.f : (c.brake > 0.6f ? 0.9f : 0.2f));
            bool marking = c.grounded && asphalt && sk > 0.35f && v > 3.f;
            if (marking) {
                vec3 cur = g + vec3(0, 0.055f, 0);
                if (tr.active[k]) {
                    vec3 d = cur - tr.last[k];
                    float len = std::sqrt(d.x * d.x + d.z * d.z);
                    if (len > 0.35f) {
                        vec3 side = normalize(vec3(-d.z, 0, d.x)) * 0.13f;
                        Skid s{tr.last[k] - side, tr.last[k] + side, cur - side, cur + side, clampf((sk - 0.3f) * 1.4f, 0.15f, 0.7f), 0.f};
                        if (len < 4.f) {
                            if (skids.size() < MAX_SKIDS) skids.push_back(s);
                            else { skids[skidHead] = s; skidHead = (skidHead + 1) % MAX_SKIDS; }
                        }
                        tr.last[k] = cur;
                    }
                } else {
                    tr.last[k] = cur;
                    tr.active[k] = true;
                }
            } else tr.active[k] = false;
        }
    }
}

void Effects::stream(const std::vector<Vertex>& v) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size()));
    glBindVertexArray(0);
}

void Effects::drawSkids(Renderer& R, const Camera& c) {
    if (skids.empty()) return;
    static std::vector<Vertex> v;
    v.clear();
    for (auto& s : skids) {
        vec3 m = (s.a0 + s.b1) * 0.5f - c.pos;
        if (m.x * m.x + m.z * m.z > 350.f * 350.f) continue;
        float a = s.alpha * (1.f - clampf((s.age - 90.f) / 60.f, 0.f, 1.f));
        if (a <= 0.01f) continue;
        Vertex A{s.a0, vec3(0, 0, 0), vec4(0, 0, 0, a)}, B{s.a1, vec3(1, 0, 0), vec4(0, 0, 0, a)};
        Vertex C{s.b0, vec3(0, 0, 0), vec4(0, 0, 0, a)}, D{s.b1, vec3(1, 0, 0), vec4(0, 0, 0, a)};
        v.insert(v.end(), {A, C, D, A, D, B});
    }
    if (v.empty()) return;
    R.setCommon(skid, c);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.f, -6.f);
    stream(v);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Effects::drawParticles(Renderer& R, const Camera& c) {
    if (parts.empty()) return;
    vec3 fwd = normalize(c.target - c.pos);
    vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
    vec3 up = cross(right, fwd);
    std::vector<const Particle*> order;
    order.reserve(parts.size());
    for (auto& p : parts) order.push_back(&p);
    std::sort(order.begin(), order.end(), [&](const Particle* a, const Particle* b) {
        vec3 da = a->p - c.pos, db = b->p - c.pos;
        return dot(da, da) > dot(db, db);
    });
    static std::vector<Vertex> v;
    v.clear();
    for (const Particle* p : order) {
        float t = p->life / p->maxLife;
        float a = p->alpha * std::min(1.f, p->life * 6.f) * (1.f - t) * (1.f - t);
        if (a < 0.004f) continue;
        float s = p->size;
        float ang = p->seed * 6.2831f + p->life * (p->seed - 0.5f);
        float ca = std::cos(ang), sa = std::sin(ang);
        vec3 r = right * ca + up * sa, u2 = up * ca - right * sa;
        vec4 col(p->col.x, p->col.y, p->col.z, a);
        Vertex A{p->p - r * s - u2 * s, vec3(0, 0, p->seed), col}, B{p->p + r * s - u2 * s, vec3(1, 0, p->seed), col};
        Vertex C{p->p + r * s + u2 * s, vec3(1, 1, p->seed), col}, D{p->p - r * s + u2 * s, vec3(0, 1, p->seed), col};
        v.insert(v.end(), {A, B, C, A, C, D});
    }
    if (v.empty()) return;
    R.setCommon(smoke, c);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    stream(v);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}
