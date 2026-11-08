#include "game.h"
#include "washi.h"
#include "carimport.h"
#include "icon_data.h"
#include "logo_data.h"
#include "assets.h"
#include "inflate.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

static const vec4 ACCENT(0.96f, 0.10f, 0.50f, 1.f);
static const vec4 WHITE(1, 1, 1, 1);
static const vec4 DIM(1, 1, 1, 0.6f);
static const vec4 PANEL(0.13f, 0.46f, 0.42f, 0.86f);  // festival teal
static const vec4 HUD_LIME(0.80f, 0.94f, 0.16f, 1.f);
static const vec4 HUD_GOLD(0.98f, 0.86f, 0.2f, 1.f);
static const char* kBotNames[] = {"Vex", "Juniper", "Halcyon", "Brick", "Nova", "Tamsin", "Ozzy", "Kiri", "Marlow", "Sable", "Dax", "Wren"};

static vec3 stripeFor(vec3 paint) {
    float lum = paint.x * 0.3f + paint.y * 0.59f + paint.z * 0.11f;
    return lum > 0.45f ? vec3(0.05f, 0.05f, 0.06f) : vec3(0.92f, 0.92f, 0.9f);
}
static int cylindersFor(const CarSpec& s) {
    if (s.cylinders > 0) return s.cylinders;
    switch (s.body) {
        case BODY_HATCH: case BODY_RALLY: return 4;
        case BODY_COUPE: case BODY_TRUCK: return 6;
        case BODY_GT: case BODY_HYPER: return 12;
        default: return 8;
    }
}
static std::string ordinal(int n) {
    const char* suf = "th";
    if (n % 100 < 11 || n % 100 > 13) {
        if (n % 10 == 1) suf = "st";
        else if (n % 10 == 2) suf = "nd";
        else if (n % 10 == 3) suf = "rd";
    }
    return std::to_string(n) + suf;
}
static std::string carName(const CarSpec& s) { return std::string(s.make) + " " + s.model; }
static int stockPaint(const CarSpec& s) {
    int best = 0; float bd = 1e9f;
    for (int i = 0; i < kPaintCount; i++) {
        vec3 d = kPaints[i] - s.color;
        if (dot(d, d) < bd) { bd = dot(d, d); best = i; }
    }
    return best;
}
static vec4 eventColor(EventType t) {
    if (t == EV_CIRCUIT) return vec4(1.0f, 0.45f, 0.1f, 1);
    if (t == EV_SPRINT) return vec4(0.25f, 0.6f, 1.0f, 1);
    if (t == EV_DRIFT) return vec4(0.85f, 0.25f, 1.0f, 1);
    return vec4(0.45f, 0.9f, 0.25f, 1);
}
static const char* eventTypeName(EventType t) {
    if (t == EV_CIRCUIT) return "CIRCUIT";
    if (t == EV_SPRINT) return "SPRINT";
    if (t == EV_DRIFT) return "DRIFT ZONE";
    return "CROSS COUNTRY";
}

// ====================================================================== init
bool Game::init(const Options& o) {
    opt = o;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) SDL_Log("No audio: %s", SDL_GetError());

    const char* pp = SDL_GetPrefPath("rxvy", "ridgeline");
    prefPath = pp ? pp : "./";
    SDL_free((void*)pp);
    bool haveSave = load();
    if (!opt.recordCmd.empty()) settings.msaa = std::min(settings.msaa, 4);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (settings.fullscreen && opt.shot.empty()) flags |= SDL_WINDOW_FULLSCREEN;
    win = SDL_CreateWindow("Ridgeline", opt.width, opt.height, flags);
    if (!win) { SDL_Log("Window failed: %s", SDL_GetError()); return false; }
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(kIconW, kIconH, SDL_PIXELFORMAT_RGBA32, (void*)kIconRGBA, kIconW * 4)) {
        SDL_SetWindowIcon(win, icon);
        SDL_DestroySurface(icon);
    }
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) { SDL_Log("OpenGL 3.3 context failed: %s", SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(win, ctx);
    const char* missing = nullptr;
    if (!glLoadAll(&missing)) { SDL_Log("Missing GL function %s", missing); return false; }
    SDL_Log("OpenGL: %s | %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    SDL_GL_SetSwapInterval(settings.vsync && opt.shot.empty() ? 1 : 0);

    if (!R.init()) return false;
    R.msaa = settings.msaa;
    R.shadows = settings.shadows;
    int pw, ph;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    R.resize(pw, ph);
    hud.init(&R.hud, 0);
    {
        std::vector<uint8_t> px;
        if (zlibInflate(kLogoZ, kLogoZSize, px, size_t(kLogoW) * kLogoH * 4)) {
            glGenTextures(1, &logoTex);
            glBindTexture(GL_TEXTURE_2D, logoTex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kLogoW, kLogoH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }

    // loading screen
    hud.begin(pw, ph);
    hud.rect(0, 0, float(pw), float(ph), vec4(0.04f, 0.04f, 0.05f, 1));
    hud.text(pw * 0.5f, ph * 0.45f, 56 * hud.ui, "RIDGELINE", WHITE, 1);
    hud.text(pw * 0.5f, ph * 0.45f + 70 * hud.ui, 18 * hud.ui, "Generating the world...", DIM, 1);
    hud.end();
    SDL_GL_SwapWindow(win);

    Uint64 t0 = SDL_GetTicks();
    world.generate(1337);
    world.buildGPU();
    gps.build(world);
    if (!fx.init()) SDL_Log("Effects shaders failed to build");
    buildStunts();
    buildFestival();
    hud.mapTex = world.mapTex;
    // imported cars: user folder, installed share folder, and ./cars next to the binary
    {
        std::vector<std::string> dirs = {prefPath + "cars"};
        if (const char* bp = SDL_GetBasePath()) {
            std::string b = bp;
            dirs.push_back(b + "../share/ridgeline/cars");
            dirs.push_back(b + "cars");
            dirs.push_back(b + "../cars");
        }
        importedModels = loadImportedCars(dirs);
        loadProps();
        placeProps();
        // the community car models replace the old procedural placeholder cars
        int real = 0;
        for (auto& c : kCars) real += (c.importIdx >= 0 && !c.hidden) ? 1 : 0;
        if (real >= 4)
            for (auto& c : kCars) if (c.importIdx < 0) c.hidden = true;
    }
    models.resize(kCars.size());
    for (size_t i = 0; i < kCars.size(); i++) {
        const CarSpec& cs = kCars[i];
        if (cs.importIdx < 0) { models[i].build(cs); continue; }
        ImportedModel* m = importedModels[cs.importIdx];
        CarModel& cm = models[i];
        cm.imp = m;
        for (int k = 0; k < 4; k++) cm.wheelPos[k] = m->wc[k];
        cm.wheelR = cs.wheelR;
        cm.wheelW = (m->ww[0] + m->ww[2]) * 0.5f;
        cm.eye = m->eye;
        cm.hoodCam = m->hood;
        cm.bumperCam = m->bumper;
        cm.dims = vec3(m->L, m->W, m->H);
        cm.deckY = m->deckY;
        cm.deckZ = m->deckZ;
    }
    haveSave = load();  // resolve saved garage entries now that imported cars exist
    wa::setDark(settings.darkMode || getenv("RL_DARK"));  // the menu theme from the save
    dealerOrder.clear();
    for (int i = 0; i < int(kCars.size()); i++) if (listed(kCars[i])) dealerOrder.push_back(i);
    std::stable_sort(dealerOrder.begin(), dealerOrder.end(), [](int a, int b) { return kCars[a].price < kCars[b].price; });
    carThumb.assign(kCars.size(), 0);
    events = buildEvents(world);
    builtinEvents = int(events.size());
    rebuildCustomEvents();
    if (bestPlace.size() < events.size()) bestPlace.resize(events.size(), 0);
    if (opt.scene.empty()) festNewDay();
    else if (festDay < 0) { festDay = 0; festPick[0] = 3; festPick[1] = 5; festPick[2] = 11; }
    SDL_Log("World ready in %llu ms", (unsigned long long)(SDL_GetTicks() - t0));

    // helper meshes
    {
        // contact shadow: a soft rounded rectangle, darkest in the middle under the car
        MeshBuilder b;
        const int G = 16;
        for (int j = 0; j <= G; j++)
            for (int i = 0; i <= G; i++) {
                float x = -1.f + 2.f * i / G, z = -1.f + 2.f * j / G;
                float d = std::pow(std::pow(std::fabs(x), 4.f) + std::pow(std::fabs(z), 4.f), 0.25f);  // squircle
                float a = 0.72f * (1.f - smoothstepf(0.35f, 1.f, d));
                b.add(vec3(x, 0, z), vec3(0, 1, 0), vec4(0, 0, 0, a));
            }
        for (int j = 0; j < G; j++)
            for (int i = 0; i < G; i++) {
                uint32_t a = j * (G + 1) + i;
                b.tri(a, a + G + 1, a + 1);
                b.tri(a + 1, a + G + 1, a + G + 2);
            }
        b.build(blobMesh);
        // tyre contact: a small dark patch under each wheel
        MeshBuilder t;
        uint32_t c = t.add(vec3(0, 0, 0), vec3(0, 1, 0), vec4(0, 0, 0, 0.75f));
        for (int i = 0; i <= 20; i++) {
            float a = i / 20.f * 2 * PI;
            t.add(vec3(std::cos(a), 0, std::sin(a)), vec3(0, 1, 0), vec4(0, 0, 0, 0));
        }
        for (int i = 0; i < 20; i++) t.tri(c, c + 2 + i, c + 1 + i);
        t.build(tyreBlobMesh);
    }
    {
        MeshBuilder b;
        const int S = 20;
        for (int i = 0; i <= S; i++) {
            float a = i / float(S) * 2 * PI;
            vec3 d(std::cos(a), 0, std::sin(a));
            b.add(d * 3.5f, d, vec4(1, 1, 1, 0.55f));
            b.add(d * 3.5f + vec3(0, 110, 0), d, vec4(1, 1, 1, 0));
        }
        for (int i = 0; i < S; i++) { uint32_t a = i * 2; b.tri(a, a + 1, a + 3); b.tri(a, a + 3, a + 2); }
        b.build(beaconMesh);
    }
    {
        // boost pad: three glowing chevrons pointing down the road (+z)
        MeshBuilder b;
        for (int k = 0; k < 3; k++) {
            float z = -1.6f + k * 1.4f;
            vec4 c(1, 1, 1, 0.9f);
            for (int s = -1; s <= 1; s += 2) {
                vec3 a(s * 2.6f, 0, z), bb(s * 2.6f, 0, z + 0.5f), cc(0, 0, z + 1.6f), d(0, 0, z + 1.1f);
                uint32_t i0 = b.add(a, vec3(0, 1, 0), c);
                b.add(bb, vec3(0, 1, 0), c);
                b.add(cc, vec3(0, 1, 0), c);
                b.add(d, vec3(0, 1, 0), c);
                b.tri(i0, i0 + 1, i0 + 2); b.tri(i0, i0 + 2, i0 + 3);
                b.tri(i0, i0 + 2, i0 + 1); b.tri(i0, i0 + 3, i0 + 2);
            }
        }
        b.build(boostMesh);
    }
    {
        // countdown lamp: a bright disc with a soft halo, facing -z
        MeshBuilder b;
        uint32_t c = b.add(vec3(0, 0, 0), vec3(0, 0, -1), vec4(1, 1, 1, 1));
        const int S = 24;
        for (int i = 0; i <= S; i++) {
            float a = i / float(S) * 2 * PI;
            b.add(vec3(std::cos(a), std::sin(a), 0), vec3(0, 0, -1), vec4(1, 1, 1, 1));
            b.add(vec3(std::cos(a), std::sin(a), 0) * 3.0f, vec3(0, 0, -1), vec4(1, 1, 1, 0));
        }
        for (int i = 0; i < S; i++) {
            uint32_t a = c + 1 + i * 2, n = a + 2;
            b.tri(c, a, n); b.tri(c, n, a);
            b.tri(a, a + 1, n + 1); b.tri(a, n + 1, n);
            b.tri(a, n + 1, a + 1); b.tri(a, n, n + 1);
        }
        b.build(lampMesh);
    }
    {
        // racing-line chevron, flat on the road, pointing +z
        MeshBuilder b;
        vec4 c(1, 1, 1, 1);
        vec3 up(0, 1, 0);
        auto q = [&](vec3 p0, vec3 p1, vec3 p2, vec3 p3) {
            uint32_t i0 = b.add(p0, up, c); b.add(p1, up, c); b.add(p2, up, c); b.add(p3, up, c);
            b.tri(i0, i0 + 1, i0 + 2); b.tri(i0, i0 + 2, i0 + 3); b.tri(i0, i0 + 2, i0 + 1); b.tri(i0, i0 + 3, i0 + 2);
        };
        q(vec3(-0.75f, 0, -0.35f), vec3(-0.75f, 0, -0.05f), vec3(0, 0, 0.45f), vec3(0, 0, 0.15f));
        q(vec3(0, 0, 0.15f), vec3(0, 0, 0.45f), vec3(0.75f, 0, -0.05f), vec3(0.75f, 0, -0.35f));
        b.build(lineMesh);
    }
    {
        MeshBuilder b;
        const int S = 40;
        for (int i = 0; i <= S; i++) {
            float a = i / float(S) * 2 * PI;
            vec3 d(std::cos(a), 0, std::sin(a));
            b.add(d * 6.2f, vec3(0, 1, 0), vec4(1, 1, 1, 0.9f));
            b.add(d * 7.4f, vec3(0, 1, 0), vec4(1, 1, 1, 0.9f));
        }
        for (int i = 0; i < S; i++) { uint32_t a = i * 2; b.tri(a, a + 1, a + 3); b.tri(a, a + 3, a + 2); }
        uint32_t c = b.add(vec3(0, 0, 0), vec3(0, 1, 0), vec4(1, 1, 1, 0.18f));
        uint32_t f = uint32_t(b.v.size());
        for (int i = 0; i <= S; i++) {
            float a = i / float(S) * 2 * PI;
            b.add(vec3(std::cos(a), 0, std::sin(a)) * 6.2f, vec3(0, 1, 0), vec4(1, 1, 1, 0.05f));
        }
        for (int i = 0; i < S; i++) b.tri(c, f + i, f + i + 1);
        b.build(ringMesh);
    }

    if (opt.recordCmd.empty()) audio.init();  // when recording, audio is rendered offline in lock-step
    audio.master = settings.volume / 100.f;
    int npads = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&npads);
    if (ids && npads > 0) pad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);

    if (!haveSave) newGame();
    // title attract car
    titleCar.car.init(&kCars[showcaseSpec()], vec3(0, 0, 0), 0);
    titleCar.model = &models[showcaseSpec()];
    titleCar.paint = kPaints[9];
    titleCar.stripe = vec3(0.85f, 0.2f, 0.1f);
    titleCar.skill = 0.95f;
    {
        const Route& rt = events[0].route;
        titleCar.car.placeAt(rt.pts[40], std::atan2(rt.tan[40].x, rt.tan[40].z), world);
        titleCar.idx = 40;
    }
    showCar.model = &models[0];
    dayAtmo = R.atmo;
    {
        // a real sky (Poly Haven HDRI) if one is installed: light the day to match it
        SkyInfo sky;
        if (loadSkyHdr(findAsset("sky/sky.hdr"), sky)) {
            auto lum = [](vec3 c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };
            R.skyTex = sky.tex;
            dayAtmo.sunDir = sky.sunDir;
            float sm = std::max(dayAtmo.sunColor.x, std::max(dayAtmo.sunColor.y, dayAtmo.sunColor.z));
            dayAtmo.sunColor = sky.sunColor * sm;
            dayAtmo.skyTop = sky.zenith;
            dayAtmo.skyHorizon = sky.horizon;
            dayAtmo.fogColor = sky.horizon * 0.4f + dayAtmo.fogColor * 0.6f;
            dayAtmo.fogDensity *= 0.85f;
            dayAtmo.skyAmb = sky.ambient * (lum(dayAtmo.skyAmb) / std::max(lum(sky.ambient), 1e-3f));
            // the sky's diffuse light, scaled so the overall fill matches the old lighting
            float upLum = 0;
            {
                vec3 e = sky.sh[0] * 0.282095f + sky.sh[1] * 0.488603f;  // n = (0,1,0)
                e = e + sky.sh[8] * (-0.546274f) + sky.sh[6] * (-0.315392f);
                upLum = lum(e);
            }
            float k = upLum > 1e-4f ? lum(dayAtmo.skyAmb) * 1.3f / upLum : 1.f;
            for (int i = 0; i < 9; i++) R.skySH[i] = sky.sh[i] * k;
            R.atmo = dayAtmo;
        }
    }
    buildEras();
    setupTitle();
    setPlayerCar(std::clamp(current, 0, int(garage.size()) - 1));
    player.car.placeAt(world.hubSpawn, world.hubYaw, world);
    camYaw = world.hubYaw;

    // headless AI benchmark: every car drives an event route for 120 s
    if (opt.scene == "bench") {
        const EventDef& E = events[std::clamp(opt.event, 0, int(events.size()) - 1)];
        const Route& rt = E.route;
        for (size_t s = 0; s < kCars.size(); s++) {
            Racer r;
            r.model = &models[s];
            r.skill = 0.92f;
            r.car.init(&kCars[s], vec3(), 0);
            r.car.placeAt(rt.pts[0], std::atan2(rt.tan[0].x, rt.tan[0].z), world);
            r.idx = 0; r.lap = 0; r.progress = 0;
            std::vector<Racer*> none;
            float vmax = 0, slipT = 0; int resets = 0;
            vec3 last = r.car.pos;
            float lapTime = -1;
            for (int step = 0; step < 120 * 120; step++) {
                Controls c = aiDrive(r, rt, world, none, 1.f, 1.f / 120.f);
                r.car.update(1.f / 120.f, world, c);
                if (length(r.car.pos - last) > 5.f) resets++;
                last = r.car.pos;
                trackProgress(r, E);
                vmax = std::max(vmax, r.car.speedKmh());
                if (std::fabs(r.car.slipAngle) > 0.3f) slipT += 1.f / 120.f;
                if (lapTime < 0 && r.progress >= E.totalLength()) lapTime = step / 120.f;
            }
            std::printf("%-24s dist %6.0f m  avg %5.1f km/h  max %5.1f km/h  slip>0.3 %5.1fs  resets %d  finish %s\n",
                        carName(kCars[s]).c_str(), r.progress, r.progress / 120.f * 3.6f, vmax, slipT, resets,
                        lapTime > 0 ? fmtTime(lapTime).c_str() : "-");
        }
        std::fflush(stdout);
        running = false;
        return true;
    }

    // test / screenshot scenes
    if (!opt.scene.empty()) {
        if (opt.car >= 0 && opt.car < int(kCars.size())) {
            garage.push_back({opt.car, stockPaint(kCars[opt.car]), kCars[opt.car].stripes});
            setPlayerCar(int(garage.size()) - 1);
        }
        if (opt.scene == "drive") {
            const Route& rt = events[std::clamp(opt.event, 0, int(events.size()) - 1)].route;
            enterFreeRoam(rt.pts[10], std::atan2(rt.tan[10].x, rt.tan[10].z));
            player.idx = 10;
            autopilot = true;
        }
        else if (opt.scene == "hub") enterFreeRoam(world.hubSpawn, world.hubYaw);
        else if (opt.scene == "spot") {
            float x = 0, z = 0, yw = 0;
            std::sscanf(opt.at.c_str(), "%f,%f,%f", &x, &z, &yw);
            enterFreeRoam(vec3(x, world.heightAt(x, z), z), yw);
            if (opt.orbit != 0) {
                enterPhoto();
                photoHideUi = true;
                photoYaw = opt.look != 0 ? opt.look : 2.6f;
                photoPitch = 0.12f;
                photoDist = player.car.spec->length * 1.7f + 2.5f;
                photoFov = 0.9f;
            }
        }
        else if (opt.scene == "racestart") {
            enterFreeRoam(world.hubSpawn, world.hubYaw);
            startRace(std::clamp(opt.event, 0, int(events.size()) - 1));
            stateTime = 4.1f;
            countdown = 3.f - (stateTime - 2.5f);
        }
        else if (opt.scene == "racegate") {
            // race already under way, parked just before the first checkpoint arch
            enterFreeRoam(world.hubSpawn, world.hubYaw);
            startRace(std::clamp(opt.event, 0, int(events.size()) - 1));
            const EventDef& E = events[raceEvent];
            if (gates.size() > 1) {
                float d = gates[1].d - 34.f;
                vec3 p = E.route.pointAt(d), t = E.route.tangentAt(d);
                player.car.placeAt(p, std::atan2(t.x, t.z), world);
                camYaw = player.car.yaw;
                state = ST_RACE;
                stateTime = 3.f;
                raceTime = 40.f;
                trackProgress(player, E);
                gateIdx = 1;
            }
        }
        else if (opt.scene == "rampshot") {
            // mid-air over the first stunt ramp, part-way through the trick (look = phase 0..1)
            enterFreeRoam(world.hubSpawn, world.hubYaw);
            startRace(std::clamp(opt.event, 0, int(events.size()) - 1));
            const EventDef& E = events[raceEvent];
            if (!E.ramps.empty()) {
                int k = std::clamp(int(std::fabs(opt.look)), 0, int(E.ramps.size()) - 1);
                if (opt.look < 0) {  // approach view
                    float d = E.ramps[k] - 40.f;
                    vec3 p = E.route.pointAt(d), t = E.route.tangentAt(d);
                    player.car.placeAt(p, std::atan2(t.x, t.z), world);
                    camYaw = player.car.yaw;
                    state = ST_RACE; stateTime = 3.f; raceTime = 20.f;
                    trackProgress(player, E);
                    if (opt.simSeconds > 0) { player.car.vel = t * 32.f; autopilot = true; }  // drive it off the ramp
                    goto rampDone;
                }
                float ph = opt.look - int(opt.look);
                float vy0 = 9.f, T = 2.f * vy0 / 9.81f, tt = ph * T;
                float d = E.ramps[k] + 30.f * tt;
                vec3 p = E.route.pointAt(d), t = E.route.tangentAt(d);
                player.car.placeAt(p, std::atan2(t.x, t.z), world);
                player.car.pos.y = groundAtRoute(E.route, E.ramps[k], p) + 1.25f + vy0 * tt - 4.905f * tt * tt;
                player.car.vel = t * 30.f + vec3(0, vy0 - 9.81f * tt, 0);
                int kind = E.rampKind[k];
                player.car.startStunt(kind == 0 ? 1.f : kind == 1 ? -1.f : kind == 3 ? 2.f : 0.f, kind == 2 ? 1.f : 0.f, T * 0.9f);
                player.car.stuntT = tt;
                player.car.wasAir = true;
                camYaw = player.car.yaw;
                state = ST_RACE; stateTime = 3.f; raceTime = 20.f;
                trackProgress(player, E);
            }
            rampDone:;
        }
        else if (opt.scene == "race") { enterFreeRoam(world.hubSpawn, world.hubYaw); autopilot = true; startRace(std::clamp(opt.event, 0, int(events.size()) - 1)); }
        else if (opt.scene == "dealer") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_DEALER; subSel = std::clamp(opt.event, 0, int(kCars.size()) - 1); dealerPaint = stockPaint(kCars[dealerSpec()]); }
        else if (opt.scene == "garage") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_GARAGE; }
        else if (opt.scene.rfind("home", 0) == 0 && !world.estates.empty()) {
            int hi = std::clamp(std::atoi(opt.scene.c_str() + 4), 0, int(world.estates.size()) - 1);
            const Estate& he = world.estates[hi];
            vec3 F(std::sin(he.yaw), 0, std::cos(he.yaw));
            enterFreeRoam(he.spawn + F * 14.f, he.yaw + PI);
            camYaw = he.yaw + PI;
        }
        else if (opt.scene.rfind("tab", 0) == 0) {
            enterFreeRoam(world.hubSpawn, world.hubYaw);
            state = ST_PAUSE; returnState = ST_FREEROAM; menuSel = 0; pauseShotPending = true;
            menuTab = std::clamp(std::atoi(opt.scene.c_str() + 3), 0, 4);
        }
        else if (opt.scene == "eventlab") {
            enterFreeRoam(world.hubSpawn, world.hubYaw);
            labEv.road = 2; labEv.laps = 3; labEv.opponents = 7; labEv.cls = 4; labEv.wet = true;
            openEventLab();
            labSel = 1;
        }
        else if (opt.scene == "night" || opt.scene == "sunset") {
            const Road& rd = world.roads[1];
            int i = int(rd.pts.size()) / 3;
            enterFreeRoam(rd.pts[i], std::atan2(rd.tan[i].x, rd.tan[i].z));
            camYaw = player.car.yaw;
        }
        else if (opt.scene == "rain") {
            const Road& rd = world.roads[1];
            int i = int(rd.pts.size()) / 3;
            enterFreeRoam(rd.pts[i], std::atan2(rd.tan[i].x, rd.tan[i].z));
            camYaw = player.car.yaw;
        }
        else if (opt.scene == "custom") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_CUSTOM; subSel = current; custSel = 1; }
        else if (opt.scene == "map") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_MAP; }
        else if (opt.scene == "pause") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_PAUSE; returnState = ST_FREEROAM; }
        else if (opt.scene == "titlemenu") { titleMenu = true; titleSel = 0; }
        else if (opt.scene == "settings") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_SETTINGS; }
        else if (opt.scene == "wheelspin") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_WHEELSPIN; wheelspins = std::max(wheelspins, 2); startWheelspin(); }
        else if (opt.scene == "upgrade") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_UPGRADE; subSel = current; upSel = 0; credits = std::max(credits, 60000LL); }
        else if (opt.scene == "donut") {
            const Road& rd = world.roads[0];
            int i = int(rd.pts.size()) / 3;
            enterFreeRoam(rd.pts[i], std::atan2(rd.tan[i].x, rd.tan[i].z));
            devDonut = true;
            settings.assists = false;
            player.car.assists = false;
            camMode = 0;
        }
        else if (opt.scene == "keyart") {
            // poster shot: the title-screen lookout at sunset, low and wide
            garage[current].paint = stockPaint(kCars[garage[current].spec]);
            setupTitle();
            enterFreeRoam(titleCar.car.pos, titleCar.car.yaw);
            player.car.placeAt(titleCar.car.pos, titleCar.car.yaw, world);
            enterPhoto();
            keyArt = true;
            photoHideUi = true;
            photoYaw = opt.look != 0 ? opt.look : 0.62f;
            photoPitch = 0.03f;
            photoDist = player.car.spec->length * 1.0f + 1.0f;
            photoFov = 0.66f;
            if (opt.height > opt.width) { photoDist = player.car.spec->length * 1.6f + 2.2f; photoFov = 0.85f; photoPitch = 0.08f; }
        }
        else if (opt.scene == "photo") { enterFreeRoam(world.hubSpawn, world.hubYaw); enterPhoto(); }
        else if (opt.scene == "kitty") {
            // type the secret code into the Autoshow's hidden box
            enterFreeRoam(world.hubSpawn, world.hubYaw);
            state = ST_DEALER;
            dealerPaint = stockPaint(kCars[dealerSpec()]);
            setCheatFocus(true);
            cheatBuf = "kittycat";
            submitCheat();
        }
        else if (opt.scene == "switch") { enterFreeRoam(world.hubSpawn, world.hubYaw); openSwitcher(); }
        else if (opt.scene == "catspin" || opt.scene == "timejump") {
            // full throttle down the straightest road; the cat brakes after a while
            if (opt.scene == "timejump") settings.mph = true;
            for (auto& st : stunts) if (st.type == 0) {
                const Road& rd = world.roads[st.road];
                int i = (st.a - (opt.scene == "timejump" ? 60 : 20) + int(rd.pts.size())) % int(rd.pts.size());
                enterFreeRoam(rd.pts[i], std::atan2(rd.tan[i].x, rd.tan[i].z));
                player.car.vel = rd.tan[i] * (opt.scene == "timejump" ? 40.5f : 14.f);
                devRoad = st.road;
                if (opt.scene == "timejump") tm.charge = 0.45f;
                break;
            }
        }
        else if (opt.scene == "trap") {
            // drive through the first speed trap
            for (auto& st : stunts) if (st.type == 0) {
                const Road& rd = world.roads[st.road];
                int i = (st.a - 30 + int(rd.pts.size())) % int(rd.pts.size());
                enterFreeRoam(rd.pts[i], std::atan2(rd.tan[i].x, rd.tan[i].z));
                player.car.vel = rd.tan[i] * 52.f;
                break;
            }
        }
        // fast-forward the simulation
        int steps = int(opt.simSeconds * 60);
        if (opt.look != 0) { lookYaw = opt.look; lookPitch = 0.25f; lookIdle = -1e9f; }
        if (opt.cam >= 0) camMode = opt.cam;
        for (int i = 0; i < steps; i++) {
            update(1.f / 60.f);
            if (state == ST_RACE_INTRO && i == 0) { stateTime = 6.f; }
        }
    }
    return true;
}

void Game::setupTitle() {
    // park the best car in the garage at a lookout high on the pass road, sun setting behind it
    int bestOwned = 0;
    for (int i = 0; i < int(garage.size()); i++)
        if (kCars[garage[i].spec].price > kCars[garage[bestOwned].spec].price) bestOwned = i;
    const OwnedCar& oc = garage[bestOwned];
    titleCar.car.init(&kCars[oc.spec], vec3(), 0);
    titleCar.model = &models[oc.spec];
    titleCar.paint = kPaints[oc.paint];
    titleCar.stripe = oc.stripes ? stripeFor(titleCar.paint) : titleCar.paint;
    applyCosmetics(titleCar, oc);
    const Road& pass = world.roads[3];
    int bi = int(pass.pts.size()) * 2 / 3;
    vec3 p = pass.pts[bi];
    vec3 t = pass.tan[bi], right(-t.z, 0, t.x);
    vec3 toValley = normalize(vec3(-p.x, 0, -p.z));
    if (dot(right, toValley) < 0) right = -right;
    p += right * (pass.width * 0.3f);
    float yaw = std::atan2(toValley.x, toValley.z) + 0.5f;
    titleCar.car.placeAt(p, yaw, world);
    // sunset light coming from behind the car, towards the camera
    titleAtmo = dayAtmo;
    vec3 f(std::sin(yaw), 0, std::cos(yaw)), r(-std::cos(yaw), 0, std::sin(yaw));
    vec3 toCar = normalize(-(f * 5.8f - r * 2.6f));  // camera -> car
    titleAtmo.sunDir = normalize(vec3(toCar.x, 0.14f, toCar.z));
    titleAtmo.sunColor = vec3(2.1f, 1.25f, 0.62f);
    titleAtmo.skyTop = vec3(0.13f, 0.27f, 0.55f);
    titleAtmo.skyHorizon = vec3(0.98f, 0.70f, 0.46f);
    titleAtmo.fogColor = vec3(0.86f, 0.66f, 0.48f);
    titleAtmo.skyAmb = vec3(0.46f, 0.40f, 0.42f);
    titleAtmo.groundAmb = vec3(0.30f, 0.21f, 0.14f);
}

