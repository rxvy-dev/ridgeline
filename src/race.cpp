#include "race.h"
#include <algorithm>

void Route::finalize() {
    int m = int(pts.size());
    tan.resize(m);
    dist.resize(m);
    curv.assign(m, 0.f);
    float acc = 0;
    for (int i = 0; i < m; i++) {
        int ip = closed ? (i - 1 + m) % m : std::max(i - 1, 0);
        int in = closed ? (i + 1) % m : std::min(i + 1, m - 1);
        vec3 t = pts[in] - pts[ip];
        t.y = 0;
        tan[i] = normalize(t);
        if (i > 0) { vec3 d = pts[i] - pts[i - 1]; acc += std::sqrt(d.x * d.x + d.z * d.z); }
        dist[i] = acc;
    }
    if (closed) { vec3 d = pts[0] - pts[m - 1]; acc += std::sqrt(d.x * d.x + d.z * d.z); }
    length = acc;
    // curvature from heading change over +-3 samples
    std::vector<float> c(m, 0.f);
    for (int i = 0; i < m; i++) {
        int ip = closed ? (i - 3 + m) % m : std::max(i - 3, 0);
        int in = closed ? (i + 3) % m : std::min(i + 3, m - 1);
        float a0 = std::atan2(tan[ip].x, tan[ip].z), a1 = std::atan2(tan[in].x, tan[in].z);
        float ds = std::fabs(dist[in] - dist[ip]);
        if (closed && in < ip) ds = length - dist[ip] + dist[in];
        c[i] = ds > 0.1f ? std::fabs(wrapAngle(a1 - a0)) / ds : 0.f;
    }
    for (int i = 0; i < m; i++) {
        float mx = 0;
        for (int k = -2; k <= 2; k++) {
            int j = i + k;
            if (closed) j = (j + m) % m;
            else j = std::clamp(j, 0, m - 1);
            mx = std::max(mx, c[j]);
        }
        curv[i] = mx;
    }
}

int Route::indexAt(float d) const {
    if (closed) { d = std::fmod(d, length); if (d < 0) d += length; }
    else d = clampf(d, 0.f, length);
    auto it = std::upper_bound(dist.begin(), dist.end(), d);
    int i = int(it - dist.begin()) - 1;
    return std::clamp(i, 0, int(pts.size()) - 1);
}

vec3 Route::pointAt(float d) const {
    if (closed) { d = std::fmod(d, length); if (d < 0) d += length; }
    else d = clampf(d, 0.f, length);
    int i = indexAt(d);
    int m = int(pts.size());
    int j = closed ? (i + 1) % m : std::min(i + 1, m - 1);
    float d1 = (j == 0 && closed) ? length : dist[j];
    float t = d1 > dist[i] ? (d - dist[i]) / (d1 - dist[i]) : 0.f;
    return lerp(pts[i], pts[j], clampf(t, 0.f, 1.f));
}

vec3 Route::tangentAt(float d) const { return tan[indexAt(d)]; }
float Route::curvatureAt(float d) const { return curv[indexAt(d)]; }

int Route::project(vec3 p, int hint, int window) const {
    int m = int(pts.size());
    float best = 1e18f;
    int bi = hint;
    for (int k = -window; k <= window; k++) {
        int j = hint + k;
        if (closed) j = (j % m + m) % m;
        else if (j < 0 || j >= m) continue;
        float dx = pts[j].x - p.x, dz = pts[j].z - p.z;
        float d = dx * dx + dz * dz;
        if (d < best) { best = d; bi = j; }
    }
    return bi;
}

// ---------------------------------------------------------------- events
static Route routeFromRoad(const Road& r, int from, int to, bool closed) {
    Route rt;
    rt.closed = closed;
    rt.width = r.width;
    int m = int(r.pts.size());
    if (closed) {
        for (int k = 0; k < m; k++) rt.pts.push_back(r.pts[(from + k) % m]);
    } else if (to >= from) {
        for (int i = from; i <= to; i++) rt.pts.push_back(r.pts[i]);
    } else {  // wrap around a closed road
        for (int i = from; i != to; i = (i + 1) % m) rt.pts.push_back(r.pts[i]);
        rt.pts.push_back(r.pts[to]);
    }
    rt.finalize();
    return rt;
}

static Route reversed(Route r) {
    std::reverse(r.pts.begin(), r.pts.end());
    r.finalize();
    return r;
}

static int nearestIndex(const Road& r, vec3 p) {
    int bi = 0; float bd = 1e18f;
    for (int i = 0; i < int(r.pts.size()); i++) {
        float d = (r.pts[i].x - p.x) * (r.pts[i].x - p.x) + (r.pts[i].z - p.z) * (r.pts[i].z - p.z);
        if (d < bd) { bd = d; bi = i; }
    }
    return bi;
}

