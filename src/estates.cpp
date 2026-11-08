// Player houses ("estates"): placed beside the roads on a levelled plot, each with a
// garage, a paved driveway and collision. Buying one unlocks fast travel to it and
// it works as a garage.
#include "world.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
struct EstateDef { const char* name; const char* blurb; vec3 target; int price; int style; };
const EstateDef kEstateDefs[] = {
    {"Lakeside Ryokan", "A timber inn on the water, a short drive from the festival.", vec3(380, 0, -20), 60000, 0},
    {"Inaka Farmhouse", "Two-storey farmhouse with a barn big enough for a collection.", vec3(-2050, 0, -350), 150000, 1},
    {"Daikoku Loft", "Converted dockside warehouse with a drive-in workshop.", vec3(1000, 0, -1800), 300000, 4},
    {"Akagi Lodge", "Stone and timber lodge at the top of Akagi Touge.", vec3(330, 0, 2330), 450000, 3},
    {"Clifftop Villa", "Modernist villa above the south coast. The view is the point.", vec3(-1300, 0, -1760), 750000, 2},
};
}

const char* World::estateBlurb(int i) const { return i >= 0 && i < int(estates.size()) ? estates[i].blurb.c_str() : ""; }

// Find a plot next to the road nearest each target: dry, close to road level,
// clear of other roads, then face the house towards the road (axis-aligned so the
// collision boxes fit the walls).
void World::placeEstates() {
    estates.clear();
    for (const EstateDef& d : kEstateDefs) {
        int br = -1, bi = 0;
        float bd = 1e30f;
        for (int r = 0; r < int(roads.size()); r++)
            for (int i = 0; i < int(roads[r].pts.size()); i++) {
                vec3 q = roads[r].pts[i] - d.target;
                float dd = q.x * q.x + q.z * q.z;
                if (dd < bd) { bd = dd; br = r; bi = i; }
            }
        if (br < 0) continue;
        const Road& rd = roads[br];
        bool found = false;
        Estate e;
        for (int step = 0; step < 60 && !found; step++) {
            int off = (step / 2) * 6 * ((step & 1) ? -1 : 1);
            int i = std::clamp(bi + off, 4, int(rd.pts.size()) - 5);
            vec3 t = rd.tan[i], side(-t.z, 0, t.x);
            vec3 toT = d.target - rd.pts[i];
            float pref = dot(side, toT) >= 0 ? 1.f : -1.f;
            for (float sgn : {pref, -pref}) {
                vec3 c = rd.pts[i] + side * (sgn * (rd.width * 0.5f + 25.f));
                float bh = baseHeight(c.x, c.z);
                if (bh < waterLevel + 1.5f || std::fabs(bh - rd.pts[i].y) > 14.f) continue;
                if (urban(c.x, c.z, 40.f) || std::hypot(c.x - hubPos.x, c.z - hubPos.z) < 160.f) continue;
                bool clear = true;
                for (int r = 0; r < int(roads.size()) && clear; r++)
                    for (size_t k = 0; k < roads[r].pts.size(); k += 2) {
                        vec3 q = roads[r].pts[k] - c;
                        float lim = r == br ? 20.f : 34.f;
                        if (q.x * q.x + q.z * q.z < lim * lim) { clear = false; break; }
                    }
                if (!clear) continue;
                vec3 face = normalize(rd.pts[i] - c);
                float yaw = std::round(std::atan2(face.x, face.z) / (PI * 0.5f)) * (PI * 0.5f);
                e.pos = vec3(c.x, rd.pts[i].y + 0.05f, c.z);
                e.yaw = yaw;
                e.roadGap = std::fabs(dot(rd.pts[i] - c, vec3(std::sin(yaw), 0, std::cos(yaw))));
                found = true;
                break;
            }
        }
        if (!found) { SDL_Log("Estate '%s': no plot found", d.name); continue; }
        e.name = d.name;
        e.blurb = d.blurb;
        e.price = d.price;
        e.style = d.style;
        vec3 F(std::sin(e.yaw), 0, std::cos(e.yaw)), Rt(std::cos(e.yaw), 0, -std::sin(e.yaw));
        e.spawn = e.pos + Rt * 6.5f + F * 9.f;
        e.spawnYaw = e.yaw;
        estates.push_back(e);
        SDL_Log("Estate '%s' at %.0f, %.0f (%.0f m)", d.name, e.pos.x, e.pos.z, e.pos.y);
    }
}

