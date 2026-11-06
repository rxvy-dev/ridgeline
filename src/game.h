// Top-level game: window, state machine, menus, races, camera, saving.
#pragma once
#include "audio.h"
#include "hud.h"
#include "race.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>

enum State { ST_TITLE, ST_FREEROAM, ST_PAUSE, ST_MAP, ST_DEALER, ST_GARAGE, ST_SETTINGS, ST_RACE_INTRO, ST_RACE, ST_RESULTS };
enum Action { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_CONFIRM, A_BACK, A_PAUSE, A_CAMERA, A_RESET, A_MAP, A_INTERACT, A_TOGGLE };

struct OwnedCar { int spec; int paint; bool stripes; };

struct Settings {
    bool mph = false;
    bool motionBlur = true;
    bool shadows = true;
    int msaa = 4;
    bool fullscreen = false;
    int volume = 80;
    int drawDist = 1;   // 0 low, 1 medium, 2 high
    bool vsync = true;
    bool assists = true;
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
    std::vector<EventDef> events;
    Mesh blobMesh, beaconMesh, ringMesh, gateMesh;

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
    vec3 camPos;
    float shake = 0;
    bool lookBack = false;
    float fovSm = 1.1f;
    float titleCut = 0;
    int titleShot = 0;
    Racer titleCar;

    // misc runtime
    float physAcc = 0;
    std::vector<Controls> ctl;     // per racer (race) controls
    Controls playerCtl;
    bool autopilot = false;
    int nearEvent = -1;
    bool nearHub = false;
    Racer showCar;
    int dealerPaint = 0;

    // skills / feedback
    float driftScore = 0, driftIdle = 0, driftMult = 1;
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
    void drawCar(Racer& r, Shader& s);
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
