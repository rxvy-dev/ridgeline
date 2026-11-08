// EventLab: build a race on any road (start, length/laps, direction, rivals, class,
// weather), test it, save it to My Events, and share it as a short code that other
// players paste in. No server: the code is the whole event.
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static const vec4 L_ACCENT(0.96f, 0.10f, 0.50f, 1.f);
static const vec4 L_CYAN(0.20f, 0.80f, 0.95f, 1.f);
static const vec4 L_WHITE(1, 1, 1, 1);
static const vec4 L_DIM(1, 1, 1, 0.6f);

namespace {
enum LabRow { LR_ROAD, LR_START, LR_LENGTH, LR_DIR, LR_RIVALS, LR_CLASS, LR_WEATHER, LR_TEST, LR_SAVE, LR_COPY, LR_PASTE, LR_COUNT };
const char* kLabRows[LR_COUNT] = {"ROAD", "START POINT", "LENGTH", "DIRECTION", "RIVALS", "RIVAL CLASS", "WEATHER",
                                  "TEST DRIVE", "SAVE TO MY EVENTS", "COPY SHARE CODE", "PASTE SHARE CODE"};
const char* kClassNames[6] = {"Match my car", "D", "C", "B", "A", "S"};
}

void Game::rebuildCustomEvents() {
    if (builtinEvents <= 0) return;
    events.resize(builtinEvents);
    for (int i = 0; i < int(customEvents.size()); i++) {
        EventDef e;
        if (!makeCustomEvent(world, customEvents[i], e)) continue;
        e.custom = i;
        events.push_back(e);
    }
    if (bestPlace.size() < events.size()) bestPlace.resize(events.size(), 0);
}


void Game::openEventLab() {
    if (labEv.road < 0 || labEv.road >= int(world.roads.size())) labEv = CustomEvent();
    labSel = 0;
    state = ST_EVENTLAB;
    stateTime = 0;
    labOk = makeCustomEvent(world, labEv, labPreview);
}

bool Game::importEventCode() {
    char* clip = SDL_GetClipboardText();
    std::string code = clip ? clip : "";
    SDL_free(clip);
    CustomEvent ce;
    if (!decodeEvent(code, ce) || ce.road >= int(world.roads.size())) {
        toast("No EventLab code on the clipboard (they look like RL1-XXXXXXXX-XXXXXXXX-XX)", vec4(1, 0.45f, 0.4f, 1));
        audio.beep(220, 0.2f);
        return false;
    }
    std::string enc = encodeEvent(ce);
    for (auto& c : customEvents)
        if (encodeEvent(c) == enc) { toast("You already have that event", L_CYAN); return true; }
    customEvents.push_back(ce);
    rebuildCustomEvents();
    save();
    toast("Imported: " + autoEventName(world, ce), vec4(0.4f, 1, 0.5f, 1));
    audio.beep(1400, 0.2f);
    return true;
}

