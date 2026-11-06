#include "game.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

static const vec4 ACCENT(1.0f, 0.42f, 0.12f, 1.f);
static const vec4 WHITE(1, 1, 1, 1);
static const vec4 DIM(1, 1, 1, 0.6f);
static const vec4 PANEL(0.05f, 0.05f, 0.06f, 0.78f);
static const char* kBotNames[] = {"Vex", "Juniper", "Halcyon", "Brick", "Nova", "Tamsin", "Ozzy", "Kiri", "Marlow", "Sable", "Dax", "Wren"};

static vec3 stripeFor(vec3 paint) {
    float lum = paint.x * 0.3f + paint.y * 0.59f + paint.z * 0.11f;
    return lum > 0.45f ? vec3(0.05f, 0.05f, 0.06f) : vec3(0.92f, 0.92f, 0.9f);
}
static int cylindersFor(const CarSpec& s) {
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
    return vec4(0.45f, 0.9f, 0.25f, 1);
}
static const char* eventTypeName(EventType t) {
    if (t == EV_CIRCUIT) return "CIRCUIT";
    if (t == EV_SPRINT) return "SPRINT";
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

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (settings.fullscreen && opt.shot.empty()) flags |= SDL_WINDOW_FULLSCREEN;
    win = SDL_CreateWindow("Ridgeline", opt.width, opt.height, flags);
    if (!win) { SDL_Log("Window failed: %s", SDL_GetError()); return false; }
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
    hud.mapTex = world.mapTex;
    models.resize(kCars.size());
    for (size_t i = 0; i < kCars.size(); i++) models[i].build(kCars[i]);
    events = buildEvents(world);
    if (bestPlace.size() < events.size()) bestPlace.resize(events.size(), 0);
    SDL_Log("World ready in %llu ms", (unsigned long long)(SDL_GetTicks() - t0));

    // helper meshes
    {
        MeshBuilder b;
        uint32_t c = b.add(vec3(0, 0, 0), vec3(0, 1, 0), vec4(0, 0, 0, 0.55f));
        for (int i = 0; i <= 24; i++) {
            float a = i / 24.f * 2 * PI;
            b.add(vec3(std::cos(a), 0, std::sin(a)), vec3(0, 1, 0), vec4(0, 0, 0, 0));
        }
        for (int i = 0; i < 24; i++) b.tri(c, c + 2 + i, c + 1 + i);
        b.build(blobMesh);
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

    audio.init();
    audio.master = settings.volume / 100.f;
    int npads = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&npads);
    if (ids && npads > 0) pad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);

    if (!haveSave) newGame();
    // title attract car
    titleCar.car.init(&kCars[7], vec3(0, 0, 0), 0);
    titleCar.model = &models[7];
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
            garage.push_back({opt.car, kCars[opt.car].stripes ? 0 : 2, kCars[opt.car].stripes});
            setPlayerCar(int(garage.size()) - 1);
        }
        if (opt.scene == "drive") {
            const Route& rt = events[std::clamp(opt.event, 0, int(events.size()) - 1)].route;
            enterFreeRoam(rt.pts[10], std::atan2(rt.tan[10].x, rt.tan[10].z));
            player.idx = 10;
            autopilot = true;
        }
        else if (opt.scene == "hub") enterFreeRoam(world.hubSpawn, world.hubYaw);
        else if (opt.scene == "race") { enterFreeRoam(world.hubSpawn, world.hubYaw); autopilot = true; startRace(std::clamp(opt.event, 0, int(events.size()) - 1)); }
        else if (opt.scene == "dealer") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_DEALER; subSel = std::clamp(opt.event, 0, int(kCars.size()) - 1); dealerPaint = stockPaint(kCars[subSel]); }
        else if (opt.scene == "garage") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_GARAGE; }
        else if (opt.scene == "map") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_MAP; }
        else if (opt.scene == "pause") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_PAUSE; returnState = ST_FREEROAM; }
        else if (opt.scene == "settings") { enterFreeRoam(world.hubSpawn, world.hubYaw); state = ST_SETTINGS; }
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
    for (auto& g : garage) f << "car=" << g.spec << "," << g.paint << "," << (g.stripes ? 1 : 0) << "\n";
    for (size_t i = 0; i < bestPlace.size(); i++) f << "best=" << i << "," << bestPlace[i] << "\n";
    f << "mph=" << settings.mph << "\nblur=" << settings.motionBlur << "\nshadows=" << settings.shadows << "\nmsaa=" << settings.msaa
      << "\nfullscreen=" << settings.fullscreen << "\nvolume=" << settings.volume << "\ndraw=" << settings.drawDist
      << "\nvsync=" << settings.vsync << "\nassists=" << settings.assists << "\ngrass=" << settings.grass << "\n";
}

bool Game::load() {
    std::ifstream f(prefPath + "save.txt");
    if (!f) return false;
    std::string line;
    garage.clear();
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        int a = 0, b = 0, c = 0;
        if (k == "credits") credits = std::atoll(v.c_str());
        else if (k == "current") current = std::atoi(v.c_str());
        else if (k == "wins") racesWon = std::atoi(v.c_str());
        else if (k == "car" && std::sscanf(v.c_str(), "%d,%d,%d", &a, &b, &c) == 3) {
            if (a >= 0 && a < int(kCars.size())) garage.push_back({a, std::clamp(b, 0, kPaintCount - 1), c != 0});
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
    }
    if (garage.empty()) return false;
    current = std::clamp(current, 0, int(garage.size()) - 1);
    return true;
}

void Game::applySettings() {
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
    garage = {{0, 0, false}};
    current = 0;
    credits = 25000;
    racesWon = 0;
    bestPlace.assign(events.size() > 0 ? events.size() : 16, 0);
    save();
}

