// Top-level game: window, state machine, menus, races, camera, saving.
#pragma once
#include "audio.h"
#include "hud.h"
#include "race.h"
#include "gps.h"
#include "effects.h"
#include <SDL3/SDL.h>
#include <string>
#include <functional>
#include <map>
#include <vector>

enum FestStat { FS_WIN, FS_RACE, FS_RAMP, FS_SKILL, FS_SPEED, FS_KM, FS_CAT, FS_STARS };
enum State { ST_TITLE, ST_FREEROAM, ST_PAUSE, ST_MAP, ST_DEALER, ST_GARAGE, ST_SETTINGS, ST_RACE_INTRO, ST_RACE, ST_RESULTS, ST_PHOTO, ST_WHEELSPIN, ST_UPGRADE, ST_SWITCH, ST_CUSTOM, ST_EVENTLAB, ST_MYEVENTS };
enum Action { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_CONFIRM, A_BACK, A_PAUSE, A_CAMERA, A_RESET, A_MAP, A_INTERACT, A_TOGGLE, A_SELL, A_PHOTO, A_UPGRADE, A_HIDE, A_SWITCH };

// race furniture placed along the route (gates.cpp)
struct Gate {
    float d = 0;             // distance along the route
    bool finish = false, start = false;
    vec3 p, t;
    float hw = 0;
    vec3 lamps[5];           // countdown lamps (start gantry)
};

struct OwnedCar {
    int spec; int paint; bool stripes;
    int up[3] = {0, 0, 0};  // upgrades: engine, weight, tyres
    // cosmetics
    int livery = 0, livCol = 0, tint = 0, aero = 0, ride = 0, number = 0;
    int camber = 0, camberOwned = 0;  // fitted camber kit level, highest level bought
};
constexpr int kLiveryCount = 15, kTintCount = 4, kAeroCount = 4, kRideCount = 3, kCamberCount = 4;
extern const char* kCamberNames[];
extern const int kCamberPrice[];
extern const char* kLiveryNames[];
extern const char* kTintNames[];
extern const char* kAeroNames[];
extern const char* kRideNames[];

struct PRStunt {
    int type = 0;            // 0 speed trap, 1 speed zone
    std::string name;
    int road = 0, a = 0, b = 0;
    float thr[3] = {0, 0, 0}; // km/h for 1..3 stars
    float best = 0;
};

struct Settings {
    bool mph = false;
    bool motionBlur = true;
    bool shadows = true;
    int msaa = 8;
    bool fullscreen = false;
    int volume = 80;
    int drawDist = 3;   // 0 low, 1 medium, 2 high, 3 ultra
    bool vsync = true;
    bool assists = true;
    int grass = 2;  // 0 off, 1 low, 2 high
    int weather = 0;  // 0 dynamic, 1 always clear, 2 always rain, 3 wet roads without rain
    int timeMode = 0;     // 0 day/night cycle, 1 morning, 2 sunset, 3 night, 4 noon
    int brightness = 100, contrast = 100, saturation = 100, warmth = 0;  // display colour (percent / -10..10)
    int colorBlind = 0;   // 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia
    int steerLock = 2;
    int racingLine = 1;   // Forza-style driving line in races
    bool carReflections = true;  // cars reflect the world around them (environment probe)  // cockpit wheel rotation lock to lock: 0 180, 1 360, 2 540, 3 900 degrees
};

struct Toast { std::string text; vec4 col; float t; };

struct Options {
    std::string shot;      // screenshot path (testing)
    int frames = 0;        // quit after N frames
    std::string scene;     // title|drive|race|dealer|map|garage|results
    int event = 0;
    int car = -1;
    int width = 1280, height = 720;
    float simSeconds = 0;  // simulate before shot
    float look = 0;        // initial camera orbit (testing)
    int cam = -1;          // initial camera mode (testing)
    std::string at;        // "x,z,yaw" spawn point for --scene drive (testing)
    std::string recordCmd; // pipe raw RGBA frames (fixed 30 fps) into this command (e.g. ffmpeg)
    std::string recordAudio; // write the engine audio (f32 stereo 48 kHz) to this file
    float recordSec = 3.f;
    float orbit = 0.f;     // photo mode: auto-orbit speed (rad/s)
    bool noHints = false;
    bool noHud = false;
    bool wet = false;      // --wet: wet roads
};