void Game::labInput(Action a) {
    if (a == A_BACK) { state = ST_PAUSE; menuTab = 3; menuSel = 0; return; }
    if (a == A_UP) { labSel = (labSel - 1 + LR_COUNT) % LR_COUNT; audio.beep(880, 0.03f); return; }
    if (a == A_DOWN) { labSel = (labSel + 1) % LR_COUNT; audio.beep(880, 0.03f); return; }
    CustomEvent& c = labEv;
    int nr = int(world.roads.size());
    bool closed = c.road >= 0 && c.road < nr && world.roads[c.road].closed;
    int d = a == A_LEFT ? -1 : a == A_RIGHT ? 1 : 0;
    if (d && labSel < LR_TEST) {
        switch (labSel) {
            case LR_ROAD: c.road = (c.road + d + nr) % nr; c.start = 0; break;
            case LR_START: c.start = (c.start + d * 5 + 100) % 100; break;
            case LR_LENGTH:
                if (closed) c.laps = std::clamp(c.laps + d, 1, 10);
                else c.length = std::clamp(c.length + d, 1, 40);
                break;
            case LR_DIR: c.reverse = !c.reverse; break;
            case LR_RIVALS: c.opponents = std::clamp(c.opponents + d, 0, 7); break;
            case LR_CLASS: c.cls = (c.cls + d + 6) % 6; break;
            case LR_WEATHER: c.wet = !c.wet; break;
        }
        labOk = makeCustomEvent(world, labEv, labPreview);
        audio.beep(1100, 0.03f);
        return;
    }
    if (a != A_CONFIRM) return;
    switch (labSel) {
        case LR_TEST:
            if (!labOk) { toast("That route is too short - pick another start or length", vec4(1, 0.45f, 0.4f, 1)); return; }
            // never stack test drives: clear any earlier one (and its race) first
            if (raceEvent >= 0) leaveRace(); else rebuildCustomEvents();
            events.push_back(labPreview);
            if (bestPlace.size() < events.size()) bestPlace.resize(events.size(), 0);
            enterFreeRoam(labPreview.route.pts[0], labPreview.startYaw);
            startRace(int(events.size()) - 1);
            break;
        case LR_SAVE: {
            if (!labOk) { toast("That route is too short to save", vec4(1, 0.45f, 0.4f, 1)); return; }
            std::string enc = encodeEvent(c);
            for (auto& e : customEvents) if (encodeEvent(e) == enc) { toast("Already in My Events", L_CYAN); return; }
            customEvents.push_back(c);
            rebuildCustomEvents();
            save();
            toast("Saved to My Events - it's on the map now", vec4(0.4f, 1, 0.5f, 1));
            audio.beep(1400, 0.2f);
            break;
        }
        case LR_COPY:
            SDL_SetClipboardText(encodeEvent(c).c_str());
            toast("Share code copied: " + encodeEvent(c), L_CYAN);
            audio.beep(1300, 0.1f);
            break;
        case LR_PASTE: {
            char* clip = SDL_GetClipboardText();
            std::string code = clip ? clip : "";
            SDL_free(clip);
            CustomEvent ce;
            if (decodeEvent(code, ce) && ce.road < nr) {
                labEv = ce;
                labOk = makeCustomEvent(world, labEv, labPreview);
                toast("Loaded " + autoEventName(world, ce), vec4(0.4f, 1, 0.5f, 1));
            } else toast("No EventLab code on the clipboard", vec4(1, 0.45f, 0.4f, 1));
            break;
        }
    }
}

// whole-island map with an optional highlighted route
void Game::drawRouteMap(float mx, float my, float S, const Route* rt, vec4 col) {
    float u = hud.ui;
    vec2 p[4] = {{mx, my}, {mx + S, my}, {mx + S, my + S}, {mx, my + S}};
    vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    hud.rect(mx + 8 * u, my + 10 * u, S, S, vec4(0, 0, 0, 0.5f));
    hud.mapQuad(p, uv, vec4(0.78f, 0.8f, 0.8f, 1));
    auto toM = [&](vec3 w) { return vec2(mx + (w.x + World::HALF) / (2 * World::HALF) * S, my + (World::HALF - w.z) / (2 * World::HALF) * S); };
    for (const Road& rd : world.roads)
        for (size_t k = 4; k < rd.pts.size(); k += 4) hud.line(toM(rd.pts[k - 4]), toM(rd.pts[k]), 2.f * u, vec4(1, 1, 1, 0.55f));
    if (rt && rt->pts.size() > 2) {
        for (size_t k = 2; k < rt->pts.size(); k += 2) {
            hud.line(toM(rt->pts[k - 2]), toM(rt->pts[k]), 7.f * u, vec4(0, 0, 0, 0.6f));
            hud.line(toM(rt->pts[k - 2]), toM(rt->pts[k]), 4.f * u, col);
        }
        if (rt->closed) hud.line(toM(rt->pts.back()), toM(rt->pts[0]), 4.f * u, col);
        // direction chevrons
        for (float d = rt->length * 0.1f; d < rt->length; d += rt->length * 0.2f) {
            vec3 a3 = rt->pointAt(d), t3 = rt->tangentAt(d);
            vec2 q = toM(a3), f(t3.x, -t3.z), r(-f.y, f.x);
            hud.triangle(q + f * (8 * u), q - f * (5 * u) + r * (6 * u), q - f * (5 * u) - r * (6 * u), vec4(1, 1, 1, 1));
        }
        vec2 s0 = toM(rt->pts[0]);
        hud.circle(s0, 9 * u, vec4(0, 0, 0, 1), 16);
        hud.circle(s0, 7 * u, vec4(0.4f, 1, 0.5f, 1), 16);
        if (!rt->closed) {
            vec2 s1 = toM(rt->pts.back());
            hud.rect(s1.x - 7 * u, s1.y - 7 * u, 14 * u, 14 * u, vec4(0, 0, 0, 1));
            for (int i = 0; i < 4; i++) hud.rect(s1.x - 6 * u + (i % 2) * 6 * u, s1.y - 6 * u + (i / 2) * 6 * u, 6 * u, 6 * u, (i == 0 || i == 3) ? L_WHITE : vec4(0.1f, 0.1f, 0.1f, 1));
        }
    }
    vec2 pp = toM(player.car.pos);
    hud.circle(pp, 6 * u, vec4(0, 0, 0, 1), 12);
    hud.circle(pp, 4 * u, L_WHITE, 12);
}