void Game::setPlayerCar(int idx) {
    current = std::clamp(idx, 0, int(garage.size()) - 1);
    const OwnedCar& oc = garage[current];
    vec3 p = player.car.pos;
    float y = player.car.yaw;
    player.car.init(&kCars[oc.spec], p, y);
    player.car.up = vec3(0, 1, 0);
    player.model = &models[oc.spec];
    player.paint = kPaints[oc.paint];
    player.stripe = stripeFor(player.paint);
    if (!oc.stripes) player.stripe = player.paint;
    player.isPlayer = true;
    player.name = "You";
    player.car.assists = settings.assists;
    audio.cylinders = cylindersFor(kCars[oc.spec]);
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
    raceEvent = ev;
    const EventDef& E = events[ev];
    const Route& rt = E.route;
    Rng rng(uint32_t(SDL_GetTicks() + ev * 977 + 1));
    if (!opt.scene.empty()) rng = Rng(42 + ev);
    char cls = player.car.spec->cls;
    std::vector<int> cand;
    for (int i = 0; i < int(kCars.size()); i++) if (kCars[i].cls == cls) cand.push_back(i);
    const char order[] = "DCBAS";
    int ci = int(std::string(order).find(cls));
    if (cand.size() < 2) {
        for (int i = 0; i < int(kCars.size()); i++) {
            int cj = int(std::string(order).find(kCars[i].cls));
            if (cj == ci - 1) cand.push_back(i);
        }
    }
    if (cand.empty()) cand.push_back(player.car.spec - kCars.data());
    bots.assign(5, Racer());
    std::vector<int> names = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    for (int i = 0; i < 12; i++) std::swap(names[i], names[rng.irange(0, 11)]);
    for (int i = 0; i < 5; i++) {
        Racer& b = bots[i];
        int s = cand[rng.irange(0, int(cand.size()) - 1)];
        b.model = &models[s];
        b.paint = kPaints[rng.irange(0, kPaintCount - 1)];
        b.stripe = kCars[s].stripes ? stripeFor(b.paint) : b.paint;
        b.name = kBotNames[names[i]];
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
    // checkpoint gates
    {
        MeshBuilder b;
        float total = E.totalLength();
        float L = rt.length;
        float spacing = 320.f;
        int n = int(L / spacing);
        for (int i = 1; i <= n + 1; i++) {
            float d = i <= n ? i * spacing : (rt.closed ? 0.f : L - 1.f);
            if (i <= n && L - d < 120.f) continue;
            bool fin = i == n + 1;
            vec3 p = rt.pointAt(d), t = rt.tangentAt(d), right(-t.z, 0, t.x);
            p.y = world.heightAt(p.x, p.z);
            float hw = rt.width * 0.5f + 1.8f;
            mat4 xf = basis(right, vec3(0, 1, 0), t, p);
            vec4 post(0.08f, 0.08f, 0.09f, 0.95f);
            b.box(vec3(hw, 3.0f, 0), vec3(0.3f, 3.0f, 0.3f), post, &xf);
            b.box(vec3(-hw, 3.0f, 0), vec3(0.3f, 3.0f, 0.3f), post, &xf);
            if (fin) {
                for (int s = 0; s < 10; s++) {
                    float x0 = -hw + s * (2 * hw / 10.f);
                    vec4 c = (s % 2) ? vec4(0.05f, 0.05f, 0.05f, 0.95f) : vec4(0.95f, 0.95f, 0.95f, 0.95f);
                    b.box(vec3(x0 + hw / 10.f, 6.4f, 0), vec3(hw / 10.f, 0.6f, 0.15f), c, &xf);
                }
            } else {
                b.box(vec3(0, 6.4f, 0), vec3(hw, 0.55f, 0.15f), vec4(1.0f, 0.45f, 0.1f, 0.8f), &xf);
            }
        }
        (void)total;
        b.build(gateMesh);
    }
    state = ST_RACE_INTRO;
    stateTime = 0;
    countdown = 3.f;
    raceTime = 0;
    finishDelay = 0;
    offRouteTime = 0;
    driftScore = 0;
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
    prizeWon = playerPlace <= 3 ? E.prize[playerPlace - 1] : 1500;
    credits += prizeWon;
    if (playerPlace == 1) racesWon++;
    if (int(bestPlace.size()) <= raceEvent) bestPlace.resize(raceEvent + 1, 0);
    if (bestPlace[raceEvent] == 0 || playerPlace < bestPlace[raceEvent]) bestPlace[raceEvent] = playerPlace;
    save();
    state = ST_RESULTS;
    stateTime = 0;
    audio.beep(playerPlace == 1 ? 1320.f : 660.f, 0.6f);
}

void Game::leaveRace() {
    racers.clear();
    bots.clear();
    raceEvent = -1;
    gateMesh.destroy();
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
            if (driving && (a == A_UP || a == A_DOWN || a == A_LEFT || a == A_RIGHT)) a = A_NONE;
            if (e.key.repeat && (a == A_CONFIRM || a == A_INTERACT || a == A_PAUSE || a == A_BACK)) a = A_NONE;
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            bool driving = state == ST_FREEROAM || state == ST_RACE || state == ST_RACE_INTRO;
            switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: a = driving ? A_NONE : A_UP; break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: a = driving ? A_NONE : A_DOWN; break;
                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: a = driving ? A_NONE : A_LEFT; break;
                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: a = driving ? A_NONE : A_RIGHT; break;
                case SDL_GAMEPAD_BUTTON_SOUTH: a = driving ? (state == ST_RACE_INTRO ? A_CONFIRM : A_NONE) : A_CONFIRM; break;
                case SDL_GAMEPAD_BUTTON_EAST: a = driving ? A_NONE : A_BACK; break;
                case SDL_GAMEPAD_BUTTON_NORTH: a = A_INTERACT; break;
                case SDL_GAMEPAD_BUTTON_WEST: a = driving ? A_RESET : A_TOGGLE; break;
                case SDL_GAMEPAD_BUTTON_START: a = driving ? A_PAUSE : A_BACK; break;
                case SDL_GAMEPAD_BUTTON_BACK: a = A_MAP; break;
                case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: a = A_CAMERA; break;
                default: break;
            }
            break;
        }
        default: break;
    }
    if (a != A_NONE) handleAction(a);
}

struct MenuTile { float x, y, w, h; const char* label; const char* sub; vec4 accent; };
static const MenuTile kTiles[] = {
    {0, 0, 1, 1, "Resume", "Back to the road", {1.0f, 0.42f, 0.12f, 1}},
    {1, 0, 2, 1, "World Map", "Fast travel anywhere", {0.30f, 0.62f, 0.95f, 1}},
    {3, 0, 1, 1, "Dealership", "Buy new cars", {0.98f, 0.72f, 0.18f, 1}},
    {0, 1, 1, 1, "My Garage", "Paint and switch cars", {0.88f, 0.22f, 0.16f, 1}},
    {1, 1, 1, 1, "Next Race", "", {0.45f, 0.85f, 0.35f, 1}},
    {2, 1, 1, 1, "Settings", "Graphics, sound, driving", {0.62f, 0.62f, 0.66f, 1}},
    {3, 1, 1, 1, "Save & Exit", "Back to the title screen", {0.20f, 0.70f, 0.66f, 1}},
};
static const int kTileCount = 7;
static int tileMove(int cur, int dx, int dy) {
    const MenuTile& c = kTiles[cur];
    float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f;
    int best = cur; float bd = 1e9f;
    for (int i = 0; i < kTileCount; i++) {
        if (i == cur) continue;
        float tx = kTiles[i].x + kTiles[i].w * 0.5f - cx, ty = kTiles[i].y + kTiles[i].h * 0.5f - cy;
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
            if (a == A_CONFIRM || a == A_INTERACT) { audio.beep(1200, 0.1f); enterFreeRoam(world.hubSpawn, world.hubYaw); toast("Welcome to the Ridgeline Festival", ACCENT); }
            else if (a == A_BACK) running = false;
            break;
        case ST_FREEROAM:
            if (a == A_PAUSE || a == A_BACK) { state = ST_PAUSE; returnState = ST_FREEROAM; menuSel = 0; }
            else if (a == A_MAP) { state = ST_MAP; subSel = 0; }
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
                if (nearEvent >= 0) startRace(nearEvent);
                else if (nearHub) { state = ST_PAUSE; returnState = ST_FREEROAM; menuSel = 2; }
            }
            break;
        case ST_RACE_INTRO:
            if (a == A_CONFIRM || a == A_INTERACT) { if (stateTime < 2.5f) stateTime = 2.5f; }
            else if (a == A_PAUSE) { state = ST_PAUSE; returnState = ST_RACE_INTRO; menuSel = 0; }
            break;
        case ST_RACE:
            if (a == A_PAUSE || a == A_BACK) { state = ST_PAUSE; returnState = ST_RACE; menuSel = 0; }
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
                int before = menuSel;
                if (a == A_LEFT) menuSel = tileMove(menuSel, -1, 0);
                if (a == A_RIGHT) menuSel = tileMove(menuSel, 1, 0);
                if (a == A_UP) menuSel = tileMove(menuSel, 0, -1);
                if (a == A_DOWN) menuSel = tileMove(menuSel, 0, 1);
                if (menuSel != before) audio.beep(880, 0.04f);
                if (a == A_BACK || a == A_PAUSE) state = returnState;
                if (a != A_CONFIRM) break;
                audio.beep(1100, 0.06f);
                switch (menuSel) {
                    case 0: state = returnState; break;
                    case 1: state = ST_MAP; subSel = 0; break;
                    case 2: state = ST_DEALER; subSel = 0; dealerPaint = stockPaint(kCars[0]); stateTime = 0; break;
                    case 3: state = ST_GARAGE; subSel = current; stateTime = 0; break;
                    case 4: {
                        int ev = nextRaceEvent();
                        const EventDef& E = events[ev];
                        enterFreeRoam(E.route.pts[0] - E.route.tan[0] * (E.route.closed ? 25.f : 0.f), E.startYaw);
                        if (!E.route.closed) player.car.placeAt(E.route.pts[2], E.startYaw, world);
                        toast("Fast travelled to " + E.name, ACCENT);
                        break;
                    }
                    case 5: state = ST_SETTINGS; subSel = 0; break;
                    case 6: save(); state = ST_TITLE; stateTime = 0; setupTitle(); break;
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
            else if (it == "Dealership") { state = ST_DEALER; subSel = 0; dealerPaint = stockPaint(kCars[0]); stateTime = 0; }
            else if (it == "My Garage") { state = ST_GARAGE; subSel = current; stateTime = 0; }
            else if (it == "Settings") { state = ST_SETTINGS; subSel = 0; }
            else if (it == "Save & Exit to Title") { save(); state = ST_TITLE; stateTime = 0; }
            else if (it == "Quit Game") running = false;
            else if (it == "Restart Event") startRace(raceEvent);
            else if (it == "Quit Event") leaveRace();
            break;
        }
        case ST_MAP: {
            int n = int(events.size()) + 1;
            nav(subSel, n);
            if (a == A_BACK || a == A_MAP || a == A_PAUSE) state = ST_FREEROAM;
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
            int n = int(kCars.size());
            int before = subSel;
            nav(subSel, n);
            if (subSel != before) dealerPaint = stockPaint(kCars[subSel]);
            if (a == A_LEFT) dealerPaint = (dealerPaint - 1 + kPaintCount) % kPaintCount;
            if (a == A_RIGHT) dealerPaint = (dealerPaint + 1) % kPaintCount;
            if (a == A_BACK) state = ST_PAUSE;
            if (a == A_CONFIRM) {
                const CarSpec& s = kCars[subSel];
                if (credits >= s.price) {
                    credits -= s.price;
                    garage.push_back({subSel, dealerPaint, s.stripes});
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
        case ST_GARAGE: {
            int n = int(garage.size());
            nav(subSel, n);
            OwnedCar& oc = garage[subSel];
            if (a == A_LEFT) oc.paint = (oc.paint - 1 + kPaintCount) % kPaintCount;
            if (a == A_RIGHT) oc.paint = (oc.paint + 1) % kPaintCount;
            if (a == A_TOGGLE) oc.stripes = !oc.stripes;
            if (a == A_BACK) { setPlayerCar(current); save(); state = ST_PAUSE; }
            if (a == A_CONFIRM) {
                setPlayerCar(subSel);
                player.car.placeAt(world.hubSpawn, world.hubYaw, world);
                camYaw = world.hubYaw;
                save();
                toast("Now driving: " + carName(kCars[oc.spec]), ACCENT);
                state = ST_FREEROAM;
            }
            break;
        }
        case ST_SETTINGS: {
            auto items = settingsItems();
            nav(subSel, int(items.size()));
            if (a == A_BACK) { state = ST_PAUSE; break; }
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
            std::string("Grass: ") + (settings.grass == 0 ? "Off" : settings.grass == 1 ? "Near only" : "Full distance")};
}

// ====================================================================== update
void Game::update(float dt) {
    stateTime += dt;
    R.time += dt;
    simTime += dt;
    for (auto& t : toasts) t.t -= dt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.t <= 0; }), toasts.end());
    flash = std::max(0.f, flash - dt * 2.5f);
    shake = std::max(0.f, shake - dt * 3.f);

    Vehicle* audioCar = &player.car;
    R.atmo = state == ST_TITLE ? titleAtmo : dayAtmo;
    switch (state) {
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
            playerCtl = readDriving();
            stepPhysics(dt);
            updateSkills(dt);
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
            if (autopilot && !opt.scene.empty() && opt.scene != "race") nearEvent = -1;
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
            updateRace(dt);
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
    updateCamera(dt);
    if (opt.shot.empty()) {
        bool wantMouse = (state == ST_FREEROAM || state == ST_RACE || state == ST_RACE_INTRO) && (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS);
        if (wantMouse != SDL_GetWindowRelativeMouseMode(win)) SDL_SetWindowRelativeMouseMode(win, wantMouse);
    }

    // audio
    audio.rpm = audioCar->rpm;
    audio.throttle = audioCar->throttle;
    audio.slip = audioCar->grounded ? audioCar->tyreSlip : 0.f;
    audio.speed = audioCar->speed();
    bool menus = state == ST_PAUSE || state == ST_MAP || state == ST_DEALER || state == ST_GARAGE || state == ST_SETTINGS;
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
        }
        physAcc -= h;
    }
    if (guard >= 12) physAcc = 0;
}

