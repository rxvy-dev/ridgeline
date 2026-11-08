// Car customization: liveries, livery colour, race number, window tint, aero kits,
// ride height, and the animated cockpit steering wheel.
#include "game.h"
#include "washi.h"
#include "carimport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

const char* kLiveryNames[] = {"None", "Twin Stripes", "Contrast Roof", "Two-Tone", "Shoulder Stripes",
                              "Race Number", "Speed Slashes", "Carbon Bonnet", "Split Fade", "Chequered Tail",
                              "Flames", "Camo", "Hex Grid", "Colour Fade", "Le Mans Hoop"};
const char* kTintNames[] = {"None", "Light (35%)", "Dark (15%)", "Limo (5%)"};
const char* kAeroNames[] = {"Stock", "Ducktail Spoiler", "GT Wing", "GT Wing + Splitter"};
const char* kRideNames[] = {"Stock", "Lowered (-35 mm)", "Slammed (-70 mm)"};
const char* kCamberNames[] = {"Stock (0 deg)", "Street (-2.5 deg)", "Aggressive (-6 deg)", "Stance (-11 deg)"};
const int kCamberPrice[] = {0, 3000, 6500, 10000};

static const vec4 C_ACCENT(0.96f, 0.10f, 0.50f, 1.f);
static const vec4 C_WHITE(1, 1, 1, 1);
static const vec4 C_DIM(1, 1, 1, 0.6f);

void Game::applyCosmetics(Racer& r, const OwnedCar& oc) const {
    r.livery = std::clamp(oc.livery, 0, kLiveryCount - 1);
    r.livCol = kPaints[std::clamp(oc.livCol, 0, kPaintCount - 1)];
    r.tint = std::array<float, kTintCount>{0.f, 0.45f, 0.75f, 0.95f}[std::clamp(oc.tint, 0, kTintCount - 1)];
    r.aero = std::clamp(oc.aero, 0, kAeroCount - 1);
    r.ride = std::array<float, kRideCount>{0.f, 0.035f, 0.07f}[std::clamp(oc.ride, 0, kRideCount - 1)];
    r.number = oc.number > 0 ? oc.number : 1 + (oc.spec * 37 + 11) % 98;
    const float camb[kCamberCount] = {0.f, 2.5f, 6.f, 11.f};
    r.camber = camb[std::clamp(oc.camber, 0, kCamberCount - 1)] * PI / 180.f;
}

void Game::setCosmeticUniforms(Shader& s, const Racer& r, bool body) const {
    s.set("uBody", body ? 1 : 0);
    s.set("uLivery", body ? r.livery : 0);
    s.set("uLivCol", r.livCol);
    s.set("uLivNum", r.number);
    s.set("uWinTint", r.tint);
    s.set("uCarDims", r.model ? r.model->dims : vec3(4.4f, 1.9f, 1.3f));
}

mat4 Game::bodyFrame(const Racer& r) const {
    mat4 b = r.car.fxSpin != 0.f ? r.car.bodyMatrix() * rotateY(r.car.fxSpin) : r.car.bodyMatrix();
    return r.ride > 0.f ? b * translate(vec3(0, -r.ride, 0)) : b;
}

// Steering wheel turned about its column (tilted ~20 degrees up towards the driver).
// Full lock on the road wheels maps to half the chosen lock-to-lock rotation.
mat4 Game::steeringFrame(const Racer& r, vec3 c) const {
    const float locks[4] = {180.f, 360.f, 540.f, 900.f};
    float lock = locks[std::clamp(settings.steerLock, 0, 3)] * PI / 180.f;
    float a = std::clamp(r.car.wheelSteer / 0.6f, -1.f, 1.f) * lock * 0.5f;
    const float tilt = 0.35f;
    return bodyFrame(r) * translate(c) * rotateX(-tilt) * rotateZ(a) * rotateX(tilt) * translate(vec3(0, 0, 0) - c);
}