void Game::shutdown() {
    if (opt.scene.empty()) save();
    world.stopStreaming();
    audio.shutdown();
    if (pad) SDL_CloseGamepad(pad);
    if (ctx) SDL_GL_DestroyContext(ctx);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

// ====================================================================== persistence
void Game::save() {
    std::ofstream f(prefPath + "save.txt");
    if (!f) return;
    f << "version=1\n";
    f << "credits=" << credits << "\n";
    f << "current=" << current << "\n";
    f << "wins=" << racesWon << "\n";
    f << "festday=" << festDay << "\nfestdone=" << festDone << "\nstreak=" << streak << "\nlastday=" << lastDay << "\nkm=" << kmDriven << "\n";
    f << "festpick=" << festPick[0] << "," << festPick[1] << "," << festPick[2] << "\nfestprog=" << festProg[0] << "," << festProg[1] << "," << festProg[2] << "\n";
    { std::string cs; for (auto h : catHit) cs += h ? '1' : '0'; f << "cats=" << cs << "\n"; }
    for (auto& g : garage) f << "car=" << carId(kCars[g.spec]) << "," << g.paint << "," << (g.stripes ? 1 : 0) << "," << g.up[0] << "," << g.up[1] << "," << g.up[2]
                             << "," << g.livery << "," << g.livCol << "," << g.tint << "," << g.aero << "," << g.ride << "," << g.number
                             << "," << g.camber << "," << g.camberOwned << "\n";
    f << "xp=" << xp << "\nlevel=" << level << "\nspins=" << wheelspins << "\n";
    for (int e : ownedEstates) f << "estate=" << e << "\n";
    for (auto& ce : customEvents) f << "myevent=" << encodeEvent(ce) << "\n";
    for (size_t i = 0; i < stunts.size(); i++) if (stunts[i].best > 0) f << "stunt=" << i << "," << stunts[i].best << "\n";
    for (size_t i = 0; i < bestPlace.size(); i++) f << "best=" << i << "," << bestPlace[i] << "\n";
    f << "mph=" << settings.mph << "\nblur=" << settings.motionBlur << "\nshadows=" << settings.shadows << "\nmsaa=" << settings.msaa
      << "\nfullscreen=" << settings.fullscreen << "\nvolume=" << settings.volume << "\ndraw=" << settings.drawDist
      << "\nvsync=" << settings.vsync << "\nassists=" << settings.assists << "\ngrass=" << settings.grass << "\nweather=" << settings.weather << "\nsteerlock=" << settings.steerLock
      << "\ntimemode=" << settings.timeMode << "\nbrightness=" << settings.brightness << "\ncontrast=" << settings.contrast
      << "\nsaturation=" << settings.saturation << "\nwarmth=" << settings.warmth << "\ncolorblind=" << settings.colorBlind << "\nracingline=" << settings.racingLine << "\ncarrefl=" << settings.carReflections << "\ndark=" << settings.darkMode << "\n";
}

bool Game::load() {
    std::ifstream f(prefPath + "save.txt");
    if (!f) return false;
    std::string line;
    garage.clear();
    customEvents.clear();  // load() runs twice at startup; without this every saved event doubled each launch
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        int a = 0, b = 0, c = 0;
        if (k == "credits") credits = std::atoll(v.c_str());
        else if (k == "current") current = std::atoi(v.c_str());
        else if (k == "wins") racesWon = std::atoi(v.c_str());
        else if (k == "festday") festDay = std::atoi(v.c_str());
        else if (k == "festdone") festDone = std::atoi(v.c_str());
        else if (k == "streak") streak = std::atoi(v.c_str());
        else if (k == "lastday") lastDay = std::atoi(v.c_str());
        else if (k == "km") kmDriven = std::atof(v.c_str());
        else if (k == "festpick") std::sscanf(v.c_str(), "%d,%d,%d", &festPick[0], &festPick[1], &festPick[2]);
        else if (k == "festprog") std::sscanf(v.c_str(), "%f,%f,%f", &festProg[0], &festProg[1], &festProg[2]);
        else if (k == "cats") { catHit.assign(cats.size(), 0); for (size_t i = 0; i < v.size() && i < catHit.size(); i++) catHit[i] = v[i] == '1'; catDirty = true; }
        else if (k == "car") {
            // car=<id or index>,paint,stripes
            auto c1 = v.find(',');
            if (c1 == std::string::npos) continue;
            std::string id = v.substr(0, c1);
            int u0 = 0, u1 = 0, u2 = 0, lv = 0, lc = 0, ti = 0, ae = 0, ri = 0, nu = 0, cb = 0, co = 0;
            if (std::sscanf(v.c_str() + c1 + 1, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &b, &c, &u0, &u1, &u2, &lv, &lc, &ti, &ae, &ri, &nu, &cb, &co) < 2) continue;
            int idx = -1;
            for (int i = 0; i < int(kCars.size()); i++) if (carId(kCars[i]) == id) idx = i;
            if (idx < 0 && !id.empty() && std::isdigit((unsigned char)id[0])) idx = std::atoi(id.c_str());
            if (idx >= 0 && idx < int(kCars.size())) {
                OwnedCar oc{idx, std::clamp(b, 0, kPaintCount - 1), c != 0};
                oc.up[0] = std::clamp(u0, 0, 3); oc.up[1] = std::clamp(u1, 0, 3); oc.up[2] = std::clamp(u2, 0, 3);
                oc.livery = std::clamp(lv, 0, kLiveryCount - 1); oc.livCol = std::clamp(lc, 0, kPaintCount - 1);
                oc.tint = std::clamp(ti, 0, kTintCount - 1); oc.aero = std::clamp(ae, 0, kAeroCount - 1);
                oc.ride = std::clamp(ri, 0, kRideCount - 1); oc.number = std::clamp(nu, 0, 99);
                oc.camberOwned = std::clamp(co, 0, kCamberCount - 1); oc.camber = std::clamp(cb, 0, oc.camberOwned);
                garage.push_back(oc);
            }
        }
        else if (k == "xp") xp = std::max(0, std::atoi(v.c_str()));
        else if (k == "level") level = std::max(1, std::atoi(v.c_str()));
        else if (k == "spins") wheelspins = std::max(0, std::atoi(v.c_str()));
        else if (k == "myevent") {
            CustomEvent ce;
            if (decodeEvent(v, ce)) {
                ce.name = "";
                // saves written before the fix hold many copies of the same event: keep one
                std::string enc = encodeEvent(ce);
                bool dup = false;
                for (auto& o : customEvents) if (encodeEvent(o) == enc) { dup = true; break; }
                if (!dup) customEvents.push_back(ce);
            }
        }
        else if (k == "estate") { int e = std::atoi(v.c_str()); if (e >= 0 && e < int(world.estates.size()) && !ownsEstate(e)) ownedEstates.push_back(e); }
        else if (k == "stunt") {
            int si = -1; float bv = 0;
            if (std::sscanf(v.c_str(), "%d,%f", &si, &bv) == 2 && si >= 0 && si < int(stunts.size())) stunts[si].best = bv;
        } else if (k == "best" && std::sscanf(v.c_str(), "%d,%d", &a, &b) == 2) {
            if (a >= 0 && a < 64) { if (int(bestPlace.size()) <= a) bestPlace.resize(a + 1, 0); bestPlace[a] = b; }
        } else if (k == "mph") settings.mph = std::atoi(v.c_str());
        else if (k == "blur") settings.motionBlur = std::atoi(v.c_str());
        else if (k == "shadows") settings.shadows = std::atoi(v.c_str());
        else if (k == "msaa") settings.msaa = std::clamp(std::atoi(v.c_str()), 0, 8);
        else if (k == "fullscreen") settings.fullscreen = std::atoi(v.c_str());
        else if (k == "volume") settings.volume = std::clamp(std::atoi(v.c_str()), 0, 100);
        else if (k == "draw") settings.drawDist = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "vsync") settings.vsync = std::atoi(v.c_str());
        else if (k == "assists") settings.assists = std::atoi(v.c_str());
        else if (k == "grass") settings.grass = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "wet") { if (std::atoi(v.c_str()) != 0) settings.weather = 3; }
        else if (k == "weather") settings.weather = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "steerlock") settings.steerLock = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "timemode") settings.timeMode = std::clamp(std::atoi(v.c_str()), 0, 4);
        else if (k == "brightness") settings.brightness = std::clamp(std::atoi(v.c_str()), 50, 150);
        else if (k == "contrast") settings.contrast = std::clamp(std::atoi(v.c_str()), 50, 150);
        else if (k == "saturation") settings.saturation = std::clamp(std::atoi(v.c_str()), 0, 200);
        else if (k == "warmth") settings.warmth = std::clamp(std::atoi(v.c_str()), -10, 10);
        else if (k == "colorblind") settings.colorBlind = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "racingline") settings.racingLine = std::atoi(v.c_str()) != 0;
        else if (k == "carrefl") settings.carReflections = std::atoi(v.c_str()) != 0;
        else if (k == "dark") settings.darkMode = std::atoi(v.c_str()) != 0;
    }
    if (garage.empty()) return false;
    current = std::clamp(current, 0, int(garage.size()) - 1);
    return true;
}

void Game::applySettings() {
    wa::setDark(settings.darkMode || getenv("RL_DARK"));  // RL_DARK: screenshots in dark mode
    R.gradeExposure = settings.brightness / 100.f;
    R.gradeContrast = settings.contrast / 100.f;
    R.gradeSat = settings.saturation / 100.f;
    R.gradeWarmth = settings.warmth / 10.f;
    R.colorBlind = settings.colorBlind;
    bool rebuild = R.msaa != settings.msaa;
    R.shadows = settings.shadows;
    R.msaa = settings.msaa;
    if (rebuild) R.resize(R.width, R.height);
    SDL_SetWindowFullscreen(win, settings.fullscreen);
    SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
    audio.master = settings.volume / 100.f;
    player.car.assists = settings.assists;
    save();
}

// ====================================================================== flow
void Game::newGame() {
    int start = 0;
    for (int i = 0; i < int(kCars.size()); i++) if (kCars[i].starter) { start = i; break; }
    garage = {{start, stockPaint(kCars[start]), kCars[start].stripes}};
    xp = 0;
    level = 1;
    wheelspins = 1;  // a welcome spin
    ownedEstates.clear();
    for (auto& st : stunts) st.best = 0;
    current = 0;
    credits = 25000;
    racesWon = 0;
    bestPlace.assign(events.size() > 0 ? events.size() : 16, 0);
    save();
}

void Game::setPlayerCar(int idx) {
    menuThumbsBuilt = false;
    current = std::clamp(idx, 0, int(garage.size()) - 1);
    const OwnedCar& oc = garage[current];
    vec3 p = player.car.pos;
    float y = player.car.yaw;
    rebuildTuned();
    player.car.init(&tuned[current], p, y);
    player.car.up = vec3(0, 1, 0);
    player.model = &models[oc.spec];
    player.paint = kPaints[oc.paint];
    player.stripe = stripeFor(player.paint);
    if (!oc.stripes) player.stripe = player.paint;
    applyCosmetics(player, oc);
    player.isPlayer = true;
    player.name = "You";
    player.car.assists = settings.assists;
    audio.cylinders = cylindersFor(kCars[oc.spec]);
    audio.electric = kCars[oc.spec].electric ? 1.f : 0.f;
}

void Game::enterFreeRoam(vec3 pos, float yaw) {
    setPlayerCar(current);
    player.car.placeAt(pos, yaw, world);
    state = ST_FREEROAM;
    stateTime = 0;
    camYaw = yaw;
    camPos = pos - vec3(std::sin(yaw), 0, std::cos(yaw)) * 7.f + vec3(0, 2.5f, 0);
}

void Game::toast(const std::string& s, vec4 col) {
    toasts.push_back({s, col, 3.f});
    if (toasts.size() > 4) toasts.erase(toasts.begin());
}

void Game::startRace(int ev) {
    fx.clear();
    rewindCount = 0;
    raceEvent = ev;
    const EventDef& E = events[ev];
    const Route& rt = E.route;
    Rng rng(uint32_t(SDL_GetTicks() + ev * 977 + 1));
    if (!opt.scene.empty()) rng = Rng(42 + ev);
    // rivals: never faster than you. Same performance or a bit below (by PI), unless the
    // EventLab event asks for a class.
    std::vector<int> cand;
    float pPI = float(rateCar(*player.car.spec).pi);
    if (E.clsLimit) {
        for (int i = 0; i < int(kCars.size()); i++) if (kCars[i].cls == E.clsLimit && listed(kCars[i])) cand.push_back(i);
    } else {
        for (float lo : {0.8f, 0.65f, 0.45f}) {
            cand.clear();
            for (int i = 0; i < int(kCars.size()); i++) {
                if (!listed(kCars[i])) continue;
                float pi = float(rateCar(kCars[i]).pi);
                if (pi <= pPI * 1.02f + 1.f && pi >= pPI * lo) cand.push_back(i);
            }
            if (cand.size() >= 3) break;
        }
    }
    if (cand.empty()) cand.push_back(player.car.spec - kCars.data());
    int nb = std::clamp(E.opponents, 0, 7);
    bots.assign(nb, Racer());
    std::vector<int> names = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    for (int i = 0; i < 12; i++) std::swap(names[i], names[rng.irange(0, 11)]);
    for (int i = 0; i < nb; i++) {
        Racer& b = bots[i];
        int s = cand[rng.irange(0, int(cand.size()) - 1)];
        b.model = &models[s];
        b.paint = kPaints[rng.irange(0, kPaintCount - 1)];
        b.stripe = kCars[s].stripes ? stripeFor(b.paint) : b.paint;
        b.name = kBotNames[names[i]];
        if (rng.f() < 0.6f) {  // rivals run liveries too
            b.livery = rng.irange(1, kLiveryCount - 1);
            if (b.livery == 7) b.livery = 1;
            b.livCol = kPaints[rng.irange(0, kPaintCount - 1)];
            b.number = rng.irange(2, 99);
        }
        b.skill = 0.86f + rng.f() * 0.1f;
        b.lane = b.laneTarget = rng.range(-1.5f, 1.5f);
        b.car.init(&kCars[s], vec3(), 0);
    }
    racers.clear();
    for (auto& b : bots) racers.push_back(&b);
    racers.push_back(&player);
    ctl.assign(racers.size(), Controls());
    float side = std::min(3.0f, rt.width * 0.24f);
    for (int k = 0; k < int(racers.size()); k++) {
        int row = k / 2, col = k % 2;
        float along = rt.closed ? -8.f - row * 10.f : 34.f - row * 10.f;
        vec3 t = rt.tangentAt(along), right(-t.z, 0, t.x);
        vec3 p = rt.pointAt(along) + right * (col == 0 ? -side : side);
        Racer* r = racers[k];
        r->car.placeAt(p, std::atan2(t.x, t.z), world);
        r->idx = 0;
        r->lap = 0;
        r->progress = 0;
        r->finished = false;
        r->finishTime = 0;
        r->stuck = 0;
        trackProgress(*r, E);
    }
    buildGates(E);
    state = ST_RACE_INTRO;
    stateTime = 0;
    countdown = 3.f;
    raceTime = 0;
    finishDelay = 0;
    offRouteTime = 0;
    driftScore = 0;
    driftTotal = 0;
    for (Racer* r : racers) r->lastProg = 0;
    playerPlace = int(racers.size());
    toasts.clear();
    camYaw = player.car.yaw;
}

void Game::finishRace() {
    const EventDef& E = events[raceEvent];
    // estimate times for unfinished racers
    for (Racer* r : racers) {
        if (r->finished) continue;
        float remaining = std::max(0.f, E.totalLength() - r->progress);
        float avg = std::max(15.f, r->progress / std::max(raceTime, 1.f));
        r->finishTime = raceTime + remaining / avg;
        r->finished = true;
    }
    std::sort(racers.begin(), racers.end(), [](Racer* a, Racer* b) { return a->finishTime < b->finishTime; });
    for (int i = 0; i < int(racers.size()); i++) if (racers[i]->isPlayer) playerPlace = i + 1;
    if (E.type == EV_DRIFT) {  // drift zones: placed by score
        int s = int(driftTotal);
        playerPlace = s >= E.driftTarget[0] ? 1 : s >= E.driftTarget[1] ? 2 : s >= E.driftTarget[2] ? 3 : 4;
    }
    prizeWon = playerPlace <= 3 ? E.prize[playerPlace - 1] : 1500;
    credits += prizeWon;
    addXP(playerPlace == 1 ? 1500 : playerPlace <= 3 ? 900 : 500);
    if (playerPlace == 1) { racesWon++; festEvent(FS_WIN, 1); }
    festEvent(FS_RACE, 1);
    if (int(bestPlace.size()) <= raceEvent) bestPlace.resize(raceEvent + 1, 0);
    if (bestPlace[raceEvent] == 0 || playerPlace < bestPlace[raceEvent]) bestPlace[raceEvent] = playerPlace;
    save();
    state = ST_RESULTS;
    stateTime = 0;
    audio.beep(playerPlace == 1 ? 1320.f : 660.f, 0.6f);
}

void Game::leaveRace() {
    rebuildCustomEvents();  // drops an EventLab test drive
    racers.clear();
    bots.clear();
    raceEvent = -1;
    gateMesh.destroy();
    clearGates();
    state = ST_FREEROAM;
    stateTime = 0;
    autopilot = autopilot && !opt.scene.empty();
}

// ====================================================================== input
Controls Game::readDriving() {
    Controls c;
    const bool* k = SDL_GetKeyboardState(nullptr);
    float steer = 0;
    if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]) steer -= 1;
    if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) steer += 1;
    c.steer = steer;
    c.throttle = (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) ? 1.f : 0.f;
    c.brake = (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) ? 1.f : 0.f;
    c.handbrake = k[SDL_SCANCODE_SPACE] ? 1.f : 0.f;
    if (devRoad >= 0) {
        // dev scenes: hold the road, floor it (the cat brakes and spins after 2 s)
        int r, si;
        if (world.nearestRoad(player.car.pos, r, si)) {
            const Road& rd = world.roads[r];
            vec3 t = rd.tan[si];
            if (dot(t, player.car.forward()) < 0) t = -t;
            vec3 off = player.car.pos - rd.pts[si];
            float lat = dot(off, player.car.right());
            float hd = std::asin(clampf(dot(cross(player.car.forward(), t), vec3(0, 1, 0)), -1.f, 1.f));
            c.steer = clampf(-hd * 2.5f - lat * 0.25f, -1.f, 1.f);
        }
        bool cat = player.car.spec->fx == "oiia";
        c.throttle = (!cat || simTime < 2.f) ? 1.f : 0.f;
        c.brake = (cat && simTime >= 2.f) ? 1.f : 0.f;
    }
    lookBack = k[SDL_SCANCODE_B];
    if (pad) {
        float ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f;
        if (std::fabs(ax) > 0.12f) c.steer = clampf((ax - signf(ax) * 0.12f) / 0.88f, -1.f, 1.f);
        float rt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.f;
        float lt = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.f;
        c.throttle = std::max(c.throttle, rt > 0.05f ? rt : 0.f);
        c.brake = std::max(c.brake, lt > 0.05f ? lt : 0.f);
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) c.handbrake = 1;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)) lookBack = true;
    }
    if (devDonut) { c.throttle = 1.f; c.steer = simTime < 1.0f ? 0.f : 1.f; c.handbrake = 0.f; c.brake = simTime < 0.6f ? 1.f : 0.f; }
    if (autopilot) {
        const Route& rt = raceEvent >= 0 ? events[raceEvent].route : events[std::clamp(opt.event, 0, int(events.size()) - 1)].route;
        if (raceEvent < 0) player.idx = rt.project(player.car.pos, player.idx, 25);
        player.skill = 0.92f;
        std::vector<Racer*> none;
        c = aiDrive(player, rt, world, raceEvent >= 0 ? racers : none, 1.f, 1.f / 60.f);
    }
    return c;
}

void Game::handleEvent(const SDL_Event& e) {
    Action a = A_NONE;
    if (cheatEvent(e)) return;
    switch (e.type) {
        case SDL_EVENT_QUIT: running = false; return;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: R.resize(e.window.data1, e.window.data2); return;
        case SDL_EVENT_MOUSE_MOTION:
            if (SDL_GetWindowRelativeMouseMode(win)) {
                lookYaw -= e.motion.xrel * 0.0045f;
                lookPitch = clampf(lookPitch + e.motion.yrel * 0.0035f, -0.4f, 1.1f);
                lookIdle = 0;
            }
            return;
        case SDL_EVENT_MOUSE_WHEEL:
            lookZoom = clampf(lookZoom - e.wheel.y * 0.08f, 0.6f, 2.2f);
            return;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!pad) pad = SDL_OpenGamepad(e.gdevice.which);
            return;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) { SDL_CloseGamepad(pad); pad = nullptr; }
            return;
        case SDL_EVENT_KEY_DOWN: {
            SDL_Keycode k = e.key.key;
            bool driving = state == ST_FREEROAM || state == ST_RACE || state == ST_RACE_INTRO;
            if (k == SDLK_F11) { settings.fullscreen = !settings.fullscreen; applySettings(); return; }
            if (k == SDLK_F12) { saveShot(prefPath + "screenshot-" + std::to_string(SDL_GetTicks()) + ".bmp"); toast("Screenshot saved"); return; }
            if (k == SDLK_UP || (k == SDLK_W && !driving)) a = A_UP;
            else if (k == SDLK_DOWN || (k == SDLK_S && !driving)) a = A_DOWN;
            else if (k == SDLK_LEFT || (k == SDLK_A && !driving)) a = A_LEFT;
            else if (k == SDLK_RIGHT || (k == SDLK_D && !driving)) a = A_RIGHT;
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) a = driving ? A_INTERACT : A_CONFIRM;
            else if (k == SDLK_ESCAPE) a = driving ? A_PAUSE : A_BACK;
            else if (k == SDLK_BACKSPACE) a = A_BACK;
            else if (k == SDLK_C) a = A_CAMERA;
            else if (k == SDLK_R) a = A_RESET;
            else if (k == SDLK_M || k == SDLK_TAB) a = A_MAP;
            else if (k == SDLK_X) a = A_TOGGLE;
            else if ((k == SDLK_V || k == SDLK_DELETE) && !driving) a = A_SELL;
            else if (k == SDLK_P) a = A_PHOTO;
            else if (k == SDLK_Q && (state == ST_PAUSE || state == ST_MAP)) a = A_CAMERA;
            else if (k == SDLK_E && state == ST_MYEVENTS) a = A_INTERACT;
            else if (k == SDLK_E && (state == ST_PAUSE || state == ST_MAP)) a = A_UPGRADE;
            else if (k == SDLK_U && !driving) a = A_UPGRADE;
            else if (k == SDLK_H && state == ST_PHOTO) a = A_HIDE;
            else if (k == SDLK_G && (state == ST_FREEROAM || state == ST_SWITCH)) a = A_SWITCH;
            if (state == ST_PHOTO && a != A_HIDE && a != A_PHOTO && a != A_BACK && a != A_PAUSE && a != A_CONFIRM && a != A_INTERACT) a = A_NONE;
            if (k == SDLK_BACKSPACE && driving) a = A_NONE;  // held for rewind
            if (driving && (a == A_UP || a == A_DOWN || a == A_LEFT || a == A_RIGHT)) a = A_NONE;
            if (e.key.repeat && (a == A_CONFIRM || a == A_INTERACT || a == A_PAUSE || a == A_BACK)) a = A_NONE;
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            bool driving = state == ST_FREEROAM || state == ST_RACE || state == ST_RACE_INTRO;
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: a = state == ST_FREEROAM ? A_SWITCH : driving ? A_NONE : A_UP; break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: a = driving ? A_NONE : A_DOWN; break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: a = driving ? A_NONE : A_LEFT; break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: a = driving ? A_NONE : A_RIGHT; break;
                case SDL_GAMEPAD_BUTTON_SOUTH: a = driving ? (state == ST_RACE_INTRO ? A_CONFIRM : A_NONE) : A_CONFIRM; break;
                case SDL_GAMEPAD_BUTTON_EAST: a = driving ? A_NONE : A_BACK; break;
                case SDL_GAMEPAD_BUTTON_NORTH: a = state == ST_PHOTO ? A_HIDE : A_INTERACT; break;
                case SDL_GAMEPAD_BUTTON_WEST: a = driving ? A_RESET : A_TOGGLE; break;
                case SDL_GAMEPAD_BUTTON_START: a = driving ? A_PAUSE : A_BACK; break;
                case SDL_GAMEPAD_BUTTON_BACK: a = A_MAP; break;
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: a = driving ? A_CAMERA : A_UPGRADE; break;
                case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: a = driving ? A_NONE : A_CAMERA; break;
                case SDL_GAMEPAD_BUTTON_RIGHT_STICK: a = A_PHOTO; break;
                default: break;
            }
            break;
        }
        default: break;
    }
    if (a != A_NONE) handleAction(a);
}

enum MenuAct { MA_RESUME, MA_MAP, MA_SPIN, MA_AUTOSHOW, MA_GARAGE, MA_NEXTRACE, MA_SETTINGS, MA_EXIT, MA_QUIT,
               MA_CUSTOMIZE, MA_UPGRADE, MA_PHOTO, MA_EVENTLAB, MA_MYEVENTS, MA_IMPORT, MA_HOUSE };
struct MenuTile { float x, y, w, h; const char* label; const char* sub; vec4 accent; int act; int thumb; };
static const char* kTabNames[] = {"FESTIVAL", "CARS", "HOUSES", "EVENTLAB", "OPTIONS"};
static const int kTabCount = 5;
static std::vector<MenuTile> gTiles;  // tiles of the open tab (rebuilt each frame)
static const MenuTile kTabTiles[][5] = {
    {{0, 0, 2, 2, "Resume", "Back to the road", {1.0f, 0.84f, 0.10f, 1}, MA_RESUME, 0},
     {2, 0, 1, 1, "World Map", "Fast travel anywhere", {0.14f, 0.48f, 0.44f, 1}, MA_MAP, 1},
     {3, 0, 1, 1, "Next Race", "", {0.14f, 0.48f, 0.44f, 1}, MA_NEXTRACE, 5},
     {2, 1, 1, 1, "Wheelspin", "", {1.0f, 0.27f, 0.72f, 1}, MA_SPIN, -2},
     {3, 1, 1, 1, "Photo Mode", "Frame the perfect shot", {0.14f, 0.48f, 0.44f, 1}, MA_PHOTO, 0}},
    {{0, 0, 2, 2, "My Garage", "Drive, paint and sell", {1.0f, 0.84f, 0.10f, 1}, MA_GARAGE, 4},
     {2, 0, 2, 1, "Autoshow", "Buy new cars", {0.14f, 0.48f, 0.44f, 1}, MA_AUTOSHOW, 3},
     {2, 1, 1, 1, "Customize", "Livery, aero, tint, stance", {1.0f, 0.27f, 0.72f, 1}, MA_CUSTOMIZE, -1},
     {3, 1, 1, 1, "Upgrades", "Engine, weight, tyres", {0.14f, 0.48f, 0.44f, 1}, MA_UPGRADE, -1},
     {-1, 0, 0, 0, nullptr, nullptr, {}, 0, 0}},
    {},
    {{0, 0, 2, 2, "EventLab", "Build a race on any road", {1.0f, 0.84f, 0.10f, 1}, MA_EVENTLAB, -3},
     {2, 0, 2, 1, "My Events", "", {1.0f, 0.27f, 0.72f, 1}, MA_MYEVENTS, -1},
     {2, 1, 2, 1, "Import Event", "Paste a share code from the clipboard", {0.14f, 0.48f, 0.44f, 1}, MA_IMPORT, -1},
     {-1, 0, 0, 0, nullptr, nullptr, {}, 0, 0}, {-1, 0, 0, 0, nullptr, nullptr, {}, 0, 0}},
    {{0, 0, 2, 2, "Settings", "Graphics, sound, driving", {1.0f, 0.84f, 0.10f, 1}, MA_SETTINGS, 6},
     {2, 0, 2, 1, "Save & Exit", "Back to the title screen", {0.14f, 0.48f, 0.44f, 1}, MA_EXIT, 7},
     {2, 1, 2, 1, "Quit Game", "See you on the road", {0.14f, 0.48f, 0.44f, 1}, MA_QUIT, -1},
     {-1, 0, 0, 0, nullptr, nullptr, {}, 0, 0}, {-1, 0, 0, 0, nullptr, nullptr, {}, 0, 0}},
};
static void buildTabTiles(int tab, const World& w, const std::vector<int>& owned) {
    gTiles.clear();
    if (tab == 2) {
        static const float pos[5][4] = {{0, 0, 2, 2}, {2, 0, 1, 1}, {3, 0, 1, 1}, {2, 1, 1, 1}, {3, 1, 1, 1}};
        for (int i = 0; i < int(w.estates.size()) && i < 5; i++) {
            gTiles.push_back({pos[i][0], pos[i][1], pos[i][2], pos[i][3], w.estates[i].name.c_str(), "",
                              i == 0 ? vec4(1.0f, 0.84f, 0.10f, 1) : vec4(0.14f, 0.48f, 0.44f, 1), MA_HOUSE + i, 100 + i});
        }
        return;
    }
    for (const MenuTile& t : kTabTiles[tab]) if (t.label) gTiles.push_back(t);
}