void Game::updateRace(float dt) {
    const EventDef& E = events[raceEvent];
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
    stepPhysics(dt);
    for (Racer* r : racers) {
        trackProgress(*r, E);
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

void Game::updateSkills(float dt) {
    Vehicle& c = player.car;
    float v = c.speed();
    bool drifting = c.grounded && v > 9.f && std::fabs(c.slipAngle) > 0.26f && c.forwardSpeed() > 5.f;
    if (drifting) {
        driftScore += v * std::fabs(c.slipAngle) * dt * 14.f * driftMult;
        driftIdle = 0;
        driftMult = std::min(5.f, driftMult + dt * 0.25f);
    } else if (driftScore > 0) {
        driftIdle += dt;
        if (driftIdle > 1.6f) {
            int cr = int(driftScore / 4.f);
            if (cr >= 25) {
                credits += cr;
                toast("DRIFT  +" + fmtThousands(cr) + " CR", ACCENT);
                audio.beep(1250, 0.12f);
            }
            driftScore = 0;
            driftMult = 1;
        }
    }
    static float lastAir = 0;
    if (c.airTime > 0) lastAir = c.airTime;
    else if (lastAir > 0.55f) {
        int cr = int(lastAir * 300.f);
        credits += cr;
        toast("AIR " + std::to_string(int(lastAir * 10) / 10) + "." + std::to_string(int(lastAir * 10) % 10) + "s  +" + fmtThousands(cr) + " CR", vec4(0.5f, 0.85f, 1, 1));
        lastAir = 0;
    } else lastAir = 0;
}

// ====================================================================== camera
vec3 Game::showroomSpot() const {
    vec3 fwd(std::sin(world.hubYaw), 0, std::cos(world.hubYaw));
    vec3 p = world.hubPos - fwd * 12.f;
    p.y = world.hubPos.y + 0.48f;
    return p;
}

void Game::updateCamera(float dt) {
    int pw = R.width, ph = R.height;
    cam.aspect = float(pw) / float(ph);
    cam.nearZ = 0.25f;
    bool showroom = state == ST_DEALER || state == ST_GARAGE;
    Racer& tr = state == ST_TITLE ? titleCar : player;
    Vehicle& car = tr.car;
    vec3 fwd = car.forward();
    float v = car.speed();

    if (showroom) {
        vec3 c = showroomSpot();
        float a = stateTime * 0.22f + 0.6f;
        cam.pos = c + vec3(std::cos(a) * 7.5f, 2.0f, std::sin(a) * 7.5f);
        cam.target = c + vec3(0, 0.55f, 0);
        cam.fov = 0.85f;
        cam.update();
        return;
    }
    if (state == ST_TITLE) {
        vec3 f = car.forward(), rgt = car.right();
        float a = std::sin(stateTime * 0.08f) * 0.35f;
        vec3 off = f * (std::cos(a) * 5.8f) - rgt * (std::sin(a) * 5.8f + 2.6f);
        cam.pos = car.pos + off + vec3(0, 0.85f, 0);
        cam.pos.y = std::max(cam.pos.y, world.heightAt(cam.pos.x, cam.pos.z) + 0.5f);
        cam.target = car.pos + vec3(0, 0.75f, 0) - f * 0.4f - rgt * 1.6f;
        cam.fov = 0.82f;
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
    float velYaw = car.yaw;
    if (v > 4.f && car.forwardSpeed() > 0) velYaw = std::atan2(car.vel.x, car.vel.z);
    float targetYaw = lerpf(car.yaw, car.yaw + wrapAngle(velYaw - car.yaw), 0.5f);
    camYaw += wrapAngle(targetYaw - camYaw) * damp(4.5f, dt);
    float yaw = camYaw + (lookBack ? PI : 0.f);
    vec3 cf(std::sin(yaw), 0, std::cos(yaw));
    float kmh = v * 3.6f;
    float fovT = 1.08f + clampf(kmh / 300.f, 0.f, 1.f) * 0.32f;
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
        dist += clampf(kmh / 300.f, 0.f, 1.f) * 0.8f;
        float lyaw = yaw + lookYaw;
        float el = clampf(std::atan2(hgt - 0.95f, dist) + lookPitch, -0.08f, 1.35f);
        float D = std::sqrt(dist * dist + (hgt - 0.95f) * (hgt - 0.95f)) * lookZoom;
        vec3 pivot = car.pos + vec3(0, 0.95f, 0);
        vec3 back(std::sin(lyaw) * std::cos(el), -std::sin(el), std::cos(lyaw) * std::cos(el));
        cam.pos = pivot - back * D;
        cam.target = pivot + cf * (3.f * (1.f - orbit));
    } else {
        mat4 bm = car.bodyMatrix();
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
    float sh = shake * 0.12f + clampf((kmh - 200.f) / 200.f, 0.f, 1.f) * 0.015f;
    if (sh > 0) {
        cam.pos += vec3(std::sin(simTime * 53.f), std::sin(simTime * 61.f + 1.f), std::cos(simTime * 47.f)) * sh;
    }
    cam.update();
}

// ====================================================================== render
void Game::drawCar(Racer& r, Shader& s, bool glassPass) {
    s.set("uPaint", r.paint);
    s.set("uStripe", r.stripe);
    s.set("uBrake", r.car.brake > 0.1f ? 1.f : 0.f);
    s.set("uHeadlights", 0.f);
    s.set("uGlassPass", glassPass ? 1 : 0);
    mat4 bm = r.car.bodyMatrix();
    s.set("uModel", bm);
    r.model->body.draw();
    if (glassPass) return;
    s.set("uModel", bm);
    r.model->interior.draw();
    s.set("uModel", bm * r.model->steeringBase * rotateZ(r.car.wheelSteer * 6.f));
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
    R.atmo.fogDensity = 1.5f / drawDist;
    std::vector<Racer*> cars;
    if (state == ST_TITLE) cars.push_back(&titleCar);
    else {
        cars.push_back(&player);
        for (auto& b : bots) cars.push_back(&b);
    }
    // showroom / display cars on the hub podiums
    vec3 hf(std::sin(world.hubYaw), 0, std::cos(world.hubYaw)), hr(-hf.z, 0, hf.x);
    static const int displaySpecs[3] = {4, 9, 6};
    std::vector<Racer> display(3);
    for (int i = 0; i < 3; i++) {
        int sidx = displaySpecs[i];
        if (i == 1 && showroom) {
            sidx = state == ST_DEALER ? subSel : garage[std::clamp(subSel, 0, int(garage.size()) - 1)].spec;
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
        world.rocks.draw(eye, range * 0.7f, range);
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
            world.propsMesh.draw();
            for (Racer* r : cars) {
                R.depth.set("uModel", r->car.bodyMatrix());
                r->model->body.draw();
                for (int i = 0; i < 4; i++) {
                    R.depth.set("uModel", r->car.wheelMatrix(*r->model, i));
                    r->model->wheel.draw();
                }
            }
            R.depth.set("uModel", mat4());
            R.depthTree.use();
            drawTrees(R.depthTree, focus, ext[cas] * 1.45f, cas == 0 ? 400.f : 0.f);
        }
        R.endShadowPasses();
    }

    R.beginScene();
    R.drawSky(c);

    vec3 camFwd = normalize(vec3(c.target.x - c.pos.x, 0, c.target.z - c.pos.z));
    // terrain
    R.setCommon(R.terrain, c);
    R.terrain.set("uWater", world.waterLevel);
    for (auto& ch : world.chunks) {
        float dx = ch.center.x - c.pos.x, dz = ch.center.z - c.pos.z;
        float d2 = dx * dx + dz * dz;
        float lim = drawDist + 110.f;
        if (d2 > lim * lim) continue;
        if (d2 > 160.f * 160.f && dot(normalize(vec3(dx, 0, dz)), camFwd) < -0.45f) continue;
        ch.mesh.draw();
    }
    // roads
    R.setCommon(R.road, c);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -2.f);
    world.roadMesh.draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
    // hub, props, cars
    R.setCommon(R.object, c);
    R.object.set("uPaint", vec3(1, 1, 1));
    R.object.set("uStripe", vec3(1, 1, 1));
    R.object.set("uBrake", 0.f);
    R.object.set("uHeadlights", 0.f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -3.f);
    world.hubMesh.draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
    R.object.set("uGlassPass", 0);
    glDisable(GL_CULL_FACE);
    world.propsMesh.draw();
    for (Racer* r : cars) drawCar(*r, R.object);
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
    // water
    R.setCommon(R.water, c);
    world.waterMesh.draw();
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
        R.unlit.set("uModel", m * scale(vec3(r->car.spec->width * 0.68f, 1, r->car.spec->length * 0.6f)));
        blobMesh.draw();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (raceEvent >= 0 && gateMesh.count) {
        R.unlit.set("uFog", 1);
        R.unlit.set("uTint", vec4(1, 1, 1, 1));
        R.unlit.set("uModel", mat4());
        gateMesh.draw();
    }
    if (state == ST_FREEROAM || state == ST_PAUSE || state == ST_MAP) {
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
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Game::render() {
    bool showroom = state == ST_DEALER || state == ST_GARAGE;
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
    R.endScene(blur, flash);
    hud.begin(R.width, R.height);
    drawHud();
    hud.end();
}

// ====================================================================== HUD
std::string Game::speedStr(float ms) const {
    return std::to_string(int(ms * (settings.mph ? 2.23694f : 3.6f) + 0.5f));
}

void Game::drawSpeedo() {
    float u = hud.ui;
    vec2 c(hud.w - 125 * u, hud.h - 118 * u);
    float R0 = 92 * u;
    Vehicle& car = player.car;
    hud.circle(c, R0 + 12 * u, vec4(0.02f, 0.02f, 0.03f, 0.5f), 48);
    const int N = 40;
    float a0 = PI * 0.75f, a1 = PI * 2.25f;
    float frac = clampf(car.rpm / car.spec->redline, 0.f, 1.f);
    for (int i = 0; i < N; i++) {
        float t0 = i / float(N), t1 = (i + 0.72f) / float(N);
        bool lit = t0 < frac;
        bool red = t0 >= 0.84f;
        vec4 col = lit ? (red ? vec4(1.f, 0.18f, 0.12f, 1) : vec4(1, 1, 1, 0.95f)) : (red ? vec4(1.f, 0.2f, 0.15f, 0.25f) : vec4(1, 1, 1, 0.14f));
        float rin = R0 - 10 * u - (lit ? 2 * u : 0);
        hud.arc(c, rin, R0, lerpf(a0, a1, t0), lerpf(a0, a1, t1), col, 2);
    }
    int marks = int(car.spec->redline / 1000.f);
    for (int k = 0; k <= marks; k++) {
        float t = k * 1000.f / car.spec->redline;
        float a = lerpf(a0, a1, t);
        vec2 p = c + vec2(std::cos(a), std::sin(a)) * (R0 - 24 * u);
        hud.text(p.x, p.y - 7 * u, 13 * u, std::to_string(k), DIM, 1, false);
    }
    hud.text(c.x, c.y - 30 * u, 50 * u, speedStr(car.speed()), WHITE, 1);
    hud.text(c.x, c.y + 22 * u, 12 * u, settings.mph ? "MPH" : "KM/H", DIM, 1, false);
    std::string g = car.gear < 0 ? "R" : (car.speed() < 0.5f && car.throttle < 0.05f ? "N" : std::to_string(car.gear));
    hud.rect(c.x - 15 * u, c.y + 40 * u, 30 * u, 30 * u, vec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.9f));
    hud.text(c.x, c.y + 41 * u, 24 * u, g, WHITE, 1, false);
}

void Game::drawMinimap() {
    float u = hud.ui;
    vec2 c(118 * u, hud.h - 118 * u);
    float Rr = 96 * u;
    Vehicle& car = player.car;
    float yaw = camYaw;
    vec3 fwd(std::sin(yaw), 0, std::cos(yaw)), rgt(-std::cos(yaw), 0, std::sin(yaw));
    float sc = Rr / 260.f;  // px per metre
    auto toScreen = [&](vec3 p) {
        vec3 d = p - car.pos;
        return c + vec2(dot(d, rgt), -dot(d, fwd)) * sc;
    };
    hud.circle(c, Rr + 5 * u, vec4(0.02f, 0.02f, 0.03f, 0.75f), 48);
    hud.clip(c.x, c.y, Rr);
    vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    vec2 sp[4], uv[4];
    float S = Rr * 1.5f;
    for (int i = 0; i < 4; i++) {
        vec2 o = corners[i] * S;
        sp[i] = c + o;
        vec3 wp = car.pos + rgt * (o.x / sc) + fwd * (-o.y / sc);
        uv[i] = vec2((wp.x + World::HALF) / (2 * World::HALF), (World::HALF - wp.z) / (2 * World::HALF));
    }
    hud.mapQuad(sp, uv, vec4(0.95f, 0.95f, 0.95f, 0.92f));
    if (raceEvent >= 0) {
        const Route& rt = events[raceEvent].route;
        vec2 prev;
        bool havePrev = false;
        for (size_t i = 0; i < rt.pts.size(); i += 3) {
            vec2 p = toScreen(rt.pts[i]);
            if (havePrev && length(p - c) < Rr * 1.6f) hud.line(prev, p, 4 * u, vec4(1.f, 0.5f, 0.1f, 0.95f));
            prev = p;
            havePrev = true;
        }
        for (Racer* r : racers) {
            if (r->isPlayer) continue;
            vec2 p = toScreen(r->car.pos);
            hud.circle(p, 4.5f * u, vec4(0.1f, 0.1f, 0.1f, 1), 10);
            hud.circle(p, 3.2f * u, vec4(1.f, 0.85f, 0.2f, 1), 10);
        }
    } else {
        for (auto& e : events) {
            vec2 p = toScreen(e.markerPos);
            vec2 d = p - c;
            float l = length(d);
            if (l > Rr - 8 * u) p = c + d * ((Rr - 8 * u) / l);
            hud.circle(p, 6.5f * u, vec4(0.05f, 0.05f, 0.05f, 1), 12);
            hud.circle(p, 5 * u, eventColor(e.type), 12);
        }
        vec2 hp = toScreen(world.hubPos);
        vec2 d = hp - c;
        if (length(d) > Rr - 8 * u) hp = c + d * ((Rr - 8 * u) / length(d));
        hud.circle(hp, 7 * u, vec4(0.05f, 0.05f, 0.05f, 1), 12);
        hud.circle(hp, 5.5f * u, vec4(1.f, 0.25f, 0.55f, 1), 12);
    }
    hud.noClip();
    // player arrow
    vec3 cfw = car.forward();
    vec2 f(dot(cfw, rgt), -dot(cfw, fwd));
    f = f * (1.f / std::max(length(f), 1e-4f));
    vec2 r(-f.y, f.x);
    hud.triangle(c + f * (10 * u), c - f * (7 * u) + r * (7 * u), c - f * (7 * u) - r * (7 * u), vec4(0, 0, 0, 0.8f));
    hud.triangle(c + f * (8 * u), c - f * (5.5f * u) + r * (5.5f * u), c - f * (5.5f * u) - r * (5.5f * u), WHITE);
    // north marker
    vec2 nd(dot(vec3(0, 0, 1), rgt), -dot(vec3(0, 0, 1), fwd));
    vec2 np = c + nd * (Rr + 1 * u);
    hud.circle(np, 9 * u, vec4(0.05f, 0.05f, 0.05f, 0.9f), 14);
    hud.text(np.x, np.y - 7 * u, 13 * u, "N", WHITE, 1, false);
}

void Game::drawMenuPanel(const std::string& title, const std::vector<std::string>& items, int sel, float x, float y, float w) {
    float u = hud.ui;
    float rowH = 38 * u;
    float hgt = 70 * u + rowH * items.size() + 14 * u;
    hud.rect(x, y, w, hgt, PANEL);
    hud.rect(x, y, 6 * u, hgt, ACCENT);
    hud.text(x + 26 * u, y + 18 * u, 28 * u, title, WHITE);
    for (size_t i = 0; i < items.size(); i++) {
        float ry = y + 66 * u + i * rowH;
        bool s = int(i) == sel;
        if (s) hud.rect(x + 6 * u, ry, w - 6 * u, rowH - 4 * u, vec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.85f));
        hud.text(x + 26 * u, ry + 7 * u, 20 * u, items[i], s ? WHITE : vec4(0.85f, 0.85f, 0.85f, 1), 0, false);
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
        case ST_SETTINGS: drawSettings(); return;
        case ST_RESULTS: drawResults(); return;
        default: break;
    }
    if (state == ST_RACE || state == ST_RACE_INTRO) drawRaceHud();
    else {
        // credits & car
        hud.rect(18 * u, 18 * u, 250 * u, 62 * u, PANEL);
        hud.rect(18 * u, 18 * u, 5 * u, 62 * u, ACCENT);
        hud.text(34 * u, 24 * u, 26 * u, "CR " + fmtThousands(credits), WHITE);
        hud.text(34 * u, 56 * u, 14 * u, carName(*player.car.spec) + "  |  Class " + std::string(1, player.car.spec->cls), DIM, 0, false);
        // prompts
        std::string prompt;
        if (nearEvent >= 0) {
            const EventDef& e = events[nearEvent];
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s  -  %s  -  %.1f km", e.name.c_str(), eventTypeName(e.type), e.totalLength() / 1000.f);
            float pw = std::max(hud.textWidth(22 * u, buf), hud.textWidth(16 * u, e.blurb)) + 60 * u;
            float px = hud.w * 0.5f - pw * 0.5f, py = hud.h - 190 * u;
            hud.rect(px, py, pw, 92 * u, PANEL);
            hud.rect(px, py, pw, 4 * u, eventColor(e.type));
            hud.text(hud.w * 0.5f, py + 12 * u, 22 * u, buf, WHITE, 1);
            hud.text(hud.w * 0.5f, py + 42 * u, 16 * u, e.blurb, DIM, 1, false);
            hud.text(hud.w * 0.5f, py + 64 * u, 16 * u, pad ? "Press (Y) to start" : "Press ENTER to start", ACCENT, 1, false);
        } else if (nearHub) {
            float pw = 380 * u, px = hud.w * 0.5f - pw * 0.5f, py = hud.h - 160 * u;
            hud.rect(px, py, pw, 62 * u, PANEL);
            hud.rect(px, py, pw, 4 * u, vec4(1.f, 0.25f, 0.55f, 1));
            hud.text(hud.w * 0.5f, py + 10 * u, 20 * u, "Festival Hub", WHITE, 1);
            hud.text(hud.w * 0.5f, py + 36 * u, 15 * u, pad ? "(Y) Dealership & Garage" : "ENTER  Dealership & Garage", ACCENT, 1, false);
        }
        hud.text(hud.w - 18 * u, 18 * u, 13 * u, pad ? "START menu   BACK map   RB camera" : "ESC menu   M map   C camera   R reset", vec4(1, 1, 1, 0.5f), 2, false);
    }
    // drift chain
    if (driftScore > 1.f) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "DRIFT  %s", fmtThousands(int(driftScore)).c_str());
        hud.text(hud.w * 0.5f, hud.h * 0.24f, 30 * u, buf, ACCENT, 1);
        char m[16];
        std::snprintf(m, sizeof(m), "x%.1f", driftMult);
        hud.text(hud.w * 0.5f, hud.h * 0.24f + 36 * u, 18 * u, m, WHITE, 1);
    }
    // toasts
    float ty = hud.h * 0.13f;
    for (auto& t : toasts) {
        float a = clampf(t.t * 2.f, 0.f, 1.f);
        vec4 col = t.col;
        col.w *= a;
        hud.text(hud.w * 0.5f, ty, 22 * u, t.text, col, 1);
        ty += 30 * u;
    }
    drawSpeedo();
    drawMinimap();
}