class Game {
public:
    bool init(const Options& o);
    void run();
    void shutdown();

private:
    // platform
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;
    SDL_Gamepad* pad = nullptr;
    Options opt;
    bool running = true;
    int frameNo = 0;

    // systems
    Renderer R;
    World world;
    Hud hud;
    Audio audio;
    std::vector<CarModel> models;
    std::vector<struct ImportedModel*> importedModels;
    std::vector<EventDef> events;
    Mesh blobMesh, tyreBlobMesh, beaconMesh, ringMesh, gateMesh, boostMesh;

    // player / progression
    Racer player;
    std::vector<OwnedCar> garage;
    int current = 0;
    long long credits = 25000;
    std::vector<int> bestPlace;   // per event, 0 = never finished
    int racesWon = 0;
    Settings settings;
    std::string prefPath;

    // state
    State state = ST_TITLE, returnState = ST_FREEROAM;
    int menuSel = 0, subSel = 0;
    float stateTime = 0;
    float simTime = 0;

    // race
    int raceEvent = -1;
    std::vector<Racer> bots;
    std::vector<Racer*> racers;
    float raceTime = 0, countdown = 0;
    int playerPlace = 0;
    int prizeWon = 0;
    float offRouteTime = 0;
    float finishDelay = 0;

    // camera
    Camera cam;
    int camMode = 0;
    float camYaw = 0;
    float mapRangeSm = 260.f;
    const Camera* drawCam = nullptr;  // camera of the pass being rendered (main or reflection)
    // menu art: rendered once from the live world
    GLuint menuThumb[8] = {};
    bool menuThumbsBuilt = false, pauseShotPending = false;
    std::vector<GLuint> carThumb;     // per spec, lazily rendered
    std::vector<Racer*> thumbCars;
    bool thumbActive = false;
    std::vector<int> dealerOrder;     // dealership listing order (by class/price)
    int sellConfirm = -1;             // garage slot awaiting a second sell press
    int dealerSpec() const { return dealerOrder.empty() ? 0 : dealerOrder[std::clamp(subSel, 0, int(dealerOrder.size()) - 1)]; }
    GLuint captureThumb(const Camera& c, const Atmosphere* atmo, float aspect);
    void buildMenuThumbs();
    void buildCarThumb(int spec);
    Racer makeShowCar(int spec, int paint, vec3 pos, float yaw);
    void photoSpot(vec3& carPos, float& carYaw, vec3& camPos, vec3& camTarget, float len);
    void drawCarCards(bool garageMode, float y, float h);
    int sellValue(const OwnedCar& oc) const;
    vec3 camPos;
    float shake = 0;
    bool lookBack = false;
    float fovSm = 1.1f;
    float lookYaw = 0, lookPitch = 0, lookZoom = 1.f, lookIdle = 10.f;
    bool padLooking = false;
    float titleCut = 0;
    int titleShot = 0;
    Racer titleCar;
    Atmosphere dayAtmo, titleAtmo;