// Wing / ducktail / splitter, built once per car model from its measured rear deck.
const Mesh* Game::aeroMesh(const Racer& r) {
    if (r.aero <= 0 || !r.model) return nullptr;
    int mi = int(r.model - models.data());
    int key = mi * 8 + r.aero;
    auto it = aeroCache.find(key);
    if (it != aeroCache.end()) return &it->second;
    const CarModel& m = *r.model;
    float L = m.dims.x, W = m.dims.y;
    float dy = m.deckY, dz = m.deckZ;
    MeshBuilder b;
    const vec4 paint(1, 1, 1, 1), satin(1, 1, 1, 9), carbon(1, 1, 1, 18);
    if (r.aero == 1) {
        // ducktail: a swept lip along the trailing edge of the boot lid
        float hw = W * 0.4f;
        for (int k = 0; k < 6; k++) {
            float x0 = -hw + 2 * hw * k / 6.f, x1 = -hw + 2 * hw * (k + 1) / 6.f;
            auto prof = [&](float x, int i) {
                float e = 1.f - std::pow(std::fabs(x) / hw, 4.f) * 0.55f;  // tapers towards the corners
                const float zz[3] = {0.24f, -0.03f, -0.06f};
                const float yy[3] = {0.0f, 0.075f, 0.055f};
                return vec3(x, dy - 0.01f + yy[i] * e, dz + zz[i] * (0.6f + 0.4f * e));
            };
            b.flatQuad(prof(x0, 0), prof(x1, 0), prof(x1, 1), prof(x0, 1), paint);
            b.flatQuad(prof(x0, 1), prof(x1, 1), prof(x1, 2), prof(x0, 2), paint);
            b.flatQuad(prof(x0, 2), prof(x1, 2), vec3(x1, dy - 0.03f, dz - 0.06f), vec3(x0, dy - 0.03f, dz - 0.06f), paint);
        }
    } else {
        // GT wing: two-element plane on swan-neck uprights with big end plates
        float span = W * 0.42f, h = 0.24f;
        float y = dy + h, z = dz + 0.02f;
        mat4 pitch = translate(vec3(0, y, z)) * rotateX(0.12f);
        b.box(vec3(0, 0, 0), vec3(span, 0.016f, 0.16f), carbon, &pitch);
        mat4 flap = translate(vec3(0, y + 0.055f, z - 0.17f)) * rotateX(0.55f);
        b.box(vec3(0, 0, 0), vec3(span, 0.01f, 0.06f), carbon, &flap);
        b.box(vec3(0, y + 0.1f, z - 0.215f), vec3(span, 0.012f, 0.004f), satin);  // gurney
        for (int side = -1; side <= 1; side += 2) {
            b.box(vec3(side * (span + 0.006f), y + 0.02f, z - 0.03f), vec3(0.006f, 0.11f, 0.25f), carbon);  // end plate
            float ux = side * W * 0.2f;
            // swan neck: rises behind the wing and hooks over to the top surface
            b.box(vec3(ux, dy + h * 0.45f, z + 0.03f), vec3(0.011f, h * 0.5f + 0.02f, 0.045f), satin);
            b.box(vec3(ux, y + 0.04f, z - 0.03f), vec3(0.011f, 0.035f, 0.075f), satin);
            b.box(vec3(ux, dy + 0.005f, z + 0.03f), vec3(0.05f, 0.008f, 0.07f), satin);  // base plate
        }
        if (r.aero == 3) {
            float fz = L * 0.5f - 0.2f;
            b.box(vec3(0, 0.12f, fz), vec3(W * 0.4f, 0.011f, 0.1f), carbon);  // splitter blade
            for (int side = -1; side <= 1; side += 2) {
                b.box(vec3(side * W * 0.41f, 0.16f, fz + 0.02f), vec3(0.006f, 0.05f, 0.1f), carbon);  // fences
                mat4 can = translate(vec3(side * W * 0.44f, 0.36f, L * 0.5f - 0.22f)) * rotateZ(side * -0.2f) * rotateX(-0.15f);
                b.box(vec3(0, 0, 0), vec3(0.07f, 0.006f, 0.08f), carbon, &can);  // canards
            }
        }
    }
    Mesh mesh;
    mesh.upload(b.v, b.idx);
    return &(aeroCache[key] = mesh);
}