void Game::drawRaceHud() {
    float u = hud.ui;
    const EventDef& E = events[raceEvent];
    // position block
    hud.rect(18 * u, 18 * u, 210 * u, 96 * u, PANEL);
    hud.rect(18 * u, 18 * u, 5 * u, 96 * u, eventColor(E.type));
    hud.text(34 * u, 20 * u, 46 * u, std::to_string(playerPlace), WHITE);
    float pw = hud.textWidth(46 * u, std::to_string(playerPlace));
    hud.text(36 * u + pw, 28 * u, 20 * u, "/" + std::to_string(racers.size()), DIM, 0, false);
    hud.text(34 * u, 76 * u, 14 * u, "POSITION", DIM, 0, false);
    if (E.route.closed) {
        int lap = std::clamp(player.lap + 1, 1, E.laps);
        hud.text(214 * u, 26 * u, 22 * u, std::to_string(lap) + "/" + std::to_string(E.laps), WHITE, 2);
        hud.text(214 * u, 52 * u, 13 * u, "LAP", DIM, 2, false);
    } else {
        float rem = std::max(0.f, E.totalLength() - player.progress) / 1000.f;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f km", rem);
        hud.text(214 * u, 26 * u, 20 * u, buf, WHITE, 2);
        hud.text(214 * u, 52 * u, 13 * u, "TO GO", DIM, 2, false);
    }
    hud.text(214 * u, 76 * u, 16 * u, fmtTime(raceTime), WHITE, 2, false);
    // standings
    std::vector<Racer*> order = racers;
    std::sort(order.begin(), order.end(), [](Racer* a, Racer* b) {
        if (a->finished != b->finished) return a->finished;
        if (a->finished) return a->finishTime < b->finishTime;
        return a->progress > b->progress;
    });
    float sx = hud.w - 248 * u, sy = 18 * u;
    hud.rect(sx, sy, 230 * u, 14 * u + 26 * u * order.size(), PANEL);
    for (size_t i = 0; i < order.size(); i++) {
        float ry = sy + 8 * u + i * 26 * u;
        bool me = order[i]->isPlayer;
        if (me) hud.rect(sx, ry - 2 * u, 230 * u, 25 * u, vec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.8f));
        hud.text(sx + 12 * u, ry, 16 * u, std::to_string(i + 1), WHITE, 0, false);
        hud.text(sx + 38 * u, ry, 16 * u, order[i]->name, WHITE, 0, false);
        std::string gap;
        if (order[i]->finished) gap = fmtTime(order[i]->finishTime);
        else if (!me) {
            char b[24];
            std::snprintf(b, sizeof(b), "%+.0fm", order[i]->progress - player.progress);
            gap = b;
        }
        hud.text(sx + 218 * u, ry, 14 * u, gap, DIM, 2, false);
    }
    // countdown
    if (state == ST_RACE_INTRO) {
        if (stateTime < 2.5f) {
            hud.rect(0, hud.h * 0.36f, hud.w, 110 * u, vec4(0, 0, 0, 0.45f));
            hud.text(hud.w * 0.5f, hud.h * 0.36f + 14 * u, 40 * u, E.name, WHITE, 1);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  -  %.1f km  -  %s", eventTypeName(E.type), E.totalLength() / 1000.f,
                          E.route.closed ? (std::to_string(E.laps) + (E.laps > 1 ? " laps" : " lap")).c_str() : "point to point");
            hud.text(hud.w * 0.5f, hud.h * 0.36f + 66 * u, 18 * u, buf, DIM, 1, false);
        } else {
            int n = int(std::ceil(countdown));
            float f = countdown - std::floor(countdown);
            hud.text(hud.w * 0.5f, hud.h * 0.32f, (90 + f * 40) * u, std::to_string(std::max(n, 1)), WHITE, 1);
        }
    } else if (stateTime < 1.2f) {
        hud.text(hud.w * 0.5f, hud.h * 0.32f, 96 * u, "GO!", ACCENT, 1);
    }
    if (player.finished) hud.text(hud.w * 0.5f, hud.h * 0.3f, 60 * u, "FINISHED " + ordinal(playerPlace), WHITE, 1);
    if (offRouteTime > 1.5f) hud.text(hud.w * 0.5f, hud.h * 0.42f, 22 * u, pad ? "Off route! Press (X) to reset" : "Off route! Press R to reset", vec4(1, 0.5f, 0.3f, 1), 1);
}

