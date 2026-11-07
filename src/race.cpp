#include <cstdio>
#include <cctype>
#include <cstring>
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
    // ---------------- the wider island
    auto road = [&](const char* n) -> const Road* {
        for (auto& r : w.roads) if (r.name == n) return &r;
        return nullptr;
    };
    // a route through corner points, corners rounded, heights from the (carved) ground
    auto through = [&](std::vector<vec3> c, bool closed, float width) {
        Route rt;
        int n = int(c.size());
        std::vector<vec3> cut;
        for (int i = 0; i < n; i++) {
            vec3 p = c[i], a0 = c[(i - 1 + n) % n], b0 = c[(i + 1) % n];
            if (!closed && (i == 0 || i == n - 1)) { cut.push_back(p); continue; }
            cut.push_back(p + normalize(a0 - p) * 20.f);
            cut.push_back(p + normalize(b0 - p) * 20.f);
        }
        int m = int(cut.size());
        int segs = closed ? m : m - 1;
        for (int sgi = 0; sgi < segs; sgi++) {
            vec3 a = cut[sgi], b = cut[(sgi + 1) % m];
            int steps = std::max(2, int(length(b - a) / 4.f));
            for (int k = 0; k < steps; k++) {
                vec3 p = lerp(a, b, k / float(steps));
                p.y = w.heightAt(p.x, p.z);
                rt.pts.push_back(p);
            }
        }
        if (!closed) { vec3 p = cut.back(); p.y = w.heightAt(p.x, p.z); rt.pts.push_back(p); }
        // smooth the corners a little
        for (int it = 0; it < 6; it++) {
            std::vector<vec3> o = rt.pts;
            int q = int(o.size());
            for (int i = 0; i < q; i++) {
                if (!closed && (i == 0 || i == q - 1)) continue;
                vec3 a2 = rt.pts[(i - 1 + q) % q], b2 = rt.pts[(i + 1) % q];
                o[i] = (a2 + rt.pts[i] * 2.f + b2) * 0.25f;
                o[i].y = w.heightAt(o[i].x, o[i].z);
            }
            rt.pts = o;
        }
        rt.closed = closed;
        rt.width = width;
        rt.finalize();
        return rt;
    };
    {
        EventDef e;
        e.name = "Neon City Circuit";
        e.blurb = "Two laps through the downtown grid, under the towers.";
        e.type = EV_CIRCUIT; e.laps = 2;
        e.route = through({vec3(1800, 0, -850), vec3(2400, 0, -850), vec3(2400, 0, -400), vec3(2700, 0, -400), vec3(2700, 0, 200),
                           vec3(2100, 0, 200), vec3(2100, 0, -100), vec3(1800, 0, -100)}, true, 12.f);
        e.prize[0] = 18000; e.prize[1] = 10000; e.prize[2] = 6000;
        finish(e);
    }
    if (const Road* c = road("Coast Highway")) {
        EventDef e;
        e.name = "Harbour Run";
        e.blurb = "Out of the city and down the coast to the docks.";
        e.type = EV_SPRINT;
        e.route = routeFromRoad(*c, nearestIndex(*c, vec3(2400, 0, -250)), nearestIndex(*c, vec3(400, 0, -1880)), false);
        e.prize[0] = 15000; e.prize[1] = 8500; e.prize[2] = 5000;
        finish(e);
    }
    if (const Road* c = road("Coast Highway")) {
        EventDef e;
        e.name = "Coast to Coast";
        e.blurb = "The whole Coast Highway: city, docks, farmland, home.";
        e.type = EV_SPRINT;
        e.route = routeFromRoad(*c, 0, int(c->pts.size()) - 1, false);
        e.prize[0] = 30000; e.prize[1] = 17000; e.prize[2] = 9000;
        finish(e);
    }
    {
        EventDef e;
        e.name = "Docks Circuit";
        e.blurb = "Two laps between the container stacks and the cranes.";
        e.type = EV_CIRCUIT; e.laps = 2;
        e.route = through({vec3(450, 0, -1905), vec3(450, 0, -2330), vec3(1750, 0, -2330), vec3(1750, 0, -1900)}, true, 12.f);
        e.prize[0] = 14000; e.prize[1] = 8000; e.prize[2] = 4500;
        finish(e);
    }
    if (const Road* t = road("Akagi Touge")) {
        EventDef e;
        e.name = "Akagi Touge";
        e.blurb = "Hairpin after hairpin up into the northern peaks.";
        e.type = EV_SPRINT;
        e.route = routeFromRoad(*t, 0, int(t->pts.size()) - 1, false);
        e.prize[0] = 16000; e.prize[1] = 9000; e.prize[2] = 5000;
        finish(e);
    }
    if (const Road* f = road("Harvest Lanes")) {
        EventDef e;
        e.name = "Harvest Scramble";
        e.blurb = "Two dusty laps around the western farms.";
        e.type = EV_CIRCUIT; e.laps = 2;
        e.route = routeFromRoad(*f, 0, 0, true);
        e.prize[0] = 12000; e.prize[1] = 7000; e.prize[2] = 4000;
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

// ---------------------------------------------------------------- EventLab
std::string autoEventName(const World& w, const CustomEvent& c) {
    if (c.road < 0 || c.road >= int(w.roads.size())) return "Custom Event";
    const Road& r = w.roads[c.road];
    std::string n = r.name;
    if (r.closed) n += c.laps > 1 ? " " + std::to_string(c.laps) + "-Lap Circuit" : " Circuit";
    else n += " " + std::to_string(c.length) + " km Sprint";
    if (c.reverse) n += " (Rev)";
    return n;
}

bool makeCustomEvent(const World& w, const CustomEvent& c, EventDef& e) {
    if (c.road < 0 || c.road >= int(w.roads.size())) return false;
    const Road& r = w.roads[c.road];
    int m = int(r.pts.size());
    if (m < 16) return false;
    int s = std::clamp(c.start, 0, 99) * m / 100;
    e = EventDef();
    e.name = c.name.empty() ? autoEventName(w, c) : c.name;
    if (r.closed) {
        e.type = EV_CIRCUIT;
        e.laps = std::clamp(c.laps, 1, 10);
        e.route = routeFromRoad(r, s, 0, true);
        if (c.reverse) e.route = reversed(e.route);
    } else {
        e.type = EV_SPRINT;
        e.laps = 1;
        // walk the requested distance from the start, in the chosen direction
        float want = std::max(1, c.length) * 1000.f;
        int dir = c.reverse ? -1 : 1;
        int i = s;
        float got = 0;
        while (got < want) {
            int j = i + dir;
            if (j < 0 || j >= m) break;
            got += std::hypot(r.pts[j].x - r.pts[i].x, r.pts[j].z - r.pts[i].z);
            i = j;
        }
        if (got < 400.f) return false;
        e.route = c.reverse ? reversed(routeFromRoad(r, i, s, false)) : routeFromRoad(r, s, i, false);
    }
    e.opponents = std::clamp(c.opponents, 0, 7);
    e.clsLimit = c.cls >= 1 && c.cls <= 5 ? "DCBAS"[c.cls - 1] : 0;
    e.wet = c.wet;
    float km = e.totalLength() / 1000.f;
    int p = int(std::clamp(km * 1600.f * (0.6f + 0.06f * e.opponents), 2000.f, 40000.f)) / 100 * 100;
    e.prize[0] = p; e.prize[1] = p * 55 / 100; e.prize[2] = p * 3 / 10;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "EventLab: %s, %d rival%s%s%s.", r.surface == SURF_ASPHALT ? "tarmac" : "dirt", e.opponents,
                  e.opponents == 1 ? "" : "s", e.clsLimit ? (std::string(", class ") + e.clsLimit).c_str() : "", e.wet ? ", wet roads" : "");
    e.blurb = buf;
    e.markerPos = e.route.pts[0];
    e.startYaw = std::atan2(e.route.tan[0].x, e.route.tan[0].z);
    return true;
}