void Game::drawEventLab() {
    float u = hud.ui;
    drawThemeBackdrop(0.95f);
    float x0 = 48 * u;
    drawMenuHeader(x0, 30 * u, hud.w - 2 * x0, "EVENTLAB");
    const CustomEvent& c = labEv;
    bool closed = c.road >= 0 && c.road < int(world.roads.size()) && world.roads[c.road].closed;
    float pw = 500 * u, px = x0, py = 128 * u, rh = (hud.h - py - 110 * u) / LR_COUNT;
    rh = std::min(rh, 48 * u);
    themePanel(px - 10 * u, py - 10 * u, pw + 20 * u, LR_COUNT * rh + 28 * u, 1.f, true);
    for (int i = 0; i < LR_COUNT; i++) {
        float ry = py + i * rh + (i >= LR_TEST ? 14 * u : 0);
        bool sel = i == labSel;
        bool action = i >= LR_TEST;
        themeRow(px, ry, pw, rh - 5 * u, sel);
        if (action && !sel) hud.rect(px, ry, 4 * u, rh - 5 * u, vec4(0.96f, 0.10f, 0.50f, 1));
        float ty = ry + (rh - 5 * u) * 0.5f - 9 * u;
        if (action) {
            hud.text(px + 20 * u, ty, 17 * u, kLabRows[i], sel ? L_WHITE : vec4(1, 1, 1, 0.8f), 0, false);
            continue;
        }
        std::string label = kLabRows[i];
        if (i == LR_LENGTH) label = closed ? "LAPS" : "LENGTH";
        hud.text(px + 20 * u, ty + 2 * u, 12 * u, label, L_DIM, 0, false);
        std::string val;
        switch (i) {
            case LR_ROAD: {
                const Road& r = world.roads[c.road];
                val = r.name + (r.closed ? "  (loop)" : "") + (r.surface == SURF_ASPHALT ? "" : "  (dirt)");
                break;
            }
            case LR_START: val = std::to_string(c.start) + "% along"; break;
            case LR_LENGTH: val = closed ? std::to_string(c.laps) + (c.laps == 1 ? " lap" : " laps") : std::to_string(c.length) + " km"; break;
            case LR_DIR: val = c.reverse ? "Reverse" : "Forward"; break;
            case LR_RIVALS: val = c.opponents == 0 ? "None (time trial)" : std::to_string(c.opponents); break;
            case LR_CLASS: val = kClassNames[c.cls]; break;
            case LR_WEATHER: val = c.wet ? "Rain" : "Dry"; break;
        }
        hud.textFit(px + 190 * u, ty - 1 * u, 18 * u, val, L_WHITE, 0, pw - 250 * u);
        if (sel) {
            hud.text(px + pw - 52 * u, ty - 2 * u, 20 * u, "<", vec4(0.80f, 0.94f, 0.16f, 1), 1, false);
            hud.text(px + pw - 22 * u, ty - 2 * u, 20 * u, ">", vec4(0.80f, 0.94f, 0.16f, 1), 1, false);
        }
    }
    // preview
    float S = std::min(hud.h - 250 * u, hud.w - pw - x0 * 3);
    float mx = hud.w - S - x0, my = 128 * u;
    themePanel(mx - 10 * u, my - 10 * u, S + 20 * u, S + 76 * u, 1.f, true);
    drawRouteMap(mx, my, S, labOk ? &labPreview.route : nullptr, L_CYAN);
    float iy = my + S + 14 * u;
    if (labOk) {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%s   |   %.1f km   |   1st prize CR %s", labPreview.name.c_str(), labPreview.totalLength() / 1000.f,
                      fmtThousands(labPreview.prize[0]).c_str());
        hud.textFit(mx, iy, 16 * u, buf, L_WHITE, 0, S, false);
        hud.text(mx, iy + 24 * u, 14 * u, "Share code:  " + encodeEvent(c), L_CYAN, 0, false);
    } else {
        hud.text(mx, iy, 16 * u, "Route too short from here - move the start or shorten it", vec4(1, 0.45f, 0.4f, 1), 0, false);
    }
    drawThemePrompts(pad ? "D-pad up/down choose   left/right change   (A) select   (B) back" : "UP/DOWN choose   LEFT/RIGHT change   ENTER select   ESC back");
    drawMenuToasts(hud.h * 0.74f);
}