int Game::nextRaceEvent() const {
    for (int i = 0; i < int(events.size()); i++)
        if (i >= int(bestPlace.size()) || bestPlace[i] != 1) return i;
    return 0;
}

void Game::drawPause() {
    float u = hud.ui;
    bool racingMenu = returnState == ST_RACE || returnState == ST_RACE_INTRO;
    if (!racingMenu) {
        hud.rectV(0, 0, hud.w, hud.h, vec4(0.02f, 0.02f, 0.03f, 0.82f), vec4(0.05f, 0.03f, 0.02f, 0.72f));
        // top bar
        float x0 = 56 * u, y0 = 34 * u, W = hud.w - 112 * u;
        hud.text(x0, y0, 34 * u, "RIDGELINE FESTIVAL", WHITE);
        hud.rect(x0, y0 + 46 * u, 120 * u, 4 * u, ACCENT);
        hud.text(x0 + W, y0 + 2 * u, 28 * u, "CR " + fmtThousands(credits), WHITE, 2);
        hud.text(x0 + W, y0 + 38 * u, 15 * u, carName(*player.car.spec) + "   |   Class " + std::string(1, player.car.spec->cls) +
                 "   |   " + std::to_string(racesWon) + " wins", DIM, 2, false);
        // tiles
        float gy = y0 + 84 * u, gap = 12 * u;
        float cw = (W - gap * 3) / 4.f, ch = std::min((hud.h - gy - 90 * u - gap) / 2.f, cw * 0.95f);
        for (int i = 0; i < kTileCount; i++) {
            const MenuTile& t = kTiles[i];
            float tx = x0 + t.x * (cw + gap), ty = gy + t.y * (ch + gap);
            float tw = t.w * cw + (t.w - 1) * gap, th = t.h * ch + (t.h - 1) * gap;
            bool sel = i == menuSel;
            hud.rect(tx, ty, tw, th, vec4(0.09f, 0.09f, 0.10f, 0.94f));
            if (i == 1) {  // live map preview
                vec2 pc = vec2((player.car.pos.x + World::HALF) / (2 * World::HALF), (World::HALF - player.car.pos.z) / (2 * World::HALF));
                float span = 0.32f * tw / th * 0.5f, spv = 0.32f * 0.5f;
                vec2 p[4] = {{tx, ty}, {tx + tw, ty}, {tx + tw, ty + th}, {tx, ty + th}};
                vec2 uv[4] = {{pc.x - span, pc.y - spv}, {pc.x + span, pc.y - spv}, {pc.x + span, pc.y + spv}, {pc.x - span, pc.y + spv}};
                hud.mapQuad(p, uv, vec4(0.85f, 0.85f, 0.85f, 1));
                hud.rectV(tx, ty + th * 0.45f, tw, th * 0.55f, vec4(0, 0, 0, 0), vec4(0, 0, 0, 0.85f));
                vec2 me(tx + tw * 0.5f, ty + th * 0.5f);
                hud.circle(me, 7 * u, vec4(0, 0, 0, 1), 14);
                hud.circle(me, 5 * u, WHITE, 14);
            } else {
                hud.rectV(tx, ty, tw, th, vec4(t.accent.x, t.accent.y, t.accent.z, 0.30f), vec4(t.accent.x * 0.2f, t.accent.y * 0.2f, t.accent.z * 0.2f, 0.2f));
            }
            hud.rect(tx, ty + th - 5 * u, tw, 5 * u, t.accent);
            std::string sub = t.sub;
            if (i == 4) {
                const EventDef& E = events[nextRaceEvent()];
                sub = E.name;
            }
            hud.text(tx + 18 * u, ty + th - 78 * u, 28 * u, t.label, WHITE);
            hud.text(tx + 18 * u, ty + th - 38 * u, 14 * u, sub, vec4(1, 1, 1, 0.75f), 0, false);
            if (sel) {
                float b = 3 * u;
                hud.rect(tx - b, ty - b, tw + 2 * b, b, WHITE);
                hud.rect(tx - b, ty + th, tw + 2 * b, b, WHITE);
                hud.rect(tx - b, ty, b, th, WHITE);
                hud.rect(tx + tw, ty, b, th, WHITE);
            }
        }
        hud.text(hud.w * 0.5f, hud.h - 46 * u, 15 * u, pad ? "D-pad move   (A) select   (B) back" : "Arrows move   ENTER select   ESC back", DIM, 1, false);
        float tyy = hud.h * 0.8f;
        for (auto& t : toasts) { hud.text(hud.w * 0.5f, tyy, 20 * u, t.text, t.col, 1); tyy += 28 * u; }
        return;
    }
    hud.rect(0, 0, hud.w, hud.h, vec4(0, 0, 0, 0.35f));
    bool racing = returnState == ST_RACE || returnState == ST_RACE_INTRO;
    drawMenuPanel(racing ? "PAUSED" : "FESTIVAL", pauseItems(racing), menuSel, 60 * u, 120 * u, 360 * u);
    hud.rect(hud.w - 330 * u, 120 * u, 270 * u, 150 * u, PANEL);
    hud.text(hud.w - 310 * u, 134 * u, 26 * u, "CR " + fmtThousands(credits), WHITE);
    hud.text(hud.w - 310 * u, 172 * u, 15 * u, "Cars owned: " + std::to_string(garage.size()), DIM, 0, false);
    hud.text(hud.w - 310 * u, 196 * u, 15 * u, "Race wins: " + std::to_string(racesWon), DIM, 0, false);
    hud.text(hud.w - 310 * u, 220 * u, 15 * u, carName(*player.car.spec), DIM, 0, false);
    hud.text(hud.w * 0.5f, hud.h - 40 * u, 14 * u, pad ? "(A) select   (B) back" : "ENTER select   ESC back", DIM, 1, false);
}