// Share code: RL1-<fields in base 36>-<checksum>. Short enough to read out or paste in chat.
static const char* kB36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
std::string encodeEvent(const CustomEvent& c) {
    int f[9] = {c.road, c.start, c.length, c.laps, c.opponents, c.cls, c.reverse ? 1 : 0, c.wet ? 1 : 0, 0};
    std::string body;
    for (int i = 0; i < 8; i++) {
        int v = std::clamp(f[i], 0, 36 * 36 - 1);
        body += kB36[v / 36];
        body += kB36[v % 36];
    }
    unsigned ck = 7;
    for (char ch : body) ck = (ck * 31 + unsigned(ch)) % 1296;
    std::string code = "RL1-" + body.substr(0, 8) + "-" + body.substr(8) + "-" + kB36[ck / 36] + kB36[ck % 36];
    return code;
}

bool decodeEvent(const std::string& in, CustomEvent& c) {
    std::string s;
    for (char ch : in) if (std::isalnum((unsigned char)ch)) s += char(std::toupper((unsigned char)ch));
    if (s.size() != 3 + 16 + 2 || s.compare(0, 3, "RL1") != 0) return false;
    std::string body = s.substr(3, 16);
    unsigned ck = 7;
    for (char ch : body) ck = (ck * 31 + unsigned(ch)) % 1296;
    auto val = [](char ch) { const char* p = std::strchr(kB36, ch); return p ? int(p - kB36) : -1; };
    int got = val(s[19]) * 36 + val(s[20]);
    if (got != int(ck)) return false;
    int f[8];
    for (int i = 0; i < 8; i++) {
        int a = val(body[i * 2]), b = val(body[i * 2 + 1]);
        if (a < 0 || b < 0) return false;
        f[i] = a * 36 + b;
    }
    c = CustomEvent();
    c.road = f[0]; c.start = std::clamp(f[1], 0, 99); c.length = std::clamp(f[2], 1, 40); c.laps = std::clamp(f[3], 1, 10);
    c.opponents = std::clamp(f[4], 0, 7); c.cls = std::clamp(f[5], 0, 5); c.reverse = f[6] != 0; c.wet = f[7] != 0;
    return true;
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