std::vector<EventDef> buildEvents(const World& w) {
    std::vector<EventDef> ev;
    auto finish = [&](EventDef& e) {
        e.markerPos = e.route.pts[0];
        e.startYaw = std::atan2(e.route.tan[0].x, e.route.tan[0].z);
        ev.push_back(e);
    };
    const Road& loop = w.roads[0];
    const Road& valley = w.roads[1];
    const Road& summit = w.roads[2];
    const Road& pass = w.roads[3];
    const Road& trail = w.roads[4];
    {
        EventDef e;
        e.name = "Ridge Highway Circuit";
        e.blurb = "One flat-out lap of the highway ring.";
        e.type = EV_CIRCUIT; e.laps = 1;
        int s = nearestIndex(loop, vec3(-870, 0, -200));
        e.route = routeFromRoad(loop, s, 0, true);
        e.prize[0] = 16000; e.prize[1] = 9000; e.prize[2] = 5000;
        finish(e);
    }
    {
        EventDef e;
        e.name = "Valley Sprint";
        e.blurb = "Point-to-point dash across the valley floor.";
        e.type = EV_SPRINT;
        e.route = routeFromRoad(valley, 6, int(valley.pts.size()) - 1, false);
        e.prize[0] = 9000; e.prize[1] = 5000; e.prize[2] = 3000;
        finish(e);
    }
    {
        EventDef e;
        e.name = "Summit Loop";
        e.blurb = "Two laps of the twisting mountain loop.";
        e.type = EV_CIRCUIT; e.laps = 2;
        int s = nearestIndex(summit, pass.pts.back());
        e.route = routeFromRoad(summit, (s + 12) % int(summit.pts.size()), 0, true);
        e.prize[0] = 14000; e.prize[1] = 8000; e.prize[2] = 4500;
        finish(e);
    }
    {
        EventDef e;
        e.name = "Timberline Scramble";
        e.blurb = "Two laps of the forest dirt trail.";
        e.type = EV_CIRCUIT; e.laps = 2;
        e.route = routeFromRoad(trail, 0, 0, true);
        e.prize[0] = 12000; e.prize[1] = 7000; e.prize[2] = 4000;
        finish(e);
    }
    {
        EventDef e;
        e.name = "Pass Climb";
        e.blurb = "Sprint up the switchbacks to the summit.";
        e.type = EV_SPRINT;
        e.route = routeFromRoad(pass, 0, int(pass.pts.size()) - 1, false);
        e.prize[0] = 8000; e.prize[1] = 4500; e.prize[2] = 2500;
        finish(e);
    }
    {
        EventDef e;
        e.name = "Lakeshore Sprint";
        e.blurb = "Run the southern highway along the lake.";
        e.type = EV_SPRINT;
        int a = nearestIndex(loop, vec3(-250, 0, -900));
        int b = nearestIndex(loop, vec3(870, 0, 150));
        e.route = routeFromRoad(loop, a, b, false);
        e.prize[0] = 10000; e.prize[1] = 6000; e.prize[2] = 3500;
        finish(e);
    }
    {
        EventDef e;
        e.name = "Golden Fields Cross Country";
        e.blurb = "Off-road checkpoints through the meadows.";
        e.type = EV_CROSS;
        // Catmull-Rom through the checkpoints
        const auto& c = w.crossCountry;
        int n = int(c.size());
        for (int s = 0; s + 1 < n; s++) {
            vec3 p0 = c[std::max(s - 1, 0)], p1 = c[s], p2 = c[s + 1], p3 = c[std::min(s + 2, n - 1)];
            int steps = std::max(2, int(length(p2 - p1) / 4.f));
            for (int k = 0; k < steps; k++) {
                float t = k / float(steps), t2 = t * t, t3 = t2 * t;
                vec3 p = (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
                p.y = w.heightAt(p.x, p.z);
                e.route.pts.push_back(p);
            }
        }
        e.route.pts.push_back(c.back());
        e.route.closed = false;
        e.route.width = 24;
        e.route.finalize();
        e.prize[0] = 11000; e.prize[1] = 6500; e.prize[2] = 3500;
        finish(e);
    }
    // a reverse-direction circuit for variety
    {
        EventDef e;
        e.name = "Ridge Reverse";
        e.blurb = "The highway ring, counter-clockwise.";
        e.type = EV_CIRCUIT; e.laps = 1;
        int s = nearestIndex(loop, vec3(870, 0, -250));
        e.route = reversed(routeFromRoad(loop, s, 0, true));
        e.prize[0] = 16000; e.prize[1] = 9000; e.prize[2] = 5000;
        finish(e);
    }
    return ev;
}

// ---------------------------------------------------------------- progress
void trackProgress(Racer& r, const EventDef& ev) {
    const Route& rt = ev.route;
    int m = int(rt.pts.size());
    int prev = r.idx;
    int ni = rt.project(r.car.pos, prev, 25);
    if (rt.closed) {
        if (prev > m * 3 / 4 && ni < m / 4) r.lap++;
        else if (prev < m / 4 && ni > m * 3 / 4) r.lap--;
    }
    r.idx = ni;
    r.progress = r.lap * rt.length + rt.dist[ni];
    if (!rt.closed) r.progress = rt.dist[ni];
}

// ---------------------------------------------------------------- AI
Controls aiDrive(Racer& r, const Route& rt, const World& w, const std::vector<Racer*>& others, float rubber, float dt) {
    Controls c;
    Vehicle& car = r.car;
    float v = car.speed();
    float myD = rt.dist[r.idx];

    // lane changes to get around traffic
    r.laneTimer -= dt;
    for (Racer* o : others) {
        if (o == &r) continue;
        vec3 d = o->car.pos - car.pos;
        float ahead = dot(d, car.forward()), side = dot(d, car.right());
        if (ahead > 2.f && ahead < 14.f + v * 0.3f && std::fabs(side) < 2.6f && o->car.speed() < v + 2.f && r.laneTimer <= 0) {
            float half = rt.width * 0.5f - 1.6f;
            r.laneTarget = side > 0 ? -half * 0.8f : half * 0.8f;
            r.laneTimer = 2.5f;
        }
    }
    if (r.laneTimer <= 0) r.laneTarget *= 0.995f;
    r.lane += (r.laneTarget - r.lane) * damp(1.5f, dt);

    // steering towards a look-ahead point
    float look = 7.f + v * 0.55f;
    vec3 tp = rt.pointAt(myD + look);
    vec3 tt = rt.tangentAt(myD + look);
    vec3 tr(-tt.z, 0, tt.x);
    // cut the inside of corners a little
    float k = rt.curvatureAt(myD + look);
    tp += tr * r.lane;
    vec3 to = tp - car.pos;
    to.y = 0;
    float ang = std::atan2(dot(to, car.right()), dot(to, car.forward()));
    float maxSteer = 0.6f / (1.f + v * 0.055f);
    c.steer = clampf(ang / std::max(maxSteer, 0.05f) * 0.9f + car.yawRate * 0.12f, -1.f, 1.f);

    // speed planning from curvature ahead
    int surf = w.surfaceAt(car.pos.x, car.pos.z);
    float mu = car.spec->grip * 1.18f * (surf == SURF_ASPHALT ? 1.f : lerpf(0.6f, 0.95f, car.spec->offroad));
    float skill = r.skill * rubber;
    float decel = 7.0f * skill;
    float target = 120.f;
    float horizon = v * v / (2 * decel) + 40.f;
    for (float s = 0; s < horizon; s += 6.f) {
        float kk = rt.curvatureAt(myD + s);
        if (kk < 1e-4f) continue;
        float vmax = std::sqrt(mu * 9.81f * skill * 0.92f / kk) + 2.f;
        float allowed = std::sqrt(vmax * vmax + 2 * decel * s);
        target = std::min(target, allowed);
    }
    (void)k;
    if (!rt.closed && myD > rt.length - 5.f) target = 0;
    if (target > v + 1.f) c.throttle = 1.f;
    else if (target > v - 1.f) c.throttle = 0.4f;
    c.brake = v > target + 1.5f ? clampf((v - target) / 6.f, 0.f, 1.f) : 0.f;
    if (c.brake > 0) c.throttle = 0;
    // traction control for the bots
    if (std::fabs(car.slipAngle) > 0.25f) c.throttle *= 0.4f;
    // recover if facing backwards
    if (dot(car.forward(), rt.tan[r.idx]) < -0.2f && v < 6.f) { c.throttle = 0; c.brake = 1; c.steer = -c.steer; }

    // stuck detection
    if (v < 1.5f) r.stuck += dt; else r.stuck = std::max(0.f, r.stuck - dt * 2);
    if (r.stuck > 3.5f) {
        int ni = std::min(r.idx + 3, int(rt.pts.size()) - 1);
        if (rt.closed) ni = (r.idx + 3) % int(rt.pts.size());
        car.placeAt(rt.pts[ni], std::atan2(rt.tan[ni].x, rt.tan[ni].z), w);
        r.stuck = 0;
    }
    return c;
}