void Game::drawMap() {
    float u = hud.ui;
    hud.rect(0, 0, hud.w, hud.h, vec4(0.03f, 0.03f, 0.04f, 0.92f));
    float S = std::min(hud.h - 120 * u, hud.w * 0.58f);
    float mx = 50 * u, my = (hud.h - S) * 0.5f;
    vec2 p[4] = {{mx, my}, {mx + S, my}, {mx + S, my + S}, {mx, my + S}};
    vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    hud.mapQuad(p, uv, WHITE);
    auto toMap = [&](vec3 w) { return vec2(mx + (w.x + World::HALF) / (2 * World::HALF) * S, my + (World::HALF - w.z) / (2 * World::HALF) * S); };
    for (int i = 0; i < int(events.size()); i++) {
        vec2 q = toMap(events[i].markerPos);
        bool sel = i == subSel;
        hud.circle(q, (sel ? 11 : 8) * u, vec4(0.05f, 0.05f, 0.05f, 1), 14);
        hud.circle(q, (sel ? 9 : 6) * u, eventColor(events[i].type), 14);
        if (sel) hud.text(q.x + 14 * u, q.y - 10 * u, 16 * u, events[i].name, WHITE);
    }
    vec2 hq = toMap(world.hubPos);
    hud.circle(hq, 10 * u, vec4(0.05f, 0.05f, 0.05f, 1), 14);
    hud.circle(hq, 8 * u, vec4(1.f, 0.25f, 0.55f, 1), 14);
    if (subSel == int(events.size())) hud.text(hq.x + 14 * u, hq.y - 10 * u, 16 * u, "Festival Hub", WHITE);
    // player
    vec2 pp = toMap(player.car.pos);
    vec2 f(std::sin(player.car.yaw), -std::cos(player.car.yaw));
    vec2 r(-f.y, f.x);
    hud.triangle(pp + f * (11 * u), pp - f * (7 * u) + r * (7 * u), pp - f * (7 * u) - r * (7 * u), vec4(0, 0, 0, 1));
    hud.triangle(pp + f * (9 * u), pp - f * (5 * u) + r * (5 * u), pp - f * (5 * u) - r * (5 * u), WHITE);
    // list
    float lx = mx + S + 40 * u, lw = hud.w - lx - 40 * u;
    hud.text(lx, my, 30 * u, "WORLD MAP", WHITE);
    for (int i = 0; i <= int(events.size()); i++) {
        float ry = my + 50 * u + i * 34 * u;
        bool sel = i == subSel;
        if (sel) hud.rect(lx - 10 * u, ry - 4 * u, lw, 32 * u, vec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.85f));
        if (i < int(events.size())) {
            hud.circle(vec2(lx + 4 * u, ry + 11 * u), 5 * u, eventColor(events[i].type), 10);
            hud.text(lx + 18 * u, ry, 17 * u, events[i].name, WHITE, 0, false);
            int bp = i < int(bestPlace.size()) ? bestPlace[i] : 0;
            if (bp) hud.text(lx + lw - 20 * u, ry, 15 * u, "Best: " + ordinal(bp), DIM, 2, false);
        } else {
            hud.circle(vec2(lx + 4 * u, ry + 11 * u), 5 * u, vec4(1.f, 0.25f, 0.55f, 1), 10);
            hud.text(lx + 18 * u, ry, 17 * u, "Festival Hub", WHITE, 0, false);
        }
    }
    if (subSel < int(events.size())) {
        const EventDef& e = events[subSel];
        float iy = my + 60 * u + (events.size() + 1) * 34 * u;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s  |  %.1f km  |  1st prize CR %s", eventTypeName(e.type), e.totalLength() / 1000.f, fmtThousands(e.prize[0]).c_str());
        hud.text(lx, iy, 15 * u, buf, ACCENT, 0, false);
        hud.text(lx, iy + 24 * u, 15 * u, e.blurb, DIM, 0, false);
    }
    hud.text(hud.w * 0.5f, hud.h - 36 * u, 14 * u, pad ? "(A) fast travel   (B) close" : "ENTER fast travel   ESC close", DIM, 1, false);
}

