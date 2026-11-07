// Photo-scanned props (assets/props: rocks and plants converted from Poly Haven glTF).
// They replace the generated rocks and add undergrowth along the roads.
#include "game.h"
#include "assets.h"
#include "carimport.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

void Game::loadProps() {
    std::string list = findAsset("props/list.txt");
    if (list.empty()) return;
    std::string dir = list.substr(0, list.size() - std::string("list.txt").size());
    std::ifstream f(list);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        PropType t;
        std::string folder, type;
        if (!(ss >> folder >> type >> t.smin >> t.smax) || folder[0] == '#') continue;
        t.plant = type == "plant";
        t.model = new ImportedModel();
        std::string err;
        if (!t.model->load(dir + folder, err)) {
            SDL_Log("Prop %s: %s", folder.c_str(), err.c_str());
            delete t.model;
            continue;
        }
        propTypes.push_back(t);
    }
    SDL_Log("Loaded %zu props", propTypes.size());
}

void Game::placeProps() {
    propInst.clear();
    std::vector<int> rocks, plants;
    for (int i = 0; i < int(propTypes.size()); i++) (propTypes[i].plant ? plants : rocks).push_back(i);
    Rng rng(4242);
    if (!rocks.empty()) {
        // every generated rock becomes a scanned one, sized to match
        for (const Instance& in : world.rockInst) {
            int k = rocks[rng.irange(0, int(rocks.size()) - 1)];
            const ImportedModel& m = *propTypes[k].model;
            float size = std::max(m.L, m.W);
            float s = in.scale * 2.4f / std::max(size, 0.1f) * rng.range(propTypes[k].smin, propTypes[k].smax);
            vec3 p = in.pos;
            p.y = world.heightAt(p.x, p.z) - m.H * s * 0.18f;
            propInst.push_back({rotateY(in.rot) * scale(vec3(s, s, s)), p, k, size * s});
        }
        rocksAsProps = true;
    }
    if (!plants.empty()) {
        // ferns under the trees close to the roads, where you actually see them
        auto addAround = [&](const std::vector<Instance>& trees) {
            for (const Instance& t : trees) {
                int r, smp;
                if (!world.nearestRoad(t.pos, r, smp, 70.f)) continue;
                int n = rng.irange(1, 4);
                for (int j = 0; j < n; j++) {
                    float a = rng.range(0, 6.28f), d = rng.range(1.2f, 4.5f);
                    vec3 p = t.pos + vec3(std::cos(a) * d, 0, std::sin(a) * d);
                    int r2, s2;
                    if (world.nearestRoad(p, r2, s2, 6.f)) continue;  // keep the road clear
                    p.y = world.heightAt(p.x, p.z);
                    if (p.y < world.waterLevel + 0.5f) continue;
                    int k = plants[rng.irange(0, int(plants.size()) - 1)];
                    float s = rng.range(propTypes[k].smin, propTypes[k].smax);
                    propInst.push_back({rotateY(rng.range(0, 6.28f)) * scale(vec3(s, s, s)), p, k, std::max(propTypes[k].model->L, propTypes[k].model->W) * s});
                    if (propInst.size() > 30000) return;
                }
            }
        };
        addAround(world.pineInst);
        addAround(world.leafInst);
    }
    SDL_Log("Placed %zu props", propInst.size());
}

void Game::drawProps(const Camera& c, vec3 eye, float rockRange, float plantRange, bool depthOnly) {
    if (propInst.empty()) return;
    Shader& s = depthOnly ? R.depth : R.car;
    if (!depthOnly) {
        R.setCommon(s, c);
        s.set("uGlassPass", 0);
        s.set("uPaint", vec3(1, 1, 1));
        s.set("uBrake", 0.f);
    }
    for (const PropInst& p : propInst) {
        const PropType& t = propTypes[p.type];
        float range = t.plant ? plantRange : rockRange;
        vec3 d = p.pos - eye;
        if (d.x * d.x + d.z * d.z > (range + p.radius) * (range + p.radius)) continue;
        if (!depthOnly) {
            // cheap view-cone cull
            vec3 f = normalize(c.target - c.pos);
            float dist = length(d);
            if (dist > p.radius * 2.f + 8.f && dot(d, f) < -p.radius) continue;
        }
        mat4 M = translate(p.pos) * p.M;
        const ImportedModel& m = *t.model;
        for (auto& part : m.parts) {
            s.set("uModel", M);
            if (!depthOnly) {
                const ImpMaterial* mt = part.mat >= 0 && part.mat < int(m.mats.size()) ? &m.mats[part.mat] : nullptr;
                s.set("uColor", mt ? mt->color : vec4(0.6f, 0.6f, 0.6f, 1));
                s.set("uFlags", mt ? int(mt->flags & ~uint32_t(IF_PAINT | IF_PAINTMASK)) : 0);
                s.set("uEmis", vec3(0, 0, 0));
                int hb = mt && mt->base >= 0, hn = mt && mt->normal >= 0;
                s.set("uHasBase", hb);
                s.set("uHasNormal", hn);
                s.set("uHasEmis", 0);
                if (hb) { glActiveTexture(GL_TEXTURE0 + U_CARBASE); glBindTexture(GL_TEXTURE_2D, m.tex[mt->base]); }
                if (hn) { glActiveTexture(GL_TEXTURE0 + U_CARNORMAL); glBindTexture(GL_TEXTURE_2D, m.tex[mt->normal]); }
            }
            part.mesh.draw();
        }
    }
    glActiveTexture(GL_TEXTURE0);
    if (depthOnly) s.set("uModel", mat4());
}