static int tileMove(int cur, int dx, int dy) {
    if (gTiles.empty()) return 0;
    cur = std::clamp(cur, 0, int(gTiles.size()) - 1);
    const MenuTile& c = gTiles[cur];
    float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f;
    int best = cur; float bd = 1e9f;
    for (int i = 0; i < int(gTiles.size()); i++) {
        if (i == cur) continue;
        float tx = gTiles[i].x + gTiles[i].w * 0.5f - cx, ty = gTiles[i].y + gTiles[i].h * 0.5f - cy;
        float along = tx * dx + ty * dy;
        if (along <= 0.01f) continue;
        float across = std::fabs(tx * dy) + std::fabs(ty * dx);
        float d = along + across * 2.f;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static std::vector<std::string> pauseItems(bool racing) {
    if (racing) return {"Resume", "Restart Event", "Quit Event", "Settings", "Quit Game"};
    return {"Resume", "World Map", "Dealership", "My Garage", "Settings", "Save & Exit to Title", "Quit Game"};
}

void Game::handleAction(Action a) {
    auto nav = [&](int& sel, int n) {
        if (a == A_UP) { sel = (sel - 1 + n) % n; audio.beep(880, 0.04f); }
        if (a == A_DOWN) { sel = (sel + 1) % n; audio.beep(880, 0.04f); }
    };
    switch (state) {
        case ST_TITLE:
            if (!titleMenu) {
                if (a == A_CONFIRM || a == A_INTERACT) { audio.beep(1200, 0.1f); titleMenu = true; titleMenuT = 0; titleSel = 0; }
                else if (a == A_BACK) running = false;
                break;
            }
            if (a == A_LEFT || a == A_UP) { titleSel = (titleSel + 4) % 5; audio.beep(880, 0.04f); }
            if (a == A_RIGHT || a == A_DOWN) { titleSel = (titleSel + 1) % 5; audio.beep(880, 0.04f); }
            if (a == A_BACK) { titleMenu = false; titleMenuT = 0; audio.beep(600, 0.05f); }
            if (a == A_CONFIRM || a == A_INTERACT) {
                audio.beep(1200, 0.1f);
                switch (titleSel) {
                    case 0: titleMenu = false; enterFreeRoam(world.hubSpawn, world.hubYaw); toast("Welcome to the Ridgeline Festival", ACCENT); break;
                    case 1: titleMenu = false; enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_GARAGE; returnState = ST_FREEROAM; subSel = current; stateTime = 0; garageHome = -1; break;
                    case 2: titleMenu = false; enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_DEALER; returnState = ST_FREEROAM; subSel = 0; dealerPaint = stockPaint(kCars[dealerSpec()]); stateTime = 0; break;
                    case 3: state = ST_SETTINGS; settingsReturn = ST_TITLE; subSel = 0; menuOpenT = 0; break;
                    case 4: save(); running = false; break;
                }
            }
            break;
        case ST_FREEROAM:
            if (a == A_PAUSE || a == A_BACK) { state = ST_PAUSE; returnState = ST_FREEROAM; menuTab = 0; menuSel = 0; pauseShotPending = true; }
            else if (a == A_MAP) { state = ST_MAP; subSel = 0; }
            else if (a == A_PHOTO) enterPhoto();
            else if (a == A_SWITCH) openSwitcher();
            else if (a == A_CAMERA) camMode = (camMode + 1) % 5;
            else if (a == A_RESET) {
                int r, s;
                if (world.nearestRoad(player.car.pos, r, s)) {
                    const Road& rd = world.roads[r];
                    vec3 t = rd.tan[s];
                    if (dot(t, player.car.forward()) < 0) t = -t;
                    player.car.placeAt(rd.pts[s], std::atan2(t.x, t.z), world);
                    camYaw = player.car.yaw;
                    driftScore = 0;
                }
            } else if (a == A_INTERACT || a == A_CONFIRM) {
                if (homeInteract()) break;
                if (nearEvent >= 0) startRace(nearEvent);
                else if (nearHub) { state = ST_PAUSE; returnState = ST_FREEROAM; menuTab = 1; menuSel = 1; pauseShotPending = true; }
            }
            break;
        case ST_SWITCH: switcherAction(a); break;
        case ST_RACE_INTRO:
            if (a == A_CONFIRM || a == A_INTERACT) { if (stateTime < 2.5f) stateTime = 2.5f; }
            else if (a == A_PAUSE) { state = ST_PAUSE; returnState = ST_RACE_INTRO; menuSel = 0; }
            break;
        case ST_RACE:
            if (a == A_PAUSE || a == A_BACK) { state = ST_PAUSE; returnState = ST_RACE; menuSel = 0; }
            else if (a == A_PHOTO) enterPhoto();
            else if (a == A_CAMERA) camMode = (camMode + 1) % 5;
            else if (a == A_RESET) {
                const Route& rt = events[raceEvent].route;
                int i = player.idx;
                player.car.placeAt(rt.pts[i], std::atan2(rt.tan[i].x, rt.tan[i].z), world);
                camYaw = player.car.yaw;
                offRouteTime = 0;
            }
            break;
        case ST_PAUSE: {
            bool racing = returnState == ST_RACE || returnState == ST_RACE_INTRO;
            if (!racing) {
                buildTabTiles(menuTab, world, ownedEstates);
                int before = menuSel;
                if (a == A_CAMERA || a == A_UPGRADE) {
                    menuTab = (menuTab + (a == A_UPGRADE ? 1 : -1) + kTabCount) % kTabCount;
                    tabDir = a == A_UPGRADE ? 1 : -1;
                    menuSel = 0;
                    menuOpenT = 0.2f;  // header and tabs stay put; only the tiles slide over
                    audio.beep(760, 0.05f);
                    break;
                }
                if (a == A_LEFT) menuSel = tileMove(menuSel, -1, 0);
                if (a == A_RIGHT) menuSel = tileMove(menuSel, 1, 0);
                if (a == A_UP) menuSel = tileMove(menuSel, 0, -1);
                if (a == A_DOWN) menuSel = tileMove(menuSel, 0, 1);
                if (menuSel != before) audio.beep(880, 0.04f);
                if (a == A_BACK || a == A_PAUSE) state = returnState;
                if (a != A_CONFIRM || gTiles.empty()) break;
                audio.beep(1100, 0.06f);
                int act = gTiles[std::clamp(menuSel, 0, int(gTiles.size()) - 1)].act;
                if (act >= MA_HOUSE) {
                    int h = act - MA_HOUSE;
                    const Estate& he = world.estates[h];
                    if (ownsEstate(h)) {
                        enterFreeRoam(he.spawn, he.spawnYaw);
                        camYaw = he.spawnYaw;
                        toast("Welcome home: " + he.name, ACCENT);
                    } else {
                        gpsEvent = -1; gpsHub = false; gpsEstate = h; gpsTimer = 0;
                        state = returnState;
                        updateGps(0);
                        toast("GPS route set: " + he.name + " (for sale)", vec4(1.f, 0.8f, 0.3f, 1));
                    }
                    break;
                }
                switch (act) {
                    case MA_RESUME: state = returnState; break;
                    case MA_MAP: state = ST_MAP; subSel = 0; break;
                    case MA_SPIN: state = ST_WHEELSPIN; spinReel.clear(); spinT = -1; break;
                    case MA_AUTOSHOW: state = ST_DEALER; subSel = 0; dealerPaint = stockPaint(kCars[dealerSpec()]); stateTime = 0; break;
                    case MA_GARAGE: state = ST_GARAGE; subSel = current; stateTime = 0; garageHome = -1; break;
                    case MA_CUSTOMIZE: state = ST_CUSTOM; subSel = current; custSel = 0; stateTime = 0; garageHome = -1; break;
                    case MA_UPGRADE: state = ST_UPGRADE; subSel = current; upSel = 0; garageHome = -1; break;
                    case MA_PHOTO: state = returnState; enterPhoto(); break;
                    case MA_NEXTRACE: {
                        int ev = nextRaceEvent();
                        const EventDef& E = events[ev];
                        enterFreeRoam(E.route.pts[0] - E.route.tan[0] * (E.route.closed ? 25.f : 0.f), E.startYaw);
                        if (!E.route.closed) player.car.placeAt(E.route.pts[2], E.startYaw, world);
                        toast("Fast travelled to " + E.name, ACCENT);
                        break;
                    }
                    case MA_EVENTLAB: openEventLab(); break;
                    case MA_MYEVENTS: state = ST_MYEVENTS; subSel = 0; sellConfirm = -1; break;
                    case MA_IMPORT: importEventCode(); break;
                    case MA_SETTINGS: state = ST_SETTINGS; settingsReturn = ST_PAUSE; subSel = 0; break;
                    case MA_EXIT: save(); state = ST_TITLE; stateTime = 0; setupTitle(); break;
                    case MA_QUIT: save(); running = false; break;
                }
                break;
            }
            auto items = pauseItems(racing);
            nav(menuSel, int(items.size()));
            if (a == A_BACK || a == A_PAUSE) state = returnState;
            if (a != A_CONFIRM) break;
            audio.beep(1100, 0.06f);
            const std::string& it = items[menuSel];
            if (it == "Resume") state = returnState;
            else if (it == "World Map") { state = ST_MAP; subSel = 0; }
            else if (it == "Dealership") { state = ST_DEALER; subSel = 0; dealerPaint = stockPaint(kCars[dealerSpec()]); stateTime = 0; }
            else if (it == "My Garage") { state = ST_GARAGE; subSel = current; stateTime = 0; garageHome = -1; }
            else if (it == "Settings") { state = ST_SETTINGS; subSel = 0; }
            else if (it == "Save & Exit to Title") { save(); state = ST_TITLE; stateTime = 0; }
            else if (it == "Quit Game") running = false;
            else if (it == "Restart Event") startRace(raceEvent);
            else if (it == "Quit Event") leaveRace();
            break;
        }
        case ST_MAP: {
            int ne = int(events.size());
            int nh = int(world.estates.size());
            int n = ne + 1 + nh + int(world.landmarks.size());
            nav(subSel, n);
            if (a == A_CAMERA) { mapZoomTarget = std::max(1.f, mapZoomTarget / 1.5f); audio.beep(700, 0.04f); break; }
            if (a == A_UPGRADE) { mapZoomTarget = std::min(5.f, mapZoomTarget * 1.5f); audio.beep(900, 0.04f); break; }
            int home = subSel - ne - 1;
            int lmk = subSel - ne - 1 - nh;
            if (a == A_BACK || a == A_MAP || a == A_PAUSE) state = ST_FREEROAM;
            else if (a == A_TOGGLE && lmk >= 0) {
                gpsEvent = -1; gpsHub = false; gpsEstate = -1; gpsLandmark = lmk;
                toast("GPS route set: " + world.landmarks[lmk].name, vec4(0.8f, 0.45f, 1, 1));
                gpsTimer = 0;
                updateGps(0);
            }
            else if (a == A_CONFIRM && lmk >= 0) {
                // fast travel to the nearest road, facing the landmark
                vec3 lp = world.landmarks[lmk].pos;
                int ri, si;
                if (world.nearestRoad(lp, ri, si, 2000.f)) {
                    vec3 rp = world.roads[ri].pts[si];
                    vec3 d = lp - rp;
                    float yaw = std::atan2(d.x, d.z);
                    enterFreeRoam(rp, yaw);
                    camYaw = yaw;
                    toast("Fast travelled to " + world.landmarks[lmk].name, ACCENT);
                }
            }
            else if (a == A_TOGGLE) {
                gpsEvent = -1; gpsHub = false; gpsEstate = -1; gpsLandmark = -1;
                if (subSel < ne) { gpsEvent = subSel; toast("GPS route set: " + events[subSel].name, vec4(0.8f, 0.45f, 1, 1)); }
                else if (home < 0) { gpsHub = true; toast("GPS route set: Festival Hub", vec4(0.8f, 0.45f, 1, 1)); }
                else { gpsEstate = home; toast("GPS route set: " + world.estates[home].name, vec4(0.8f, 0.45f, 1, 1)); }
                gpsTimer = 0;
                updateGps(0);
            }
            else if (a == A_CONFIRM && home >= 0) {
                const Estate& he = world.estates[home];
                if (ownsEstate(home)) {
                    enterFreeRoam(he.spawn, he.spawnYaw);
                    camYaw = he.spawnYaw;
                    toast("Welcome home: " + he.name, ACCENT);
                } else {
                    gpsEvent = -1; gpsHub = false; gpsEstate = home;
                    gpsTimer = 0;
                    updateGps(0);
                    toast("Buy " + he.name + " to fast travel there - GPS route set", vec4(1.f, 0.8f, 0.3f, 1));
                }
            }
            else if (a == A_CONFIRM) {
                if (subSel < int(events.size())) {
                    const EventDef& E = events[subSel];
                    vec3 t = E.route.tan[0];
                    enterFreeRoam(E.route.pts[0] - t * (E.route.closed ? 25.f : 0.f), E.startYaw);
                    if (!E.route.closed) player.car.placeAt(E.route.pts[2], E.startYaw, world);
                } else {
                    enterFreeRoam(world.hubSpawn, world.hubYaw);
                }
                toast("Fast travelled", ACCENT);
            }
            break;
        }
        case ST_DEALER: {
            int n = int(dealerOrder.size());
            int before = subSel;
            if (a == A_LEFT) subSel = (subSel - 1 + n) % n;
            if (a == A_RIGHT) subSel = (subSel + 1) % n;
            if (subSel != before) { dealerPaint = stockPaint(kCars[dealerSpec()]); audio.beep(880, 0.04f); stateTime = 0; }
            if (a == A_UP) dealerPaint = (dealerPaint - 1 + kPaintCount) % kPaintCount;
            if (a == A_DOWN) dealerPaint = (dealerPaint + 1) % kPaintCount;
            if (a == A_BACK) state = ST_PAUSE;
            if (a == A_CONFIRM) {
                int si = dealerSpec();
                const CarSpec& s = kCars[si];
                if (credits >= s.price) {
                    credits -= s.price;
                    garage.push_back({si, dealerPaint, s.stripes});
                    setPlayerCar(int(garage.size()) - 1);
                    toast("Purchased " + carName(s) + "!", ACCENT);
                    audio.beep(1500, 0.3f);
                    save();
                } else {
                    toast("Not enough credits", vec4(1, 0.4f, 0.3f, 1));
                    audio.beep(220, 0.2f);
                }
            }
            break;
        }
        case ST_PHOTO:
            if (a == A_BACK || a == A_PAUSE || a == A_PHOTO) { state = photoReturn; audio.engineOn = 1; }
            else if (a == A_HIDE) photoHideUi = !photoHideUi;
            else if (a == A_CONFIRM || a == A_INTERACT) photoCapture = true;
            break;
        case ST_WHEELSPIN:
            if (spinT >= 0 && spinT < 4.2f) break;
            if (a == A_BACK) { state = ST_PAUSE; spinT = -1; spinReel.clear(); }
            else if (a == A_CONFIRM) {
                if (wheelspins > 0) { spinT = -1; startWheelspin(); }
                else { spinT = -1; spinReel.clear(); audio.beep(220, 0.2f); }
            }
            break;
        case ST_UPGRADE: {
            nav(upSel, 3);
            if (a == A_BACK) { state = ST_GARAGE; break; }
            if (a == A_CONFIRM) {
                OwnedCar& oc = garage[subSel];
                if (oc.up[upSel] >= 3) { toast("Already fully upgraded", DIM); break; }
                int cost = upgradeCost(oc, upSel);
                if (credits < cost) { toast("Not enough credits", vec4(1, 0.4f, 0.3f, 1)); audio.beep(220, 0.2f); break; }
                credits -= cost;
                oc.up[upSel]++;
                setPlayerCar(current);
                const char* nm[3] = {"Engine", "Weight reduction", "Tyres"};
                toast(std::string(nm[upSel]) + " stage " + std::to_string(oc.up[upSel]) + " fitted", vec4(0.4f, 1, 0.5f, 1));
                audio.beep(1400, 0.2f);
                save();
            }
            break;
        }
        case ST_CUSTOM: customInput(a); break;
        case ST_EVENTLAB: labInput(a); break;
        case ST_MYEVENTS: myEventsInput(a); break;
        case ST_GARAGE: {
            if (a == A_UPGRADE) { state = ST_UPGRADE; upSel = 0; break; }
            if (a == A_CAMERA) { state = ST_CUSTOM; custSel = 0; stateTime = 0; audio.beep(990, 0.05f); break; }
            int n = int(garage.size());
            int before = subSel;
            if (a == A_LEFT) subSel = (subSel - 1 + n) % n;
            if (a == A_RIGHT) subSel = (subSel + 1) % n;
            if (subSel != before) { sellConfirm = -1; audio.beep(880, 0.04f); stateTime = 0; }
            OwnedCar& oc = garage[subSel];
            if (a == A_UP) oc.paint = (oc.paint - 1 + kPaintCount) % kPaintCount;
            if (a == A_DOWN) oc.paint = (oc.paint + 1) % kPaintCount;
            if (a == A_TOGGLE) oc.stripes = !oc.stripes;
            if (a == A_BACK) { sellConfirm = -1; setPlayerCar(current); save(); state = ST_PAUSE; }
            if (a == A_SELL || a == A_INTERACT) {
                if (n <= 1) { toast("You can't sell your only car", vec4(1, 0.4f, 0.3f, 1)); audio.beep(220, 0.2f); }
                else if (subSel == current) { toast("Switch to another car before selling this one", vec4(1, 0.4f, 0.3f, 1)); audio.beep(220, 0.2f); }
                else if (sellConfirm != subSel) { sellConfirm = subSel; audio.beep(660, 0.08f); }
                else {
                    int value = sellValue(oc);
                    std::string name = carName(kCars[oc.spec]);
                    credits += value;
                    garage.erase(garage.begin() + subSel);
                    if (current > subSel) current--;
                    subSel = std::clamp(subSel, 0, int(garage.size()) - 1);
                    sellConfirm = -1;
                    setPlayerCar(current);
                    toast("Sold " + name + " for CR " + fmtThousands(value), vec4(0.4f, 1, 0.5f, 1));
                    audio.beep(1300, 0.25f);
                    save();
                }
                break;
            }
            if (a == A_CONFIRM) {
                sellConfirm = -1;
                setPlayerCar(subSel);
                if (garageHome >= 0 && garageHome < int(world.estates.size())) {
                    const Estate& he = world.estates[garageHome];
                    player.car.placeAt(he.spawn, he.spawnYaw, world);
                    camYaw = he.spawnYaw;
                } else {
                    player.car.placeAt(world.hubSpawn, world.hubYaw, world);
                    camYaw = world.hubYaw;
                }
                save();
                toast("Now driving: " + carName(kCars[oc.spec]), ACCENT);
                state = ST_FREEROAM;
            }
            break;
        }
        case ST_SETTINGS: {
            auto items = settingsItems();
            nav(subSel, int(items.size()));
            if (a == A_BACK) { state = settingsReturn; settingsReturn = ST_PAUSE; break; }
            int d = a == A_LEFT ? -1 : (a == A_RIGHT || a == A_CONFIRM) ? 1 : 0;
            if (!d) break;
            switch (subSel) {
                case 0: settings.mph = !settings.mph; break;
                case 1: settings.motionBlur = !settings.motionBlur; break;
                case 2: settings.shadows = !settings.shadows; break;
                case 3: { const int m[] = {0, 2, 4, 8}; int i = 0; while (i < 3 && m[i] != settings.msaa) i++; settings.msaa = m[(i + d + 4) % 4]; break; }
                case 4: settings.drawDist = (settings.drawDist + d + 4) % 4; break;
                case 5: settings.fullscreen = !settings.fullscreen; break;
                case 6: settings.vsync = !settings.vsync; break;
                case 7: settings.volume = std::clamp(settings.volume + d * 10, 0, 100); break;
                case 8: settings.assists = !settings.assists; break;
                case 9: settings.grass = (settings.grass + d + 3) % 3; break;
                case 10: settings.weather = (settings.weather + d + 4) % 4; wxTimer = 0; break;
                case 11: settings.steerLock = (settings.steerLock + d + 4) % 4; break;
                case 12: settings.timeMode = (settings.timeMode + d + 5) % 5; break;
                case 13: settings.brightness = std::clamp(settings.brightness + d * 5, 50, 150); break;
                case 14: settings.contrast = std::clamp(settings.contrast + d * 5, 50, 150); break;
                case 15: settings.saturation = std::clamp(settings.saturation + d * 5, 0, 200); break;
                case 16: settings.warmth = std::clamp(settings.warmth + d, -10, 10); break;
                case 17: settings.colorBlind = (settings.colorBlind + d + 4) % 4; break;
                case 18: settings.racingLine = !settings.racingLine; break;
                case 19: settings.carReflections = !settings.carReflections; break;
                case 20: settings.darkMode = !settings.darkMode; break;
            }
            audio.beep(990, 0.04f);
            applySettings();
            break;
        }
        case ST_RESULTS:
            if (a == A_CONFIRM || a == A_INTERACT || a == A_BACK) leaveRace();
            break;
    }
}

std::vector<std::string> Game::settingsItems() const {
    const char* dd[] = {"Low", "Medium", "High", "Ultra"};
    return {std::string("Speed units: ") + (settings.mph ? "MPH" : "KM/H"),
            std::string("Motion blur: ") + (settings.motionBlur ? "On" : "Off"),
            std::string("Shadows: ") + (settings.shadows ? "On" : "Off"),
            std::string("Anti-aliasing: ") + (settings.msaa > 1 ? std::to_string(settings.msaa) + "x MSAA" : "Off"),
            std::string("Draw distance: ") + dd[settings.drawDist],
            std::string("Fullscreen: ") + (settings.fullscreen ? "On" : "Off"),
            std::string("V-Sync: ") + (settings.vsync ? "On" : "Off"),
            "Volume: " + std::to_string(settings.volume) + "%",
            std::string("Driving assists: ") + (settings.assists ? "On" : "Off"),
            std::string("Grass: ") + (settings.grass == 0 ? "Off" : settings.grass == 1 ? "Near only" : "Full distance"),
            std::string("Weather: ") + std::array<const char*, 4>{"Dynamic (rain comes and goes)", "Always clear", "Always raining", "Wet roads, no rain"}[std::clamp(settings.weather, 0, 3)],
            std::string("Steering wheel lock: ") + std::array<const char*, 4>{"180", "360", "540", "900"}[std::clamp(settings.steerLock, 0, 3)] + " deg",
            std::string("Time of day: ") + std::array<const char*, 5>{"Day/night cycle", "Always morning", "Always sunset", "Always night", "Always noon"}[std::clamp(settings.timeMode, 0, 4)],
            "Brightness: " + std::to_string(settings.brightness) + "%",
            "Contrast: " + std::to_string(settings.contrast) + "%",
            "Saturation: " + std::to_string(settings.saturation) + "%",
            "Colour temperature: " + std::string(settings.warmth > 0 ? "+" : "") + std::to_string(settings.warmth),
            std::string("Colour-blind mode: ") + std::array<const char*, 4>{"Off", "Protanopia (red)", "Deuteranopia (green)", "Tritanopia (blue)"}[std::clamp(settings.colorBlind, 0, 3)],
            std::string("Racing line: ") + (settings.racingLine ? "On" : "Off"),
            std::string("Car reflections: ") + (settings.carReflections ? "Live (world)" : "Sky only"),
            std::string("Menu theme: ") + (settings.darkMode ? "Dark (sumi night)" : "Light (washi paper)")};
}

// ====================================================================== update
void Game::update(float dt) {
    stateTime += dt;
    titleMenuT += dt;
    menuOpenT = ((state == ST_PAUSE || state == ST_SETTINGS) && lastState != state) ? 0.f : menuOpenT + dt;
    if (state == ST_PAUSE && lastState != ST_PAUSE && (returnState == ST_RACE || returnState == ST_RACE_INTRO)) menuSel = 0;
    hintT = (state == ST_FREEROAM && lastState != ST_FREEROAM && lastState != ST_PAUSE) ? 0.f : hintT + dt;
    if (state != lastState) {
        // menu screens replay their entrance animations whenever they are opened
        if (state == ST_DEALER || state == ST_GARAGE || state == ST_CUSTOM || state == ST_UPGRADE || state == ST_WHEELSPIN || state == ST_SWITCH ||
            state == ST_EVENTLAB || state == ST_MYEVENTS || state == ST_RESULTS || state == ST_MAP) stateTime = 0;
        auto big = [](State s) { return s == ST_TITLE || s == ST_DEALER || s == ST_GARAGE || s == ST_CUSTOM || s == ST_WHEELSPIN || s == ST_UPGRADE ||
                                        s == ST_RESULTS || s == ST_RACE_INTRO || s == ST_MAP || s == ST_EVENTLAB || s == ST_MYEVENTS; };
        if (big(state) || big(lastState)) { transT = 0; transDur = 0.6f; }
    }
    transT += dt;
    lastState = state;
    if (std::fabs(credits - creditsShown) < 1.0 || creditsShown <= 0) creditsShown = double(credits);
    else creditsShown += (double(credits) - creditsShown) * double(damp(5.f, dt));
    R.time += dt;
    simTime += dt;
    for (auto& t : toasts) t.t -= dt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.t <= 0; }), toasts.end());
    flash = std::max(0.f, flash - dt * 2.5f);
    shake = std::max(0.f, shake - dt * 3.f);

    Vehicle* audioCar = &player.car;
    R.atmo = currentAtmo();
    R.useSky = tm.era == 0 && state != ST_TITLE && !keyArt;
    updateWeather(dt);
    updateTimeOfDay(dt);
    R.hdriW = 1; R.night = 0; R.hlOn = 0;
    if (tm.era == 0 && state != ST_TITLE && !keyArt) {
        float w, nt;
        R.atmo = timeOfDayAtmo(todHours, w, nt);
        R.hdriW = w;
        R.night = nt;
        // headlights on after dark (or in heavy rain)
        float on = std::max(smoothstepf(0.25f, 0.6f, nt), smoothstepf(0.5f, 0.9f, wxRain) * 0.6f);
        if (on > 0.01f && player.car.spec) {
            vec3 f = player.car.forward();
            R.hlPos = player.car.pos + f * 1.8f + vec3(0, 0.75f, 0);
            R.hlDir = normalize(f + vec3(0, -0.075f, 0));
            R.hlOn = on;
        }
    }
    {
        bool outdoors = tm.era == 0 && state != ST_TITLE && !keyArt;
        R.rain = outdoors ? wxRain : 0.f;
        R.wet = outdoors ? wxWet : 0.f;
        audio.rain = R.rain;
        if (R.rain > 0.f) {  // overcast: the sun goes in, the air thickens
            float r = R.rain;
            R.atmo.sunColor = R.atmo.sunColor * (1.f - 0.85f * r);
            R.atmo.skyAmb = R.atmo.skyAmb * (1.f - 0.3f * r);
            R.atmo.fogDensity *= 1.f + 1.8f * r;
            R.atmo.fogColor = R.atmo.fogColor + (vec3(0.42f, 0.45f, 0.5f) - R.atmo.fogColor) * r;
        }
    }
    {
        const bool* kb = SDL_GetKeyboardState(nullptr);
        bool hornHeld = (kb[SDL_SCANCODE_H] && state != ST_PHOTO) || (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK));
        audio.horn = (hornHeld && (state == ST_FREEROAM || state == ST_RACE)) ? 1 : 0;
    }
    if (state != ST_PHOTO) audio.engineOn = 1;
    if (popup.t > 0 && state != ST_FREEROAM) popup.t -= dt;
    switch (state) {
        case ST_PHOTO: updatePhoto(dt); return;
        case ST_WHEELSPIN: updateWheelspin(dt); break;
        case ST_TITLE: {
            Controls idle;
            idle.brake = 1.f;
            titleCar.car.update(dt, world, idle);
            titleCar.car.vel = vec3(0, titleCar.car.vel.y, 0);
            titleCar.car.rpm = 900.f;
            audioCar = &titleCar.car;
            break;
        }
        case ST_FREEROAM: {
            rewinding = rewindHeld() && stepRewind();
            if (rewinding) break;
            playerCtl = readDriving();
            stepPhysics(dt);
            recordRewind();
            fx.update(dt, {&player}, world);
            updateSkills(dt);
            updateStunts(dt);
            updateFestival(dt);
            if (player.car.waterTime > 1.2f) {
                int r, s;
                if (world.nearestRoad(player.car.pos, r, s)) {
                    const Road& rd = world.roads[r];
                    player.car.placeAt(rd.pts[s], std::atan2(rd.tan[s].x, rd.tan[s].z), world);
                    camYaw = player.car.yaw;
                }
                toast("Splash! Back to the road.", vec4(0.5f, 0.8f, 1, 1));
            }
            nearEvent = -1;
            for (int i = 0; i < int(events.size()); i++) {
                vec3 d = events[i].markerPos - player.car.pos;
                if (d.x * d.x + d.z * d.z < 16.f * 16.f && player.car.speed() < 20.f) nearEvent = i;
            }
            vec3 hd = world.hubPos - player.car.pos;
            nearHub = hd.x * hd.x + hd.z * hd.z < 45.f * 45.f && player.car.speed() < 12.f;
            updateHomes();
            if (autopilot && !opt.scene.empty() && opt.scene != "race") nearEvent = -1;
            updateGps(dt);
            break;
        }
        case ST_RACE_INTRO: {
            // cars wait on the grid; engine revs follow the throttle
            Controls c = readDriving();
            player.car.throttle = c.throttle;
            float target = 900.f + c.throttle * player.car.spec->redline * 0.8f;
            player.car.rpm += (target - player.car.rpm) * damp(8.f, dt);
            if (stateTime > 2.5f) {
                float before = countdown;
                countdown = 3.f - (stateTime - 2.5f);
                if (std::ceil(before) != std::ceil(countdown) && countdown > 0) audio.beep(660, 0.25f);
                if (countdown <= 0) {
                    state = ST_RACE;
                    stateTime = 0;
                    flash = 0.35f;
                    audio.beep(1320, 0.5f);
                }
            }
            break;
        }
        case ST_RACE:
            // stunt slow motion: the world slows while you're upside down
            slowmo = std::max(0.f, slowmo - dt);
            updateRace(dt * (slowmo > 0.3f ? 0.45f : 0.45f + 0.55f * (1.f - slowmo / 0.3f)));
            updateGates(dt);
            break;
        case ST_RESULTS: {
            // let everyone coast to a stop
            for (size_t i = 0; i < racers.size(); i++) ctl[i] = Controls{0, 0, 0.4f, 0};
            playerCtl = Controls{0, 0, 0.4f, 0};
            stepPhysics(dt);
            break;
        }
        default:
            break;
    }
    updateSpecial(dt);
    updateCamera(dt);
    if (opt.shot.empty()) {
        bool wantMouse = (state == ST_FREEROAM || state == ST_RACE || state == ST_RACE_INTRO) && (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS);
        if (wantMouse != SDL_GetWindowRelativeMouseMode(win)) SDL_SetWindowRelativeMouseMode(win, wantMouse);
    }

    // exhaust flames for every car near the camera; bangs and crackle for the one we're listening to
    if (state != ST_PAUSE && state != ST_MAP) {
        auto exhaust = [&](Vehicle& v, bool listen) {
            if (v.spec->electric) { v.popSeen = v.popCount; if (listen) { audio.crackleLvl = 0.f; audio.limiter = 0.f; } return; }
            bool big = v.popCount != v.popSeen;
            v.popSeen = v.popCount;
            bool crk = v.crackle > 0 && (rand() % 100) < 22;
            if (listen) {
                if (big) audio.popReq = 1.f;
                audio.crackleLvl = v.crackle > 0 ? 1.f : 0.f;
                audio.limiter = v.onLimiter ? 1.f : 0.f;
            }
            if (!big && !crk) return;
            if (length(v.pos - cam.pos) > 120.f) return;
            const CarSpec& sp = *v.spec;
            vec3 f = v.forward(), rt = v.right();
            vec3 back = v.pos - f * (sp.length * 0.5f - 0.02f) + v.up * std::max(0.28f, sp.height * 0.24f);
            int pipes = sp.cylinders >= 8 ? 2 : 1;
            for (int k = 0; k < pipes; k++) {
                vec3 ep = back + rt * (pipes == 2 ? (k ? 1.f : -1.f) * sp.width * 0.22f : -sp.width * 0.12f);
                int n = big ? 14 : 4;
                for (int j = 0; j < n; j++) {
                    float r1 = float(rand() % 1000) / 1000.f, r2 = float(rand() % 1000) / 1000.f;
                    vec3 dir = -f * (big ? 5.f + 9.f * r1 : 3.f + 4.f * r1) + rt * ((r2 - 0.5f) * 0.8f) + vec3(0, 0.4f * r2, 0);
                    float life = big ? 0.07f + 0.08f * r1 : 0.04f + 0.05f * r1;
                    float size = (big ? 0.09f + 0.13f * r2 : 0.06f + 0.07f * r2) * (1.f + 0.08f * j);
                    // blue-white core at the pipe, orange tongue further out
                    vec3 col = j < 2 ? vec3(0.6f, 0.75f, 2.4f) : vec3(2.6f, 1.05f + 0.4f * r1, 0.25f);
                    fx.glow(ep + f * (-0.05f * j), v.vel + dir, life, size, col, -1.f);
                }
            }
        };
        bool playerListed = false;
        for (Racer* r : racers) { exhaust(r->car, &r->car == audioCar); playerListed |= &r->car == &player.car; }
        if (!playerListed) exhaust(player.car, &player.car == audioCar);
    }
    // audio
    audio.rpm = audioCar->rpm;
    audio.throttle = audioCar->throttle;
    audio.slip = audioCar->grounded ? audioCar->tyreSlip : 0.f;
    audio.speed = audioCar->speed();
    bool menus = state == ST_PAUSE || state == ST_MAP || state == ST_DEALER || state == ST_GARAGE || state == ST_CUSTOM || state == ST_SETTINGS || state == ST_EVENTLAB || state == ST_MYEVENTS;
    audio.engineOn = menus ? 0.25f : 1.f;
    if (audioCar->impact > 5.f && simTime - lastImpactSfx > 0.3f) {
        audio.thud(clampf(audioCar->impact / 20.f, 0.2f, 1.f));
        shake = std::max(shake, clampf(audioCar->impact / 15.f, 0.f, 1.f));
        lastImpactSfx = simTime;
        if (audioCar->impact > 8.f && driftScore > 0) { driftScore = 0; toast("Chain broken", vec4(1, 0.4f, 0.3f, 1)); }
        audioCar->impact = 0;
    }
}

void Game::stepPhysics(float dt) {
    const float h = 1.f / 120.f;
    physAcc += dt;
    int guard = 0;
    while (physAcc >= h && guard++ < 12) {
        if (racers.empty()) {
            player.car.update(h, world, playerCtl);
        } else {
            for (size_t i = 0; i < racers.size(); i++) racers[i]->car.update(h, world, racers[i]->isPlayer ? playerCtl : ctl[i]);
            for (size_t i = 0; i < racers.size(); i++)
                for (size_t j = i + 1; j < racers.size(); j++) collideCars(racers[i]->car, racers[j]->car);
            if (raceEvent >= 0) for (Racer* r : racers) railCollide(*r);
        }
        physAcc -= h;
    }
    if (guard >= 12) physAcc = 0;
}

void Game::updateRace(float dt) {
    const EventDef& E = events[raceEvent];
    rewinding = !player.finished && rewindHeld() && stepRewind();
    if (rewinding) return;
    recordRewind();
    updateFestival(dt);
    raceTime += dt;
    playerCtl = readDriving();
    if (player.finished) playerCtl = Controls{0, 0, 0.3f, 0};
    float total = E.totalLength();
    for (size_t i = 0; i < racers.size(); i++) {
        Racer* r = racers[i];
        if (r->isPlayer) continue;
        if (r->finished) { ctl[i] = Controls{0, 0, 0.35f, 0}; continue; }
        float diff = r->progress - player.progress;
        float rubber = clampf(1.f - diff / 3000.f, 0.93f, 1.06f);
        ctl[i] = aiDrive(*r, E.route, world, racers, rubber, dt);
    }
    // stunt ramps are solid: the car rides up the wedge before it leaves the lip
    for (Racer* r : racers) {
        r->car.groundBoost = 0;
        for (size_t k = 0; k < E.ramps.size(); k++) {
            float d = E.ramps[k], len = 9.f, H = 1.25f;
            vec3 p0 = E.route.pointAt(d - len), t = E.route.tangentAt(d), side(-t.z, 0, t.x);
            vec3 q = r->car.pos - p0;
            float along = dot(q, t), lat = dot(q, side);
            if (along >= 0.f && along <= len && std::fabs(lat) < rampHalfW(E)) r->car.groundBoost = H * along / len;
        }
    }
    stepPhysics(dt);
    fx.update(dt, racers, world);
    for (Racer* r : racers) {
        trackProgress(*r, E);
        // fell off (down a drop or into water) or wandered far away: put them back on the road
        {
            const Route& rt = E.route;
            int i = std::clamp(r->idx, 0, int(rt.pts.size()) - 1);
            vec3 q = rt.pts[i];
            float off = std::hypot(r->car.pos.x - q.x, r->car.pos.z - q.z);
            bool fallen = r->car.pos.y < q.y - 7.f || r->car.waterTime > 0.6f || (E.type != EV_CROSS && off > rt.width * 0.5f + 45.f);
            r->lost = fallen && !r->finished ? r->lost + dt : 0.f;
            if (r->lost > (r->isPlayer ? 1.4f : 1.0f)) {
                int k = i;
                for (int back = 0; back < 6 && k > 0; back++) k--;  // a few metres before where they left
                if (rt.closed && i < 6) k = (i - 6 + int(rt.pts.size())) % int(rt.pts.size());
                r->car.placeAt(rt.pts[k], std::atan2(rt.tan[k].x, rt.tan[k].z), world);
                r->car.vel = rt.tan[k] * std::min(r->car.speed(), 14.f);
                r->idx = k;
                r->lost = 0;
                if (r->isPlayer) { camYaw = r->car.yaw; offRouteTime = 0; flash = 0.25f; toast("Back on track", vec4(0.4f, 0.9f, 1.f, 1)); }
            }
        }
        // stunt ramps: launch into a roll or a spin
        if (!E.ramps.empty() && !r->finished && !r->car.stunting()) {
            float L = E.route.length;
            float a0 = E.route.closed ? std::fmod(r->lastProg, L) : r->lastProg;
            float a1 = E.route.closed ? std::fmod(r->progress, L) : r->progress;
            if (a1 > a0 && a1 - a0 < 60.f)
                for (size_t k = 0; k < E.ramps.size(); k++) {
                    float rd = E.ramps[k];
                    if (!(rd > a0 && rd <= a1)) continue;
                    vec3 rp = E.route.pointAt(rd), rtn = E.route.tangentAt(rd), side(-rtn.z, 0, rtn.x);
                    float lat = dot(r->car.pos - rp, side);
                    if (std::fabs(lat) > rampHalfW(E) + 0.6f) continue;
                    if (r->car.pos.y > groundAtRoute(E.route, rd, rp) + 1.25f + 2.5f) continue;  // sailed over it
                    Vehicle& c = r->car;
                    float v = std::max(c.speed(), 27.f);
                    vec3 f = c.forward();
                    c.vel = vec3(f.x * v, 0, f.z * v);
                    c.vel.y = clampf(6.f + v * 0.09f, 8.f, 11.5f);
                    c.pos.y += 0.35f;
                    float T = 2.f * c.vel.y / (9.81f * 0.8f);
                    int kind = E.rampKind[k];
                    float roll = kind == 0 ? 1.f : kind == 1 ? -1.f : kind == 3 ? (k % 2 ? -2.f : 2.f) : 0.f;
                    float yawT = kind == 2 ? (k % 2 ? -1.f : 1.f) : 0.f;
                    c.startStunt(roll, yawT, T * 0.9f);
                    if (r->isPlayer) {
                        festEvent(FS_RAMP, 1);
                        slowmo = 1.1f;
                        shake = 0.3f;
                        audio.beep(330, 0.25f);
                        const char* nm[4] = {"BARREL ROLL", "BARREL ROLL", "FLAT 360", "DOUBLE ROLL"};
                        chainAdd(nm[kind], kind == 3 ? 900 : 600);
                    }
                }
        }
        if (!E.boosts.empty() && !r->finished) {
            float L = E.route.length;
            float a0 = E.route.closed ? std::fmod(r->lastProg, L) : r->lastProg;
            float a1 = E.route.closed ? std::fmod(r->progress, L) : r->progress;
            if (a1 > a0 && a1 - a0 < 60.f)
                for (float bd : E.boosts)
                    if (bd > a0 && bd <= a1) {
                        vec3 bp = E.route.pointAt(bd);
                        if (std::hypot(bp.x - r->car.pos.x, bp.z - r->car.pos.z) < 5.5f) {
                            vec3 f = r->car.forward();
                            float top = r->car.spec->topKmh / 3.6f * 1.12f;
                            float add = std::min(9.f, std::max(0.f, top - r->car.speed()));
                            r->car.vel = r->car.vel + f * add;
                            if (r->isPlayer) { flash = 0.15f; shake = 0.35f; audio.beep(240, 0.25f); chainAdd("BOOST", 150); }
                        }
                    }
        }
        r->lastProg = r->progress;
        if (!r->finished && r->progress >= total - (E.route.closed ? 0.f : 4.f)) {
            r->finished = true;
            r->finishTime = raceTime;
            if (r->isPlayer) { flash = 0.4f; audio.beep(1500, 0.4f); }
        }
    }
    std::vector<Racer*> order = racers;
    std::sort(order.begin(), order.end(), [](Racer* a, Racer* b) {
        if (a->finished != b->finished) return a->finished;
        if (a->finished) return a->finishTime < b->finishTime;
        return a->progress > b->progress;
    });
    for (int i = 0; i < int(order.size()); i++) if (order[i]->isPlayer) playerPlace = i + 1;
    // off-route warning
    vec3 rp = E.route.pts[player.idx];
    float off = std::hypot(rp.x - player.car.pos.x, rp.z - player.car.pos.z);
    offRouteTime = off > E.route.width * 0.5f + 35.f ? offRouteTime + dt : 0.f;
    updateSkills(dt);
    if (player.car.waterTime > 1.2f) {
        int i = player.idx;
        player.car.placeAt(E.route.pts[i], std::atan2(E.route.tan[i].x, E.route.tan[i].z), world);
    }
    if (player.finished) {
        finishDelay += dt;
        if (finishDelay > 2.f) finishRace();
    }
}