void Game::drawDealer(bool garageMode) {
    float u = hud.ui;
    float x = 40 * u, y = 40 * u, w = 380 * u;
    int n = garageMode ? int(garage.size()) : int(kCars.size());
    float rowH = 40 * u;
    int visible = std::min(n, int((hud.h - 200 * u) / rowH));
    int first = std::clamp(subSel - visible / 2, 0, std::max(0, n - visible));
    hud.rect(x, y, w, 64 * u + visible * rowH, PANEL);
    hud.rect(x, y, 6 * u, 64 * u + visible * rowH, ACCENT);
    hud.text(x + 24 * u, y + 16 * u, 28 * u, garageMode ? "MY GARAGE" : "DEALERSHIP", WHITE);
    for (int k = 0; k < visible; k++) {
        int i = first + k;
        const CarSpec& s = kCars[garageMode ? garage[i].spec : i];
        float ry = y + 62 * u + k * rowH;
        bool sel = i == subSel;
        if (sel) hud.rect(x + 6 * u, ry, w - 6 * u, rowH - 4 * u, vec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.85f));
        hud.rect(x + 20 * u, ry + 7 * u, 22 * u, 22 * u, vec4(0.1f, 0.1f, 0.12f, 1));
        hud.text(x + 31 * u, ry + 9 * u, 15 * u, std::string(1, s.cls), WHITE, 1, false);
        hud.text(x + 54 * u, ry + 9 * u, 17 * u, carName(s), WHITE, 0, false);
        std::string right;
        if (garageMode) right = i == current ? "DRIVING" : "";
        else {
            bool owned = false;
            for (auto& g : garage) owned |= g.spec == i;
            right = owned ? "OWNED" : (s.price == 0 ? "FREE" : fmtThousands(s.price));
        }
        hud.text(x + w - 14 * u, ry + 10 * u, 15 * u, right, sel ? WHITE : DIM, 2, false);
    }
    // stats panel
    const CarSpec& s = kCars[garageMode ? garage[subSel].spec : subSel];
    CarRatings rt = rateCar(s);
    float px = hud.w - 400 * u, py = 40 * u, pw = 360 * u;
    hud.rect(px, py, pw, 330 * u, PANEL);
    hud.text(px + 20 * u, py + 14 * u, 14 * u, s.make, DIM, 0, false);
    hud.text(px + 20 * u, py + 32 * u, 28 * u, s.model, WHITE);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "CLASS %c  |  PI %d", s.cls, rt.pi);
    hud.text(px + 20 * u, py + 70 * u, 15 * u, buf, ACCENT, 0, false);
    const char* labels[] = {"Speed", "Acceleration", "Handling", "Off-road"};
    float vals[] = {rt.speed, rt.accel, rt.handling, rt.offroad};
    for (int i = 0; i < 4; i++) {
        float ry = py + 104 * u + i * 34 * u;
        hud.text(px + 20 * u, ry, 15 * u, labels[i], WHITE, 0, false);
        hud.rect(px + 150 * u, ry + 4 * u, 180 * u, 12 * u, vec4(1, 1, 1, 0.12f));
        hud.rect(px + 150 * u, ry + 4 * u, 180 * u * vals[i] / 10.f, 12 * u, ACCENT);
    }
    const char* drv[] = {"FWD", "RWD", "AWD"};
    std::snprintf(buf, sizeof(buf), "%d kW (%d hp)  |  %d kg  |  %s", int(s.powerKW), int(s.powerKW * 1.341f), int(s.massKg), drv[s.drive]);
    hud.text(px + 20 * u, py + 248 * u, 15 * u, buf, DIM, 0, false);
    std::snprintf(buf, sizeof(buf), "Top speed %s %s", speedStr(s.topKmh / 3.6f).c_str(), settings.mph ? "mph" : "km/h");
    hud.text(px + 20 * u, py + 272 * u, 15 * u, buf, DIM, 0, false);
    int paint = garageMode ? garage[subSel].paint : dealerPaint;
    hud.rect(px + 20 * u, py + 298 * u, 18 * u, 18 * u, vec4(kPaints[paint].x, kPaints[paint].y, kPaints[paint].z, 1));
    hud.text(px + 46 * u, py + 298 * u, 15 * u, std::string("< ") + kPaintNames[paint] + " >", WHITE, 0, false);
    // credits & hints
    hud.rect(hud.w - 400 * u, py + 340 * u, pw, 46 * u, PANEL);
    hud.text(hud.w - 380 * u, py + 350 * u, 22 * u, "CR " + fmtThousands(credits), WHITE);
    std::string hint;
    if (garageMode) hint = pad ? "(A) drive   LEFT/RIGHT paint   (X) stripes   (B) back" : "ENTER drive   LEFT/RIGHT paint   X stripes   ESC back";
    else hint = pad ? "(A) buy   LEFT/RIGHT paint   (B) back" : "ENTER buy   LEFT/RIGHT paint   ESC back";
    hud.rect(0, hud.h - 46 * u, hud.w, 46 * u, vec4(0, 0, 0, 0.6f));
    hud.text(hud.w * 0.5f, hud.h - 34 * u, 16 * u, hint, WHITE, 1, false);
    float ty = hud.h * 0.75f;
    for (auto& t : toasts) { hud.text(hud.w * 0.5f, ty, 22 * u, t.text, t.col, 1); ty += 30 * u; }
}