    // misc runtime
    float physAcc = 0;
    std::vector<Controls> ctl;     // per racer (race) controls
    Controls playerCtl;
    bool autopilot = false;
    bool devDonut = false;
    bool keyArt = false;
    int nearEvent = -1;
    // ---- progression & Horizon-style features (features.cpp)
    int xp = 0, level = 1, wheelspins = 0;
    std::vector<CarSpec> tuned;                 // per garage slot, upgrades applied
    std::vector<PRStunt> stunts;
    Mesh stuntMesh;
    int zoneActive = -1;
    float zoneTime = 0, zoneDist = 0;
    struct Popup { std::string title, name, value; int stars = 0; bool best = false; float t = 0; vec4 col; };
    Popup popup;
    // skill chain
    float chainScore = 0, chainMult = 1, chainTimer = 0, chainFlash = 0;
    std::vector<std::pair<std::string, int>> chainSkills;
    float speedSkillAcc = 0, lastAirT = 0, nearMissCool = 0;
    // rewind
    struct Snapshot { std::vector<Racer> racers; float raceTime; float camYaw; };
    std::vector<Snapshot> rewindBuf;
    int rewindHead = 0, rewindCount = 0, rewindTick = 0;
    bool rewinding = false;
    // photo mode
    float photoYaw = 0, photoPitch = 0.2f, photoDist = 6.f, photoFov = 0.9f;
    bool photoHideUi = false;
    State photoReturn = ST_FREEROAM;
    // wheelspin
    struct SpinPrize { int credits = 0; int car = -1; };
    std::vector<SpinPrize> spinReel;
    float spinT = -1, spinPos = 0;
    int spinResult = -1;
    // upgrades screen
    int upSel = 0;
    int custSel = 0;
    // weather
    float wxRain = 0, wxTarget = 0, wxWet = 0, wxTimer = 180;
    void updateWeather(float dt);
    // race furniture (gates.cpp)
    std::vector<Gate> gates;
    std::vector<float> gateFlash;
    std::vector<Mesh> gateGlow;
    Mesh gateLit, gatePaint, lampMesh, lineMesh;
    float slowmo = 0;
    static float rampHalfW(const EventDef& E) { return std::min(E.route.width * 0.3f, 3.6f); }
    // festival playlist, streak, lucky cats, big wheel (festival.cpp)
    struct LuckyCat { vec3 p; float yaw; };
    static constexpr size_t kCatCount = 60;
    std::vector<LuckyCat> cats;
    std::vector<uint8_t> catHit;
    Mesh catMesh, ferrisMesh, ferrisStatic, gondolaMesh, festSite;
    float fwTimer = 0;
    bool catDirty = true;
    int festDay = -1, festDone = 0, festPick[3] = {0, 1, 2}, streak = 0, lastDay = -1;
    float festProg[3] = {0, 0, 0}, festShow = 0, festKmAcc = 0, ferrisAng = 0, boatT = 0;
    Mesh riverboatMesh, swanMesh;
    vec3 lastStreamPos{1e9f, 0, 1e9f};  // boats that move: one along the canal, swans round the lake torii
    double kmDriven = 0;
    vec3 ferrisPos, ferrisAxis = vec3(1, 0, 0);
    const char* festText(int k) const;
    void festNewDay();
    void festEvent(int type, float amount);
    void buildFestival();
    void rebuildCatMesh();
    void updateFestival(float dt);
    void drawFestivalWorld(const Camera& c);
    void drawFestivalHud();
    void drawCatsOnMinimap(vec2 c, float Rr, std::function<vec2(vec3)> toScreen);
    std::vector<uint8_t> railSide[2];
    float railOff = 7.f;
    void buildRails(const EventDef& E, MeshBuilder& lit, MeshBuilder& paint);
    void railCollide(Racer& r);
    int gateIdx = 0;
    std::string gateBanner;
    float gateBannerT = 0;
    Camera reflCamera;  // mirrored camera of the planar reflection pass
    float groundAtRoute(const Route& rt, float d, vec3 p) const;
    void buildGates(const EventDef& E);
    void clearGates();
    float gateAbs(int n) const;
    void updateGates(float dt);
    void drawGateFx();
    void drawRacingLine(const EventDef& E);
    void drawGateHud();
    float todHours = 10.5f;  // time of day
    Atmosphere timeOfDayAtmo(float h, float& hdriW, float& night) const;
    void updateTimeOfDay(float dt);
    std::string clockText() const;
    float tabSlideX = 0, tabSlideW = 0;  // pause hub tab underline animation
    int menuTab = 0;                  // pause hub tab
    std::vector<GLuint> houseThumb;   // menu shots of each house
    // EventLab
    std::vector<CustomEvent> customEvents;
    int builtinEvents = 0;
    CustomEvent labEv;
    int labSel = 0;
    EventDef labPreview;
    bool labOk = false;
    void rebuildCustomEvents();
    void openEventLab();
    bool importEventCode();
    void labInput(Action a);
    void drawEventLab();
    void myEventsInput(Action a);
    void drawMyEvents();
    void drawRouteMap(float x, float y, float S, const Route* rt, vec4 col);
    std::map<int, Mesh> aeroCache;   // spec*8 + kit -> wing / splitter mesh

