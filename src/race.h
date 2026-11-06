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

enum EventType { EV_CIRCUIT, EV_SPRINT, EV_CROSS };

struct EventDef {
    std::string name;
    std::string blurb;
    EventType type;
    int laps = 1;
    Route route;
    int prize[3] = {0, 0, 0};
    vec3 markerPos;
    float startYaw = 0;
    float totalLength() const { return route.closed ? route.length * laps : route.length; }
};

std::vector<EventDef> buildEvents(const World& w);

struct Racer {
    Vehicle car;
    CarModel* model = nullptr;
    vec3 paint, stripe;
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
