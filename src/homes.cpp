// Houses: buying, parking up at home (garage), map markers.
#include "game.h"
#include <algorithm>
#include <cmath>

static const vec4 H_ACCENT(0.96f, 0.10f, 0.50f, 1.f);
static const vec4 H_WHITE(1, 1, 1, 1);
static const vec4 H_DIM(1, 1, 1, 0.6f);
static const vec4 H_PANEL(0.05f, 0.05f, 0.06f, 0.78f);
static const vec4 H_HOME(0.35f, 0.85f, 1.f, 1), H_SALE(1.f, 0.78f, 0.25f, 1);

void Game::updateHomes() {
    nearHome = -1;
    if (player.car.speed() > 8.f) return;
    for (int i = 0; i < int(world.estates.size()); i++) {
        vec3 d = world.estates[i].spawn - player.car.pos;
        if (d.x * d.x + d.z * d.z < 11.f * 11.f) nearHome = i;
    }
}

void Game::buyEstate(int i) {
    const Estate& e = world.estates[i];
    if (credits < e.price) {
        toast("Not enough credits - " + e.name + " costs CR " + fmtThousands(e.price), vec4(1, 0.4f, 0.3f, 1));
        audio.beep(220, 0.2f);
        return;
    }
    credits -= e.price;
    ownedEstates.push_back(i);
    wheelspins++;
    toast("You bought " + e.name + "!", vec4(0.4f, 1, 0.5f, 1));
    toast("Fast travel unlocked  +1 Wheelspin", H_HOME);
    audio.beep(1500, 0.3f);
    save();
}

// ENTER / (Y) at a house: buy it, or open its garage.
bool Game::homeInteract() {
    if (nearHome < 0) return false;
    if (!ownsEstate(nearHome)) { buyEstate(nearHome); return true; }
    garageHome = nearHome;
    state = ST_GARAGE;
    subSel = current;
    stateTime = 0;
    audio.beep(990, 0.06f);
    return true;
}

void Game::drawHomePrompt() {
    float u = hud.ui;
    const Estate& e = world.estates[nearHome];
    bool own = ownsEstate(nearHome);
    std::string title = e.name;
    std::string sub = own ? std::string(pad ? "(Y) Garage" : "ENTER  Garage") + "   |   fast travel from the map"
                          : std::string(pad ? "(Y) Buy for CR " : "ENTER  Buy for CR ") + fmtThousands(e.price);
    float pw = std::max({hud.textWidth(22 * u, title), hud.textWidth(15 * u, sub), hud.textWidth(14 * u, e.blurb)}) + 70 * u;
    float px = hud.w * 0.5f - pw * 0.5f, py = hud.h - 196 * u;
    card(px, py, pw, 96 * u, 1.f, false, 151);
    hud.rect(px + 2 * u, py + 2 * u, 10 * u, 92 * u, own ? vec4(0.18f, 0.27f, 0.45f, 1) : vec4(0.80f, 0.60f, 0.18f, 1));
    hud.text(hud.w * 0.5f, py + 12 * u, 11 * u, own ? "YOUR HOUSE" : "HOUSE FOR SALE", own ? vec4(0.18f, 0.27f, 0.45f, 1) : vec4(0.80f, 0.60f, 0.18f, 1), 1, false);
    hud.text(hud.w * 0.5f, py + 26 * u, 22 * u, title, vec4(0.102f, 0.090f, 0.078f, 1), 1, false);
    hud.text(hud.w * 0.5f, py + 54 * u, 14 * u, e.blurb, vec4(0.45f, 0.41f, 0.36f, 1), 1, false);
    vec4 sc = own || credits >= e.price ? vec4(0.102f, 0.090f, 0.078f, 1) : vec4(0.784f, 0.196f, 0.165f, 1);
    hud.text(hud.w * 0.5f, py + 72 * u, 15 * u, sub, sc, 1, false);
}

// Beacons over the garage doors: gold for sale, blue when owned. Called inside
// the additive marker pass (unlit shader bound).
void Game::drawEstateMarkers() {
    for (int i = 0; i < int(world.estates.size()); i++) {
        const Estate& e = world.estates[i];
        vec4 c = ownsEstate(i) ? H_HOME : H_SALE;
        R.unlit.set("uTint", vec4(c.x, c.y, c.z, 0.55f));
        R.unlit.set("uModel", translate(e.spawn + vec3(0, 0.15f, 0)) * scale(vec3(0.8f, 0.45f, 0.8f)));
        beaconMesh.draw();
        ringMesh.draw();
    }
}