    int xpToNext() const { return 1000 + (level - 1) * 400; }
    void addXP(int amount);
    CarSpec tunedSpec(const OwnedCar& oc) const;
    int showcaseSpec() const {  // priciest car in the dealership
        int best = -1;
        for (int i = 0; i < int(kCars.size()); i++)
            if (listed(kCars[i]) && (best < 0 || kCars[i].price > kCars[best].price)) best = i;
        return best < 0 ? 0 : best;
    }
    void rebuildTuned();
    int upgradeCost(const OwnedCar& oc, int part) const;
    void buildStunts();
    void updateStunts(float dt);
    void showPopup(const std::string& title, const std::string& name, const std::string& value, int stars, bool best, vec4 col);
    void chainAdd(const std::string& skill, int points);
    void chainBank();
    void chainBreak();
    void recordRewind();
    bool stepRewind();
    bool rewindHeld();
    void enterPhoto();
    void updatePhoto(float dt);
    void startWheelspin();
    void updateWheelspin(float dt);
    void drawPopup();
    void drawChain();
    void drawWheelspin();
    void drawUpgrades();
    // customization (custom.cpp)
    void applyCosmetics(Racer& r, const OwnedCar& oc) const;
    void setCosmeticUniforms(Shader& s, const Racer& r, bool body) const;
    const Mesh* aeroMesh(const Racer& r);
    mat4 bodyFrame(const Racer& r) const;
    mat4 steeringFrame(const Racer& r, vec3 c) const;
    void customInput(Action a);
    void drawCustomize();
    void drawPhoto();
    void drawStuntsOnMinimap(vec2 c, float Rr, std::function<vec2(vec3)> toScreen);
    bool photoCapture = false;