// ---------------------------------------------------------------- menu
namespace {
enum CustomRow { CR_PAINT, CR_LIVERY, CR_LIVCOL, CR_NUMBER, CR_TINT, CR_AERO, CR_RIDE, CR_CAMBER, CR_STRIPES, CR_COUNT };
const char* kRowNames[CR_COUNT] = {"PAINT", "LIVERY", "LIVERY COLOUR", "RACE NUMBER", "WINDOW TINT", "AERO KIT", "RIDE HEIGHT", "CAMBER KIT", "RACING STRIPES"};
}

void Game::customInput(Action a) {
    OwnedCar& oc = garage[std::clamp(subSel, 0, int(garage.size()) - 1)];
    if (a == A_CONFIRM && custSel == CR_CAMBER && oc.camber > oc.camberOwned) {
        int price = kCamberPrice[oc.camber];
        if (credits < price) { toast("Not enough credits for that camber kit", vec4(1, 0.4f, 0.3f, 1)); audio.beep(220, 0.2f); return; }
        credits -= price;
        oc.camberOwned = oc.camber;
        if (subSel == current) setPlayerCar(current);
        toast(std::string(kCamberNames[oc.camber]) + " camber fitted", vec4(0.4f, 1, 0.5f, 1));
        audio.beep(1400, 0.2f);
        save();
        return;
    }
    if (a == A_BACK || a == A_CONFIRM) {
        if (oc.camber > oc.camberOwned) oc.camber = oc.camberOwned;  // previewed but not bought
        if (subSel == current) setPlayerCar(current);
        save();
        state = ST_GARAGE;
        stateTime = 0;
        audio.beep(660, 0.05f);
        return;
    }
    if (a == A_UP) { custSel = (custSel - 1 + CR_COUNT) % CR_COUNT; audio.beep(880, 0.03f); return; }
    if (a == A_DOWN) { custSel = (custSel + 1) % CR_COUNT; audio.beep(880, 0.03f); return; }
    int d = a == A_LEFT ? -1 : a == A_RIGHT ? 1 : a == A_TOGGLE ? 1 : 0;
    if (!d) return;
    auto cyc = [&](int& v, int n) { v = (v + d + n) % n; };
    switch (custSel) {
        case CR_PAINT: cyc(oc.paint, kPaintCount); break;
        case CR_LIVERY: cyc(oc.livery, kLiveryCount); break;
        case CR_LIVCOL: cyc(oc.livCol, kPaintCount); break;
        case CR_NUMBER: {
            int n = oc.number > 0 ? oc.number : 1 + (oc.spec * 37 + 11) % 98;
            oc.number = (n - 1 + d + 99) % 99 + 1;
            if (oc.livery != 5) oc.livery = 5;
            break;
        }
        case CR_TINT: cyc(oc.tint, kTintCount); break;
        case CR_AERO: cyc(oc.aero, kAeroCount); break;
        case CR_RIDE: cyc(oc.ride, kRideCount); break;
        case CR_CAMBER: cyc(oc.camber, kCamberCount); break;
        case CR_STRIPES: oc.stripes = !oc.stripes; break;
    }
    audio.beep(1100, 0.04f);
}