void Game::drawSettings() {
    float u = hud.ui;
    hud.rect(0, 0, hud.w, hud.h, vec4(0, 0, 0, 0.35f));
    drawMenuPanel("SETTINGS", settingsItems(), subSel, 60 * u, 100 * u, 440 * u);
    hud.text(hud.w * 0.5f, hud.h - 40 * u, 14 * u, pad ? "LEFT/RIGHT change   (B) back" : "LEFT/RIGHT change   ESC back", DIM, 1, false);
}

void Game::drawResults() {
    float u = hud.ui;
    const EventDef& E = events[raceEvent];
    float w = 640 * u, x = hud.w * 0.5f - w * 0.5f, y = 80 * u;
    float hgt = 120 * u + racers.size() * 34 * u + 56 * u;
    hud.rect(x, y, w, hgt, PANEL);
    hud.rect(x, y, w, 5 * u, eventColor(E.type));
    hud.text(x + 24 * u, y + 18 * u, 18 * u, E.name, DIM, 0, false);
    hud.text(x + 24 * u, y + 42 * u, 40 * u, playerPlace == 1 ? "YOU WIN!" : ("FINISHED " + ordinal(playerPlace)), playerPlace == 1 ? ACCENT : WHITE);
    for (size_t i = 0; i < racers.size(); i++) {
        float ry = y + 112 * u + i * 34 * u;
        Racer* r = racers[i];
        if (r->isPlayer) hud.rect(x, ry - 4 * u, w, 32 * u, vec4(ACCENT.x, ACCENT.y, ACCENT.z, 0.8f));
        hud.text(x + 24 * u, ry, 18 * u, ordinal(int(i) + 1), WHITE, 0, false);
        hud.text(x + 90 * u, ry, 18 * u, r->name, WHITE, 0, false);
        hud.text(x + 230 * u, ry, 16 * u, carName(*r->car.spec), DIM, 0, false);
        hud.text(x + w - 24 * u, ry, 18 * u, fmtTime(r->finishTime), WHITE, 2, false);
    }
    float by = y + 120 * u + racers.size() * 34 * u + 10 * u;
    hud.text(x + 24 * u, by, 22 * u, "Prize: CR " + fmtThousands(prizeWon), ACCENT);
    hud.text(x + w - 24 * u, by + 4 * u, 16 * u, pad ? "(A) continue" : "ENTER continue", WHITE, 2, false);
}

void Game::drawTitle() {
    float u = hud.ui;
    hud.rectV(0, hud.h * 0.55f, hud.w, hud.h * 0.45f, vec4(0, 0, 0, 0), vec4(0, 0, 0, 0.55f));
    float rx = hud.w - 60 * u, by = hud.h - 70 * u;
    hud.text(rx, by - 150 * u, 74 * u, "RIDGELINE", WHITE, 2);
    float tw = hud.textWidth(74 * u, "RIDGELINE");
    hud.rect(rx - tw, by - 62 * u, tw, 4 * u, ACCENT);
    hud.text(rx, by - 50 * u, 15 * u, "OPEN-WORLD FESTIVAL RACING", vec4(1, 1, 1, 0.8f), 2, false);
    float blink = 0.55f + 0.45f * std::sin(stateTime * 3.f);
    hud.text(rx, by - 16 * u, 20 * u, pad ? "PRESS (A)" : "PRESS ENTER", vec4(1, 1, 1, blink), 2);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "CR %s   |   %d %s   |   %d %s", fmtThousands(credits).c_str(), int(garage.size()), garage.size() == 1 ? "car" : "cars",
                  racesWon, racesWon == 1 ? "win" : "wins");
    hud.text(56 * u, hud.h - 56 * u, 15 * u, buf, vec4(1, 1, 1, 0.7f), 0, false);
    hud.text(56 * u, hud.h - 34 * u, 12 * u, std::string(pad ? "(B) quit" : "ESC quit") + "   |   v2.1  |  free & open source (MIT)", vec4(1, 1, 1, 0.45f), 0, false);
}

void Game::saveShot(const std::string& path) {
    int w = R.width, h = R.height;
    std::vector<uint8_t> px(size_t(w) * h * 4), flip(size_t(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) std::memcpy(&flip[size_t(y) * w * 4], &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, flip.data(), w * 4);
    if (s) {
        if (!SDL_SaveBMP(s, path.c_str())) SDL_Log("Screenshot failed: %s", SDL_GetError());
        SDL_DestroySurface(s);
    }
}

// ====================================================================== main loop
void Game::run() {
    Uint64 last = SDL_GetPerformanceCounter();
    double freq = double(SDL_GetPerformanceFrequency());
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = float((now - last) / freq);
        last = now;
        if (!opt.shot.empty()) dt = 1.f / 60.f;
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