// ====================================================================== camera
vec3 Game::showroomSpot() const {
    vec3 fwd(std::sin(world.hubYaw), 0, std::cos(world.hubYaw));
    vec3 p = world.hubPos - fwd * 12.f;
    p.y = world.hubPos.y + 0.48f;
    return p;
}

int Game::cameraContext() const {
    if (state == ST_TITLE) return 2;
    if (state == ST_DEALER || state == ST_GARAGE || state == ST_CUSTOM || state == ST_UPGRADE) return 1;
    if (state == ST_RACE_INTRO && stateTime < 2.5f) return 3;
    if (state == ST_RESULTS) return 4;
    return 10 + (lookBack ? 9 : camMode);
}

void Game::updateCamera(float dt) {
    vec3 lp = cam.pos, lt = cam.target;
    float lf = cam.fov;
    int ctx = cameraContext();
    Racer& tr = state == ST_TITLE ? titleCar : player;
    if (ctx != camCtx && camCtx >= 0) {
        // glide from wherever the last camera was, relative to the car so it keeps up at speed
        bool chaseSwap = ctx >= 10 && camCtx >= 10;
        blendFromOff = lp - tr.car.pos;
        blendFromTgtOff = lt - tr.car.pos;
        blendFromFov = lf;
        blendT = 0;
        blendDur = chaseSwap ? 0.32f : 1.25f;
        blendLift = chaseSwap ? 0.f : 2.2f;
        if (length(blendFromOff) > 80.f || ctx == 3) blendT = 99;  // too far to fly: the screen wipe covers the cut
    }
    camCtx = ctx;
    updateCameraRaw(dt);
    if (blendT < blendDur) {
        blendT += dt;
        float t = clampf(blendT / blendDur, 0.f, 1.f);
        t = t * t * t * (t * (t * 6.f - 15.f) + 10.f);  // smootherstep
        vec3 from = tr.car.pos + blendFromOff, fromT = tr.car.pos + blendFromTgtOff;
        vec3 p = from + (cam.pos - from) * t;
        p.y += std::sin(t * PI) * blendLift;
        p.y = std::max(p.y, world.heightAt(p.x, p.z) + 0.4f);
        cam.target = fromT + (cam.target - fromT) * t;
        cam.pos = p;
        cam.fov = blendFromFov + (cam.fov - blendFromFov) * t;
        cam.update();
    }
    // test hook: RL_AERIAL="x,y,z,tx,ty,tz" pins the camera (aerial screenshots of the world)
    if (const char* ae = getenv("RL_AERIAL")) {
        float v[6] = {0, 300, 0, 0, 0, 1};
        std::sscanf(ae, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
        if (getenv("RL_AERIAL_REL")) { float g = world.heightAt(v[3], v[5]); v[1] += g; v[4] += g; }  // heights above the ground at the target
        cam.pos = vec3(v[0], v[1], v[2]);
        cam.target = vec3(v[3], v[4], v[5]);
        cam.farZ = 9000.f;
        cam.update();
    }
}

void Game::updateCameraRaw(float dt) {
    int pw = R.width, ph = R.height;
    cam.roll = 0;
    cam.aspect = float(pw) / float(ph);
    cam.nearZ = 0.25f;
    bool showroom = state == ST_DEALER || state == ST_GARAGE || state == ST_CUSTOM || state == ST_UPGRADE;
    Racer& tr = state == ST_TITLE ? titleCar : player;
    Vehicle& car = tr.car;
    vec3 fwd = car.forward();
    float v = car.speed();

    if (showroom) {
        vec3 c = showroomSpot();
        if (subSel != lastSubSel) { showKick += 0.9f; lastSubSel = subSel; }
        showKickSm += (showKick - showKickSm) * damp(3.5f, dt);
        float a = stateTime * 0.16f + 0.6f + showKickSm;
        float rad = 7.6f + 0.9f * std::sin(stateTime * 0.21f);
        float hgt = 1.55f + 0.55f * std::sin(stateTime * 0.13f + 1.f);
        cam.pos = c + vec3(std::cos(a) * rad, hgt, std::sin(a) * rad);
        cam.target = c + vec3(0, 0.6f, 0);
        if (state == ST_CUSTOM || state == ST_UPGRADE) {
            // the list fills the left of the screen: look a little left of the car so it sits right of centre
            vec3 d = normalize(vec3(cam.target.x - cam.pos.x, 0, cam.target.z - cam.pos.z));
            vec3 left(d.z, 0, -d.x);
            float sh = state == ST_UPGRADE ? 0.f : 1.7f;
            shoOff += (sh - shoOff) * damp(4.f, dt);
            cam.target = cam.target + left * shoOff;
            cam.pos = cam.pos + left * (shoOff * 0.5f);
        } else shoOff += (0.f - shoOff) * damp(4.f, dt);
        cam.fov = 0.80f + 0.03f * std::sin(stateTime * 0.17f);
        cam.update();
        return;
    }
    if (state == ST_TITLE) {
        // three slow cinematic shots on a loop: front three-quarter, low side track, high rear
        vec3 f = car.forward(), rgt = car.right();
        int shot = keyArt ? 0 : int(stateTime / 8.f) % 3;
        float t = keyArt ? 0.f : std::fmod(stateTime, 8.f) / 8.f;
        if (shot == 0) {
            float a = std::sin(stateTime * 0.08f) * 0.35f;
            float d = 5.8f - t * 0.8f;
            vec3 off = f * (std::cos(a) * d) - rgt * (std::sin(a) * d + 2.6f);
            cam.pos = car.pos + off + vec3(0, 0.85f, 0);
            cam.target = car.pos + vec3(0, 0.75f, 0) - f * 0.4f - rgt * 1.6f;
        } else if (shot == 1) {
            cam.pos = car.pos + rgt * 5.2f + f * (-4.5f + 7.5f * t) + vec3(0, 0.55f, 0);
            cam.target = car.pos + f * (-1.f + 2.f * t) + vec3(0, 0.55f, 0);
        } else {
            cam.pos = car.pos - f * 9.f - rgt * 3.5f + vec3(0, 4.2f - 2.4f * t, 0);
            cam.target = car.pos + f * 2.f + vec3(0, 0.4f, 0);
        }
        cam.pos.y = std::max(cam.pos.y, world.heightAt(cam.pos.x, cam.pos.z) + 0.45f);
        cam.fov = 0.80f;
        cam.update();
        return;
    }
    if (state == ST_RACE_INTRO && stateTime < 2.5f) {
        float a = stateTime * 0.5f + car.yaw + 0.8f;
        cam.pos = car.pos + vec3(std::sin(a) * 16.f, 6.f - stateTime, std::cos(a) * 16.f);
        cam.target = car.pos + fwd * 8.f;
        cam.fov = 1.0f;
        cam.update();
        camYaw = car.yaw;
        camPos = cam.pos;
        return;
    }
    if (state == ST_RESULTS) {
        float a = stateTime * 0.25f;
        cam.pos = car.pos + vec3(std::sin(a) * 8.f, 2.5f, std::cos(a) * 8.f);
        cam.pos.y = std::max(cam.pos.y, world.heightAt(cam.pos.x, cam.pos.z) + 1.f);
        cam.target = car.pos + vec3(0, 0.7f, 0);
        cam.update();
        return;
    }

    // chase cameras
    // body-relative acceleration drives the camera's weight: it hangs back when you launch,
    // creeps in under braking and swings wide through corners
    if (dt > 0) {
        vec3 acc = (car.vel - prevCarVel) / dt;
        if (length(acc) > 60.f) acc = vec3(0, 0, 0);  // teleports and resets
        accLongSm += (clampf(dot(acc, fwd), -25.f, 25.f) - accLongSm) * damp(2.6f, dt);
        accLatSm += (clampf(dot(acc, car.right()), -25.f, 25.f) - accLatSm) * damp(2.2f, dt);
        if (car.grounded && !wasGrounded && prevVy < -3.5f) shake = std::max(shake, std::min(0.7f, -prevVy * 0.07f));
        wasGrounded = car.grounded;
        prevVy = car.vel.y;
        prevCarVel = car.vel;
    }
    float velYaw = car.yaw;
    if (v > 4.f && car.forwardSpeed() > 0) velYaw = std::atan2(car.vel.x, car.vel.z);
    float targetYaw = lerpf(car.yaw, car.yaw + wrapAngle(velYaw - car.yaw), 0.55f);
    float yerr = wrapAngle(targetYaw - camYaw);
    if (std::fabs(yerr) > 2.4f) { camYaw = targetYaw; camYawVel = 0; }
    else {
        camYawVel += (yerr * 26.f - camYawVel * 9.f) * dt;  // spring: eases in and out of turns
        camYaw += camYawVel * dt;
    }
    float yaw = camYaw + (lookBack ? PI : 0.f);
    vec3 cf(std::sin(yaw), 0, std::cos(yaw));
    float kmh = v * 3.6f;
    float fovT = 1.08f + clampf(kmh / 300.f, 0.f, 1.f) * 0.32f + clampf(accLongSm / 12.f, -0.4f, 1.f) * 0.07f;
    fovSm += (fovT - fovSm) * damp(3.f, dt);
    cam.fov = fovSm;
    // free look: gamepad right stick (springs back) or mouse (recentres after a pause)
    if (pad) {
        float rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.f;
        float ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.f;
        if (std::fabs(rx) > 0.2f || std::fabs(ry) > 0.2f) {
            lookYaw += (-rx * PI * 0.95f - lookYaw) * damp(8.f, dt);
            lookPitch += (ry * 0.55f - lookPitch) * damp(8.f, dt);
            lookIdle = 0;
            padLooking = true;
        } else if (padLooking) {
            padLooking = false;
            lookIdle = 10.f;  // spring straight back
        }
    }
    lookIdle += dt;
    if (lookIdle > 1.6f && (v > 3.f || lookIdle > 5.f)) {
        lookYaw = wrapAngle(lookYaw) * (1.f - damp(2.5f, dt));
        lookPitch *= 1.f - damp(2.5f, dt);
    }
    float orbit = clampf(std::fabs(wrapAngle(lookYaw)) / 0.6f, 0.f, 1.f);
    if (camMode <= 1 || lookBack) {
        float dist = camMode == 0 ? 6.4f : 4.8f;
        float hgt = camMode == 0 ? 1.95f : 1.45f;
        dist *= car.spec->length / 4.4f;
        if (car.spec->body == BODY_TRUCK) hgt += 0.4f;
        dist += clampf(kmh / 300.f, 0.f, 1.f) * 0.8f + accLongSm * 0.045f;
        hgt -= accLongSm * 0.006f;
        float lyaw = yaw + lookYaw;
        float el = clampf(std::atan2(hgt - 0.95f, dist) + lookPitch, -0.08f, 1.35f);
        float D = std::sqrt(dist * dist + (hgt - 0.95f) * (hgt - 0.95f)) * lookZoom;
        vec3 pivot = car.pos + vec3(0, 0.95f, 0);
        vec3 back(std::sin(lyaw) * std::cos(el), -std::sin(el), std::cos(lyaw) * std::cos(el));
        cam.pos = pivot - back * D - car.right() * (accLatSm * 0.045f);
        cam.target = pivot + cf * (3.f * (1.f - orbit)) - car.right() * (accLatSm * 0.02f);
        camRollSm += (clampf(accLatSm * 0.0028f, -0.04f, 0.04f) - camRollSm) * damp(3.f, dt);
        cam.roll = camRollSm * (1.f - orbit);
    } else {
        mat4 bm = car.cabinMatrix();  // the camera stays upright through stunt spins
        const CarModel& m = *tr.model;
        vec3 local = camMode == 2 ? m.eye : (camMode == 3 ? m.hoodCam : m.bumperCam);
        cam.nearZ = camMode == 2 ? 0.04f : 0.12f;
        cam.pos = bm.transformPoint(local);
        vec3 dir = bm.transformDir(vec3(0, -0.02f, 1));
        float a = std::atan2(dir.x, dir.z) + lookYaw, p = std::asin(clampf(dir.y, -1.f, 1.f)) - lookPitch * 0.8f;
        cam.target = cam.pos + vec3(std::sin(a) * std::cos(p), std::sin(p), std::cos(a) * std::cos(p)) * 10.f;
        cam.fov = camMode == 2 ? 1.05f + (fovSm - 1.08f) * 0.4f : fovSm + 0.1f;
    }
    if (camMode <= 1 || lookBack) {
        float g = world.heightAt(cam.pos.x, cam.pos.z) + 0.5f;
        cam.pos.y = std::max(cam.pos.y, g);
    }
    float sh = shake * 0.12f + clampf((kmh - 160.f) / 200.f, 0.f, 1.f) * 0.02f + (car.grounded ? 0.f : 0.f);
    if (sh > 0) {
        // layered, non-repeating wobble (smoother than one sine per axis)
        float t = simTime;
        vec3 n(std::sin(t * 31.f) * 0.6f + std::sin(t * 57.3f + 1.7f) * 0.4f, std::sin(t * 43.1f + 0.4f) * 0.6f + std::sin(t * 71.7f) * 0.4f,
               std::sin(t * 37.9f + 2.2f) * 0.6f + std::sin(t * 63.f + 0.9f) * 0.4f);
        cam.pos += n * sh;
        cam.target += n * sh * 0.4f;
    }
    cam.update();
}

// ====================================================================== render
mat4 Game::importedPartMatrix(Racer& r, int role) const {
    if (role == 0) return bodyFrame(r);
    if (role == 9) return steeringFrame(r, r.model->imp->steerC);
    int k = (role - 1) % 4;
    bool spin = role <= 4;
    mat4 M = r.car.chassisMatrix() * translate(r.model->wheelPos[k]);
    if (k < 2) M = M * rotateY(-r.car.wheelSteer);
    if (r.camber != 0.f) M = M * rotateZ(k % 2 == 0 ? r.camber : -r.camber);
    if (spin) M = M * rotateX(r.car.wheelSpin);
    return M;
}

void Game::drawImported(Racer& r, bool glassPass, const Camera& c) {
    ImportedModel& m = *r.model->imp;
    Shader& s = R.car;
    R.setCommon(s, c);
    {
        vec3 d = r.car.pos - R.probePos;
        bool useProbe = R.probeOn && R.probeFrames >= 6 && d.x * d.x + d.z * d.z < 45.f * 45.f && drawCam != &reflCamera;
        s.set("uProbe", useProbe ? 1 : 0);
        s.set("uProbeLod", 0.6f);
    }
    s.set("uGlassPass", glassPass ? 1 : 0);
    s.set("uPaint", r.paint);
    s.set("uBrake", r.car.brake > 0.1f ? 1.f : 0.f);
    for (auto& part : m.parts) {
        const ImpMaterial* mt = part.mat >= 0 && part.mat < int(m.mats.size()) ? &m.mats[part.mat] : nullptr;
        bool glass = mt && (mt->flags & (IF_GLASS | IF_BLEND));
        if (glass != glassPass) continue;
        s.set("uModel", importedPartMatrix(r, part.role));
        setCosmeticUniforms(s, r, part.role == 0);
        s.set("uColor", mt ? mt->color : vec4(0.6f, 0.6f, 0.6f, 1));
        int fl = mt ? int(mt->flags) : 0;
        // a wheel sharing the body's paint material (some packs do this) would come out body coloured:
        // wheels get a metallic finish instead
        if (part.role >= 1 && part.role <= 8 && (fl & (IF_PAINT | 256))) fl = (fl & ~(IF_PAINT | 256)) | IF_CHROME;
        s.set("uFlags", fl);
        s.set("uEmis", mt ? mt->emis : vec3(0, 0, 0));
        int hb = mt && mt->base >= 0, hn = mt && mt->normal >= 0, he = mt && mt->emissive >= 0;
        s.set("uHasBase", hb);
        s.set("uHasNormal", hn);
        s.set("uHasEmis", he);
        if (hb) { glActiveTexture(GL_TEXTURE0 + U_CARBASE); glBindTexture(GL_TEXTURE_2D, m.tex[mt->base]); }
        if (hn) { glActiveTexture(GL_TEXTURE0 + U_CARNORMAL); glBindTexture(GL_TEXTURE_2D, m.tex[mt->normal]); }
        if (he) { glActiveTexture(GL_TEXTURE0 + U_CAREMIS); glBindTexture(GL_TEXTURE_2D, m.tex[mt->emissive]); }
        part.mesh.draw();
    }
    s.set("uBody", 0);
    s.set("uLivery", 0);
    glActiveTexture(GL_TEXTURE0);
}

void Game::drawCar(Racer& r, Shader& s, bool glassPass) {
    auto drawAero = [&]() {
        if (glassPass) return;
        const Mesh* am = aeroMesh(r);
        if (!am) return;
        s.set("uPaint", r.paint);
        s.set("uStripe", r.stripe);
        s.set("uModel", bodyFrame(r));
        am->draw();
    };
    if (r.model->imp) {
        drawImported(r, glassPass, drawCam ? *drawCam : cam);
        s.use();
        s.set("uBody", 0);
        s.set("uLivery", 0);
        drawAero();
        return;
    }
    s.set("uPaint", r.paint);
    s.set("uStripe", r.stripe);
    s.set("uBrake", r.car.brake > 0.1f ? 1.f : 0.f);
    s.set("uHeadlights", R.night * 2.5f);
    s.set("uGlassPass", glassPass ? 1 : 0);
    mat4 bm = bodyFrame(r);
    s.set("uModel", bm);
    setCosmeticUniforms(s, r, true);
    r.model->body.draw();
    setCosmeticUniforms(s, r, false);
    if (glassPass) return;
    drawAero();
    s.set("uModel", bm);
    r.model->interior.draw();
    {
        const float locks[4] = {180.f, 360.f, 540.f, 900.f};
        float a = std::clamp(r.car.wheelSteer / 0.6f, -1.f, 1.f) * locks[std::clamp(settings.steerLock, 0, 3)] * PI / 360.f;
        s.set("uModel", bm * r.model->steeringBase * rotateZ(a));
    }
    r.model->steering.draw();
    for (int i = 0; i < 4; i++) {
        s.set("uModel", r.car.wheelMatrix(*r.model, i));
        r.model->wheel.draw();
        s.set("uModel", r.car.wheelMatrix(*r.model, i, false));
        r.model->caliper.draw();
    }
}

void Game::renderWorld(const Camera& c, bool showroom) {
    const float dists[] = {800.f, 1200.f, 1800.f, 2600.f};
    float drawDist = dists[settings.drawDist];
    R.atmo.fogDensity = 1.1f / drawDist;
    std::vector<Racer*> cars;
    if (thumbActive) cars = thumbCars;
    else if (state == ST_TITLE) cars.push_back(&titleCar);
    else {
        cars.push_back(&player);
        for (auto& b : bots) cars.push_back(&b);
    }
    // showroom / display cars on the hub podiums
    vec3 hf(std::sin(world.hubYaw), 0, std::cos(world.hubYaw)), hr(-hf.z, 0, hf.x);
    int displaySpecs[3] = {0, 0, 0};
    {
        std::vector<int> byPrice;
        for (int i = 0; i < int(kCars.size()); i++) if (listed(kCars[i])) byPrice.push_back(i);
        std::sort(byPrice.begin(), byPrice.end(), [](int a, int b) { return kCars[a].price > kCars[b].price; });
        for (int i = 0; i < 3; i++) displaySpecs[i] = byPrice[std::min(i, int(byPrice.size()) - 1)];
        std::swap(displaySpecs[0], displaySpecs[1]);
    }
    std::vector<Racer> display(3);
    for (int i = 0; i < 3; i++) {
        int sidx = displaySpecs[i];
        if (i == 1 && showroom) {
            sidx = state == ST_DEALER ? dealerSpec() : garage[std::clamp(subSel, 0, int(garage.size()) - 1)].spec;
        }
        Racer& d = display[i];
        d.model = &models[sidx];
        d.car.spec = &kCars[sidx];
        vec3 p = world.hubPos + hr * ((i - 1) * 12.f) - hf * 12.f;
        d.car.pos = vec3(p.x, world.hubPos.y + 0.48f, p.z);
        d.car.yaw = R.time * 0.3f + i * 2.f;
        d.car.up = vec3(0, 1, 0);
        d.paint = kCars[sidx].color;
        d.stripe = kCars[sidx].stripes ? stripeFor(d.paint) : d.paint;
        if (i == 1 && showroom) {
            if (state == ST_DEALER) {
                d.paint = kPaints[dealerPaint];
                d.stripe = kCars[sidx].stripes ? stripeFor(d.paint) : d.paint;
            } else {
                const OwnedCar& oc = garage[std::clamp(subSel, 0, int(garage.size()) - 1)];
                d.paint = kPaints[oc.paint];
                d.stripe = oc.stripes ? stripeFor(d.paint) : d.paint;
                applyCosmetics(d, oc);
            }
            d.car.yaw = stateTime * 0.35f;
        }
        cars.push_back(&d);
    }

    // ---- shadow passes: a sharp near cascade around the car and a wide far cascade
    auto drawTrees = [&](Shader& sh, vec3 eye, float range, float lodDist) {
        sh.set("tFoliage", int(U_PINE));
        world.pines.draw(eye, range, lodDist);
        sh.set("tFoliage", int(U_LEAVES));
        world.leafy.draw(eye, range, lodDist);
        if (!rocksAsProps) world.rocks.draw(eye, range * 0.7f, range);
    };
    if (R.shadows) {
        R.tex.bindAll();
        vec3 fdir = normalize(vec3(c.target.x - c.pos.x, 0, c.target.z - c.pos.z));
        const float ext[2] = {48.f, 430.f};
        const float lead[2] = {18.f, 300.f};
        for (int cas = 0; cas < 2; cas++) {
            vec3 focus = c.target + fdir * lead[cas];
            R.beginShadowPass(cas, focus, ext[cas], cas == 0 ? 320.f : 1000.f);
            R.depth.use();
            float reach = ext[cas] * 1.5f + 120.f;
            for (auto& ch : world.chunks) {
                float dx = ch.center.x - focus.x, dz = ch.center.z - focus.z;
                if (dx * dx + dz * dz < reach * reach) ch.mesh.draw();
            }
            world.hubMesh.draw();
            world.cityMesh.draw();
            world.propsMesh.draw();
            if (raceEvent >= 0) gateLit.draw();
            for (Racer* r : cars) {
                if (r->model->imp) {
                    for (auto& part : r->model->imp->parts) {
                        R.depth.set("uModel", importedPartMatrix(*r, part.role));
                        part.mesh.draw();
                    }
                    if (const Mesh* am = aeroMesh(*r)) { R.depth.set("uModel", bodyFrame(*r)); am->draw(); }
                    continue;
                }
                R.depth.set("uModel", r->car.bodyMatrix());
                r->model->body.draw();
                for (int i = 0; i < 4; i++) {
                    R.depth.set("uModel", r->car.wheelMatrix(*r->model, i));
                    r->model->wheel.draw();
                }
            }
            R.depth.set("uModel", mat4());
            if (cas == 0) drawProps(c, focus, ext[cas] * 1.45f, 0.f, true);
            R.depthTree.use();
            drawTrees(R.depthTree, focus, ext[cas] * 1.45f, cas == 0 ? 400.f : 0.f);
        }
        R.endShadowPasses();
    }

    // ---- planar reflection for the lakes (half resolution, clipped at the water plane)
    bool reflOn = false;
    mat4 reflVP;
    if (settings.drawDist >= 1 && R.reflFbo) {
        for (auto& L : world.lakes) {
            float dx = L.c.x - c.pos.x, dz = L.c.z - c.pos.z;
            if (std::sqrt(dx * dx + dz * dz) < drawDist * 0.8f + L.r) reflOn = true;
        }
        // near the sea: look for open water around the camera
        if (!reflOn && c.pos.y < world.waterLevel + 140.f)
            for (float rr : {50.f, 140.f, 300.f})
                for (int k = 0; k < 12 && !reflOn; k++) {
                    float a = k / 12.f * 2.f * PI;
                    if (world.heightAt(c.pos.x + std::cos(a) * rr, c.pos.z + std::sin(a) * rr) < world.waterLevel - 1.f) reflOn = true;
                }
        if (c.pos.y < world.waterLevel + 0.3f) reflOn = false;
    }
    // wet roads: mirror the world in a plane at the road height under the car (puddles)
    bool wetRefl = false;
    float wetPlane = 0;
    bool nearLake = false;
    for (auto& L : world.lakes) {
        float dx = L.c.x - c.pos.x, dz = L.c.z - c.pos.z;
        if (std::sqrt(dx * dx + dz * dz) < L.r + 60.f) nearLake = true;
    }
    if (R.wet > 0.3f && R.reflFbo && !nearLake && (state == ST_FREEROAM || state == ST_RACE || state == ST_RACE_INTRO || state == ST_PHOTO)) {
        wetRefl = true;
        reflOn = false;  // the lakes fall back to sky reflections while you're away from them
        wetPlane = world.heightAt(player.car.pos.x, player.car.pos.z);
    }
    // environment probe: one cube face per frame around the car you're looking at
    if (R.probeOn && R.probeTex && !thumbActive && settings.carReflections) {
        vec3 pp = showroom ? c.target + vec3(0, 0.5f, 0) : player.car.pos + vec3(0, 1.1f, 0);
        vec3 dp = pp - R.probePos;
        if (dp.x * dp.x + dp.y * dp.y + dp.z * dp.z > 80.f * 80.f) R.probeFrames = 0;  // teleported: rebuild all faces
        R.probePos = pp;
        int facesNow = R.probeFrames < 6 ? 6 : 1;
        for (int k = 0; k < facesNow; k++) {
            int face = R.probeFrames % 6;
            Camera pc = R.beginProbeFace(face, pp);
            drawCam = &pc;
            R.drawSky(pc);
            R.setCommon(R.terrain, pc);
            R.terrain.set("uWater", world.waterLevel);
        R.terrain.set("uHalf", World::HALF);
        R.terrain.set("uFuji", vec4(kFuji.x, kFujiBase, kFuji.z, kFujiR));
        glActiveTexture(GL_TEXTURE0 + U_HEIGHT); glBindTexture(GL_TEXTURE_2D, world.heightTex); glActiveTexture(GL_TEXTURE0);
            for (auto& ch : world.chunks) {
                float dx = ch.center.x - pp.x, dz = ch.center.z - pp.z;
                if (dx * dx + dz * dz < 700.f * 700.f) ch.mesh.draw();
            }
            R.setCommon(R.road, pc);
            world.roadMesh.draw();
            R.setCommon(R.object, pc);
            R.object.set("uPaint", vec3(1, 1, 1));
            R.object.set("uStripe", vec3(1, 1, 1));
            R.object.set("uBrake", 0.f);
            world.hubMesh.draw();
            world.cityMesh.draw();
            glDisable(GL_CULL_FACE);
            if (raceEvent >= 0) gateLit.draw();
            R.setCommon(R.tree, pc);
            drawTrees(R.tree, pp, 260.f, 120.f);
            glEnable(GL_CULL_FACE);
            R.probeFrames++;
        }
        R.endProbe();
        drawCam = &c;
    }
    if (reflOn || wetRefl) {
        float Lw = reflOn ? world.waterLevel : wetPlane;
        Camera& rc = reflCamera;
        rc = c;
        rc.pos.y = 2 * Lw - c.pos.y;
        rc.target.y = 2 * Lw - c.target.y;
        rc.update();
        reflVP = rc.viewProj;
        drawCam = &rc;
        R.beginReflection();
        R.clipY = Lw - (reflOn ? 0.25f : 0.6f);
        R.drawSky(rc);
        R.setCommon(R.terrain, rc);
        R.terrain.set("uWater", world.waterLevel);
        R.terrain.set("uHalf", World::HALF);
        R.terrain.set("uFuji", vec4(kFuji.x, kFujiBase, kFuji.z, kFujiR));
        glActiveTexture(GL_TEXTURE0 + U_HEIGHT); glBindTexture(GL_TEXTURE_2D, world.heightTex); glActiveTexture(GL_TEXTURE0);
        float rlim = std::min(drawDist, 1400.f);
        for (auto& ch : world.chunks) {
            float dx = ch.center.x - c.pos.x, dz = ch.center.z - c.pos.z;
            if (dx * dx + dz * dz < rlim * rlim) ch.mesh.draw();
        }
        R.setCommon(R.road, rc);
        world.roadMesh.draw();
        R.setCommon(R.object, rc);
        R.object.set("uPaint", vec3(1, 1, 1));
        R.object.set("uStripe", vec3(1, 1, 1));
        R.object.set("uBrake", 0.f);
        R.object.set("uHeadlights", R.night * 2.5f);
        world.hubMesh.draw();
        world.cityMesh.draw();
        glDisable(GL_CULL_FACE);
        world.propsMesh.draw();
        if (raceEvent >= 0) gateLit.draw();
        for (Racer* r : cars) drawCar(*r, R.object);
        R.setCommon(R.tree, rc);
        drawTrees(R.tree, c.pos, rlim * 0.7f, 150.f);
        glEnable(GL_CULL_FACE);
        R.clipY = -1e9f;
        drawCam = &c;
    }
    drawCam = &c;

    R.beginScene();
    R.drawSky(c);

    vec3 camFwd = normalize(vec3(c.target.x - c.pos.x, 0, c.target.z - c.pos.z));
    // terrain
    R.setCommon(R.terrain, c);
    R.terrain.set("uWater", world.waterLevel);
        R.terrain.set("uHalf", World::HALF);
        R.terrain.set("uFuji", vec4(kFuji.x, kFujiBase, kFuji.z, kFujiR));
        glActiveTexture(GL_TEXTURE0 + U_HEIGHT); glBindTexture(GL_TEXTURE_2D, world.heightTex); glActiveTexture(GL_TEXTURE0);
    for (auto& ch : world.chunks) {
        // chunks are 300 m across: judge them by their nearest point and their corners, not the centre,
        // or the ground right under a camera looking down can belong to a chunk "behind" it and vanish
        if (ch.state != 2) continue;
        float nx = clampf(c.pos.x, ch.bmin.x, ch.bmax.x) - c.pos.x;
        float nz = clampf(c.pos.z, ch.bmin.z, ch.bmax.z) - c.pos.z;
        float d2 = nx * nx + nz * nz;
        float lim = drawDist + 110.f;
        if (d2 > lim * lim) continue;
        if (d2 > 120.f * 120.f) {
            bool behind = true;
            for (int k = 0; k < 4 && behind; k++) {
                vec3 q((k & 1) ? ch.bmax.x : ch.bmin.x, 0, (k & 2) ? ch.bmax.z : ch.bmin.z);
                vec3 dq(q.x - c.pos.x, 0, q.z - c.pos.z);
                if (dot(normalize(dq), camFwd) > -0.35f) behind = false;
            }
            if (behind) continue;
        }
        ch.mesh.draw();
    }
    // the rest of the island out to the horizon (Fuji, the Alps, the far coast)
    R.terrain.set("uFarCut", drawDist * 0.92f);
    world.farTerrain.draw();
    R.terrain.set("uFarCut", 0.f);
    // roads
    R.setCommon(R.road, c);
    R.road.set("uReflVP", reflVP);
    R.road.set("uHasRefl", wetRefl ? 1 : 0);
    R.road.set("tRefl", int(U_REFL));
    if (wetRefl) { glActiveTexture(GL_TEXTURE0 + U_REFL); glBindTexture(GL_TEXTURE_2D, R.reflTex); glActiveTexture(GL_TEXTURE0); }
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -2.f);
    world.roadMesh.draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (!thumbActive) fx.drawSkids(R, c);
    // hub, props, cars
    R.setCommon(R.object, c);
    R.object.set("uPaint", vec3(1, 1, 1));
    R.object.set("uStripe", vec3(1, 1, 1));
    R.object.set("uBrake", 0.f);
    R.object.set("uHeadlights", R.night * 2.5f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -3.f);
    world.hubMesh.draw();
    world.cityMesh.draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
    R.object.set("uGlassPass", 0);
    glDisable(GL_CULL_FACE);
    world.propsMesh.draw();
    stuntMesh.draw();
    if (!thumbActive) drawFestivalWorld(c);
    if (raceEvent >= 0 && !thumbActive) {
        gateLit.draw();
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.f, -6.f);
        gatePaint.draw();
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    for (Racer* r : cars) drawCar(*r, R.object);
    drawProps(c, c.pos, drawDist * 0.6f, 110.f, false);
    R.object.use();
    glEnable(GL_CULL_FACE);
    // vegetation (alpha-tested, double sided)
    glDisable(GL_CULL_FACE);
    bool a2c = R.msaa > 1 && R.msFbo;
    if (a2c) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    R.setCommon(R.tree, c);
    drawTrees(R.tree, c.pos, drawDist * 0.85f, 420.f);
    if (settings.grass) {
        R.setCommon(R.grass, c);
        R.grass.set("uHalf", World::HALF);
        R.grass.set("uWaterLevel", world.waterLevel);
        R.grass.set("uHub", world.hubPos);
        glActiveTexture(GL_TEXTURE0 + U_HEIGHT);
        glBindTexture(GL_TEXTURE_2D, world.heightTex);
        glActiveTexture(GL_TEXTURE0 + U_ROADDIST);
        glBindTexture(GL_TEXTURE_2D, world.roadTex);
        glActiveTexture(GL_TEXTURE0);
        world.drawGrass(R.grass, c.pos, settings.grass == 1 ? 2 : 4);
    }
    if (a2c) glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glEnable(GL_CULL_FACE);
    // water (blended so the shallows show the lake bed)
    R.setCommon(R.water, c);
    R.water.set("uReflVP", reflVP);
    R.water.set("uHasRefl", reflOn ? 1 : 0);
    R.water.set("uHalf", World::HALF);
    R.water.set("uWaterLevel", world.waterLevel);
    glActiveTexture(GL_TEXTURE0 + U_HEIGHT);
    glBindTexture(GL_TEXTURE_2D, world.heightTex);
    glActiveTexture(GL_TEXTURE0 + U_REFL);
    glBindTexture(GL_TEXTURE_2D, reflOn ? R.reflTex : 0);
    glActiveTexture(GL_TEXTURE0);
    // two passes: the bed seen through the water is tinted by what the water absorbs,
    // then the water's own colour, reflections, sun glitter and foam go on top
    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    R.water.set("uPass", 0);
    world.waterMesh.draw();
    glBlendFunc(GL_ONE, GL_ONE);
    R.water.set("uPass", 1);
    world.waterMesh.draw();
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    // car glass (transparent, after everything opaque)
    R.setCommon(R.object, c);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    for (Racer* r : cars) drawCar(*r, R.object, true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    if (!thumbActive) fx.drawParticles(R, c);
    if (!thumbActive) fx.drawGlow(R, c);

    // ---- blended overlays
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    R.setCommon(R.unlit, c);
    R.unlit.set("uFog", 0);
    R.unlit.set("uTint", vec4(1, 1, 1, 1));
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.f, -4.f);
    for (Racer* r : cars) {
        float hgt = r->car.pos.y - world.heightAt(r->car.pos.x, r->car.pos.z);
        float a = 1.f - clampf(hgt / 4.f, 0.f, 1.f);
        R.unlit.set("uTint", vec4(1, 1, 1, a));
        mat4 m = r->car.chassisMatrix();
        m.m[13] -= hgt - 0.05f;
        vec3 dims = r->model ? r->model->dims : vec3(r->car.spec->length, r->car.spec->width, 1.3f);
        R.unlit.set("uModel", m * scale(vec3(dims.y * 0.5f, 1, dims.x * 0.52f)));
        blobMesh.draw();
        if (r->model && a > 0.3f) {
            R.unlit.set("uTint", vec4(1, 1, 1, a * 0.85f));
            for (int k = 0; k < 4; k++) {
                vec3 wp = r->model->wheelPos[k];
                R.unlit.set("uModel", m * translate(vec3(wp.x, 0.01f, wp.z)) * scale(vec3(r->model->wheelW * 0.9f, 1, r->model->wheelR * 0.75f)));
                tyreBlobMesh.draw();
            }
            R.unlit.set("uTint", vec4(1, 1, 1, a));
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (raceEvent >= 0 && !thumbActive) {
        drawGateFx();
        const EventDef& RE = events[raceEvent];
        if (!RE.boosts.empty()) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            float pulse = 0.65f + 0.35f * std::sin(R.time * 8.f);
            R.unlit.set("uTint", vec4(0.2f * pulse, 0.9f * pulse, 1.f, 1));
            for (float bd : RE.boosts) {
                vec3 bp = RE.route.pointAt(bd), bt = RE.route.tangentAt(bd);
                bp.y = world.heightAt(bp.x, bp.z) + 0.06f;
                R.unlit.set("uModel", translate(bp) * rotateY(std::atan2(bt.x, bt.z)));
                boostMesh.draw();
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }
    if (!thumbActive && (state == ST_FREEROAM || state == ST_PAUSE || state == ST_MAP)) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        R.unlit.set("uFog", 0);
        for (auto& e : events) {
            vec4 col = eventColor(e.type);
            vec3 p = e.markerPos;
            R.unlit.set("uTint", col);
            R.unlit.set("uModel", translate(vec3(p.x, p.y + 0.15f, p.z)));
            beaconMesh.draw();
            ringMesh.draw();
        }
        R.unlit.set("uTint", vec4(1.0f, 0.25f, 0.55f, 1));
        R.unlit.set("uModel", translate(world.hubPos + vec3(0, 0.2f, 0)) * scale(vec3(2.f, 1.4f, 2.f)));
        beaconMesh.draw();
        drawEstateMarkers();
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    if (!thumbActive && !showroom) R.drawRain(c, vec3(1.6f, 0, 0.7f));
}

GLuint Game::captureThumb(const Camera& c0, const Atmosphere* atmo, float aspect) {
    Camera c = c0;
    c.aspect = float(R.width) / float(R.height);
    c.update();
    Atmosphere saved = R.atmo;
    if (atmo) R.atmo = *atmo;
    thumbActive = true;
    renderWorld(c, false);
    thumbActive = false;
    float sa = R.sunAmt;
    R.sunAmt = 0;
    R.endScene(0, 0, &c);
    R.sunAmt = sa;
    R.atmo = saved;
    int cw = R.width, ch = R.height;
    if (cw / float(ch) > aspect) cw = int(ch * aspect);
    else ch = int(cw / aspect);
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, (R.width - cw) / 2, (R.height - ch) / 2, cw, ch, 0);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

Racer Game::makeShowCar(int spec, int paint, vec3 pos, float yaw) {
    Racer r;
    r.car.init(&kCars[spec], pos, yaw);
    r.model = &models[spec];
    r.paint = paint >= 0 ? kPaints[paint] : kCars[spec].color;
    r.stripe = kCars[spec].stripes ? stripeFor(r.paint) : r.paint;
    r.car.placeAt(pos, yaw, world);
    return r;
}

// A lakeside lay-by used for car portraits: car angled three-quarters to camera, lake behind.
void Game::photoSpot(vec3& carPos, float& carYaw, vec3& camPos, vec3& camTarget, float len) {
    const World::Lake& L = world.lakes[0];
    vec3 p(L.c.x + L.r * 1.42f, 0, L.c.z + 40.f);
    p.y = world.heightAt(p.x, p.z);
    vec3 toLake = normalize(vec3(L.c.x - p.x, 0, L.c.z - p.z));
    vec3 side(-toLake.z, 0, toLake.x);
    vec3 fwd = normalize(-toLake * 0.55f + side * 0.85f);
    carPos = p;
    carYaw = std::atan2(fwd.x, fwd.z);
    float d = 3.6f + len * 0.62f;
    camPos = p - toLake * d + side * (d * 0.12f) + vec3(0, 1.05f, 0);
    camTarget = p + toLake * 1.2f + vec3(0, 0.62f, 0);
}

void Game::buildCarThumb(int spec) {
    vec3 cp, ct, pp;
    float yaw;
    photoSpot(pp, yaw, cp, ct, kCars[spec].length);
    Racer r = makeShowCar(spec, -1, pp, yaw);
    thumbCars = {&r};
    Camera c;
    c.pos = vec3(cp.x, world.heightAt(cp.x, cp.z) + 1.05f, cp.z);
    c.pos.y = std::max(c.pos.y, r.car.pos.y + 0.8f);
    c.target = vec3(ct.x, r.car.pos.y + 0.55f, ct.z);
    c.fov = 0.62f;
    carThumb[spec] = captureCutout(c, r, 16.f / 10.f);
    thumbCars.clear();
}

// A studio cut-out of a car, like the Horizon menus: the scene is rendered twice, with and without the
// car, and whatever changed becomes the picture. Where the only change is the ground getting darker,
// that's the car's shadow, kept as a soft black shadow under it.
GLuint Game::captureCutout(const Camera& c0, Racer& r, float aspect) {
    Camera c = c0;
    c.aspect = float(R.width) / float(R.height);
    c.update();
    int cw = R.width, ch = R.height;
    if (cw / float(ch) > aspect) cw = int(ch * aspect);
    else ch = int(cw / aspect);
    int x0 = (R.width - cw) / 2, y0 = (R.height - ch) / 2;
    std::vector<uint8_t> A(size_t(cw) * ch * 4), B(size_t(cw) * ch * 4);
    auto pass = [&](std::vector<uint8_t>& out, bool withCar) {
        thumbCars.clear();
        if (withCar) thumbCars.push_back(&r);
        thumbActive = true;
        renderWorld(c, false);
        thumbActive = false;
        float sa = R.sunAmt;
        R.sunAmt = 0;
        R.endScene(0, 0, &c);
        R.sunAmt = sa;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x0, y0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    };
    pass(A, true);
    pass(B, false);
    std::vector<uint8_t> O(A.size());
    for (size_t i = 0; i < A.size(); i += 4) {
        float a[3] = {float(A[i]), float(A[i + 1]), float(A[i + 2])}, b[3] = {float(B[i]), float(B[i + 1]), float(B[i + 2])};
        float diff = std::max({std::fabs(a[0] - b[0]), std::fabs(a[1] - b[1]), std::fabs(a[2] - b[2])});
        float la = 0.3f * a[0] + 0.59f * a[1] + 0.11f * a[2], lb = 0.3f * b[0] + 0.59f * b[1] + 0.11f * b[2];
        float outA = 0, oc[3] = {0, 0, 0};
        if (diff > 5.f) {
            float k = lb > 6.f ? la / lb : 1.f;
            float err = std::max({std::fabs(a[0] - k * b[0]), std::fabs(a[1] - k * b[1]), std::fabs(a[2] - k * b[2])});
            if (k < 0.98f && err < 9.f) {
                outA = clampf((1.f - k) * 0.95f, 0.f, 0.8f);  // shadow
            } else {
                outA = smoothstepf(5.f, 36.f, diff);
                for (int q = 0; q < 3; q++) oc[q] = clampf((a[q] - (1.f - outA) * b[q]) / std::max(outA, 0.05f), 0.f, 255.f);
            }
        }
        O[i] = uint8_t(oc[0]); O[i + 1] = uint8_t(oc[1]); O[i + 2] = uint8_t(oc[2]); O[i + 3] = uint8_t(outA * 255.f + 0.5f);
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, O.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void Game::buildMenuThumbs() {
    menuThumbsBuilt = true;
    for (int i = 2; i < 8; i++)
        if (menuThumb[i]) { glDeleteTextures(1, &menuThumb[i]); menuThumb[i] = 0; }
    const float aspect = 4.f / 3.f;
    // dealership: the priciest car posed at the lake
    {
        int best = showcaseSpec();
        vec3 cp, ct, pp;
        float yaw;
        photoSpot(pp, yaw, cp, ct, kCars[best].length);
        Racer r = makeShowCar(best, -1, pp, yaw);
        thumbCars = {&r};
        Camera c;
        c.pos = vec3(cp.x, r.car.pos.y + 0.7f, cp.z);
        c.target = vec3(ct.x, r.car.pos.y + 0.7f, ct.z);
        c.fov = 0.75f;
        menuThumb[3] = captureThumb(c, nullptr, aspect);
    }
    // garage: your car at the festival hub
    {
        const OwnedCar& oc = garage[current];
        vec3 hf(std::sin(world.hubYaw), 0, std::cos(world.hubYaw)), hr(-hf.z, 0, hf.x);
        vec3 p = world.hubSpawn + hf * 4.f;
        Racer r = makeShowCar(oc.spec, oc.paint, p, world.hubYaw + 2.4f);
        thumbCars = {&r};
        vec3 f = r.car.forward();
        Camera c;
        c.pos = r.car.pos + f * 5.5f + r.car.right() * 2.2f + vec3(0, 0.9f, 0);
        c.target = r.car.pos + vec3(0, 0.6f, 0) - f * 0.4f;
        c.fov = 0.8f;
        menuThumb[4] = captureThumb(c, nullptr, aspect);
    }
    // houses: your car parked on the drive, house behind it
    for (GLuint t : houseThumb) if (t) glDeleteTextures(1, &t);
    houseThumb.assign(world.estates.size(), 0);
    for (int i = 0; i < int(world.estates.size()); i++) {
        const Estate& e = world.estates[i];
        const OwnedCar& oc = garage[current];
        vec3 F(std::sin(e.yaw), 0, std::cos(e.yaw)), Rt(std::cos(e.yaw), 0, -std::sin(e.yaw));
        Racer r = makeShowCar(oc.spec, oc.paint, e.spawn + F * 1.5f, e.yaw + 2.6f);
        thumbCars = {&r};
        Camera c;
        c.pos = e.pos + F * (e.roadGap - 4.f) - Rt * 9.f + vec3(0, 3.2f, 0);
        c.target = e.pos + Rt * 1.5f - F * 4.f + vec3(0, 2.6f, 0);
        c.fov = 0.95f;
        houseThumb[i] = captureThumb(c, nullptr, aspect);
    }
    // next race: low chase view on the start line
    {
        const EventDef& E = events[nextRaceEvent()];
        const OwnedCar& oc = garage[current];
        vec3 p = E.route.pts[3];
        float yaw = std::atan2(E.route.tan[3].x, E.route.tan[3].z);
        Racer r = makeShowCar(oc.spec, oc.paint, p, yaw);
        thumbCars = {&r};
        vec3 f = r.car.forward();
        Camera c;
        c.pos = r.car.pos - f * 6.5f + r.car.right() * 1.4f + vec3(0, 1.0f, 0);
        c.target = r.car.pos + f * 12.f + vec3(0, 0.8f, 0);
        c.fov = 0.9f;
        menuThumb[5] = captureThumb(c, nullptr, aspect);
    }
    // settings: a vista over the lake
    {
        thumbCars.clear();
        const World::Lake& L = world.lakes[0];
        vec3 from(L.c.x + L.r * 1.9f, 0, L.c.z + L.r * 0.9f);
        from.y = world.heightAt(from.x, from.z) + 14.f;
        Camera c;
        c.pos = from;
        c.target = vec3(L.c.x, world.waterLevel + 4.f, L.c.z);
        c.fov = 0.9f;
        menuThumb[6] = captureThumb(c, nullptr, aspect);
    }
    // save & exit: the sunset title shot
    {
        thumbCars = {&titleCar};
        vec3 p = titleCar.car.pos, f = titleCar.car.forward(), r = titleCar.car.right();
        Camera c;
        c.pos = p + f * 5.8f - r * 2.6f + vec3(0, 1.0f, 0);
        c.target = p + vec3(0, 0.7f, 0);
        c.fov = 0.8f;
        menuThumb[7] = captureThumb(c, &titleAtmo, aspect);
    }
    thumbCars.clear();
}

int Game::sellValue(const OwnedCar& oc) const {
    int p = kCars[oc.spec].price;
    if (p <= 0) p = 20000;
    return (p * 6 / 10 + 500) / 1000 * 1000;
}

void Game::render() {
    bool showroom = state == ST_DEALER || state == ST_GARAGE || state == ST_CUSTOM || state == ST_UPGRADE;
    {
        // terrain streams in around the camera; after a jump (spawn, fast travel) load the view at once
        const float dists[] = {800.f, 1200.f, 1800.f, 2600.f};
        float keep = dists[settings.drawDist] + 250.f;
        vec3 cp = cam.pos;
        bool jumped = length(vec3(cp.x - lastStreamPos.x, 0, cp.z - lastStreamPos.z)) > 300.f;
        world.loadChunksNow(cp, jumped ? keep : 450.f);
        world.streamChunks(cp, keep, 3);
        lastStreamPos = cp;
    }
    if ((state == ST_PAUSE || state == ST_SETTINGS) && !menuThumbsBuilt) buildMenuThumbs();
    if ((state == ST_PAUSE || state == ST_SETTINGS || state == ST_MAP || (state == ST_TITLE && titleMenu)) && current < int(garage.size())) {
        int sp = garage[current].spec;
        if (sp < int(carThumb.size()) && !carThumb[sp]) buildCarThumb(sp);
    }
    if (state == ST_DEALER || state == ST_GARAGE || state == ST_CUSTOM) {
        // render missing car cards: all at once for screenshots, otherwise one per frame
        int budget = opt.shot.empty() ? 1 : 1000;
        int n = state == ST_DEALER ? int(dealerOrder.size()) : int(garage.size());
        for (int k = 0; k < n && budget > 0; k++) {
            int i = (subSel + (k % 2 ? -(k + 1) / 2 : k / 2) + n * 4) % n;
            int sp = state == ST_DEALER ? dealerOrder[i] : garage[i].spec;
            if (!carThumb[sp]) { buildCarThumb(sp); budget--; }
        }
    }
    renderWorld(cam, showroom);
    float blur = 0;
    if (settings.motionBlur && (state == ST_FREEROAM || state == ST_RACE)) blur = clampf((player.car.speedKmh() - 90.f) / 260.f, 0.f, 1.f) * (camMode == 2 ? 0.35f : 0.85f);
    if (state == ST_TITLE && settings.motionBlur) blur = clampf((titleCar.car.speedKmh() - 90.f) / 260.f, 0.f, 0.6f);
    // sun shafts when looking towards the sun
    {
        vec3 sd = R.atmo.sunDir;
        vec4 sp = cam.viewProj * vec4(cam.pos + sd * 3000.f, 1.f);
        R.sunAmt = 0;
        if (sp.w > 0) {
            vec2 ndc(sp.x / sp.w, sp.y / sp.w);
            R.sunUV = vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
            float facing = dot(normalize(cam.target - cam.pos), sd);
            float edge = 1.f - smoothstepf(1.0f, 1.6f, std::max(std::fabs(ndc.x), std::fabs(ndc.y)));
            R.sunAmt = smoothstepf(0.1f, 0.6f, facing) * edge * (state == ST_TITLE ? 1.4f : 1.f);
        }
    }
    R.useAO = settings.shadows;
    R.motionBlur = (settings.motionBlur && (state == ST_FREEROAM || state == ST_RACE || state == ST_TITLE)) ? 0.55f : 0.f;
    R.endScene(blur * (R.motionBlur > 0 ? 0.35f : 1.f), flash, &cam, true);
    if (photoCapture && state == ST_PHOTO) {
        photoCapture = false;
        std::string path = prefPath + "photo-" + std::to_string(SDL_GetTicks()) + ".bmp";
        saveShot(path);
        toast("Photo saved", WHITE);
        audio.beep(2000, 0.05f);
        flash = 0.5f;
    }
    if ((pauseShotPending || !menuThumb[0]) && state == ST_PAUSE) {
        pauseShotPending = false;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        if (!menuThumb[0]) glGenTextures(1, &menuThumb[0]);
        glBindTexture(GL_TEXTURE_2D, menuThumb[0]);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, R.width, R.height, 0);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    hud.begin(R.width, R.height);
    if (!opt.noHud) { drawHud(); drawTransition(); }
    hud.end();
}

// ====================================================================== HUD
std::string Game::speedStr(float ms) const {
    return std::to_string(int(ms * (settings.mph ? 2.23694f : 3.6f) + 0.5f));
}

void Game::updateGps(float dt) {
    gpsTimer -= dt;
    if (gpsTimer > 0) return;
    gpsTimer = 0.4f;
    vec3 from = player.car.pos;
    vec3 to;
    int target = -1;
    if (gpsHub) { to = world.hubPos; target = -2; }
    else if (gpsEstate >= 0 && gpsEstate < int(world.estates.size())) { to = world.estates[gpsEstate].spawn; target = -3; }
    else if (gpsLandmark >= 0 && gpsLandmark < int(world.landmarks.size())) { to = world.landmarks[gpsLandmark].pos; target = -4; }
    else if (gpsEvent >= 0 && gpsEvent < int(events.size())) { to = events[gpsEvent].markerPos; target = gpsEvent; }
    else {
        // auto: nearest event not yet won
        float bd = 1e30f;
        for (int i = 0; i < int(events.size()); i++) {
            bool won = i < int(bestPlace.size()) && bestPlace[i] == 1;
            vec3 d = events[i].markerPos - from;
            float dd = d.x * d.x + d.z * d.z + (won ? 4e6f : 0.f);
            if (dd < bd) { bd = dd; target = i; to = events[i].markerPos; }
        }
        if (target < 0) { gps.path.clear(); gpsShown = -1; return; }
    }
    vec3 d = to - from;
    if (d.x * d.x + d.z * d.z < (target == -4 ? 70.f * 70.f : 30.f * 30.f)) {
        if (gpsHub || gpsEvent >= 0 || gpsEstate >= 0 || gpsLandmark >= 0) toast("Destination reached", vec4(0.8f, 0.45f, 1, 1));
        gpsHub = false;
        gpsEstate = -1;
        gpsLandmark = -1;
        gpsEvent = -1;
        gps.path.clear();
        gpsShown = -1;
        return;
    }
    gps.route(world, from, to);
    gpsShown = target;
}

void Game::drawSpeedo() {
    float u = hud.ui;
    Vehicle& car = player.car;
    float R0 = 104 * u;
    vec2 c(hud.w - R0 - 34 * u, hud.h - R0 - 30 * u);
    float redline = car.spec->redline;
    float dialMax = std::ceil((redline + 400.f) / 1000.f) * 1000.f;
    float a0 = PI * 0.75f, a1 = PI * 2.25f;
    auto ang = [&](float rpm) { return lerpf(a0, a1, clampf(rpm / dialMax, 0.f, 1.f)); };
    auto dir = [](float a) { return vec2(std::cos(a), std::sin(a)); };

    using namespace wa;
    // a paper gauge face: ink ring, ink ticks, a vermilion redline and needle
    hud.circle(c + vec2(5, 6) * u, R0 + 14 * u, A(SHADOW, 0.55f), 64);
    hud.circle(c, R0 + 14 * u, A(PAPER, 0.94f), 64);
    hud.arc(c, R0 + 10 * u, R0 + 14 * u, 0, 2 * PI, INK, 64);
    // speed against top speed, painted round the rim in ink
    hud.arc(c, R0 + 4 * u, R0 + 8 * u, a0, lerpf(a0, a1, clampf(car.speedKmh() / std::max(60.f, car.spec->topKmh), 0.f, 1.f)), A(INK2, 0.8f), 48);
    hud.arc(c, R0 - 10 * u, R0 - 2 * u, ang(redline * 0.97f), a1, VERM, 16);
    static float needle = 0, needleV = 0, lastT = 0;
    float ndt = clampf(R.time - lastT, 0.f, 0.05f);
    lastT = R.time;
    for (int k = 0; k < 4; k++) {   // a needle on a spring: overshoots a touch on hard revs, then settles
        needleV += ((car.rpm - needle) * 260.f - needleV * 22.f) * ndt * 0.25f;
        needle += needleV * ndt * 0.25f;
    }
    float rpmA = ang(std::max(0.f, needle));
    for (int i = 0; i <= int(dialMax / 250.f); i++) {
        float rpm = i * 250.f;
        float a = ang(rpm);
        bool major = i % 4 == 0, half = i % 2 == 0;
        float len = major ? 16 * u : half ? 10 * u : 6 * u;
        float th = major ? 3.2f * u : 1.5f * u;
        bool red = rpm >= redline * 0.97f;
        hud.line(c + dir(a) * (R0 - 2 * u - len), c + dir(a) * (R0 - 2 * u), th, red ? PAPER : A(INK, major ? 1.f : 0.6f));
        if (major) {
            vec2 p = c + dir(a) * (R0 - 32 * u);
            hud.text(p.x, p.y - 9 * u, 17 * u, std::to_string(int(rpm / 1000.f)), red ? VERM : INK, 1, false);
        }
    }
    hud.text(c.x, c.y - 52 * u, 10 * u, "x1000 RPM", MUTED, 1, false);
    vec2 nd = dir(rpmA), np(-nd.y, nd.x);
    vec2 tip = c + nd * (R0 + 2 * u), base = c + nd * (R0 * 0.5f);
    hud.triangle(tip, base + np * (3.5f * u), base - np * (3.5f * u), VERM);
    std::string sp = speedStr(car.speed());
    while (sp.size() < 3) sp = "0" + sp;
    int lead = 0;
    while (lead < 2 && sp[lead] == '0') lead++;
    float sz = 50 * u;
    float total = hud.textWidth(sz, sp);
    float sx = c.x - total * 0.5f, sy = c.y - 32 * u;
    float wl = hud.textWidth(sz, sp.substr(0, lead));
    if (lead) hud.text(sx, sy, sz, sp.substr(0, lead), A(INK, 0.18f), 0, false);
    hud.text(sx + wl, sy, sz, sp.substr(lead), INK, 0, false);
    hud.text(c.x, c.y + 20 * u, 11 * u, settings.mph ? "MPH" : "KM/H", VERM, 1, false);
    std::string g = car.gear < 0 ? "R" : (car.speed() < 0.5f && car.throttle < 0.05f ? "N" : std::to_string(car.gear));
    bool shift = car.rpm > redline * 0.94f && car.gear > 0 && car.gear < 6 && std::fmod(simTime, 0.16f) < 0.08f;
    vec2 gp = c + vec2(0, 56 * u);
    static std::string lastGear;
    static float gearT = -10;
    if (g != lastGear) { lastGear = g; gearT = R.time; }
    float gk = 1.f + 0.35f * (1.f - ease((R.time - gearT) / 0.25f));   // pops big, settles
    hud.rect(gp.x - 17 * u * gk, gp.y - 17 * u * gk, 34 * u * gk, 34 * u * gk, shift ? VERM : INK);
    hud.text(gp.x, gp.y - 14 * u * gk, 26 * u * gk, g, PAPER, 1, false);
    // assist lamps
    if (settings.assists) {
        const char* names[3] = {"ABS", "TCS", "SC"};
        float lv[3] = {car.absLight, car.tcsLight, car.scLight};
        float lx = c.x - 34 * u;
        for (int i = 0; i < 3; i++) {
            vec4 col = lv[i] > 0.05f ? wa::A(wa::VERM, 0.5f + 0.5f * lv[i]) : wa::A(wa::INK, 0.3f);
            float w = hud.textWidth(10 * u, names[i]);
            hud.text(lx, c.y + 80 * u, 10 * u, names[i], col, 0, false);
            lx += w + 8 * u;
        }
    }
}

void Game::drawMinimap() {
    float u = hud.ui;
    float Rr = 110 * u;
    vec2 c(Rr + 34 * u, hud.h - Rr - 30 * u);
    Vehicle& car = player.car;
    float yaw = camYaw;
    vec3 fwd(std::sin(yaw), 0, std::cos(yaw)), rgt(-std::cos(yaw), 0, std::sin(yaw));
    float range = 230.f + clampf(car.speed() * 2.5f, 0.f, 160.f);
    mapRangeSm += (range - mapRangeSm) * 0.05f;
    float sc = Rr / mapRangeSm;  // px per metre
    auto toScreen = [&](vec3 p) {
        vec3 d = p - car.pos;
        return c + vec2(dot(d, rgt), -dot(d, fwd)) * sc;
    };
    const vec4 GPS = wa::VERM;
    // an old paper chart: sepia land, ink roads, vermilion route
    hud.circle(c + vec2(5, 6) * u, Rr + 8 * u, wa::A(wa::SHADOW, 0.5f), 64);
    hud.circle(c, Rr + 5 * u, wa::A(wa::PAPER, 0.95f), 64);
    hud.clip(c.x, c.y, Rr);
    // terrain underlay
    vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    vec2 sp[4], uv[4];
    float S = Rr * 1.5f;
    for (int i = 0; i < 4; i++) {
        vec2 o = corners[i] * S;
        sp[i] = c + o;
        vec3 wp = car.pos + rgt * (o.x / sc) + fwd * (-o.y / sc);
        uv[i] = vec2((wp.x + World::HALF) / (2 * World::HALF), (World::HALF - wp.z) / (2 * World::HALF));
    }
    hud.mapQuad(sp, uv, vec4(0.92f, 0.84f, 0.70f, 0.42f));
    // roads
    float lim = Rr * 1.15f;
    for (const Road& rd : world.roads) {
        bool dirt = rd.surface != SURF_ASPHALT;
        float th = (dirt ? 2.2f : 3.6f) * u * clampf(rd.width / 12.f, 0.8f, 1.3f);
        vec4 col = dirt ? wa::A(wa::MUTED, 0.75f) : wa::A(wa::INK, 0.9f);
        int n = int(rd.pts.size());
        vec2 prev = toScreen(rd.pts[0]);
        for (int i = 2; i <= n + (rd.closed ? 0 : -1); i += 2) {
            vec2 p = toScreen(rd.pts[std::min(i, n - 1) % n]);
            if (rd.closed && i >= n) p = toScreen(rd.pts[0]);
            if (length(p - c) < lim || length(prev - c) < lim) hud.line(prev, p, th, col);
            prev = p;
        }
    }
    auto polyline = [&](const std::vector<vec3>& pts, int step, float th, vec4 col) {
        if (pts.size() < 2) return;
        vec2 prev = toScreen(pts[0]);
        for (size_t i = step; i < pts.size() + step; i += step) {
            vec2 p = toScreen(pts[std::min(i, pts.size() - 1)]);
            if (length(p - c) < lim * 1.3f || length(prev - c) < lim * 1.3f) {
                hud.line(prev, p, th, col);
                hud.circle(p, th * 0.5f, col, 8);
            }
            prev = p;
            if (i >= pts.size() - 1) break;
        }
    };
    if (raceEvent >= 0) {
        polyline(events[raceEvent].route.pts, 3, 8 * u, vec4(0.1f, 0.03f, 0.15f, 0.7f));
        polyline(events[raceEvent].route.pts, 3, 5 * u, GPS);
        for (Racer* r : racers) {
            if (r->isPlayer) continue;
            vec2 p = toScreen(r->car.pos);
            hud.circle(p, 5 * u, vec4(0.05f, 0.05f, 0.05f, 1), 12);
            hud.circle(p, 3.6f * u, vec4(r->paint.x, r->paint.y, r->paint.z, 1), 12);
        }
    } else {
        polyline(gps.path, 1, 8 * u, vec4(0.1f, 0.03f, 0.15f, 0.7f));
        polyline(gps.path, 1, 5 * u, GPS);
    }
    hud.noClip();
    if (raceEvent < 0) {
        // event icons, pinned to the rim when off-map
        auto pin = [&](vec3 w, vec4 col, float r, bool sel) {
            vec2 p = toScreen(w), d = p - c;
            float l = length(d);
            if (l > Rr - 9 * u) p = c + d * ((Rr - 9 * u) / l);
            if (sel) hud.circle(p, r + 4 * u, vec4(GPS.x, GPS.y, GPS.z, 0.9f), 16);
            hud.circle(p, r + 1.6f * u, wa::A(wa::PAPER, 1), 16);
            hud.circle(p, r, col, 16);
        };
        drawStuntsOnMinimap(c, Rr, toScreen);
        drawCatsOnMinimap(c, Rr, toScreen);
        for (int i = 0; i < int(events.size()); i++) pin(events[i].markerPos, washiEventColor(events[i].type), 5 * u, gpsShown == i);
        pin(world.hubPos, wa::A(wa::VERM, 1), 6 * u, gpsShown == -2);
    }
    hud.arc(c, Rr + 1 * u, Rr + 5 * u, 0, 2 * PI, wa::A(wa::INK, 1), 72);
    // player arrow
    vec3 cfw = car.forward();
    vec2 f(dot(cfw, rgt), -dot(cfw, fwd));
    f = f * (1.f / std::max(length(f), 1e-4f));
    vec2 r(-f.y, f.x);
    hud.triangle(c + f * (12 * u), c - f * (8 * u) + r * (8.5f * u), c - f * (8 * u) - r * (8.5f * u), vec4(0, 0, 0, 0.85f));
    hud.triangle(c + f * (9.5f * u), c - f * (6 * u) + r * (6.5f * u), c - f * (3.5f * u), wa::A(wa::VERM, 1));
    hud.triangle(c + f * (9.5f * u), c - f * (3.5f * u), c - f * (6 * u) - r * (6.5f * u), wa::A(wa::VERM, 0.75f * 1));
    // north marker
    vec2 nd(dot(vec3(0, 0, 1), rgt), -dot(vec3(0, 0, 1), fwd));
    vec2 np = c + nd * (Rr + 1 * u);
    hud.circle(np, 12 * u, wa::A(wa::INK, 1), 16);
    hud.text(np.x, np.y - 9 * u, 15 * u, "北", wa::A(wa::PAPER, 1), 1, false);
    // destination readout
    std::string label;
    float dist = 0;
    if (raceEvent >= 0) {
        const EventDef& E = events[raceEvent];
        label = E.name;
        dist = std::max(0.f, E.totalLength() - player.progress);
    } else if (gpsShown != -1 && !gps.path.empty()) {
        if (gpsShown == -2) label = "Festival Hub";
        else if (gpsShown == -3) label = gpsEstate >= 0 && gpsEstate < int(world.estates.size()) ? world.estates[gpsEstate].name : "Home";
        else if (gpsShown >= 0 && gpsShown < int(events.size())) label = events[gpsShown].name;
        dist = gps.pathLen;
    }
    if (!label.empty()) {
        char buf[32];
        if (settings.mph) std::snprintf(buf, sizeof(buf), "%.1f mi", dist / 1609.34f);
        else std::snprintf(buf, sizeof(buf), "%.1f km", dist / 1000.f);
        float tw = std::max(hud.textWidth(13 * u, label), hud.textWidth(20 * u, buf)) + 30 * u;
        float px = c.x + Rr + 16 * u, py = c.y + Rr - 50 * u;
        card(px, py, tw + 20 * u, 52 * u, 0.96f, false, 89);
        hud.rect(px + 2 * u, py + 2 * u, 6 * u, 48 * u, wa::A(wa::VERM, 1));
        hud.text(px + 16 * u, py + 7 * u, 13 * u, label, wa::A(wa::INK2, 1), 0, false);
        hud.text(px + 16 * u, py + 23 * u, 20 * u, buf, wa::A(wa::INK, 1), 0, false);
    }
}

void Game::drawMenuPanel(const std::string& title, const std::vector<std::string>& items, int sel, float x, float y, float w) {
    // a numbered list on a card: 01, 02... in vermilion, the chosen line swept with ink
    float u = hud.ui;
    float rowH = 42 * u;
    int n = int(items.size());
    int vis = std::max(4, std::min(n, int((hud.h - y - 110 * u - 94 * u) / rowH)));
    int first = std::clamp(sel - vis / 2, 0, std::max(0, n - vis));
    float hgt = 82 * u + rowH * vis + 16 * u;
    card(x, y, w, hgt, 1.f, false, 31);
    std::string k = kanjiFor(title);
    hud.text(x + 26 * u, y + 22 * u, 22 * u, k, wa::VERM, 0, false);
    hud.textFit(x + 34 * u + hud.textWidth(22 * u, k), y + 18 * u, 28 * u, title, wa::INK, 0, w - 120 * u, false);
    brush(vec2(x + 24 * u, y + 62 * u), vec2(x + w * 0.45f, y + 63 * u), 4 * u, wa::INK, 12, 1.f);
    if (first > 0) hud.text(x + w - 30 * u, y + 26 * u, 14 * u, "^", wa::MUTED, 2, false);
    if (first + vis < n) hud.text(x + w * 0.5f, y + hgt - 18 * u, 14 * u, "v", wa::MUTED, 1, false);
    for (size_t i = size_t(first); i < size_t(std::min(n, first + vis)); i++) {
        float ry = y + 76 * u + (i - first) * rowH;
        bool s = int(i) == sel;
        themeRow(x + 22 * u, ry, w - 44 * u, rowH - 4 * u, s);
        char num[16];
        std::snprintf(num, sizeof(num), "%02d", int(i) + 1);
        hud.text(x + 30 * u, ry + 11 * u, 15 * u, num, s ? wa::GOLD : wa::VERM, 0, false);
        size_t c = items[i].find(": ");
        vec4 tc = s ? wa::PAPER : wa::INK;
        if (c == std::string::npos) hud.text(x + 66 * u, ry + 9 * u, 19 * u, items[i], tc, 0, false);
        else {
            hud.text(x + 66 * u, ry + 10 * u, 17 * u, items[i].substr(0, c), tc, 0, false);
            std::string v = items[i].substr(c + 2);
            float vx = x + w - 34 * u;
            if (s) { hud.text(vx, ry + 10 * u, 17 * u, ">", wa::GOLD, 2, false); vx -= 22 * u; }
            float vw = hud.text(vx, ry + 10 * u, 17 * u, v, s ? tc : wa::INK2, 2, false);
            if (s) hud.text(vx - vw - 10 * u, ry + 10 * u, 17 * u, "<", wa::GOLD, 2, false);
        }
    }
}

void Game::drawHud() {
    float u = hud.ui;
    switch (state) {
        case ST_TITLE: drawTitle(); return;
        case ST_PAUSE: drawPause(); return;
        case ST_MAP: drawMap(); return;
        case ST_DEALER: drawDealer(false); return;
        case ST_GARAGE: drawDealer(true); return;
        case ST_CUSTOM: drawCustomize(); return;
        case ST_EVENTLAB: drawEventLab(); return;
        case ST_MYEVENTS: drawMyEvents(); return;
        case ST_SETTINGS: drawSettings(); return;
        case ST_RESULTS: drawResults(); return;
        case ST_PHOTO: drawPhoto(); return;
        case ST_WHEELSPIN: drawWheelspin(); return;
        case ST_UPGRADE: drawUpgrades(); return;
        case ST_SWITCH: drawSwitcher(); return;
        default: break;
    }
    hudGlass = true;
    drawTimeCircuits();
    if (state == ST_RACE || state == ST_RACE_INTRO) { drawRaceHud(); drawGateHud(); }
    else {
        // the driver's tag: a slip of paper with credits, the car and level, a seal for spins
        using namespace wa;
        std::string carLine = carName(*player.car.spec) + "  ·  " + std::string(1, player.car.spec->cls) + " " + std::to_string(rateCar(*player.car.spec).pi);
        std::string crs = fmtThousands((long long)(creditsShown + 0.5));
        float pw = std::max({260 * u, hud.textWidth(14 * u, carLine) + 40 * u, hud.textWidth(28 * u, crs) + 130 * u});
        hudPanelW = pw;
        card(18 * u, 18 * u, pw, 92 * u, 0.96f, false, 71);
        hud.circle(vec2(42 * u, 44 * u), 12 * u, GOLD, 20);
        hud.text(42 * u, 37 * u, 10 * u, "CR", CARD, 1, false);
        hud.text(62 * u, 26 * u, 28 * u, crs, INK, 0, false);
        hud.text(30 * u, 64 * u, 14 * u, carLine, INK2, 0, false);
        std::string lv = "段 " + std::to_string(level);
        hud.text(30 * u, 84 * u, 14 * u, lv, VERM, 0, false);
        float lvw = hud.textWidth(14 * u, lv) + 12 * u, bw = pw - lvw - 40 * u;
        hud.rect(30 * u + lvw, 92 * u, bw, 2 * u, A(INK, 0.18f));
        brush(vec2(30 * u + lvw, 93 * u), vec2(30 * u + lvw + bw * clampf(xp / float(xpToNext()), 0.f, 1.f), 93 * u), 6 * u, VERM, 19, 1.f);
        if (wheelspins > 0) {
            hanko(vec2(18 * u + pw - 30 * u, 44 * u), 34 * u, "福", VERM, 0.12f, 1.f);
            if (wheelspins > 1) hud.text(18 * u + pw - 30 * u, 62 * u, 12 * u, "x" + std::to_string(wheelspins), VERM, 1, false);
        }
        {
            std::string ck = clockText();
            float cw2 = hud.textWidth(12 * u, ck) + 18 * u;
            hud.rect(18 * u, 118 * u, cw2, 20 * u, A(INK, 0.82f));
            hud.text(27 * u, 121 * u, 12 * u, ck, PAPER, 0, false);
        }
        drawFestivalHud();
        // prompts
        std::string prompt;
        if (nearEvent >= 0) {
            const EventDef& e = events[nearEvent];
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s  -  %s  -  %.1f km", e.name.c_str(), eventTypeName(e.type), e.totalLength() / 1000.f);
            float pw = std::max(hud.textWidth(22 * u, buf), hud.textWidth(16 * u, e.blurb)) + 60 * u;
            float px = hud.w * 0.5f - pw * 0.5f, py = hud.h - 200 * u;
            card(px, py, pw, 108 * u, 1.f, false, 83);
            vec4 ec = washiEventColor(e.type);
            hud.rect(px + 2 * u, py + 2 * u, 10 * u, 104 * u, ec);
            hud.text(hud.w * 0.5f, py + 12 * u, 22 * u, buf, INK, 1, false);
            hud.text(hud.w * 0.5f, py + 42 * u, 16 * u, e.blurb, INK2, 1, false);
            std::string go = pad ? "(Y)  START EVENT" : "ENTER  START EVENT";
            float gw = hud.textWidth(16 * u, go) + 40 * u, gx = hud.w * 0.5f - gw * 0.5f, gy = py + 68 * u;
            brushSwipe(gx, gy, gw, 30 * u, INK, 29, ease(std::fmod(R.time, 2.f) / 0.4f));
            hud.text(gx + gw * 0.5f, gy + 6 * u, 16 * u, go, PAPER, 1, false);
        } else if (nearHome >= 0) {
            drawHomePrompt();
        } else if (nearHub) {
            float pw = 380 * u, px = hud.w * 0.5f - pw * 0.5f, py = hud.h - 160 * u;
            card(px, py, pw, 66 * u, 1.f, false, 85);
            hanko(vec2(px + 34 * u, py + 33 * u), 40 * u, "祭", wa::VERM, -0.1f, 1.f);
            hud.text(hud.w * 0.5f + 20 * u, py + 10 * u, 22 * u, "Festival Hub", wa::INK, 1, false);
            hud.text(hud.w * 0.5f + 20 * u, py + 38 * u, 15 * u, pad ? "(Y) Dealership & Garage" : "ENTER  Dealership & Garage", wa::VERM, 1, false);
        }
        if (!opt.noHints && hintT < 14.f)  // controls fade out once you're driving
        {
            const char* hints = pad ? "START menu   BACK map   UP switch car   RB camera   (Y) rewind   R3 photo" : "ESC menu   M map   G switch car   C camera   R reset   BKSP rewind   P photo   H horn";
            float ha = clampf((14.f - hintT) / 2.f, 0.f, 1.f);
            float hwid = hud.prompts(hud.w - 24 * u, 22 * u, 12 * u, hints, 2, ha, true);
            if (ha > 0.01f) card(hud.w - 24 * u - hwid - 14 * u, 13 * u, hwid + 28 * u, 32 * u, ha, false, 87);
            hud.capFill = wa::INK; hud.capText = wa::PAPER; hud.labelCol = wa::INK; hud.labelShadow = false; hud.capRound = 0.08f;
            hud.prompts(hud.w - 24 * u, 22 * u, 12 * u, hints, 2, ha);
            hud.capFill = vec4(1, 1, 1, 0.92f); hud.capText = vec4(0.06f, 0.06f, 0.08f, 1); hud.labelCol = vec4(1, 1, 1, 0.88f);
            hud.labelShadow = true; hud.capRound = 0.22f;
        }
    }
    drawChain();
    drawPopup();
    if (rewinding) {
        float pulse = 0.6f + 0.4f * std::sin(simTime * 10.f);
        hud.rect(0, 0, hud.w, hud.h, vec4(0.94f, 0.9f, 0.82f, 0.18f));
        hud.triangle(vec2(hud.w * 0.5f - 6 * u, hud.h * 0.4f - 18 * u), vec2(hud.w * 0.5f - 6 * u, hud.h * 0.4f + 18 * u), vec2(hud.w * 0.5f - 34 * u, hud.h * 0.4f), vec4(1, 1, 1, pulse));
        hud.triangle(vec2(hud.w * 0.5f + 22 * u, hud.h * 0.4f - 18 * u), vec2(hud.w * 0.5f + 22 * u, hud.h * 0.4f + 18 * u), vec2(hud.w * 0.5f - 6 * u, hud.h * 0.4f), vec4(1, 1, 1, pulse));
        hud.text(hud.w * 0.5f, hud.h * 0.4f + 28 * u, 18 * u, "REWIND", WHITE, 1);
    }
    drawMenuToasts(hud.h * 0.13f);
    drawSpeedo();
    drawMinimap();
    hudGlass = false;
}

void Game::drawRaceHud() {
    float u = hud.ui;
    const EventDef& E = events[raceEvent];
    // position block (drift zones: score and the next target instead)
    themePanel(18 * u, 18 * u, 210 * u, 96 * u);
    hud.rect(20 * u, 20 * u, 8 * u, 92 * u, washiEventColor(E.type));
    if (E.type == EV_DRIFT) {
        int s = int(driftTotal);
        hud.text(34 * u, 22 * u, 34 * u, fmtThousands(s), wa::INK);
        int tier = s >= E.driftTarget[0] ? 0 : s >= E.driftTarget[1] ? 1 : s >= E.driftTarget[2] ? 2 : 3;
        const char* names[4] = {"1ST", "2ND", "3RD", "-"};
        std::string nxt = tier == 0 ? "1ST PLACE SCORE!" : "NEXT  " + std::string(names[tier - 1]) + "  " + fmtThousands(E.driftTarget[tier - 1]);
        hud.text(34 * u, 62 * u, 13 * u, nxt, tier == 0 ? wa::VERM : wa::MUTED, 0, false);
        hud.text(34 * u, 82 * u, 13 * u, "DRIFT SCORE", wa::MUTED, 0, false);
    } else {
        hud.text(34 * u, 20 * u, 46 * u, std::to_string(playerPlace), playerPlace == 1 ? wa::VERM : wa::INK);
        float pw = hud.textWidth(46 * u, std::to_string(playerPlace));
        hud.text(36 * u + pw, 28 * u, 20 * u, "/" + std::to_string(racers.size()), wa::MUTED, 0, false);
        hud.text(34 * u, 76 * u, 14 * u, "POSITION", wa::MUTED, 0, false);
    }
    if (E.route.closed) {
        int lap = std::clamp(player.lap + 1, 1, E.laps);
        hud.text(214 * u, 26 * u, 22 * u, std::to_string(lap) + "/" + std::to_string(E.laps), wa::INK, 2);
        hud.text(214 * u, 52 * u, 13 * u, "LAP", wa::MUTED, 2, false);
    } else {
        float rem = std::max(0.f, E.totalLength() - player.progress) / 1000.f;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f km", rem);
        hud.text(214 * u, 26 * u, 20 * u, buf, wa::INK, 2);
        hud.text(214 * u, 52 * u, 13 * u, "TO GO", wa::MUTED, 2, false);
    }
    hud.text(214 * u, 76 * u, 16 * u, fmtTime(raceTime), wa::INK, 2, false);
    // standings
    std::vector<Racer*> order = racers;
    std::sort(order.begin(), order.end(), [](Racer* a, Racer* b) {
        if (a->finished != b->finished) return a->finished;
        if (a->finished) return a->finishTime < b->finishTime;
        return a->progress > b->progress;
    });
    float sx = hud.w - 248 * u, sy = 18 * u;
    themePanel(sx, sy, 230 * u, 14 * u + 26 * u * order.size());
    for (size_t i = 0; i < order.size(); i++) {
        float ry = sy + 8 * u + i * 26 * u;
        bool me = order[i]->isPlayer;
        if (me) { hud.rect(sx, ry - 2 * u, 230 * u, 25 * u, wa::INK); hud.rect(sx, ry - 2 * u, 5 * u, 25 * u, wa::VERM); }
        vec4 tc = me ? wa::PAPER : wa::INK;
        hud.text(sx + 12 * u, ry, 16 * u, std::to_string(i + 1), tc, 0, false);
        hud.text(sx + 38 * u, ry, 16 * u, order[i]->name, tc, 0, false);
        std::string gap;
        if (order[i]->finished) gap = fmtTime(order[i]->finishTime);
        else if (!me) {
            char b[24];
            std::snprintf(b, sizeof(b), "%+.0fm", order[i]->progress - player.progress);
            gap = b;
        }
        hud.text(sx + 218 * u, ry, 14 * u, gap, me ? wa::A(wa::PAPER2, 1) : wa::MUTED, 2, false);
    }
    // place tags floating over the rivals (Forza style): colour-coded, fading with distance
    if (E.type != EV_DRIFT) {
        for (size_t i = 0; i < order.size(); i++) {
            Racer* r = order[i];
            if (r->isPlayer) continue;
            vec3 head = r->car.pos + vec3(0, r->car.spec->height + 1.0f, 0);
            float dist = length(head - cam.pos);
            if (dist > 260.f || dist < 2.f) continue;
            vec4 cp = cam.viewProj * vec4(head, 1.f);
            if (cp.w <= 0.1f) continue;
            vec2 ndc(cp.x / cp.w, cp.y / cp.w);
            if (std::fabs(ndc.x) > 1.1f || std::fabs(ndc.y) > 1.1f) continue;
            float x = (ndc.x * 0.5f + 0.5f) * hud.w, y = (0.5f - ndc.y * 0.5f) * hud.h;
            float sc = clampf(30.f / dist, 0.55f, 1.25f);
            float a = 1.f - smoothstepf(180.f, 260.f, dist);
            int place = int(i) + 1;
            std::string t = ordinal(place);
            vec4 bg = place == 1 ? vec4(1.f, 0.78f, 0.15f, 0.92f * a) : place == 2 ? vec4(0.82f, 0.85f, 0.9f, 0.92f * a)
                    : place == 3 ? vec4(0.85f, 0.52f, 0.28f, 0.92f * a) : vec4(0.06f, 0.06f, 0.08f, 0.85f * a);
            vec4 fg = place <= 3 ? vec4(0.05f, 0.05f, 0.06f, a) : vec4(1, 1, 1, a);
            float fs = 20.f * u * sc, tw = hud.textWidth(fs, t) + 16.f * u * sc, th = fs * 1.35f;
            hud.rect(x - tw * 0.5f + 3 * u, y - th + 3 * u, tw, th, vec4(0, 0, 0, 0.35f * a));
            hud.rect(x - tw * 0.5f, y - th, tw, th, bg);
            hud.triangle(vec2(x - 6 * u * sc, y), vec2(x + 6 * u * sc, y), vec2(x, y + 7 * u * sc), bg);
            hud.text(x, y - th + fs * 0.12f, fs, t, fg, 1, false);
            if (dist < 90.f) hud.text(x, y - th - 15 * u * sc, 12 * u * sc, r->name, vec4(1, 1, 1, 0.85f * a * (1.f - smoothstepf(60.f, 90.f, dist))), 1, true);
        }
    }
    // countdown
    if (state == ST_RACE_INTRO) {
        if (stateTime < 2.5f) {
            float ie = clampf(stateTime / 0.35f, 0.f, 1.f);
            ie = 1.f - (1.f - ie) * (1.f - ie);
            paper(0, hud.h * 0.36f, hud.w * ie, 110 * u, 0.96f, 93);
            brush(vec2(0, hud.h * 0.36f), vec2(hud.w * ie, hud.h * 0.36f + 2 * u), 6 * u, wa::INK, 4, 1.f);
            brush(vec2(0, hud.h * 0.36f + 110 * u), vec2(hud.w * ie, hud.h * 0.36f + 108 * u), 6 * u, wa::INK, 5, 1.f);
            hud.rect(0, hud.h * 0.36f + 4 * u, 14 * u, 102 * u, washiEventColor(E.type));
            hud.text(hud.w * 0.5f, hud.h * 0.36f + 14 * u, 40 * u, E.name, wa::INK, 1, false);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  -  %.1f km  -  %s", eventTypeName(E.type), E.totalLength() / 1000.f,
                          E.route.closed ? (std::to_string(E.laps) + (E.laps > 1 ? " laps" : " lap")).c_str() : "point to point");
            hud.text(hud.w * 0.5f, hud.h * 0.36f + 66 * u, 18 * u, buf, wa::INK2, 1, false);
        } else {
            int n = int(std::ceil(countdown));
            float f = countdown - std::floor(countdown);
            float cs = (90 + f * 40) * u;
            hud.circle(vec2(hud.w * 0.5f, hud.h * 0.32f + cs * 0.55f), cs * 0.85f, wa::A(wa::INK, 0.85f), 48);
            enso(vec2(hud.w * 0.5f, hud.h * 0.32f + cs * 0.55f), cs * 0.92f, cs * 0.14f, wa::VERM, 1.f - f, n);
            hud.text(hud.w * 0.5f, hud.h * 0.32f, cs, std::to_string(std::max(n, 1)), wa::PAPER, 1, false);
        }
    } else if (stateTime < 1.2f) {
        hud.text(hud.w * 0.5f, hud.h * 0.32f, 96 * u * (1.f + 0.2f * std::max(0.f, 0.3f - stateTime)), "GO!", wa::VERM, 1, false);
        hanko(vec2(hud.w * 0.5f + 150 * u, hud.h * 0.32f + 40 * u), 60 * u, "走", wa::VERM, 0.12f, clampf(1.2f - stateTime, 0.f, 1.f));
    }
    if (player.finished) {
        std::string ft = "FINISHED " + ordinal(playerPlace);
        float fw = hud.textWidth(60 * u, ft) + 80 * u;
        card(hud.w * 0.5f - fw * 0.5f, hud.h * 0.3f - 10 * u, fw, 86 * u, 1.f, false, 91);
        hud.text(hud.w * 0.5f, hud.h * 0.3f, 60 * u, ft, wa::INK, 1, false);
    }
    if (offRouteTime > 1.5f) hud.text(hud.w * 0.5f, hud.h * 0.42f, 22 * u, pad ? "Off route! Press (X) to reset" : "Off route! Press R to reset", vec4(1, 0.5f, 0.3f, 1), 1);
}

int Game::nextRaceEvent() const {
    for (int i = 0; i < int(events.size()); i++)
        if (i >= int(bestPlace.size()) || bestPlace[i] != 1) return i;
    return 0;
}

void Game::drawPause() {
    using namespace wa;
    float u = hud.ui;
    bool racingMenu = returnState == ST_RACE || returnState == ST_RACE_INTRO;
    if (!racingMenu) {
        float open = ease(menuOpenT / 0.35f);
        static const char* kTabK[] = {"祭", "車", "家", "創作", "設定"};
        washiSide = kTabK[std::clamp(menuTab, 0, 4)];
        drawThemeBackdrop(1.f);
        float x0 = 100 * u, W = hud.w - x0 - 50 * u;
        float hy = drawThemeHeader(x0, 26 * u - (1.f - open) * 40 * u, W, open);
        buildTabTiles(menuTab, world, ownedEstates);
        float ty = drawThemeTabs(x0, hy + 8 * u, W, kTabNames, kTabCount, menuTab, open);
        float gy = ty + 18 * u, gap = 16 * u;
        float cw = (W - gap * 3) / 4.f, ch = std::min((hud.h - gy - 96 * u - gap) / 2.f, cw * 0.95f);
        menuSel = std::clamp(menuSel, 0, std::max(0, int(gTiles.size()) - 1));
        const int kTileCount = int(gTiles.size());
        static float selAnim[8] = {}, selT = 0;
        static int lastSel = -1;
        if (menuSel != lastSel) { lastSel = menuSel; selT = R.time; }
        // the kanji each tile is known by
        auto tileKanji = [&](const MenuTile& t) -> std::string {
            switch (t.act) {
                case MA_RESUME: return "再開"; case MA_MAP: return "地図"; case MA_SPIN: return "福引"; case MA_AUTOSHOW: return "販売";
                case MA_GARAGE: return "車庫"; case MA_NEXTRACE: return "次戦"; case MA_SETTINGS: return "設定"; case MA_EXIT: return "保存";
                case MA_QUIT: return "終了"; case MA_CUSTOMIZE: return "塗装"; case MA_UPGRADE: return "改造"; case MA_PHOTO: return "写真";
                case MA_EVENTLAB: return "創作"; case MA_MYEVENTS: return "記録"; case MA_IMPORT: return "取込"; default: return "家";
            }
        };
        std::vector<int> order;
        for (int i = 0; i < kTileCount; i++) if (i != menuSel) order.push_back(i);
        if (menuSel < kTileCount) order.push_back(menuSel);
        for (int i : order) {
            const MenuTile& t = gTiles[i];
            bool sel = i == menuSel;
            float tx = x0 + t.x * (cw + gap), tyy = gy + t.y * (ch + gap);
            float tw = t.w * cw + (t.w - 1) * gap, th = t.h * ch + (t.h - 1) * gap;
            selAnim[i] += ((sel ? 1.f : 0.f) - selAnim[i]) * 0.25f;
            float sa = selAnim[i];
            float e = ease((menuOpenT - 0.1f - i * 0.05f) / 0.3f);
            float alpha = e;
            tx += (1.f - e) * 60 * u * tabDir;
            tyy += (1.f - e) * 20 * u - sa * 5 * u;
            tx -= sa * 5 * u;
            if (alpha <= 0.01f) continue;
            bool hero = t.w == 2 && t.h == 2;
            card(tx, tyy, tw, th, alpha, sa > 0.5f, 100 + i);
            std::string kj = tileKanji(t);
            GLuint img = t.thumb >= 100 ? (t.thumb - 100 < int(houseThumb.size()) ? houseThumb[t.thumb - 100] : 0) : t.thumb >= 0 && t.thumb < 8 ? menuThumb[t.thumb] : 0;
            bool carImg = false;
            if (t.act == MA_GARAGE && current < int(garage.size()) && garage[current].spec < int(carThumb.size()) && carThumb[garage[current].spec]) {
                img = carThumb[garage[current].spec];
                carImg = true;
            }
            float m = 12 * u;
            auto print = [&](float ix, float iy, float iw, float ih) {  // a photo printed into the card, with an ink rule round it
                float ia = (t.thumb == 0) ? R.width / float(R.height) : 4.f / 3.f, ta = iw / ih;
                float pan = std::sin(R.time * 0.25f + i) * 0.5f;
                vec2 uv0(0, 1), uv1(1, 0);
                if (ta > ia) { float v = ia / ta; uv0.y = 0.5f + v * 0.5f; uv1.y = 0.5f - v * 0.5f; }
                else { float hh = ta / ia; float c = 0.5f + pan * (1.f - hh) * 0.5f; uv0.x = c - hh * 0.5f; uv1.x = c + hh * 0.5f; }
                hud.image(ix, iy, iw, ih, img, vec4(1, 0.97f, 0.92f, alpha), uv0, uv1);
                inkBox(ix, iy, iw, ih, 2 * u, A(INK, alpha));
            };
            auto liveMap = [&](float ix, float iy, float iw, float ih) {  // an old paper map, centred on you
                vec2 pc = vec2((player.car.pos.x + World::HALF) / (2 * World::HALF), (World::HALF - player.car.pos.z) / (2 * World::HALF));
                float span = 0.22f * iw / ih * 0.5f, spv = 0.22f * 0.5f;
                vec2 p[4] = {{ix, iy}, {ix + iw, iy}, {ix + iw, iy + ih}, {ix, iy + ih}};
                vec2 uv[4] = {{pc.x - span, pc.y - spv}, {pc.x + span, pc.y - spv}, {pc.x + span, pc.y + spv}, {pc.x - span, pc.y + spv}};
                hud.mapQuad(p, uv, vec4(1.0f, 0.93f, 0.80f, alpha));
                hud.rect(ix, iy, iw, ih, A(PAPER, 0.35f * alpha));
                auto toT = [&](vec3 w) {
                    vec2 mm((w.x + World::HALF) / (2 * World::HALF), (World::HALF - w.z) / (2 * World::HALF));
                    return vec2(ix + (mm.x - (pc.x - span)) / (2 * span) * iw, iy + (mm.y - (pc.y - spv)) / (2 * spv) * ih);
                };
                for (const Road& rd : world.roads)
                    for (size_t k = 4; k < rd.pts.size(); k += 4) {
                        vec2 a2 = toT(rd.pts[k - 4]), b2 = toT(rd.pts[k]);
                        if (a2.x < ix || a2.x > ix + iw || a2.y < iy || a2.y > iy + ih || b2.x < ix || b2.x > ix + iw || b2.y < iy || b2.y > iy + ih) continue;
                        hud.line(a2, b2, 2.f * u, A(INK, 0.75f * alpha));
                    }
                for (auto& ev : events) {
                    vec2 q = toT(ev.markerPos);
                    if (q.x < ix + 8 * u || q.x > ix + iw - 8 * u || q.y < iy + 8 * u || q.y > iy + ih - 8 * u) continue;
                    hud.rect(q.x - 5 * u, q.y - 5 * u, 10 * u, 10 * u, A(washiEventColor(ev.type), alpha));
                }
                vec2 me(ix + iw * 0.5f, iy + ih * 0.5f);
                float ping = std::fmod(R.time, 1.6f) / 1.6f;
                hud.arc(me, 7 * u + ping * 20 * u, 9 * u + ping * 20 * u, 0, 2 * PI, A(VERM, (1.f - ping) * alpha), 32);
                hud.circle(me, 6 * u, A(VERM, alpha), 14);
                inkBox(ix, iy, iw, ih, 2 * u, A(INK, alpha));
            };
            // the number, the title in brush serif, the subtitle
            char num[16];
            std::snprintf(num, sizeof(num), "%02d", i + 1);
            std::string sub = t.sub;
            if (t.act == MA_NEXTRACE) sub = events[nextRaceEvent()].name;
            if (t.act == MA_GARAGE) sub = std::to_string(garage.size()) + (garage.size() == 1 ? " car owned" : " cars owned");
            if (t.act == MA_MYEVENTS) sub = customEvents.empty() ? "Nothing saved yet" : std::to_string(customEvents.size()) + " saved event" + (customEvents.size() > 1 ? "s" : "");
            if (t.act >= MA_HOUSE) { int h = t.act - MA_HOUSE; sub = ownsEstate(h) ? "Yours - fast travel here" : "For sale  CR " + fmtThousands(world.estates[h].price); }
            if (t.act == MA_SPIN) sub = wheelspins > 0 ? std::to_string(wheelspins) + (wheelspins == 1 ? " draw waiting" : " draws waiting") : "Level up to earn a draw";
            if (hero) {
                // left: the big title with its kanji set vertically; right: the picture inside an enso
                float half = tw * 0.52f;
                vtext(tx + 40 * u, tyy + 26 * u, 34 * u, kj, A(VERM, alpha));
                hud.text(tx + 72 * u, tyy + 22 * u, 15 * u, num, A(VERM, alpha), 0, false);
                std::string lab = t.label;
                float px = std::min(58 * u, th * 0.15f);
                size_t sp = lab.find(' ');
                if (sp != std::string::npos && hud.textWidth(px, lab) > half - 60 * u) {
                    hud.text(tx + 72 * u, tyy + 44 * u, px, lab.substr(0, sp), A(INK, alpha), 0, false);
                    hud.text(tx + 72 * u, tyy + 44 * u + px * 1.02f, px, lab.substr(sp + 1), A(INK, alpha), 0, false);
                } else hud.textFit(tx + 72 * u, tyy + 44 * u, px, lab, A(INK, alpha), 0, half - 50 * u, false);
                hud.textFit(tx + 72 * u, tyy + th - 46 * u, 17 * u, sub, A(INK2, alpha), 0, half - 80 * u, false);
                float r = std::min(tw - half, th) * 0.40f;
                vec2 ec(tx + half + (tw - half) * 0.5f, tyy + th * 0.5f);
                enso(ec, r, r * 0.16f, A(INK, 0.9f * alpha), ease((menuOpenT - 0.2f) / 0.6f), 5 + menuTab);
                if (t.act == MA_MAP || t.act == MA_EVENTLAB) liveMap(ec.x - r * 0.95f, ec.y - r * 0.62f, r * 1.9f, r * 1.24f);
                else if (img && carImg) hud.image(ec.x - r * 1.25f, ec.y - r * 0.75f, r * 2.5f, r * 1.25f, img, vec4(1, 1, 1, alpha), vec2(0.08f, 0.84f), vec2(0.92f, 0.2f));
                else if (img) print(ec.x - r * 0.95f, ec.y - r * 0.62f, r * 1.9f, r * 1.24f);
                else hud.text(ec.x, ec.y - r * 0.5f, r, kj.substr(0, 3), A(INK, alpha), 1, false);
            } else {
                // a watermark of the kanji, then the picture (if any) printed in, then the words
                hud.text(tx + tw - 10 * u, tyy + th * 0.18f, th * 0.55f, kj.substr(0, 3), A(INK, 0.07f * alpha), 2, false);
                float textTop = tyy + 16 * u;
                if (t.act == MA_MAP || t.act == MA_EVENTLAB) { liveMap(tx + m, tyy + m, tw - 2 * m, th * 0.5f); textTop = tyy + m + th * 0.5f + 8 * u; }
                else if (img && carImg) { hud.image(tx + m, tyy + m, tw - 2 * m, th * 0.5f, img, vec4(1, 1, 1, alpha), vec2(0.1f, 0.84f), vec2(0.9f, 0.2f)); textTop = tyy + m + th * 0.5f + 8 * u; }
                else if (img) { print(tx + m, tyy + m, tw - 2 * m, th * 0.5f); textTop = tyy + m + th * 0.5f + 8 * u; }
                else if (t.act == MA_SPIN) {   // an omikuji draw: an enso with the lucky character inside
                    float r = std::min(tw, th) * 0.26f;
                    vec2 ec(tx + tw - r - 24 * u, tyy + th * 0.58f);
                    enso(ec, r, r * 0.18f, A(INK, alpha), 1.f, 9);
                    float pulse = wheelspins > 0 ? 1.f + 0.06f * std::sin(R.time * 4.f) : 1.f;
                    hanko(ec, r * 1.05f * pulse, "福", VERM, -0.1f + 0.05f * std::sin(R.time), alpha);
                }
                bool pic = textTop > tyy + 20 * u;
                float px = std::min(pic ? 28 * u : 34 * u, th * 0.16f);
                hud.text(tx + 18 * u, textTop, 13 * u, num, A(VERM, alpha), 0, false);
                hud.textFit(tx + 18 * u, textTop + 14 * u, px, t.label, A(INK, alpha), 0, tw - 36 * u, false);
                float subY = std::max(tyy + th - 30 * u, textTop + 14 * u + px * 1.2f);
                if (subY + 16 * u < tyy + th - 4 * u) hud.textFit(tx + 18 * u, subY, 14 * u, sub, A(INK2, alpha), 0, tw - 36 * u, false);
            }
            if (sa > 0.02f) {
                // the chosen tile gets a seal pressed into its corner
                float st = ease((R.time - selT) / 0.22f);
                float sz = (hero ? 70.f : 52.f) * u * (1.6f - 0.6f * st);
                if (sel) hanko(vec2(tx + tw - sz * 0.7f, tyy + th - sz * 0.7f), sz, kj.substr(0, 3), VERM, -0.14f, st * alpha);
            }
        }
        drawThemePrompts(pad ? "LB/RB tabs   D-pad move   (A) select   (B) back" : "Q/E tabs   Arrows move   ENTER select   ESC back");
        if (!gTiles.empty()) {
            static const char* kWhat[MA_HOUSE + 1] = {
                "Close the menu and carry on driving", "Every race, house and landmark - fast travel or set a GPS route",
                "Draw for credits and cars", "Browse and buy new cars, delivered to your garage",
                "Swap cars, sell, customise and upgrade the ones you own", "Fast travel straight to the next race you haven't won",
                "Display, audio, driving assists and controls", "Save your progress and go back to the title screen", "Save and quit to the desktop",
                "Paint, liveries, aero, ride height and more for this car", "Engine, weight and grip upgrades - watch the PI climb",
                "Freeze time and frame the perfect shot", "Build your own race on any road and share it with a code",
                "Race the events you've saved", "Paste a friend's share code to add their event", "Houses: fast travel points with a garage"};
            int act = std::clamp(gTiles[std::clamp(menuSel, 0, int(gTiles.size()) - 1)].act, 0, int(MA_HOUSE));
            float e = ease((R.time - selT) / 0.25f);
            float dw = hud.textWidth(15 * u, kWhat[act]) + 40 * u;
            float dx = hud.w - 50 * u - dw + (1.f - e) * 30 * u, dy = hud.h - 60 * u;
            card(dx, dy, dw, 40 * u, e, false, 71);
            hud.rect(dx + 2 * u, dy + 2 * u, 6 * u, 36 * u, A(VERM, e));
            hud.text(dx + 22 * u, dy + 11 * u, 15 * u, kWhat[act], A(INK, e), 0, false);
        }
        drawMenuToasts(hud.h * 0.72f);
        return;
    }
    // ---- paused mid-race: a sheet of paper laid over the left of the frozen race
    float open = ease(menuOpenT / 0.3f);
    const EventDef& E = events[raceEvent];
    vec4 ec = washiEventColor(E.type);
    float sheetW = hud.w * 0.52f * open;
    paper(0, 0, sheetW, hud.h, 0.96f, 19, PAPER);
    for (float y = 0; y < hud.h; y += 14 * u) {  // deckled right edge
        float d = (4.f + 8.f * hash(23, int(y))) * u;
        hud.triangle(vec2(sheetW, y), vec2(sheetW + d, y + 7 * u), vec2(sheetW, y + 14 * u), A(PAPER, 0.96f));
    }
    hud.rect(0, 0, 58 * u * open, hud.h, A(COLUMN, open));
    vtext(29 * u, 40 * u, 30 * u, "休憩", A(COLTEXT, open));
    float x0 = 100 * u;
    hud.text(x0, 46 * u, 15 * u, "PAUSED", A(VERM, open), 0, false);
    hud.textFit(x0, 66 * u, 44 * u, E.name, A(INK, open), 0, sheetW - x0 - 40 * u, false);
    brush(vec2(x0 - 6 * u, 124 * u), vec2(x0 + std::min(sheetW - x0 - 60 * u, 420 * u), 126 * u), 7 * u, A(INK, open), 15, open);
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s   |   %.1f km   |   %s", eventTypeName(E.type), E.totalLength() / 1000.f,
                      E.route.closed ? (std::to_string(E.laps) + (E.laps > 1 ? " LAPS" : " LAP")).c_str() : "POINT TO POINT");
        hud.rect(x0, 140 * u, 12 * u, 12 * u, A(ec, open));
        hud.text(x0 + 20 * u, 137 * u, 15 * u, buf, A(INK2, open), 0, false);
    }
    auto items = pauseItems(true);
    float ly = 196 * u, lh = 62 * u, lw = std::min(460 * u, sheetW - x0 - 60 * u);
    for (int i = 0; i < int(items.size()); i++) {
        float ie = ease((menuOpenT - 0.05f - i * 0.05f) / 0.25f);
        bool on = i == menuSel;
        float yy = ly + i * lh, xx = x0 - (1.f - ie) * 60 * u;
        themeRow(xx, yy, lw, lh - 8 * u, on, ie);
        char num[16];
        std::snprintf(num, sizeof(num), "%02d", i + 1);
        hud.text(xx + 10 * u, yy + 17 * u, 18 * u, num, A(on ? wa::GOLD : VERM, ie), 0, false);
        hud.text(xx + 56 * u, yy + 10 * u, 30 * u, items[i], A(on ? PAPER : INK, ie), 0, false);
    }
    // the race so far, on a card over the right
    {
        float pw = 340 * u, px = hud.w - 50 * u - pw + (1.f - open) * 80 * u, py = 100 * u;
        std::vector<Racer*> ord = racers;
        std::sort(ord.begin(), ord.end(), [](Racer* a, Racer* b) {
            if (a->finished != b->finished) return a->finished;
            if (a->finished) return a->finishTime < b->finishTime;
            return a->progress > b->progress;
        });
        float ph = 150 * u + ord.size() * 32 * u;
        card(px, py, pw, ph, open, false, 83);
        hud.text(px + 22 * u, py + 18 * u, 13 * u, E.type == EV_DRIFT ? "DRIFT SCORE" : "POSITION", A(VERM, open), 0, false);
        if (E.type == EV_DRIFT) hud.text(px + 22 * u, py + 36 * u, 44 * u, fmtThousands(int(driftTotal)), A(INK, open), 0, false);
        else {
            hud.text(px + 22 * u, py + 32 * u, 60 * u, std::to_string(playerPlace), A(INK, open), 0, false);
            hud.text(px + 26 * u + hud.textWidth(60 * u, std::to_string(playerPlace)), py + 64 * u, 18 * u, "/ " + std::to_string(racers.size()), A(MUTED, open), 0, false);
            if (playerPlace == 1) hanko(vec2(px + pw - 54 * u, py + 62 * u), 56 * u, "勝", VERM, 0.12f, open);
        }
        hud.text(px + 22 * u, py + 108 * u, 13 * u, "TIME", A(MUTED, open), 0, false);
        hud.text(px + 70 * u, py + 104 * u, 20 * u, fmtTime(raceTime), A(INK, open), 0, false);
        if (E.route.closed) hud.text(px + pw - 22 * u, py + 104 * u, 18 * u, "周 " + std::to_string(std::clamp(player.lap + 1, 1, E.laps)) + " / " + std::to_string(E.laps), A(INK, open), 2, false);
        for (size_t i = 0; i < ord.size(); i++) {
            float ry = py + 140 * u + i * 32 * u;
            bool me = ord[i]->isPlayer;
            themeRow(px + 16 * u, ry - 4 * u, pw - 32 * u, 30 * u, me, open);
            vec4 tc = me ? PAPER : INK;
            hud.text(px + 26 * u, ry, 17 * u, std::to_string(i + 1), A(me ? wa::GOLD : VERM, open), 0, false);
            hud.textFit(px + 56 * u, ry, 17 * u, ord[i]->name, A(tc, open), 0, pw - 180 * u);
            hud.textFit(px + pw - 26 * u, ry + 2 * u, 13 * u, carName(*ord[i]->car.spec), A(me ? PAPER : MUTED, open), 2, 110 * u);
        }
    }
    drawThemePrompts(pad ? "D-pad choose   (A) select   (B) resume" : "UP/DOWN choose   ENTER select   ESC resume");
}

void Game::drawMap() {
    float u = hud.ui;
    washiSide = "地図";
    drawThemeBackdrop(1.f);
    float S = std::min(hud.h - 120 * u, hud.w * 0.55f);
    float mx = 92 * u, my = (hud.h - S) * 0.5f;
    // ---- the view glides and zooms to whatever is picked in the list
    static vec2 vf(0.5f, 0.5f);
    static float vz = 1.f;
    {
        vec3 fw = player.car.pos;
        int ne = int(events.size()), nh = int(world.estates.size());
        if (subSel < ne) fw = events[subSel].markerPos;
        else if (subSel == ne) fw = world.hubPos;
        else if (subSel <= ne + nh) fw = world.estates[subSel - ne - 1].pos;
        else if (subSel - ne - 1 - nh < int(world.landmarks.size())) fw = world.landmarks[subSel - ne - 1 - nh].pos;
        vec2 tf((fw.x + World::HALF) / (2 * World::HALF), (World::HALF - fw.z) / (2 * World::HALF));
        if (stateTime < 0.05f) { vf = vec2(0.5f, 0.5f); vz = 1.f; }
        float k = stateTime < 0.25f ? 0.f : 0.12f;  // a beat of the whole island first, then zoom in
        vz += (mapZoomTarget - vz) * k;
        vf = vf + (tf - vf) * k;
        float hlf = 0.5f / vz;
        vf.x = clampf(vf.x, hlf, 1.f - hlf);
        vf.y = clampf(vf.y, hlf, 1.f - hlf);
    }
    vec2 p[4] = {{mx, my}, {mx + S, my}, {mx + S, my + S}, {mx, my + S}};
    float hv = 0.5f / vz;
    vec2 uv[4] = {{vf.x - hv, vf.y - hv}, {vf.x + hv, vf.y - hv}, {vf.x + hv, vf.y + hv}, {vf.x - hv, vf.y + hv}};
    auto inside = [&](vec2 q, float m) { return q.x > mx + m && q.x < mx + S - m && q.y > my + m && q.y < my + S - m; };
    auto cline = [&](vec2 a2, vec2 b2, float th, vec4 col) {  // a line clipped to the map square
        float t0 = 0, t1 = 1;
        vec2 dd = b2 - a2;
        float pp[4] = {-dd.x, dd.x, -dd.y, dd.y}, qq[4] = {a2.x - mx, mx + S - a2.x, a2.y - my, my + S - a2.y};
        for (int k = 0; k < 4; k++) {
            if (std::fabs(pp[k]) < 1e-6f) { if (qq[k] < 0) return; continue; }
            float r2 = qq[k] / pp[k];
            if (pp[k] < 0) t0 = std::max(t0, r2); else t1 = std::min(t1, r2);
            if (t0 > t1) return;
        }
        hud.line(a2 + dd * t0, a2 + dd * t1, th, col);
    };
    hud.rect(mx + 8 * u, my + 8 * u, S, S, wa::A(wa::SHADOW, 0.9f));
    hud.rect(mx, my, S, S, wa::PAPER);
    hud.mapQuad(p, uv, vec4(0.93f, 0.84f, 0.68f, 0.85f));
    paper(mx, my, S, S, 0.25f, 99, wa::PAPER);
    {
        auto toM = [&](vec3 w) { return vec2(mx + S * (0.5f + ((w.x + World::HALF) / (2 * World::HALF) - vf.x) * vz), my + S * (0.5f + ((World::HALF - w.z) / (2 * World::HALF) - vf.y) * vz)); };
        for (const Road& rd : world.roads) {
            bool dirt = rd.surface != SURF_ASPHALT;
            for (size_t k = 3; k < rd.pts.size() + (rd.closed ? 3 : 0); k += 3) {
                vec2 a2 = toM(rd.pts[k - 3]), b2 = toM(rd.pts[std::min(k, rd.pts.size() - 1) % rd.pts.size()]);
                if (rd.closed && k >= rd.pts.size()) b2 = toM(rd.pts[0]);
                float zw = std::sqrt(vz);
                cline(a2, b2, (dirt ? 4.f : 6.f) * u * zw, wa::A(wa::PAPER, 0.6f));
                cline(a2, b2, (dirt ? 2.f : 3.2f) * u * zw, dirt ? wa::A(wa::INK2, 0.75f) : wa::INK);
            }
        }
    }
    auto toMap = [&](vec3 w) { return vec2(mx + S * (0.5f + ((w.x + World::HALF) / (2 * World::HALF) - vf.x) * vz), my + S * (0.5f + ((World::HALF - w.z) / (2 * World::HALF) - vf.y) * vz)); };
    // the sakura canal: a long ribbon of water with its stone banks, and its name
    if (world.canalPath.size() > 1) {
        const auto& cp = world.canalPath;
        float zw = std::sqrt(vz);
        for (size_t k = 1; k < cp.size(); k++) cline(toMap(cp[k - 1]), toMap(cp[k]), 9.f * u * zw, wa::PAPER);
        for (size_t k = 1; k < cp.size(); k++) cline(toMap(cp[k - 1]), toMap(cp[k]), 5.f * u * zw, wa::INDIGO);
        vec2 lab = toMap(cp[cp.size() / 2]);
        if (inside(lab, 50 * u)) hud.text(lab.x, lab.y + 8 * u, 13 * u, "Sakura Canal", wa::INDIGO, 1, false);
    }
    // landmarks: a small gold diamond and the name (bigger when picked in the list)
    std::vector<vec4> labelRects;
    {   // the canal's label is already down
        vec2 lab = toMap(world.canalPath.empty() ? vec3(0, 0, 0) : world.canalPath[world.canalPath.size() / 2]);
        float cw0 = hud.textWidth(13 * u, "Sakura Canal");
        if (!world.canalPath.empty()) labelRects.push_back(vec4(lab.x - cw0 * 0.5f, lab.y + 8 * u, cw0, 15 * u));
    }
    for (int li = 0; li < int(world.landmarks.size()); li++) {
        const auto& lm = world.landmarks[li];
        vec2 q = toMap(lm.pos);
        if (!inside(q, 4 * u)) continue;
        bool lsel = subSel == int(events.size()) + 1 + int(world.estates.size()) + li;
        float d = (lsel ? 9.f : 5.f) * u;
        hud.triangle(q + vec2(0, -d - u), q + vec2(d + u, 0), q + vec2(-d - u, 0), wa::INK);
        hud.triangle(q + vec2(0, d + u), q + vec2(-d - u, 0), q + vec2(d + u, 0), wa::INK);
        hud.triangle(q + vec2(0, -d), q + vec2(d, 0), q + vec2(-d, 0), wa::GOLD);
        hud.triangle(q + vec2(0, d), q + vec2(-d, 0), q + vec2(d, 0), wa::GOLD);
        // the name goes wherever it does not cover another name or a race marker; if nowhere fits,
        // the diamond stands alone (picking it in the list always shows the name)
        float lw = hud.textWidth(12 * u, lm.name), lh = 14 * u;
        const vec2 offs[4] = {{8 * u, -7 * u}, {-8 * u - lw, -7 * u}, {-lw * 0.5f, -22 * u}, {-lw * 0.5f, 8 * u}};
        bool placed = false;
        for (int k = 0; k < 4 && !placed; k++) {
            vec4 r(q.x + offs[k].x, q.y + offs[k].y, lw, lh);
            bool hit = r.x < mx || r.x + r.z > mx + S || r.y < my || r.y + r.w > my + S;
            for (auto& o : labelRects) hit |= r.x < o.x + o.z && r.x + r.z > o.x && r.y < o.y + o.w && r.y + r.w > o.y;
            for (auto& e : events) {
                vec2 eq = toMap(e.markerPos);
                hit |= eq.x + 7 * u > r.x && eq.x - 7 * u < r.x + r.z && eq.y + 7 * u > r.y && eq.y - 7 * u < r.y + r.w;
            }
            if (hit && !lsel) continue;
            labelRects.push_back(r);
            if (lsel) hud.rect(r.x - 4 * u, r.y - 2 * u, r.z + 8 * u, r.w + 4 * u, wa::INK);
            hud.text(r.x, r.y, 12 * u, lm.name, lsel ? wa::PAPER : wa::INK, 0, false);
            placed = true;
        }
    }
    for (size_t i = 1; i < gps.path.size(); i++) cline(toMap(gps.path[i - 1]), toMap(gps.path[i]), 4 * u, wa::VERM);
    for (int i = 0; i < int(events.size()); i++) {
        vec2 q = toMap(events[i].markerPos);
        if (!inside(q, 6 * u)) continue;
        bool sel = i == subSel;
        hud.circle(q, (sel ? 11 : 8) * u, wa::INK, 14);
        hud.circle(q, (sel ? 9 : 6) * u, washiEventColor(events[i].type), 14);

    }
    vec2 hq = toMap(world.hubPos);
    if (inside(hq, 8 * u)) {
        hud.circle(hq, 10 * u, wa::INK, 14);
        hud.circle(hq, 8 * u, wa::VERM, 14);
        if (subSel == int(events.size())) hud.text(hq.x + 14 * u, hq.y - 10 * u, 16 * u, "Festival Hub", wa::INK, 0, false);
    }
    const vec4 homeCol = wa::INDIGO, saleCol = wa::GOLD;
    for (int i = 0; i < int(world.estates.size()); i++) {
        vec2 q = toMap(world.estates[i].pos);
        if (!inside(q, 10 * u)) continue;
        bool sel = subSel == int(events.size()) + 1 + i;
        float s = (sel ? 10.f : 8.f) * u;
        vec4 c = ownsEstate(i) ? homeCol : saleCol;
        hud.rect(q.x - s, q.y - s * 0.4f, 2 * s, s * 1.3f, wa::INK);
        hud.triangle(vec2(q.x - s * 1.25f, q.y - s * 0.3f), vec2(q.x + s * 1.25f, q.y - s * 0.3f), vec2(q.x, q.y - s * 1.45f), wa::INK);
        hud.rect(q.x - s * 0.72f, q.y - s * 0.3f, s * 1.44f, s * 1.0f, c);
        hud.triangle(vec2(q.x - s * 0.98f, q.y - s * 0.3f), vec2(q.x + s * 0.98f, q.y - s * 0.3f), vec2(q.x, q.y - s * 1.2f), c);
        if (sel) hud.text(q.x + 16 * u, q.y - 12 * u, 16 * u, world.estates[i].name, wa::INK, 0, false);
    }
    // legend: what each colour on the map means
    {
        float gx = mx + 14 * u, gy = my + S - 14 * u - 9 * 22 * u, rowH = 22 * u;
        card(gx - 8 * u, gy - 10 * u, 200 * u, 9 * rowH + 12 * u, 0.96f, false, 101);
        auto dotRow = [&](int k, vec4 col, const char* label) {
            hud.circle(vec2(gx + 6 * u, gy + k * rowH + 8 * u), 6 * u, wa::INK, 12);
            hud.circle(vec2(gx + 6 * u, gy + k * rowH + 8 * u), 4.5f * u, col, 12);
            hud.text(gx + 20 * u, gy + k * rowH, 14 * u, label, wa::INK, 0, false);
        };
        dotRow(0, washiEventColor(EV_CIRCUIT), "Circuit race");
        dotRow(1, washiEventColor(EV_SPRINT), "Sprint race");
        dotRow(2, washiEventColor(EV_CROSS), "Cross country");
        dotRow(3, washiEventColor(EV_DRIFT), "Drift zone");
        dotRow(4, wa::VERM, "Festival hub");
        dotRow(5, saleCol, "House for sale");
        dotRow(6, homeCol, "Your house");
        {
            vec2 q(gx + 6 * u, gy + 7 * rowH + 8 * u);
            float dd = 6 * u;
            hud.triangle(q + vec2(0, -dd), q + vec2(dd, 0), q + vec2(-dd, 0), wa::GOLD);
            hud.triangle(q + vec2(0, dd), q + vec2(-dd, 0), q + vec2(dd, 0), wa::GOLD);
            hud.text(gx + 20 * u, gy + 7 * rowH, 14 * u, "Landmark", wa::INK, 0, false);
        }
        hud.line(vec2(gx, gy + 8 * rowH + 8 * u), vec2(gx + 13 * u, gy + 8 * rowH + 8 * u), 5 * u, wa::INDIGO);
        hud.text(gx + 20 * u, gy + 8 * rowH, 14 * u, "Water / canal", wa::INK, 0, false);
    }
    // player
    vec2 pp = toMap(player.car.pos);
    vec2 f(std::sin(player.car.yaw), -std::cos(player.car.yaw));
    vec2 r(-f.y, f.x);
    if (inside(pp, 10 * u)) {
        float ping = std::fmod(R.time, 1.6f) / 1.6f;
        hud.arc(pp, 8 * u + ping * 20 * u, 10 * u + ping * 20 * u, 0, 2 * PI, wa::A(wa::INK, (1.f - ping) * 0.7f), 32);
        hud.triangle(pp + f * (11 * u), pp - f * (7 * u) + r * (7 * u), pp - f * (7 * u) - r * (7 * u), wa::INK);
        hud.triangle(pp + f * (9 * u), pp - f * (5 * u) + r * (5 * u), pp - f * (5 * u) - r * (5 * u), wa::VERM);
    }
    if (subSel < int(events.size())) {   // the picked race's name on a black tag, over everything else
        vec2 q = toMap(events[subSel].markerPos);
        if (inside(q, 6 * u)) {
            float nw = hud.textWidth(16 * u, events[subSel].name) + 20 * u;
            float nx = std::min(q.x + 14 * u, mx + S - nw - 4 * u);
            hud.rect(nx, q.y - 38 * u, nw, 26 * u, wa::INK);
            hud.rect(nx, q.y - 14 * u, nw, 3 * u, washiEventColor(events[subSel].type));
            hud.text(nx + 10 * u, q.y - 34 * u, 16 * u, events[subSel].name, wa::PAPER, 0, false);
        }
    }
    {   // zoom chip
        char zb[24];
        std::snprintf(zb, sizeof(zb), "ZOOM %.1fx", vz);
        float zw2 = hud.textWidth(12 * u, zb) + 20 * u;
        hud.rect(mx + S - zw2 - 10 * u, my + 10 * u, zw2, 22 * u, wa::INK);
        hud.text(mx + S - zw2 * 0.5f - 10 * u, my + 14 * u, 12 * u, zb, wa::PAPER, 1, false);
    }
    // list
    float lx = mx + S + 40 * u, lw = hud.w - lx - 40 * u;
    float lt = clampf(stateTime / 0.35f, 0.f, 1.f), lin = 1.f - (1.f - lt) * (1.f - lt) * (1.f - lt);
    lx += (1.f - lin) * 80 * u;
    themePanel(lx - 18 * u, my - 8 * u, lw + 26 * u, S - 20 * u, lin, true);
    {
        float tw = hud.textWidth(24 * u, "WORLD MAP") + 44 * u;
        (void)tw;
        hud.text(lx, my + 10 * u, 24 * u, "地図", wa::VERM, 0, false);
        hud.text(lx + hud.textWidth(24 * u, "地図") + 10 * u, my + 6 * u, 28 * u, "World map", wa::INK, 0, false);
        brush(vec2(lx, my + 44 * u), vec2(lx + lw * 0.45f, my + 45 * u), 4 * u, wa::INK, 12, lin);
    }
    my += 12 * u;
    int total = int(events.size()) + 1 + int(world.estates.size()) + int(world.landmarks.size());
    int rows = std::max(4, int((S - 190 * u) / (34 * u)));
    int first = std::clamp(subSel - rows / 2, 0, std::max(0, total - rows));
    if (first > 0) hud.text(lx + lw * 0.5f, my + 46 * u - 16 * u, 12 * u, "^", wa::MUTED, 1, false);
    if (first + rows < total) hud.text(lx + lw * 0.5f, my + 50 * u + rows * 34 * u, 12 * u, "v  more", wa::MUTED, 1, false);
    for (int i = first; i < std::min(total, first + rows); i++) {
        float ry = my + 50 * u + (i - first) * 34 * u;
        bool sel = i == subSel;
        themeRow(lx - 10 * u, ry - 4 * u, lw, 31 * u, sel);
        if (i > int(events.size()) + int(world.estates.size())) {
            const World::Landmark& lm = world.landmarks[i - int(events.size()) - 1 - int(world.estates.size())];
            vec2 q(lx + 4 * u, ry + 11 * u);
            float dd = 6 * u;
            hud.triangle(q + vec2(0, -dd), q + vec2(dd, 0), q + vec2(-dd, 0), wa::GOLD);
            hud.triangle(q + vec2(0, dd), q + vec2(-dd, 0), q + vec2(dd, 0), wa::GOLD);
            hud.text(lx + 18 * u, ry, 17 * u, lm.name, sel ? wa::PAPER : wa::INK, 0, false);
            hud.text(lx + lw - 20 * u, ry, 15 * u, "LANDMARK", wa::GOLD, 2, false);
        } else if (i > int(events.size())) {
            int h = i - int(events.size()) - 1;
            const Estate& e = world.estates[h];
            bool own = ownsEstate(h);
            hud.circle(vec2(lx + 4 * u, ry + 11 * u), 5 * u, own ? homeCol : saleCol, 10);
            hud.text(lx + 18 * u, ry, 17 * u, e.name, sel ? wa::PAPER : wa::INK, 0, false);
            hud.text(lx + lw - 20 * u, ry, 15 * u, own ? "OWNED" : "CR " + fmtThousands(e.price), own ? homeCol : (sel ? wa::PAPER2 : wa::MUTED), 2, false);
        } else if (i < int(events.size())) {
            hud.circle(vec2(lx + 4 * u, ry + 11 * u), 5 * u, washiEventColor(events[i].type), 10);
            hud.text(lx + 18 * u, ry, 17 * u, events[i].name, sel ? wa::PAPER : wa::INK, 0, false);
            int bp = i < int(bestPlace.size()) ? bestPlace[i] : 0;
            if (bp) hud.text(lx + lw - 20 * u, ry, 15 * u, "Best: " + ordinal(bp), sel ? wa::PAPER2 : wa::MUTED, 2, false);
        } else {
            hud.circle(vec2(lx + 4 * u, ry + 11 * u), 5 * u, wa::VERM, 10);
            hud.text(lx + 18 * u, ry, 17 * u, "Festival Hub", sel ? wa::PAPER : wa::INK, 0, false);
        }
    }
    if (subSel < int(events.size())) {
        const EventDef& e = events[subSel];
        float iy = my + 60 * u + (std::min(total, rows) + 1) * 34 * u;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s  |  %.1f km  |  1st prize CR %s", eventTypeName(e.type), e.totalLength() / 1000.f, fmtThousands(e.prize[0]).c_str());
        hud.textFit(lx, iy, 15 * u, buf, wa::VERM, 0, lw - 10 * u);
        hud.textFit(lx, iy + 24 * u, 15 * u, e.blurb, wa::INK2, 0, lw - 10 * u);
    } else if (subSel > int(events.size()) + int(world.estates.size())) {
        float iy = my + 60 * u + (std::min(total, rows) + 1) * 34 * u;
        hud.text(lx, iy, 15 * u, "LANDMARK", wa::GOLD, 0, false);
        hud.textFit(lx, iy + 24 * u, 15 * u, "ENTER to fast travel there, X for a GPS route.", wa::INK2, 0, lw - 10 * u);
    } else if (subSel > int(events.size())) {
        int h = subSel - int(events.size()) - 1;
        float iy = my + 60 * u + (std::min(total, rows) + 1) * 34 * u;
        hud.text(lx, iy, 15 * u, ownsEstate(h) ? "YOUR HOUSE  |  fast travel & garage" : "FOR SALE  |  CR " + fmtThousands(world.estates[h].price), ownsEstate(h) ? homeCol : saleCol, 0, false);
        hud.textFit(lx, iy + 24 * u, 15 * u, world.estateBlurb(h), wa::INK2, 0, lw - 10 * u);
    }
    drawThemePrompts(pad ? "(A) travel   (X) GPS   LB/RB zoom   (B) close" : "ENTER travel   X GPS   Q/E zoom   ESC close", lx);
}

void Game::drawCarCards(bool garageMode, float y, float h) {
    float u = hud.ui;
    int n = garageMode ? int(garage.size()) : int(dealerOrder.size());
    float cw = h * 1.18f, gap = 12 * u;
    float centerX = hud.w * 0.5f;
    // smooth scroll towards the selection
    static float scroll = 0;
    scroll += (float(subSel) - scroll) * 0.2f;
    if (std::fabs(scroll - subSel) > n * 0.5f) scroll = float(subSel);
    for (int i = 0; i < n; i++) {
        float x = centerX + (i - scroll) * (cw + gap) - cw * 0.5f;
        if (x + cw < -20 || x > hud.w + 20) continue;
        bool sel = i == subSel;
        int spec = garageMode ? garage[i].spec : dealerOrder[i];
        const CarSpec& s = kCars[spec];
        float lift = sel ? 10 * u : 0;
        float cy = y - lift, chh = h + (sel ? lift : 0);
        float ent = clampf((stateTime - 0.05f - std::fabs(float(i) - scroll) * 0.05f) / 0.3f, 0.f, 1.f);
        ent = 1.f - (1.f - ent) * (1.f - ent);
        cy += (1.f - ent) * 60 * u;
        hud.rect(x + 6 * u, cy + 6 * u, cw, chh, wa::A(sel ? wa::VERM : wa::SHADOW, 0.9f * ent));
        float ih = chh - 52 * u;
        // the picture sits on a teal studio wall with a yellow-green diagonal behind the selected car
        paper(x, cy, cw, chh, ent, 200 + i, wa::CARD);
        if (sel) hud.circle(vec2(x + cw * 0.62f, cy + ih * 0.5f), ih * 0.42f, wa::A(wa::VERM, 0.85f), 40);  // a red sun behind the chosen car
        if (carThumb[spec]) hud.image(x, cy, cw, ih, carThumb[spec], vec4(1, 1, 1, 1), vec2(0.13f, 0.86f), vec2(0.87f, 0.18f));
        if (sel) brushSwipe(x + 4 * u, cy + ih + 2 * u, cw - 8 * u, chh - ih - 6 * u, wa::INK, 300 + i, 1.f);
        else hud.rect(x + 8 * u, cy + ih, cw - 16 * u, 1.5f * u, wa::A(wa::INK, 0.3f));
        // class badge
        vec4 cc = s.cls == 'S' ? wa::PLUM : s.cls == 'A' ? wa::VERM : s.cls == 'B' ? wa::GOLD
                : s.cls == 'C' ? wa::MOSS : wa::INDIGO;
        hud.rect(x + 8 * u, cy + 8 * u, 24 * u, 22 * u, cc);
        hud.text(x + 20 * u, cy + 9 * u, 16 * u, std::string(1, s.cls), wa::CARD, 1, false);
        hud.text(x + 12 * u, cy + ih + 6 * u, 11 * u, s.make, sel ? wa::GOLD : wa::VERM, 0, false);
        std::string nm = s.model;
        while (nm.size() > 4 && hud.textWidth(15 * u, nm) > cw - 20 * u) nm = nm.substr(0, nm.size() - 4) + "...";
        hud.text(x + 12 * u, cy + ih + 21 * u, 15 * u, nm, sel ? wa::PAPER : wa::INK, 0, false);
        std::string tag;
        vec4 tc = wa::MUTED;
        if (garageMode) { if (i == current) { tag = "DRIVING"; tc = wa::VERM; } }
        else {
            bool owned = false;
            for (auto& g : garage) owned |= g.spec == spec;
            tag = owned ? "OWNED" : (s.price == 0 ? "FREE" : "CR " + fmtThousands(s.price));
            tc = owned ? wa::MOSS : (credits >= s.price ? wa::INK : wa::VERM);
        }
        if (!tag.empty()) {
            float tw = hud.textWidth(12 * u, tag) + 14 * u;
            hud.rect(x + cw - tw - 8 * u, cy + 8 * u, tw, 20 * u, wa::CARD);
            inkBox(x + cw - tw - 8 * u, cy + 8 * u, tw, 20 * u, 1.5f * u, wa::A(tc, 1.f));
            hud.text(x + cw - tw * 0.5f - 8 * u, cy + 11 * u, 12 * u, tag, tc, 1, false);
        }
        inkBox(x, cy, cw, chh, (sel ? 3.f : 2.f) * u, wa::INK);

    }
    // edge fades so the strip reads as a carousel
    hud.rectH(0, y - 20 * u, 120 * u, h + 40 * u, wa::A(wa::PAPER, 0.9f), wa::A(wa::PAPER, 0));
    hud.rectH(hud.w - 120 * u, y - 20 * u, 120 * u, h + 40 * u, wa::A(wa::PAPER, 0), wa::A(wa::PAPER, 0.9f));
}

void Game::drawDealer(bool garageMode) {
    float u = hud.ui;
    int n = garageMode ? int(garage.size()) : int(dealerOrder.size());
    subSel = std::clamp(subSel, 0, std::max(0, n - 1));
    int spec = garageMode ? garage[subSel].spec : dealerSpec();
    const CarSpec& s = kCars[spec];
    // cinematic framing: top and bottom gradients over the live showroom
    hud.rectV(0, hud.h - 290 * u, hud.w, 290 * u, wa::A(wa::PAPER, 0), wa::A(wa::PAPER, 0.85f));
    // header
    float x0 = 48 * u;
    drawMenuHeader(x0, 30 * u, hud.w - 2 * x0, garageMode ? "MY GARAGE" : "AUTOSHOW");
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%d OF %d   |   %d %s OWNED", subSel + 1, n, int(garage.size()), garage.size() == 1 ? "CAR" : "CARS");
    {   // counter chip under the header
        float cw2 = hud.textWidth(12 * u, buf) + 24 * u;
        hud.rect(x0, 118 * u, cw2, 24 * u, wa::INK);
        hud.text(x0 + 12 * u, 123 * u, 12 * u, buf, wa::PAPER, 0, false);
    }
    float slide = 1.f - clampf(stateTime / 0.4f, 0.f, 1.f);
    slide = slide * slide * slide;

    // info panel (right)
    CarRatings rt = rateCar(s);
    CarRatings cur = rateCar(*player.car.spec);
    float pw = 380 * u, px = hud.w - pw - x0 + slide * 120 * u, py = 118 * u;
    float ph = 292 * u;
    themePanel(px, py, pw, ph, 1.f, true);
    hud.text(px + 22 * u, py + 16 * u, 15 * u, s.make, wa::VERM, 0, false);
    std::string model = s.model;
    float msz = 30 * u;
    while (msz > 18 * u && hud.textWidth(msz, model) > pw - 40 * u) msz -= 1 * u;
    hud.text(px + 22 * u, py + 34 * u, msz, model, wa::INK, 0, false);
    vec4 cc = s.cls == 'S' ? wa::PLUM : s.cls == 'A' ? wa::VERM : s.cls == 'B' ? wa::GOLD
            : s.cls == 'C' ? wa::MOSS : wa::INDIGO;
    hud.rect(px + 22 * u, py + 78 * u, 30 * u, 26 * u, cc);
    hud.text(px + 37 * u, py + 80 * u, 19 * u, std::string(1, s.cls), wa::CARD, 1, false);
    inkBox(px + 52 * u, py + 78 * u, 54 * u, 26 * u, 2 * u, wa::INK);
    hud.text(px + 79 * u, py + 81 * u, 17 * u, std::to_string(rt.pi), wa::INK, 1, false);
    const char* drv[] = {"FWD", "RWD", "AWD"};
    std::snprintf(buf, sizeof(buf), "%d HP   %d KG   %s", int(s.powerKW * 1.341f), int(s.massKg), drv[s.drive]);
    hud.text(px + 120 * u, py + 83 * u, 14 * u, buf, wa::INK, 0, false);
    const char* labels[] = {"SPEED", "ACCELERATION", "HANDLING", "OFF-ROAD"};
    float vals[] = {rt.speed, rt.accel, rt.handling, rt.offroad};
    float cvals[] = {cur.speed, cur.accel, cur.handling, cur.offroad};
    bool compare = player.car.spec != &s;
    for (int i = 0; i < 4; i++) {
        float ry = py + 120 * u + i * 34 * u;
        hud.text(px + 22 * u, ry, 12 * u, labels[i], wa::MUTED, 0, false);
        char vb[16];
        std::snprintf(vb, sizeof(vb), "%.1f", vals[i]);
        hud.text(px + pw - 22 * u, ry - 2 * u, 15 * u, vb, wa::INK, 2, false);
        float bx = px + 22 * u, by = ry + 18 * u, bw = pw - 44 * u;
        hud.rect(bx, by + 3 * u, bw, 1.5f * u, wa::A(wa::INK, 0.25f));
        float v = bw * vals[i] / 10.f, cv = bw * cvals[i] / 10.f;
        if (compare && cv > v) {
            hud.rect(bx, by, v, 7 * u, wa::INK);
            hud.rect(bx + v, by, cv - v, 7 * u, wa::VERM);
        } else if (compare) {
            hud.rect(bx, by, cv, 7 * u, wa::INK);
            hud.rect(bx + cv, by, v - cv, 7 * u, wa::MOSS);
        } else hud.rect(bx, by, v, 7 * u, wa::INK);
    }
    std::snprintf(buf, sizeof(buf), "Top speed %s %s   |   %d cyl", speedStr(s.topKmh / 3.6f).c_str(), settings.mph ? "mph" : "km/h", cylindersFor(s));
    hud.text(px + 22 * u, py + 256 * u, 13 * u, buf, wa::MUTED, 0, false);
    if (!s.credit.empty()) {
        std::string cr = s.credit;  // shown without the long URL in brackets
        size_t a = cr.find(" ("), b = cr.find(')', a == std::string::npos ? 0 : a);
        if (a != std::string::npos && b != std::string::npos) cr.erase(a, b - a + 1);
        hud.textFit(px + 22 * u, py + 274 * u, 11 * u, cr, wa::MUTED, 0, pw - 44 * u);
    }

    // paint swatches
    int paint = garageMode ? garage[subSel].paint : dealerPaint;
    float sy = py + ph + 10 * u;
    themePanel(px, sy, pw, 64 * u);
    hud.text(px + 16 * u, sy + 8 * u, 12 * u, std::string("PAINT  -  ") + kPaintNames[paint], wa::INK, 0, false);
    float sw = std::min(26 * u, (pw - 32 * u) / kPaintCount - 4 * u);
    for (int i = 0; i < kPaintCount; i++) {
        float qx = px + 16 * u + i * (sw + 4 * u), qy = sy + 30 * u;
        if (i == paint) hud.rect(qx - 3 * u, qy - 3 * u, sw + 6 * u, sw + 6 * u, wa::INK);
        hud.rect(qx, qy, sw, sw, vec4(kPaints[i].x, kPaints[i].y, kPaints[i].z, 1));
    }

    // price / value block (left)
    float lx = x0 + 20 * u - slide * 120 * u, ly = 168 * u;
    themePanel(lx - 20 * u, ly - 14 * u, 340 * u, garageMode && garage[subSel].stripes ? 88 * u : 76 * u, 1.f, true);
    if (garageMode) {
        const OwnedCar& oc = garage[subSel];
        hud.text(lx, ly, 12 * u, "RESALE VALUE", wa::MUTED, 0, false);
        hud.text(lx, ly + 16 * u, 28 * u, "CR " + fmtThousands(sellValue(oc)), wa::INK);
        if (oc.stripes) hud.text(lx, ly + 54 * u, 13 * u, "Racing stripes fitted", wa::MUTED, 0, false);
        if (sellConfirm == subSel) {
            hud.rect(lx - 20 * u, ly + 84 * u, 340 * u, 56 * u, wa::VERM);
            hud.text(lx + 14 * u, ly + 88 * u, 16 * u, "Sell this car?", wa::PAPER, 0, false);
            hud.text(lx + 14 * u, ly + 110 * u, 13 * u, pad ? "Press (Y) again to confirm" : "Press V again to confirm", wa::PAPER, 0, false);
        }
    } else {
        hud.text(lx, ly, 12 * u, "PRICE", wa::MUTED, 0, false);
        hud.text(lx, ly + 16 * u, 30 * u, s.price == 0 ? "FREE" : "CR " + fmtThousands(s.price), credits >= s.price ? wa::INK : wa::VERM);
        if (credits < s.price) hud.text(lx, ly + 56 * u, 13 * u, "Need CR " + fmtThousands(s.price - credits) + " more", wa::VERM, 0, false);
    }

    // car cards
    drawCarCards(garageMode, hud.h - 236 * u, 160 * u);

    // hint bar
    std::string hint;
    if (garageMode) hint = pad ? "D-pad: car / paint   (A) drive   LB customize   RB upgrade   (Y) sell   (B) back"
                               : "LEFT/RIGHT car   UP/DOWN paint   ENTER drive   C customize   U upgrade   V sell   ESC back";
    else hint = pad ? "LEFT/RIGHT car   UP/DOWN paint   (A) buy   (B) back" : "LEFT/RIGHT car   UP/DOWN paint   ENTER buy   ESC back";
    drawThemePrompts(hint);
    drawMenuToasts(hud.h * 0.42f);
    if (!garageMode) drawCheatBox();
    drawPopup();
}

void Game::drawSettings() {
    float u = hud.ui;
    float open = wa::ease(menuOpenT / 0.4f);
    washiSide = "設定";
    drawThemeBackdrop(1.f);
    float x0 = 100 * u, W = hud.w - x0 - 50 * u;
    float hy = drawThemeHeader(x0, 26 * u - (1.f - open) * 30 * u, W, open);
    static const char* const kSet[] = {"SETTINGS"};
    float ty = drawThemeTabs(x0, hy + 10 * u, W, kSet, 1, 0, open);
    float pw = std::min(560 * u, W * 0.48f);
    float slideL = (1.f - open) * -80 * u, slideR = (1.f - open) * 80 * u;
    drawMenuPanel("SETTINGS", settingsItems(), subSel, x0 + slideL, ty + 16 * u, pw);
    std::vector<std::string> items = settingsItems();
    int si = std::clamp(subSel, 0, int(items.size()) - 1);
    std::string it = items[si];
    size_t c = it.find(": ");
    std::string label = c == std::string::npos ? it : it.substr(0, c), value = c == std::string::npos ? "" : it.substr(c + 2);
    static const char* kCat[] = {"DISPLAY", "GRAPHICS", "GRAPHICS", "GRAPHICS", "GRAPHICS", "DISPLAY", "DISPLAY", "AUDIO", "DRIVING", "GRAPHICS",
                                 "WORLD", "CONTROLS", "WORLD", "COLOUR", "COLOUR", "COLOUR", "COLOUR", "ACCESSIBILITY", "DRIVING", "GRAPHICS", "DISPLAY"};
    static const char* kCatK[] = {"画面", "画面", "画面", "画面", "画面", "画面", "画面", "音", "操作", "画面",
                                  "天気", "操作", "時間", "画面", "画面", "画面", "画面", "補助", "補助", "画面", "夜"};
    static const char* kInfo[] = {
        "Show speeds in kilometres or miles per hour.",
        "Streaks the world at speed, like a camera shutter.",
        "Sun shadows from cars, trees and buildings.",
        "Smooths jagged edges. Higher costs more GPU time.",
        "How far the detailed world reaches before the horizon takes over.",
        "Fill the whole screen.",
        "Lock the frame rate to the display to stop tearing.",
        "Overall sound level.",
        "Traction and stability help, and gentler braking.",
        "Grass blades near you, or all the way to the horizon.",
        "Let the rain come and go, or pick the weather you like.",
        "How far a steering wheel turns lock to lock.",
        "Let the day turn, or freeze the light at your favourite hour.",
        "Overall image brightness.",
        "The difference between light and shade.",
        "How strong the colours are.",
        "Warmer (golden) or cooler (blue) light.",
        "Shifts colours so they stay distinct for colour-blind players.",
        "The arrows on the road that show where to brake and turn.",
        "Live: cars reflect the world around them. Sky only is cheaper.",
        "Washi paper by day, or sumi-ink night for the menus and HUD."};
    // the right-hand card: a print of the game, then what the setting does and its value
    float cx = x0 + pw + 30 * u + slideR, cw = W - pw - 30 * u, cy = ty + 16 * u;
    float ch = std::min(hud.h - cy - 90 * u, 540 * u);
    card(cx, cy, cw, ch, open, false, 47);
    float m = 16 * u, ih = std::min((cw - 2 * m) * 9.f / 16.f, ch * 0.48f);
    GLuint prev = menuThumb[6] ? menuThumb[6] : menuThumb[5] ? menuThumb[5] : menuThumb[0];
    if (prev) {
        float iw = cw - 2 * m, ta = iw / ih, ia = 4.f / 3.f, v = std::min(1.f, ia / ta), pan = std::sin(R.time * 0.2f) * 0.04f;
        hud.image(cx + m, cy + m, iw, ih, prev, vec4(1, 1, 1, open), vec2(0.04f + pan, 0.5f + v * 0.5f), vec2(0.96f + pan, 0.5f - v * 0.5f));
        inkBox(cx + m, cy + m, iw, ih, 2 * u, wa::A(wa::INK, open));
    }
    float tx = cx + 26 * u, yy = cy + m + ih + 22 * u;
    std::string cat = si < 21 ? kCat[si] : "GAME";
    hanko(vec2(cx + cw - 46 * u, cy + m + ih - 6 * u), 52 * u, si < 21 ? kCatK[si] : "設", wa::VERM, 0.12f, open);
    hud.text(tx, yy, 13 * u, cat, wa::A(wa::VERM, open), 0, false);
    hud.textFit(tx, yy + 20 * u, 34 * u, label, wa::A(wa::INK, open), 0, cw - 60 * u, false);
    hud.textFit(tx, yy + 66 * u, 16 * u, si < 21 ? kInfo[si] : "", wa::A(wa::INK2, open), 0, cw - 52 * u, false);
    if (!value.empty()) {
        float nudge = std::sin(R.time * 5.f) * 3 * u;
        float vy = yy + 104 * u;
        hud.text(tx - nudge, vy, 26 * u, "<", wa::A(wa::VERM, open), 0, false);
        float vw = hud.textFit(tx + 30 * u, vy, 26 * u, value, wa::A(wa::INK, open), 0, cw - 120 * u, false);
        hud.text(tx + 42 * u + vw + nudge, vy, 26 * u, ">", wa::A(wa::VERM, open), 0, false);
        int pct = -1;
        if (value.back() == '%') pct = std::atoi(value.c_str());
        if (pct >= 0) {
            float gw = cw - 52 * u, gy = vy + 44 * u, maxV = label == "Volume" ? 100.f : 200.f;
            hud.rect(tx, gy + 4 * u, gw, 2 * u, wa::A(wa::INK, 0.25f * open));
            brush(vec2(tx, gy + 5 * u), vec2(tx + gw * clampf(pct / maxV, 0.f, 1.f), gy + 5 * u), 10 * u, wa::A(wa::INK, open), 21, 1.f);
        }
    }
    drawThemePrompts(pad ? "UP/DOWN choose   LEFT/RIGHT change   (B) back" : "UP/DOWN choose   LEFT/RIGHT change   ESC back");
}

void Game::drawResults() {
    using namespace wa;
    float u = hud.ui;
    const EventDef& E = events[raceEvent];
    float in = ease(stateTime / 0.4f);
    // a sheet of paper slides over the finished race from the left, the result brushed on in ink
    float sw = std::min(hud.w * 0.62f, 820 * u), sx = -sw * (1.f - in);
    hud.rect(sx + sw, 0, 8 * u, hud.h, A(SHADOW, 0.6f * in));
    paper(sx, 0, sw, hud.h, 1.f, 95);
    hud.rect(sx, 0, 58 * u, hud.h, COLUMN);
    hud.rect(sx + 58 * u, 0, 2 * u, hud.h, VERM);
    vtext(sx + 29 * u, 40 * u, 30 * u, playerPlace == 1 ? "勝利" : "結果", COLTEXT);
    if (playerPlace == 1) drawPetals(0.8f, R.time);
    float x = sx + 100 * u, y = 48 * u, w = sw - 140 * u;
    hud.text(x, y, 15 * u, E.name, VERM, 0, false);
    float pop = ease((stateTime - 0.2f) / 0.35f);
    std::string big = playerPlace == 1 ? "Victory" : ("Finished " + ordinal(playerPlace));
    hud.text(x, y + 22 * u, 60 * u, big, A(INK, pop), 0, false);
    brush(vec2(x, y + 100 * u), vec2(x + std::min(w, hud.textWidth(60 * u, big) + 40 * u), y + 102 * u), 9 * u, VERM, 7, ease((stateTime - 0.35f) / 0.4f));
    if (playerPlace == 1) hanko(vec2(x + w - 50 * u, y + 56 * u), 90 * u, "勝", VERM, -0.12f, ease((stateTime - 0.6f) / 0.2f));
    float py = y + 130 * u;
    for (size_t i = 0; i < racers.size(); i++) {
        float re = ease((stateTime - 0.35f - i * 0.06f) / 0.3f);
        float ry = py + i * 40 * u, rx = x + (1.f - re) * 40 * u;
        Racer* r = racers[i];
        bool me = r->isPlayer;
        if (me) brushSwipe(rx, ry, w, 34 * u, A(INK, re), 41, re);
        else for (float dx = 0; dx < w; dx += 8 * u) hud.rect(rx + dx, ry + 33 * u, 3 * u, 1.2f * u, A(INK, 0.25f * re));
        vec4 tc = A(me ? PAPER : INK, re), dc = A(me ? PAPER2 : MUTED, re);
        vec4 medal = i == 0 ? GOLD : i == 1 ? vec4(0.62f, 0.62f, 0.60f, 1) : i == 2 ? vec4(0.62f, 0.38f, 0.2f, 1) : (me ? PAPER2 : MUTED);
        char num[16];
        std::snprintf(num, sizeof(num), "%02d", int(i) + 1);
        hud.text(rx + 8 * u, ry + 6 * u, 20 * u, num, A(medal, re), 0, false);
        hud.text(rx + 56 * u, ry + 7 * u, 18 * u, r->name, tc, 0, false);
        hud.textFit(rx + 210 * u, ry + 9 * u, 15 * u, carName(*r->car.spec), dc, 0, w - 330 * u);
        hud.text(rx + w - 10 * u, ry + 7 * u, 18 * u, fmtTime(r->finishTime), tc, 2, false);
    }
    float by = py + racers.size() * 40 * u + 24 * u;
    float prt = ease((stateTime - 0.6f - racers.size() * 0.06f) / 0.3f);
    hud.text(x, by, 13 * u, "PRIZE", A(MUTED, prt), 0, false);
    hud.circle(vec2(x + 14 * u, by + 36 * u), 13 * u, A(GOLD, prt), 20);
    hud.text(x + 14 * u, by + 29 * u, 11 * u, "CR", A(CARD, prt), 1, false);
    hud.text(x + 36 * u, by + 18 * u, 34 * u, fmtThousands(prizeWon), A(INK, prt), 0, false);
    drawThemePrompts(pad ? "(A) continue" : "ENTER continue", x);
}

std::string Game::crText() const { return "CR " + fmtThousands((long long)(creditsShown + 0.5)); }

// Forza-style wipe: a slanted dark panel with a pink leading edge sweeps off the screen
void Game::drawTransition() {
    // a sheet of washi pulled across the screen, its edge a ragged brushline, a seal in the middle
    if (transT >= transDur || opt.noHud || !opt.shot.empty()) return;
    using namespace wa;
    float u = hud.ui;
    float t = clampf(transT / transDur, 0.f, 1.f);
    t = t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) / 2.f;
    float e = -60 * u + (hud.w + 160 * u) * t;  // the sheet's trailing edge
    if (e < hud.w) {
        paper(e, 0, hud.w - e + 4, hud.h, 1.f, 97);
        brush(vec2(e, -10 * u), vec2(e - 20 * u, hud.h + 10 * u), 14 * u, INK, 9, 1.f);
        if (t < 0.4f) hanko(vec2(std::max(e + 140 * u, hud.w * 0.5f), hud.h * 0.5f), 120 * u, "峠", VERM, -0.08f, 1.f - t / 0.4f);
    }
}

void Game::drawLogo(float x, float y, float w, float alpha) {
    if (!logoTex) { hud.text(x, y, w * 0.16f, "RIDGELINE", vec4(1, 1, 1, alpha)); return; }
    hud.image(x, y, w, w * kLogoH / kLogoW, logoTex, vec4(1, 1, 1, alpha), vec2(0, 0), vec2(1, 1));
}

// shared top bar for the menus: logo, section name, and the driver's level / credits on the right
std::string Game::clockText() const {
    int h = int(todHours), m = int((todHours - h) * 60.f);
    char buf[64];
    const char* sky = wxRain > 0.5f ? "RAIN" : wxRain > 0.15f ? "SHOWERS" : R.night > 0.6f ? "CLEAR NIGHT" : todHours > 17.f && todHours < 19.6f ? "SUNSET" : "CLEAR";
    std::snprintf(buf, sizeof(buf), "AUTUMN  |  %02d:%02d  |  %s", h, m, sky);
    return buf;
}


void Game::drawPetals(float alpha, float T) {
    float u = hud.ui;
    for (int k = 0; k < 46; k++) {
        float sp = 0.35f + 0.65f * std::fmod(k * 0.618f, 1.f);
        float t = T * sp * 0.09f + k * 0.137f;
        float px = std::fmod(t * 1.3f + k * 0.21f, 1.2f) * hud.w * 1.1f - hud.w * 0.1f;
        float py = std::fmod(t + std::fmod(k * 0.377f, 1.f), 1.f) * (hud.h + 60 * u) - 30 * u + std::sin(T * 1.3f + k) * 18 * u;
        float r = (4.f + 4.f * std::fmod(k * 0.29f, 1.f)) * u, ang = T * (0.8f + sp) + k;
        vec2 c(px, py), ax(std::cos(ang) * r, std::sin(ang) * r * 0.55f), ay(-std::sin(ang) * r * 0.55f, std::cos(ang) * r);
        vec4 pc = k % 3 ? vec4(1.f, 0.78f, 0.86f, 0.85f * alpha) : vec4(1.f, 0.92f, 0.95f, 0.85f * alpha);
        hud.quad(c - ax, c - ay * 0.6f, c + ax, c + ay, pc);
    }
}

void Game::drawTitle() {
    using namespace wa;
    float u = hud.ui;
    if (!keyArt) {
        float tl = std::fmod(stateTime, 8.f);
        float fade = std::max(1.f - smoothstepf(0.f, 0.6f, tl), smoothstepf(7.5f, 8.f, tl));
        if (fade > 0.01f) hud.rect(0, 0, hud.w, hud.h, vec4(0, 0, 0, fade * (stateTime < 1.f ? 1.f : 0.85f)));
    }
    float in = ease(stateTime / 0.8f);
    float me = titleMenu ? ease(titleMenuT / 0.45f) : 0.f;
    float att = titleMenu ? 1.f - me : (stateTime < 1.f ? 1.f : ease(titleMenuT / 0.35f));
    if (titleMenuT > 100.f) att = titleMenu ? 0.f : 1.f;
    // the whole view framed like a print: a paper mat round the edge and an ink rule inside it
    {
        float b = 18 * u;
        paper(0, 0, hud.w, b, in, 1, PAPER);
        paper(0, hud.h - b, hud.w, b, in, 2, PAPER);
        paper(0, b, b, hud.h - 2 * b, in, 3, PAPER);
        paper(hud.w - b, b, b, hud.h - 2 * b, in, 4, PAPER);
        inkBox(b, b, hud.w - 2 * b, hud.h - 2 * b, 2 * u, A(INK, in));
    }
    drawPetals(in * 0.8f, stateTime);
    const char* user = getenv("USER");
    std::string who = user && *user ? user : "Driver";

    if (att > 0.01f) {
        float a = in * att;
        // a hanging scroll on the right: 峠 (the mountain pass) and 祭り (festival)
        float sw = 150 * u, sx = hud.w - 80 * u - sw + (1.f - a) * 60 * u, sy = 18 * u, sh = hud.h * 0.62f * ease((stateTime - 0.2f) / 0.7f);
        if (sh > 10 * u) {
            hud.rect(sx + 7 * u, sy + 7 * u, sw, sh, A(INK, 0.5f * a));
            paper(sx, sy, sw, sh, a, 8, CARD);
            hud.rect(sx + 10 * u, sy, sw - 20 * u, sh, A(PAPER2, 0.5f * a));
            hud.rect(sx - 10 * u, sy + sh - 10 * u, sw + 20 * u, 14 * u, A(INK, a));
            if (sh > 300 * u) {
                vtext(sx + sw * 0.5f, sy + 40 * u, 110 * u, "峠", A(INK, a));
                vtext(sx + sw * 0.5f, sy + 170 * u, 52 * u, "祭り", A(VERM, a));
            }
        }
        // the name, set in spaced capitals on a paper strip, with a seal
        float ty = hud.h - 230 * u, tx = 70 * u - (1.f - a) * 60 * u;
        float tw = hud.textWidth(64 * u, "R I D G E L I N E") + 60 * u;
        card(tx, ty, tw, 112 * u, a, false, 9);
        inkLetters(tx + 30 * u, ty + 14 * u, 64 * u, "R I D G E L I N E", A(INK, a), stateTime - 0.5f);
        hud.text(tx + 32 * u, ty + 84 * u, 15 * u, "OPEN-WORLD FESTIVAL RACING", A(VERM, a), 0, false);
        hanko(vec2(tx + tw - 4 * u, ty + 8 * u), 64 * u, "走", VERM, 0.14f, ease((stateTime - 0.9f) / 0.3f) * a);
        // press start: a swipe of ink, breathing
        std::string ps = pad ? "PRESS  (A)  TO START" : "PRESS ENTER TO START";
        float pw = hud.textWidth(20 * u, ps) + 40 * u, py = ty + 132 * u;
        float pulse = 0.82f + 0.18f * std::sin(stateTime * 3.f);
        brushSwipe(tx + 10 * u, py, pw, 40 * u, A(INK, pulse * a), 27, ease((stateTime - 1.0f) / 0.4f));
        hud.text(tx + 30 * u, py + 9 * u, 20 * u, ps, A(PAPER, a * ease((stateTime - 1.2f) / 0.3f)), 0, false);
        // the driver's tally, written small along the top
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s   ·   段 %d   ·   CR %s   ·   %d %s   ·   %d %s", who.c_str(), level, fmtThousands(credits).c_str(), int(garage.size()),
                      garage.size() == 1 ? "car" : "cars", racesWon, racesWon == 1 ? "win" : "wins");
        float bw = hud.textWidth(15 * u, buf) + 36 * u;
        card(70 * u, 50 * u - (1.f - a) * 30 * u, bw, 36 * u, a, false, 4);
        hud.text(88 * u, 59 * u - (1.f - a) * 30 * u, 15 * u, buf, A(INK, a), 0, false);
    }

    if (titleMenu) {
        // ---- the main menu: a sheet of paper over the left, a numbered list, your car on a card
        float sheetW = std::min(hud.w * 0.42f, 620 * u) * me;
        paper(0, 0, sheetW, hud.h, 0.97f, 29, PAPER);
        for (float y = 0; y < hud.h; y += 14 * u) {
            float d = (4.f + 8.f * hash(31, int(y))) * u;
            hud.triangle(vec2(sheetW, y), vec2(sheetW + d, y + 7 * u), vec2(sheetW, y + 14 * u), A(PAPER, 0.97f * me));
        }
        hud.rect(0, 0, 58 * u * me, hud.h, A(COLUMN, me));
        vtext(29 * u, 40 * u, 30 * u, "峠", A(COLTEXT, me));
        hanko(vec2(29 * u, hud.h - 60 * u), 34 * u, "走", VERM, -0.08f, me);
        float x0 = 100 * u;
        inkLetters(x0, 50 * u, 38 * u, "R I D G E L I N E", A(INK, me), titleMenuT - 0.1f);
        brush(vec2(x0 - 6 * u, 104 * u), vec2(x0 + 360 * u, 106 * u), 7 * u, A(INK, me), 6, me);
        hud.text(x0, 118 * u, 14 * u, "OPEN-WORLD FESTIVAL RACING", A(VERM, me), 0, false);
        static const char* const kLabel[5] = {"Continue", "My Garage", "Autoshow", "Settings", "Quit"};
        static const char* const kK[5] = {"再開", "車庫", "販売", "設定", "終了"};
        std::string sub[5] = {"Back to the Ridgeline Festival", std::to_string(garage.size()) + (garage.size() == 1 ? " car in your garage" : " cars in your garage"),
                              std::to_string(kCars.size()) + " cars for sale", "Display, audio, driving, controls", "Save and leave"};
        float ly = 190 * u, lh = 86 * u, lw = sheetW - x0 - 50 * u;
        for (int i = 0; i < 5; i++) {
            float ie = ease((titleMenuT - 0.1f - i * 0.06f) / 0.3f);
            if (titleMenuT > 100.f) ie = 1.f;
            bool on = i == titleSel;
            float yy = ly + i * lh, xx = x0 - (1.f - ie) * 50 * u;
            themeRow(xx, yy, lw, lh - 14 * u, on, ie * me);
            char num[16];
            std::snprintf(num, sizeof(num), "%02d", i + 1);
            hud.text(xx + 12 * u, yy + 12 * u, 18 * u, num, A(on ? wa::GOLD : VERM, ie * me), 0, false);
            hud.text(xx + 58 * u, yy + 4 * u, 34 * u, kLabel[i], A(on ? PAPER : INK, ie * me), 0, false);
            hud.text(xx + 58 * u, yy + 50 * u, 14 * u, sub[i], A(on ? PAPER2 : INK2, ie * me), 0, false);
            hud.text(xx + lw - 14 * u, yy + 14 * u, 26 * u, kK[i], A(on ? PAPER : INK, (on ? 0.9f : 0.18f) * ie * me), 2, false);
        }
        // your car, on a card in the corner of the picture
        int si = current < int(garage.size()) ? garage[current].spec : -1;
        GLuint img = si >= 0 && si < int(carThumb.size()) ? carThumb[si] : 0;
        float cw = 380 * u, ch = 250 * u, cx = hud.w - 60 * u - cw + (1.f - me) * 80 * u, cy = hud.h - 60 * u - ch;
        card(cx, cy, cw, ch, me, false, 51);
        enso(vec2(cx + cw * 0.5f, cy + ch * 0.46f), ch * 0.36f, ch * 0.06f, A(INK, 0.85f * me), ease((titleMenuT - 0.3f) / 0.6f), 13);
        if (img) hud.image(cx + 20 * u, cy + 16 * u, cw - 40 * u, (cw - 40 * u) * 0.48f, img, vec4(1, 1, 1, me), vec2(0.08f, 0.84f), vec2(0.92f, 0.2f));
        if (si >= 0) {
            const CarSpec& sp = kCars[si];
            std::string mk = sp.make;
            for (auto& ch2 : mk) ch2 = char(std::toupper(static_cast<unsigned char>(ch2)));
            hud.text(cx + 22 * u, cy + ch - 68 * u, 13 * u, mk, A(VERM, me), 0, false);
            hud.textFit(cx + 22 * u, cy + ch - 50 * u, 24 * u, sp.model, A(INK, me), 0, cw - 44 * u, false);
        }
        drawThemePrompts(pad ? "D-pad choose   (A) confirm   (B) back" : "UP/DOWN choose   ENTER confirm   ESC back");
    }
    hud.text(hud.w - 40 * u, hud.h - 16 * u, 11 * u, "v3.2  ·  FREE & OPEN SOURCE (MIT)", A(MUTED, in), 2, false);
}