// ---------------------------------------------------------------- My Events
void Game::myEventsInput(Action a) {
    int n = int(customEvents.size());
    if (a == A_BACK) { state = ST_PAUSE; menuTab = 3; menuSel = 1; sellConfirm = -1; return; }
    if (n == 0) { if (a == A_CONFIRM) openEventLab(); return; }
    if (a == A_UP) { subSel = (subSel - 1 + n) % n; sellConfirm = -1; audio.beep(880, 0.03f); }
    if (a == A_DOWN) { subSel = (subSel + 1) % n; sellConfirm = -1; audio.beep(880, 0.03f); }
    subSel = std::clamp(subSel, 0, n - 1);
    if (a == A_CONFIRM) {
        for (int i = builtinEvents; i < int(events.size()); i++)
            if (events[i].custom == subSel) {
                enterFreeRoam(events[i].route.pts[0], events[i].startYaw);
                startRace(i);
                return;
            }
    }
    if (a == A_TOGGLE) {
        SDL_SetClipboardText(encodeEvent(customEvents[subSel]).c_str());
        toast("Share code copied: " + encodeEvent(customEvents[subSel]), L_CYAN);
    }
    if (a == A_INTERACT) {  // edit in EventLab
        labEv = customEvents[subSel];
        openEventLab();
    }
    if (a == A_SELL || a == A_CAMERA) {
        if (sellConfirm != subSel) { sellConfirm = subSel; audio.beep(660, 0.08f); return; }
        customEvents.erase(customEvents.begin() + subSel);
        sellConfirm = -1;
        subSel = std::max(0, subSel - 1);
        rebuildCustomEvents();
        save();
        toast("Event deleted", L_DIM);
    }
}

void Game::drawMyEvents() {
    float u = hud.ui;
    drawThemeBackdrop(0.95f);
    float x0 = 48 * u;
    drawMenuHeader(x0, 30 * u, hud.w - 2 * x0, "MY EVENTS");
    int n = int(customEvents.size());
    float pw = 560 * u, px = x0, py = 128 * u, rh = 58 * u;
    if (n == 0) {
        hud.text(px, py + 10 * u, 22 * u, "No saved events yet", L_WHITE, 0, false);
        hud.text(px, py + 44 * u, 15 * u, "Build one in EventLab, or copy a friend's share code and use Import Event.", L_DIM, 0, false);
        drawThemePrompts(pad ? "(A) open EventLab   (B) back" : "ENTER open EventLab   ESC back");
        return;
    }
    int rows = std::max(3, int((hud.h - py - 110 * u) / rh));
    themePanel(px - 10 * u, py - 10 * u, pw + 20 * u, std::min(n, rows) * rh + 14 * u, 1.f, true);
    int first = std::clamp(subSel - rows / 2, 0, std::max(0, n - rows));
    const EventDef* selEv = nullptr;
    for (int i = builtinEvents; i < int(events.size()); i++) if (events[i].custom == subSel) selEv = &events[i];
    for (int i = first; i < std::min(n, first + rows); i++) {
        float ry = py + (i - first) * rh;
        bool sel = i == subSel;
        themeRow(px, ry, pw, rh - 6 * u, sel);
        hud.textFit(px + 20 * u, ry + 8 * u, 18 * u, autoEventName(world, customEvents[i]), L_WHITE, 0, pw - 40 * u);
        std::string meta = encodeEvent(customEvents[i]);
        if (sellConfirm == i) meta = "Press again to delete";
        hud.text(px + 20 * u, ry + 32 * u, 12 * u, meta, sellConfirm == i ? vec4(1, 0.45f, 0.4f, 1) : L_DIM, 0, false);
    }
    float S = std::min(hud.h - 250 * u, hud.w - pw - x0 * 3);
    float mx = hud.w - S - x0;
    themePanel(mx - 10 * u, py - 10 * u, S + 20 * u, S + 76 * u, 1.f, true);
    drawRouteMap(mx, py, S, selEv ? &selEv->route : nullptr, L_CYAN);
    if (selEv) {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%.1f km   |   1st prize CR %s", selEv->totalLength() / 1000.f, fmtThousands(selEv->prize[0]).c_str());
        hud.text(mx, py + S + 14 * u, 16 * u, buf, L_WHITE, 0, false);
        hud.textFit(mx, py + S + 38 * u, 14 * u, selEv->blurb, L_DIM, 0, S, false);
    }
    drawThemePrompts(pad ? "(A) race   (X) copy code   (Y) edit   LB delete   (B) back" : "ENTER race   X copy share code   E edit   V delete   ESC back");
    drawMenuToasts(hud.h * 0.74f);
}