    Effects fx;
    // special cars (special.cpp)
    struct TimeMachine { float charge = 0, panel = 0, trailT = 0, jumpT = 99; int era = 0, lastEra = 0; bool armed = true; vec3 last[2]; } tm;
    Atmosphere eraAtmo[3];
    std::string songFile;
    bool catWasSpinning = false;
    std::string cheatBuf;
    bool cheatFocus = false;
    float cheatFlash = 0, cheatFail = 0;
    vec4 cheatRect;
    State switchFrom = ST_FREEROAM;
    int devRoad = -1;
    // scanned props (props.cpp)
    struct PropType { ImportedModel* model = nullptr; bool plant = false; float smin = 1, smax = 1; };
    struct PropInst { mat4 M; vec3 pos; int type; float radius; };
    std::vector<PropType> propTypes;
    std::vector<PropInst> propInst;
    bool rocksAsProps = false;
    void loadProps();
    void placeProps();
    void drawProps(const Camera& c, vec3 eye, float rockRange, float plantRange, bool depthOnly);
    float menuOpenT = 0, hintT = 0;
    // camera dynamics and transitions
    vec3 prevCarVel;
    float accLongSm = 0, accLatSm = 0, camYawVel = 0, camRollSm = 0, prevVy = 0;
    bool wasGrounded = true;
    int camCtx = -1;
    vec3 blendFromOff, blendFromTgtOff;
    float blendFromFov = 1, blendT = 99, blendDur = 1, blendLift = 0;
    float showKick = 0, showKickSm = 0;
    int lastSubSel = 0;
    float transT = 99, transDur = 0.55f;
    double creditsShown = 0;
    int cameraContext() const;
    void updateCameraRaw(float dt);
    void drawTransition();
    std::string crText() const;
    GLuint logoTex = 0;
    void drawLogo(float x, float y, float w, float alpha = 1.f);
    void drawMenuHeader(float x0, float y0, float W, const std::string& section);
    // festival menu theme: teal backdrop with drifting triangles, the car/driver header, tab bar
    void drawThemeBackdrop(float alpha);
    float drawThemeHeader(float x0, float y0, float W, float in);   // returns the bottom edge
    float drawThemeTabs(float x0, float y0, float W, const char* const* names, int count, int sel, float in);
    void drawThemePrompts(const std::string& spec, float x = -1.f);
    void themePanel(float x, float y, float w, float h, float a = 1.f, bool lit = false);  // festival teal card
    void drawMenuToasts(float y);
    GLuint captureCutout(const Camera& c, Racer& r, float aspect);
    void themeRow(float x, float y, float w, float h, bool sel, float a = 1.f);         // list row with a sliding highlight
    int custStripRow = -1;
    float shoOff = 0;
    bool hudGlass = false;  // panels drawn over the live world use dark glass instead of menu teal
    float mapZoomTarget = 2.2f;  // world map zoom (Q/E)  // showroom camera's sideways framing
    float custStripT = 0;
    bool titleMenu = false;   // title: false = attract screen, true = main menu tiles
    int titleSel = 0;
    float titleMenuT = 0;
    State settingsReturn = ST_PAUSE;
    void drawPetals(float alpha, float t);
    int tabDir = 1;  // which way the tiles slide when the tab changes
    State lastState = ST_TITLE;
    void buildEras();
    std::string eraDate(int era) const;
    void updateSpecial(float dt);
    Atmosphere currentAtmo() const;
    void drawTimeCircuits();
    void drawCheatBox();
    void setCheatFocus(bool on);
    bool cheatEvent(const SDL_Event& e);
    void submitCheat();
    void openSwitcher();
    void drawSwitcher();
    void switcherAction(Action a);
    // GPS
    Gps gps;
    int gpsEvent = -1;          // user-selected waypoint event (-1 = auto: nearest unfinished event)
    bool gpsHub = false;        // waypoint is the festival hub
    int gpsShown = -1;          // event the route currently leads to (-2 = hub)
    float gpsTimer = 0;
    void updateGps(float dt);
    bool nearHub = false;
    // houses
    std::vector<int> ownedEstates;    // indices into world.estates
    int nearHome = -1;                // estate the player is parked at
    int garageHome = -1;              // garage opened from this estate (-1 = festival hub)
    int gpsEstate = -1;
    int gpsLandmark = -1;
    float hudPanelW = 250.f;  // width of the top-left HUD panels this frame
    bool ownsEstate(int i) const { return std::find(ownedEstates.begin(), ownedEstates.end(), i) != ownedEstates.end(); }
    void updateHomes();
    bool homeInteract();
    void buyEstate(int i);
    void drawHomePrompt();
    void drawEstateMarkers();
    Racer showCar;
    int dealerPaint = 0;

    // skills / feedback
    float driftScore = 0, driftIdle = 0, driftMult = 1;
    float driftTotal = 0;  // drift zone events: points scored this run
    std::vector<Toast> toasts;
    float flash = 0;
    float lastImpactSfx = 0;

    // input
    Controls readDriving();
    void handleAction(Action a);
    void handleEvent(const SDL_Event& e);

    // flow
    void newGame();
    void enterFreeRoam(vec3 pos, float yaw);
    void setPlayerCar(int ownedIdx);
    void startRace(int ev);
    void finishRace();
    void leaveRace();
    void toast(const std::string& s, vec4 col = vec4(1, 1, 1, 1));

    // update
    void update(float dt);
    void stepPhysics(float dt);
    void updateCamera(float dt);
    void updateSkills(float dt);
    void updateRace(float dt);

    // render
    void render();
    void renderWorld(const Camera& c, bool showroom);
    void drawCar(Racer& r, Shader& s, bool glassPass = false);
    void drawImported(Racer& r, bool glassPass, const Camera& c);
    mat4 importedPartMatrix(Racer& r, int role) const;
    void drawHud();
    void drawSpeedo();
    void drawMinimap();
    void drawMenuPanel(const std::string& title, const std::vector<std::string>& items, int sel, float x, float y, float w);
    void drawPause();
    void drawMap();
    void drawDealer(bool garageMode);
    void drawSettings();
    void drawRaceHud();
    void drawResults();
    void drawTitle();
    void setupTitle();
    int nextRaceEvent() const;
    void saveShot(const std::string& path);

    // persistence
    void save();
    bool load();
    void applySettings();

    // helpers
    std::string speedStr(float ms) const;
    vec3 showroomSpot() const;
    std::vector<std::string> settingsItems() const;
};
