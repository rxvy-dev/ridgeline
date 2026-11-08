// Race events, routes, AI drivers and race progress tracking.
#pragma once
#include "vehicle.h"
#include <string>
#include <vector>

struct Route {
    std::vector<vec3> pts;
    std::vector<vec3> tan;
    std::vector<float> dist;
    std::vector<float> curv;   // curvature (1/m), smoothed
    bool closed = false;
    float length = 0;
    float width = 12;
    void finalize();
    vec3 pointAt(float d) const;    // interpolated position at distance d
    vec3 tangentAt(float d) const;
    float curvatureAt(float d) const;
    int indexAt(float d) const;
    // Project p onto the route, searching near `hint`. Returns index.
    int project(vec3 p, int hint, int window) const;
};

enum EventType { EV_CIRCUIT, EV_SPRINT, EV_CROSS, EV_DRIFT };

struct EventDef {
    std::string name;
    std::string blurb;
    EventType type;
    int laps = 1;
    Route route;
    int prize[3] = {0, 0, 0};
    vec3 markerPos;
    float startYaw = 0;
    // EventLab rules (built-in events use the defaults)
    int opponents = 5;
    char clsLimit = 0;    // 0 = match the player's class
    bool wet = false;
    int custom = -1;      // index into the player's saved custom events
    std::vector<float> boosts;  // boost pads: distance along the route
    int driftTarget[3] = {0, 0, 0};  // drift events: points for 1st / 2nd / 3rd
    float totalLength() const { return route.closed ? route.length * laps : route.length; }
};

std::vector<EventDef> buildEvents(const World& w);
void addBoostPads(EventDef& e, float spacing);
int routeObstacles(const World& w, const Route& rt, vec3* firstBad);

// EventLab: player-made events on any road, shareable as a short text code.
struct CustomEvent {
    std::string name;
    int road = 0;        // index into World::roads
    int start = 0;       // start point, percent along the road
    int length = 3;      // sprint length in km (open roads)
    int laps = 2;        // circuit laps (closed roads)
    int opponents = 5;   // 0..7
    int cls = 0;         // 0 any (match player), 1..5 = D C B A S
    bool reverse = false;
    bool wet = false;
};
bool makeCustomEvent(const World& w, const CustomEvent& c, EventDef& out);
std::string encodeEvent(const CustomEvent& c);
bool decodeEvent(const std::string& code, CustomEvent& c);
std::string autoEventName(const World& w, const CustomEvent& c);

struct Racer {
    Vehicle car;
    CarModel* model = nullptr;
    vec3 paint, stripe;
    // cosmetics
    int livery = 0, aero = 0, number = 7;
    vec3 livCol = vec3(1, 1, 1);
    float tint = 0, ride = 0, camber = 0;
    float lastProg = 0;  // route progress last frame (boost pads)  // camber: radians, top of the wheel tilted in
    std::string name;
    bool isPlayer = false;
    int idx = 0;              // last projected route index
    int lap = 0;
    float progress = 0;       // metres along the whole race
    bool finished = false;
    float finishTime = 0;
    // AI
    float skill = 0.9f;
    float lane = 0, laneTarget = 0;
    float stuck = 0;
    float laneTimer = 0;
};

// AI driving along a route. `rubber` scales pace (rubber-banding).
Controls aiDrive(Racer& r, const Route& route, const World& w, const std::vector<Racer*>& others, float rubber, float dt);
// Update race progress for a racer.
void trackProgress(Racer& r, const EventDef& ev);