void Game::drawCustomize() {
    float u = hud.ui;
    OwnedCar& oc = garage[std::clamp(subSel, 0, int(garage.size()) - 1)];
    hud.rectV(0, hud.h - 120 * u, hud.w, 120 * u, wa::A(wa::PAPER, 0), wa::A(wa::PAPER, 0.85f));
    float slide = 1.f - clampf(stateTime / 0.4f, 0.f, 1.f);
    slide = slide * slide * slide;
    float x0 = 48 * u;
    drawMenuHeader(x0, 30 * u, hud.w - 2 * x0, "CUSTOMIZE");

    float pw = 400 * u, px = x0 - slide * 140 * u, py = 128 * u, rh = 52 * u;
    themePanel(px - 10 * u, py - 10 * u, pw + 20 * u, CR_COUNT * rh + 14 * u, 1.f, true);
    int num = oc.number > 0 ? oc.number : 1 + (oc.spec * 37 + 11) % 98;
    for (int i = 0; i < CR_COUNT; i++) {
        float ry = py + i * rh;
        bool sel = i == custSel;
        themeRow(px, ry, pw, rh - 6 * u, sel);
        hud.text(px + 20 * u, ry + 6 * u, 11 * u, kRowNames[i], sel ? wa::GOLD : wa::VERM, 0, false);
        std::string val;
        vec3 swatch(-1, -1, -1);
        switch (i) {
            case CR_PAINT: val = kPaintNames[oc.paint]; swatch = kPaints[oc.paint]; break;
            case CR_LIVERY: val = kLiveryNames[oc.livery]; break;
            case CR_LIVCOL: val = kPaintNames[oc.livCol]; swatch = kPaints[oc.livCol]; break;
            case CR_NUMBER: val = "#" + std::to_string(num) + (oc.livery == 5 ? "" : "   (shown with Race Number livery)"); break;
            case CR_TINT: val = kTintNames[oc.tint]; break;
            case CR_AERO: val = kAeroNames[oc.aero]; break;
            case CR_RIDE: val = kRideNames[oc.ride]; break;
            case CR_CAMBER:
                val = kCamberNames[oc.camber];
                if (oc.camber > oc.camberOwned) val += "   CR " + fmtThousands(kCamberPrice[oc.camber]) + (pad ? "  (A) buy" : "  ENTER buy");
                break;
            case CR_STRIPES: val = oc.stripes ? "Fitted" : "None"; break;
        }
        float vx = px + 20 * u;
        if (swatch.x >= 0) {
            hud.rect(vx, ry + 26 * u, 18 * u, 18 * u, vec4(swatch.x, swatch.y, swatch.z, 1));
            inkBox(vx, ry + 26 * u, 18 * u, 18 * u, 1.5f * u, sel ? wa::PAPER : wa::INK);
            vx += 28 * u;
        }
        hud.text(vx, ry + 23 * u, 19 * u, val, sel ? wa::PAPER : wa::INK, 0, false);
        if (sel) {
            float ax = px + pw - 22 * u;
            float nudge = std::sin(R.time * 6.f) * 2 * u;
            hud.text(ax - 34 * u - nudge, ry + 18 * u, 22 * u, "<", wa::GOLD, 1, false);
            hud.text(ax + nudge, ry + 18 * u, 22 * u, ">", wa::GOLD, 1, false);
        }
    }
    // ---- the car's name, top right
    {
        const CarSpec& cs = kCars[oc.spec];
        float cw = std::max(hud.textWidth(15 * u, cs.make), hud.textWidth(26 * u, cs.model)) + 44 * u;
        float cx = hud.w - x0 - cw + slide * 140 * u, cy = 124 * u;
        themePanel(cx, cy, cw, 76 * u, 1.f, true);
        hud.text(cx + 22 * u, cy + 12 * u, 15 * u, cs.make, wa::VERM, 0, false);
        hud.text(cx + 22 * u, cy + 32 * u, 26 * u, cs.model, wa::INK, 0, false);
    }
    // ---- every choice for the selected row, laid out under the car so you can see what's next
    {
        std::vector<std::string> opts;
        std::vector<vec3> sw;
        int cur = 0;
        auto names = [&](const char* const* arr, int n, int c) { for (int i = 0; i < n; i++) opts.push_back(arr[i]); cur = c; };
        switch (custSel) {
            case CR_PAINT: for (int i = 0; i < kPaintCount; i++) sw.push_back(kPaints[i]); cur = oc.paint; break;
            case CR_LIVCOL: for (int i = 0; i < kPaintCount; i++) sw.push_back(kPaints[i]); cur = oc.livCol; break;
            case CR_LIVERY: names(kLiveryNames, kLiveryCount, oc.livery); break;
            case CR_TINT: names(kTintNames, kTintCount, oc.tint); break;
            case CR_AERO: names(kAeroNames, kAeroCount, oc.aero); break;
            case CR_RIDE: names(kRideNames, kRideCount, oc.ride); break;
            case CR_CAMBER: names(kCamberNames, kCamberCount, oc.camber); break;
            case CR_STRIPES: opts = {"None", "Fitted"}; cur = oc.stripes ? 1 : 0; break;
            case CR_NUMBER: for (int k = -3; k <= 3; k++) opts.push_back("#" + std::to_string((num - 1 + k + 99) % 99 + 1)); cur = 3; break;
        }
        float sx0 = px + pw + 40 * u + slide * 200 * u, sx1 = hud.w - x0, sy = hud.h - 150 * u;
        float sw0 = sx1 - sx0;
        float ent = 1.f - std::min(1.f, (R.time - custStripT) * 5.f);
        if (custStripRow != custSel) { custStripRow = custSel; custStripT = R.time; }
        float rise = std::max(0.f, ent) * 16 * u;
        themePanel(sx0, sy + rise, sw0, 88 * u, 1.f, true);
        hud.text(sx0 + 18 * u, sy + rise + 10 * u, 12 * u, kRowNames[custSel], wa::VERM, 0, false);
        hud.text(sx1 - 18 * u, sy + rise + 10 * u, 12 * u, std::to_string(cur + 1) + " / " + std::to_string(std::max(opts.size(), sw.size())), wa::MUTED, 2, false);
        float iy = sy + rise + 34 * u, ih = 40 * u;
        if (!sw.empty()) {
            float cell = std::min(40 * u, (sw0 - 36 * u) / sw.size());
            for (int i = 0; i < int(sw.size()); i++) {
                float qx = sx0 + 18 * u + i * cell;
                bool on = i == cur;
                if (on) hud.rect(qx - 4 * u, iy - 4 * u, cell, ih + 8 * u, wa::INK);
                hud.rect(qx, iy, cell - 8 * u, ih, vec4(sw[i].x, sw[i].y, sw[i].z, 1));
            }
        } else {
            // chips, scrolled so the current one is in view
            std::vector<float> w(opts.size());
            float total = 0;
            for (size_t i = 0; i < opts.size(); i++) { w[i] = hud.textWidth(15 * u, opts[i]) + 30 * u; total += w[i] + 8 * u; }
            float before = 0;
            for (int i = 0; i < cur; i++) before += w[i] + 8 * u;
            float avail = sw0 - 36 * u;
            float off = total <= avail ? 0.f : clampf(before + w[cur] * 0.5f - avail * 0.5f, 0.f, total - avail);
            float qx = sx0 + 18 * u - off;
            for (size_t i = 0; i < opts.size(); i++) {
                bool on = int(i) == cur;
                if (qx >= sx0 + 16 * u && qx + w[i] <= sx1 - 16 * u) {
                    float a = 1.f;
                    if (on) hud.rect(qx, iy, w[i], ih, wa::INK);
                    else inkBox(qx, iy, w[i], ih, 1.5f * u, wa::A(wa::INK, 0.5f));
                    if (on) hud.rect(qx, iy + ih - 4 * u, w[i], 4 * u, wa::VERM);
                    hud.text(qx + w[i] * 0.5f, iy + 11 * u, 15 * u, opts[i], on ? wa::PAPER : wa::INK, 1, false);
                }
                qx += w[i] + 8 * u;
            }
            // more choices off either end
            if (off > 1.f) hud.text(sx0 + 6 * u, iy + 9 * u, 18 * u, "<", wa::VERM, 0, false);
            if (total - off > avail + 1.f) hud.text(sx1 - 6 * u, iy + 9 * u, 18 * u, ">", wa::VERM, 2, false);
        }
    }
    drawThemePrompts(pad ? "D-pad up/down choose   left/right change   (A) done   (B) back"
                         : "UP/DOWN choose   LEFT/RIGHT change   ENTER done   ESC back");
}