// Paved driveway (garage to road) and house footprint, in estate-local coordinates.
bool World::estatePaved(float x, float z, float pad) const {
    for (const Estate& e : estates) {
        float dx = x - e.pos.x, dz = z - e.pos.z;
        if (dx * dx + dz * dz > 60.f * 60.f) continue;
        float s = std::sin(e.yaw), c = std::cos(e.yaw);
        float lx = dx * c - dz * s, lz = dx * s + dz * c;  // local right / forward
        if (lx > 1.5f - pad && lx < 11.5f + pad && lz > -1.f - pad && lz < e.roadGap + pad) return true;
        if (lx > -13.f - pad && lx < 13.f + pad && lz > -13.f - pad && lz < 1.5f + pad) return true;
    }
    return false;
}

bool World::nearEstate(float x, float z, float r) const {
    for (const Estate& e : estates)
        if ((x - e.pos.x) * (x - e.pos.x) + (z - e.pos.z) * (z - e.pos.z) < r * r) return true;
    return false;
}

void World::buildEstates() {
    for (Estate& e : estates) {
        mat4 M = translate(e.pos) * rotateY(e.yaw);
        auto P = [&](vec3 l) { return M.transformPoint(l); };
        auto solid = [&](vec3 c, vec3 h, vec4 col) {
            city.box(c, h, col, &M);
            vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
            for (int i = 0; i < 8; i++) {
                vec3 q = P(vec3(c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y), c.z + ((i & 4) ? h.z : -h.z)));
                lo = vec3(std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z));
                hi = vec3(std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z));
            }
            boxes.push_back({lo.x, lo.z, hi.x, hi.z, hi.y});
        };
        auto deco = [&](vec3 c, vec3 h, vec4 col) { city.box(c, h, col, &M); };
        // quad that faces `out` (local), whatever order the corners come in
        auto quad = [&](vec3 a, vec3 b, vec3 c, vec3 d, vec4 col, vec3 out) {
            vec3 n = cross(b - a, c - a);
            if (dot(n, out) < 0) std::swap(b, d);
            city.flatQuad(P(a), P(b), P(c), P(d), col);
        };
        auto tri = [&](vec3 a, vec3 b, vec3 c, vec4 col, vec3 out) {
            vec3 n = cross(b - a, c - a);
            if (dot(n, out) < 0) std::swap(b, c);
            city.flatTri(P(a), P(b), P(c), col);
        };
        // gable roof over [x0,x1] x [z0,z1] from height y, ridge along x
        auto gable = [&](float x0, float x1, float z0, float z1, float y, float ph, vec4 roof, vec4 wall) {
            float o = 0.45f, zc = (z0 + z1) * 0.5f;
            quad(vec3(x0 - o, y - 0.2f, z1 + o), vec3(x1 + o, y - 0.2f, z1 + o), vec3(x1 + o, y + ph, zc), vec3(x0 - o, y + ph, zc), roof, vec3(0, 1, 1));
            quad(vec3(x0 - o, y - 0.2f, z0 - o), vec3(x1 + o, y - 0.2f, z0 - o), vec3(x1 + o, y + ph, zc), vec3(x0 - o, y + ph, zc), roof, vec3(0, 1, -1));
            quad(vec3(x0 - o, y - 0.2f, z1 + o), vec3(x1 + o, y - 0.2f, z1 + o), vec3(x1 + o, y + ph, zc), vec3(x0 - o, y + ph, zc), roof, vec3(0, -1, -1));
            quad(vec3(x0 - o, y - 0.2f, z0 - o), vec3(x1 + o, y - 0.2f, z0 - o), vec3(x1 + o, y + ph, zc), vec3(x0 - o, y + ph, zc), roof, vec3(0, -1, 1));
            tri(vec3(x0, y, z0), vec3(x0, y, z1), vec3(x0, y + ph, zc), wall, vec3(-1, 0, 0));
            tri(vec3(x1, y, z0), vec3(x1, y, z1), vec3(x1, y + ph, zc), wall, vec3(1, 0, 0));
        };
        // window: frame + reflective pane, on a face whose outward normal is `n` (x or z)
        auto window = [&](vec3 c, float w, float h, vec3 n, vec4 frame) {
            bool sx = std::fabs(n.x) > 0.5f;
            vec3 pane = sx ? vec3(0.06f, h * 0.5f, w * 0.5f) : vec3(w * 0.5f, h * 0.5f, 0.06f);
            vec3 fr = sx ? vec3(0.06f, h * 0.5f + 0.09f, w * 0.5f + 0.09f) : vec3(w * 0.5f + 0.09f, h * 0.5f + 0.09f, 0.06f);
            deco(c + n * 0.02f, fr, frame);
            deco(c + n * 0.07f, pane, vec4(1, 1, 1, 10));
            deco(c + n * 0.13f, sx ? vec3(0.03f, h * 0.5f, 0.03f) : vec3(0.03f, h * 0.5f, 0.03f), frame);  // mullion
            deco(c + n * 0.08f + vec3(0, -h * 0.5f - 0.1f, 0), sx ? vec3(0.12f, 0.05f, w * 0.5f + 0.12f) : vec3(w * 0.5f + 0.12f, 0.05f, 0.12f), frame);  // sill
        };
        auto garageDoor = [&](float x0, float x1, float z, float h) {
            float cx = (x0 + x1) * 0.5f, hw = (x1 - x0) * 0.5f;
            deco(vec3(cx, h * 0.5f, z + 0.03f), vec3(hw, h * 0.5f, 0.05f), vec4(0.62f, 0.63f, 0.64f, 7));
            for (int k = 1; k < 5; k++) deco(vec3(cx, h * k / 5.f, z + 0.09f), vec3(hw, 0.025f, 0.02f), vec4(1, 1, 1, 9));
            deco(vec3(cx, h + 0.12f, z + 0.1f), vec3(hw + 0.15f, 0.12f, 0.06f), vec4(0.2f, 0.2f, 0.21f, 0));
            deco(vec3(cx, h + 0.45f, z + 0.12f), vec3(0.18f, 0.08f, 0.06f), vec4(1.f, 0.9f, 0.7f, 15));  // lamp
        };
        auto door = [&](float x, float z, vec4 col) {
            deco(vec3(x, 1.12f, z + 0.03f), vec3(0.55f, 1.12f, 0.06f), col);
            deco(vec3(x, 2.4f, z + 0.25f), vec3(0.9f, 0.06f, 0.4f), vec4(0.25f, 0.24f, 0.23f, 0));  // canopy
            deco(vec3(x + 0.7f, 2.0f, z + 0.1f), vec3(0.08f, 0.1f, 0.06f), vec4(1.f, 0.85f, 0.6f, 15));
        };
        // plinth so the ground never shows under the walls
        vec4 plinth(0.42f, 0.41f, 0.40f, 0);
        deco(vec3(0, -0.9f, -6.f), vec3(12.5f, 1.f, 6.2f), plinth);
        switch (e.style) {
            case 0: {  // timber cabin + timber garage
                vec4 wood(0.40f, 0.26f, 0.15f, 0), roof(0.16f, 0.15f, 0.15f, 0), trim(0.85f, 0.82f, 0.75f, 0);
                solid(vec3(-5.f, 2.f, -6.f), vec3(6.f, 2.f, 4.5f), wood);
                gable(-11.f, 1.f, -10.5f, -1.5f, 4.f, 2.6f, roof, wood);
                for (float x : {-9.f, -6.f}) window(vec3(x, 1.7f, -1.5f), 1.4f, 1.2f, vec3(0, 0, 1), trim);
                window(vec3(-11.f, 1.7f, -6.f), 1.4f, 1.2f, vec3(-1, 0, 0), trim);
                door(-2.5f, -1.5f, vec4(0.25f, 0.13f, 0.07f, 0));
                deco(vec3(-9.5f, 5.2f, -7.f), vec3(0.55f, 2.2f, 0.55f), vec4(0.48f, 0.46f, 0.43f, 0));  // chimney
                deco(vec3(-5.f, 0.15f, 0.2f), vec3(6.f, 0.15f, 1.7f), vec4(0.33f, 0.22f, 0.13f, 0));  // porch deck
                solid(vec3(6.5f, 1.8f, -5.f), vec3(4.5f, 1.8f, 5.f), wood);
                gable(2.f, 11.f, -10.f, 0.f, 3.6f, 1.9f, roof, wood);
                garageDoor(3.2f, 9.8f, 0.f, 2.6f);
                break;
            }
            case 1: {  // farmhouse + red barn + silo
                vec4 white(0.86f, 0.85f, 0.81f, 0), roof(0.22f, 0.23f, 0.25f, 0), red(0.50f, 0.11f, 0.07f, 0), trim(0.95f, 0.95f, 0.93f, 0);
                solid(vec3(-5.f, 3.2f, -6.f), vec3(6.f, 3.2f, 4.5f), white);
                gable(-11.f, 1.f, -10.5f, -1.5f, 6.4f, 3.f, roof, white);
                for (float x : {-9.f, -6.f, -2.5f}) window(vec3(x, 4.6f, -1.5f), 1.1f, 1.5f, vec3(0, 0, 1), trim);
                for (float x : {-9.f, -6.f}) window(vec3(x, 1.6f, -1.5f), 1.1f, 1.5f, vec3(0, 0, 1), trim);
                door(-2.5f, -1.5f, vec4(0.15f, 0.25f, 0.18f, 0));
                deco(vec3(-5.f, 3.1f, -0.6f), vec3(6.2f, 0.08f, 1.1f), roof);  // porch roof
                for (float x : {-10.6f, -6.f, 0.6f}) deco(vec3(x, 1.5f, 0.35f), vec3(0.09f, 1.5f, 0.09f), trim);
                solid(vec3(6.8f, 2.6f, -5.f), vec3(4.8f, 2.6f, 5.5f), red);
                gable(2.f, 11.6f, -10.5f, 0.5f, 5.2f, 3.2f, roof, red);
                garageDoor(3.4f, 10.2f, 0.5f, 3.2f);
                for (float x : {3.4f, 10.2f}) deco(vec3(x, 1.7f, 0.62f), vec3(0.1f, 1.7f, 0.05f), trim);
                city.cylinder(P(vec3(14.5f, 0, -10.f)), 2.4f, 11.f, 14, vec4(0.62f, 0.63f, 0.64f, 7));
                boxes.push_back({P(vec3(14.5f, 0, -10.f)).x - 2.4f, P(vec3(14.5f, 0, -10.f)).z - 2.4f, P(vec3(14.5f, 0, -10.f)).x + 2.4f, P(vec3(14.5f, 0, -10.f)).z + 2.4f, e.pos.y + 11.f});
                break;
            }
            case 2: {  // modernist villa: stacked white volumes, flat roofs, lots of glass
                vec4 white(0.90f, 0.90f, 0.88f, 0), dark(0.18f, 0.18f, 0.19f, 0), wood(0.45f, 0.30f, 0.18f, 0);
                solid(vec3(-5.5f, 1.8f, -6.f), vec3(6.5f, 1.8f, 4.5f), white);
                deco(vec3(-5.5f, 1.75f, -1.45f), vec3(5.6f, 1.45f, 0.06f), vec4(1, 1, 1, 10));  // full-height glazing
                for (float x : {-10.f, -7.f, -4.f, -1.f}) deco(vec3(x, 1.75f, -1.38f), vec3(0.05f, 1.45f, 0.04f), dark);
                solid(vec3(-3.5f, 5.2f, -7.f), vec3(7.5f, 1.6f, 4.f), wood);  // cantilevered upper floor
                deco(vec3(-3.5f, 5.2f, -2.95f), vec3(7.f, 1.1f, 0.06f), vec4(1, 1, 1, 10));
                deco(vec3(-3.5f, 6.9f, -7.f), vec3(7.7f, 0.12f, 4.2f), dark);  // roof slab
                deco(vec3(-5.5f, 3.65f, -6.f), vec3(6.7f, 0.1f, 4.7f), dark);
                solid(vec3(6.5f, 1.7f, -5.f), vec3(4.5f, 1.7f, 5.f), white);
                deco(vec3(6.5f, 3.5f, -5.f), vec3(4.7f, 0.12f, 5.2f), dark);
                garageDoor(3.f, 10.f, 0.f, 2.7f);
                deco(vec3(-6.f, 0.02f, 4.5f), vec3(5.f, 0.06f, 2.2f), vec4(0.12f, 0.45f, 0.62f, 10));  // pool
                deco(vec3(-6.f, 0.01f, 4.5f), vec3(5.6f, 0.04f, 2.8f), vec4(0.85f, 0.84f, 0.8f, 0));
                break;
            }
            case 3: {  // mountain lodge: stone ground floor, timber upper, steep roof
                vec4 stone(0.46f, 0.44f, 0.41f, 0), wood(0.36f, 0.22f, 0.12f, 0), roof(0.14f, 0.15f, 0.15f, 0), trim(0.2f, 0.12f, 0.07f, 0);
                solid(vec3(-5.f, 1.4f, -6.f), vec3(6.5f, 1.4f, 5.f), stone);
                solid(vec3(-5.f, 4.f, -6.f), vec3(6.5f, 1.2f, 5.f), wood);
                gable(-11.5f, 1.5f, -11.f, -1.f, 5.2f, 4.2f, roof, wood);
                for (float x : {-9.5f, -6.5f, -3.5f}) window(vec3(x, 4.f, -1.f), 1.6f, 1.4f, vec3(0, 0, 1), trim);
                for (float x : {-9.5f, -6.5f}) window(vec3(x, 1.4f, -1.f), 1.4f, 1.2f, vec3(0, 0, 1), trim);
                door(-2.5f, -1.f, trim);
                deco(vec3(-5.f, 2.85f, 0.3f), vec3(6.6f, 0.1f, 1.3f), wood);  // balcony
                deco(vec3(-5.f, 3.4f, 1.55f), vec3(6.6f, 0.5f, 0.05f), trim);
                deco(vec3(-1.f, 7.5f, -8.f), vec3(0.6f, 2.5f, 0.6f), stone);  // chimney
                solid(vec3(6.8f, 1.9f, -5.f), vec3(4.8f, 1.9f, 5.2f), stone);
                gable(2.f, 11.6f, -10.2f, 0.2f, 3.8f, 2.4f, roof, stone);
                garageDoor(3.3f, 10.3f, 0.2f, 2.7f);
                break;
            }
            default: {  // harbour loft: corrugated warehouse with a big roller door
                vec4 tin(0.32f, 0.40f, 0.43f, 16), brick(0.45f, 0.22f, 0.16f, 0), dark(0.17f, 0.17f, 0.18f, 0), trim(0.12f, 0.12f, 0.13f, 0);
                solid(vec3(-5.f, 3.5f, -6.f), vec3(6.5f, 3.5f, 5.f), brick);
                for (float x : {-10.f, -7.f, -4.f}) {
                    window(vec3(x, 1.8f, -1.f), 1.8f, 2.2f, vec3(0, 0, 1), trim);
                    window(vec3(x, 5.1f, -1.f), 1.8f, 2.2f, vec3(0, 0, 1), trim);
                }
                door(-1.5f, -1.f, dark);
                deco(vec3(-5.f, 7.1f, -6.f), vec3(6.7f, 0.15f, 5.2f), dark);
                solid(vec3(7.f, 2.9f, -5.f), vec3(5.f, 2.9f, 5.5f), tin);
                deco(vec3(7.f, 5.9f, -5.f), vec3(5.3f, 0.12f, 5.8f), dark);
                garageDoor(3.f, 11.f, 0.5f, 4.2f);
                deco(vec3(7.f, 5.0f, 0.62f), vec3(2.2f, 0.32f, 0.05f), vec4(1.f, 0.25f, 0.55f, 15));  // neon sign
                break;
            }
        }
        // mailbox / lamp post at the end of the drive
        deco(vec3(12.6f, 1.6f, e.roadGap - 7.f), vec3(0.08f, 1.6f, 0.08f), vec4(0.15f, 0.15f, 0.16f, 0));
        deco(vec3(12.6f, 3.25f, e.roadGap - 7.f), vec3(0.22f, 0.12f, 0.22f), vec4(1.f, 0.88f, 0.65f, 15));
    }
}