void Game::saveShot(const std::string& path) {
    int w = R.width, h = R.height;
    std::vector<uint8_t> px(size_t(w) * h * 4), flip(size_t(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) std::memcpy(&flip[size_t(y) * w * 4], &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
    for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;  // the HUD leaves partial alpha behind
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, flip.data(), w * 4);
    if (s) {
        if (!SDL_SaveBMP(s, path.c_str())) SDL_Log("Screenshot failed: %s", SDL_GetError());
        SDL_DestroySurface(s);
    }
}

// ====================================================================== main loop
void Game::run() {
    if (!opt.recordCmd.empty()) {
        // offline capture: fixed 30 fps, frames piped to an encoder, audio rendered in lock-step
        FILE* pipe = popen(opt.recordCmd.c_str(), "w");
        FILE* aud = opt.recordAudio.empty() ? nullptr : std::fopen(opt.recordAudio.c_str(), "wb");
        if (!pipe) { SDL_Log("could not start: %s", opt.recordCmd.c_str()); return; }
        int total = int(opt.recordSec * 30.f + 0.5f);
        std::vector<uint8_t> px(size_t(R.width) * R.height * 4);
        std::vector<float> ab(1600 * 2);
        for (int f = 0; f < total && running; f++) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {}
            update(1.f / 30.f);
            render();
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, R.width, R.height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
            std::fwrite(px.data(), 1, px.size(), pipe);
            if (aud) { audio.generate(ab.data(), 1600); std::fwrite(ab.data(), sizeof(float), ab.size(), aud); }
            SDL_GL_SwapWindow(win);
            if (f % 15 == 0) SDL_Log("recorded %d / %d", f, total);
        }
        pclose(pipe);
        if (aud) std::fclose(aud);
        return;
    }
    Uint64 last = SDL_GetPerformanceCounter();
    double freq = double(SDL_GetPerformanceFrequency());
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = float((now - last) / freq);
        last = now;
        if (!opt.shot.empty()) dt = getenv("RL_SHOTDT") ? float(std::atof(getenv("RL_SHOTDT"))) : 1.f / 60.f;  // screenshots: fixed steps (bigger to fast-forward UI animations)
        dt = clampf(dt, 0.f, 0.05f);
        update(dt);
        render();
        frameNo++;
        if (!opt.shot.empty() && frameNo >= std::max(opt.frames, 1)) {
            saveShot(opt.shot);
            running = false;
        }
        SDL_GL_SwapWindow(win);
        if (opt.frames > 0 && frameNo >= opt.frames) running = false;
    }
}
