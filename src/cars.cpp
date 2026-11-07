#include "cars.h"
#include <algorithm>
#include <cctype>

// All manufacturers and models are invented for this game.
std::vector<CarSpec> kCars = {
    // make       model            cls price    kW    kg    top  drive grip  off   redl  body          color                    strp  spl   len   wid   hgt   wb    wheelR
    {"Kito",     "Pulsar 1.6",     'D', 0,       92,  1080, 192, FWD, 1.00f, 0.55f, 6800, BODY_HATCH,     {0.80f, 0.10f, 0.08f}, false, false, 3.95f, 1.74f, 1.45f, 2.50f, 0.31f},
    {"Corvo",    "Rallye 2.0",     'D', 18000,  138,  1230, 208, AWD, 1.02f, 0.85f, 7200, BODY_RALLY,     {0.92f, 0.92f, 0.90f}, true,  true,  4.05f, 1.78f, 1.46f, 2.55f, 0.32f},
    {"Bravado",  "Stallion 5.0",   'C', 36000,  315,  1640, 250, RWD, 1.00f, 0.45f, 6500, BODY_MUSCLE,    {0.95f, 0.42f, 0.06f}, true,  false, 4.80f, 1.92f, 1.36f, 2.80f, 0.34f},
    {"Nordwind", "Ridgeback TR",   'C', 42000,  290,  1950, 195, AWD, 0.95f, 1.00f, 6000, BODY_TRUCK,     {0.28f, 0.34f, 0.26f}, false, false, 4.95f, 2.02f, 1.85f, 3.00f, 0.42f},
    {"Kestrel",  "GT-S",           'B', 64000,  335,  1450, 285, RWD, 1.10f, 0.40f, 7600, BODY_COUPE,     {0.70f, 0.72f, 0.75f}, false, true,  4.50f, 1.86f, 1.30f, 2.62f, 0.34f},
    {"Ardent",   "Mantis RT",      'A', 118000, 480,  1520, 318, RWD, 1.15f, 0.35f, 6800, BODY_GT,        {0.45f, 0.78f, 0.10f}, false, true,  4.55f, 1.95f, 1.24f, 2.55f, 0.35f},
    {"Vespera",  "7 Spyder",       'A', 142000, 470,  1490, 312, RWD, 1.18f, 0.30f, 8200, BODY_MIDENGINE, {0.05f, 0.55f, 0.55f}, false, false, 4.50f, 1.98f, 1.18f, 2.65f, 0.34f},
    {"Aurex",    "Corsa R",        'A', 168000, 455,  1560, 308, AWD, 1.20f, 0.45f, 8000, BODY_MIDENGINE, {0.93f, 0.93f, 0.92f}, true,  true,  4.45f, 1.96f, 1.17f, 2.62f, 0.34f},
    {"Strada",   "Rossa V12",      'S', 360000, 610,  1630, 335, RWD, 1.20f, 0.30f, 8500, BODY_GT,        {0.45f, 0.04f, 0.06f}, false, false, 4.70f, 1.98f, 1.27f, 2.72f, 0.35f},
    {"Tempest",  "Vortex",         'S', 1150000, 880, 1420, 395, AWD, 1.30f, 0.35f, 9000, BODY_HYPER,     {0.06f, 0.06f, 0.07f}, true,  true,  4.60f, 2.03f, 1.13f, 2.70f, 0.35f},
};

std::string carId(const CarSpec& s) {
    if (!s.id.empty()) return s.id;
    std::string r;
    for (char c : s.make + "_" + s.model) r += (std::isalnum((unsigned char)c) ? char(std::tolower((unsigned char)c)) : '_');
    return r;
}

const vec3 kPaints[] = {
    {0.80f, 0.10f, 0.08f}, {0.95f, 0.42f, 0.06f}, {0.95f, 0.72f, 0.10f}, {0.45f, 0.78f, 0.10f},
    {0.10f, 0.35f, 0.16f}, {0.05f, 0.55f, 0.55f}, {0.20f, 0.50f, 0.90f}, {0.05f, 0.10f, 0.35f},
    {0.40f, 0.12f, 0.55f}, {0.93f, 0.93f, 0.92f}, {0.70f, 0.72f, 0.75f}, {0.06f, 0.06f, 0.07f},
};
const int kPaintCount = 12;
const char* kPaintNames[] = {"Festival Red", "Sunburst Orange", "Harvest Gold", "Acid Lime", "Pine Green", "Lagoon Teal",
                             "Sky Blue", "Midnight", "Plum", "Pearl White", "Silver", "Jet Black"};

CarRatings rateCar(const CarSpec& s) {
    CarRatings r;
    r.speed = clampf((s.topKmh - 170.f) / 230.f * 9.f + 1.f, 1.f, 10.f);
    float pw = s.powerKW / (s.massKg / 1000.f);
    r.accel = clampf((pw - 60.f) / 560.f * 9.f + 1.f, 1.f, 10.f) + (s.drive == AWD ? 0.4f : 0.f);
    r.accel = std::min(r.accel, 10.f);
    r.handling = clampf((s.grip - 0.9f) / 0.42f * 8.f + 2.f - (s.massKg - 1300.f) / 600.f, 1.f, 10.f);
    r.offroad = clampf(s.offroad * 10.f, 1.f, 10.f);
    r.pi = int(100 + r.speed * 30 + r.accel * 35 + r.handling * 25);
    return r;
}

// ---------------------------------------------------------------- body shapes
struct Shape { float clear, nose, belt, tail, roof, sA, sB, sC, sD, roofRatio, hip, flare; int spokes; };

static Shape shapeFor(int body) {
    switch (body) {
        case BODY_HATCH:     return {0.11f, 0.45f, 0.63f, 0.68f, 0.98f, 0.22f, 0.43f, 0.86f, 1.00f, 0.84f, 0.00f, 0.02f, 7};
        case BODY_RALLY:     return {0.13f, 0.46f, 0.63f, 0.69f, 0.98f, 0.22f, 0.43f, 0.85f, 1.00f, 0.84f, 0.02f, 0.05f, 6};
        case BODY_MUSCLE:    return {0.10f, 0.56f, 0.67f, 0.66f, 0.98f, 0.40f, 0.54f, 0.70f, 0.88f, 0.74f, 0.03f, 0.04f, 5};
        case BODY_COUPE:     return {0.09f, 0.44f, 0.66f, 0.70f, 0.98f, 0.34f, 0.51f, 0.64f, 0.89f, 0.72f, 0.04f, 0.04f, 10};
        case BODY_MIDENGINE: return {0.08f, 0.37f, 0.64f, 0.77f, 0.98f, 0.20f, 0.41f, 0.52f, 0.81f, 0.66f, 0.05f, 0.06f, 10};
        case BODY_TRUCK:     return {0.17f, 0.58f, 0.69f, 0.69f, 0.98f, 0.28f, 0.41f, 0.93f, 1.00f, 0.88f, 0.02f, 0.06f, 6};
        case BODY_GT:        return {0.08f, 0.47f, 0.66f, 0.71f, 0.98f, 0.42f, 0.57f, 0.68f, 0.91f, 0.68f, 0.04f, 0.05f, 5};
        case BODY_HYPER:     return {0.07f, 0.35f, 0.64f, 0.83f, 0.98f, 0.18f, 0.41f, 0.48f, 0.79f, 0.60f, 0.06f, 0.07f, 10};
    }
    return shapeFor(BODY_COUPE);
}

// ---------------------------------------------------------------- wheels
// Unit wheel: radius 1, width 1 along x, outer face at +x.
static void buildWheelMesh(MeshBuilder& b, int spokes, bool darkRim) {
    const int S = 48;
    vec4 rubber(1, 1, 1, 8), alloy = darkRim ? vec4(0.2f, 0.2f, 0.22f, 9) : vec4(1, 1, 1, 7), lip(1, 1, 1, 4), dark(0.05f, 0.05f, 0.06f, 9), disc(0.42f, 0.42f, 0.42f, 0);
    auto ring = [&](float a) { return vec3(0, std::cos(a), std::sin(a)); };
    // tyre: revolve a rounded profile (x, r)
    const float prof[][2] = {{-0.50f, 0.66f}, {-0.50f, 0.80f}, {-0.46f, 0.93f}, {-0.38f, 0.99f}, {0.38f, 0.99f}, {0.46f, 0.93f}, {0.50f, 0.80f}, {0.50f, 0.66f}};
    const int P = 8;
    uint32_t base = uint32_t(b.v.size());
    for (int i = 0; i <= S; i++) {
        float a = i / float(S) * 2 * PI;
        vec3 d = ring(a);
        for (int k = 0; k < P; k++) {
            int kp = std::max(k - 1, 0), kn = std::min(k + 1, P - 1);
            vec2 t(prof[kn][0] - prof[kp][0], prof[kn][1] - prof[kp][1]);
            vec3 n = normalize(vec3(0, 0, 0) + vec3(t.y, 0, 0) * -1.f + d * t.x);
            float groove = (k >= 3 && k <= 4) ? (0.85f + 0.15f * ((i % 3) == 0)) : 1.f;
            b.add(vec3(prof[k][0], 0, 0) + d * prof[k][1], normalize(n), vec4(groove, groove, groove, 8));
        }
    }
    for (int i = 0; i < S; i++)
        for (int k = 0; k + 1 < P; k++) {
            uint32_t a0 = base + i * P + k, a1 = base + (i + 1) * P + k;
            b.tri(a0, a0 + 1, a1 + 1);
            b.tri(a0, a1 + 1, a1);
        }
    (void)rubber;
    for (int i = 0; i < S; i++) {
        float a0 = i / float(S) * 2 * PI, a1 = (i + 1) / float(S) * 2 * PI;
        vec3 d0 = ring(a0), d1 = ring(a1);
        // rim lip and inner barrel
        b.flatQuad(vec3(0.48f, 0, 0) + d0 * 0.66f, vec3(0.48f, 0, 0) + d1 * 0.66f, vec3(0.46f, 0, 0) + d1 * 0.61f, vec3(0.46f, 0, 0) + d0 * 0.61f, lip);
        b.flatQuad(vec3(0.46f, 0, 0) + d0 * 0.61f, vec3(0.46f, 0, 0) + d1 * 0.61f, vec3(-0.3f, 0, 0) + d1 * 0.61f, vec3(-0.3f, 0, 0) + d0 * 0.61f, dark);
        b.flatQuad(vec3(-0.3f, 0, 0) + d0 * 0.61f, vec3(-0.3f, 0, 0) + d1 * 0.61f, vec3(-0.3f, 0, 0) + d1 * 0.1f, vec3(-0.3f, 0, 0) + d0 * 0.1f, dark);
        // brake disc
        b.flatQuad(vec3(0.02f, 0, 0) + d0 * 0.56f, vec3(0.02f, 0, 0) + d1 * 0.56f, vec3(0.02f, 0, 0) + d1 * 0.24f, vec3(0.02f, 0, 0) + d0 * 0.24f, disc);
        // hub cap
        b.flatTri(vec3(0.47f, 0, 0), vec3(0.45f, 0, 0) + d0 * 0.16f, vec3(0.45f, 0, 0) + d1 * 0.16f, lip);
        b.flatQuad(vec3(0.45f, 0, 0) + d0 * 0.16f, vec3(0.42f, 0, 0) + d0 * 0.22f, vec3(0.42f, 0, 0) + d1 * 0.22f, vec3(0.45f, 0, 0) + d1 * 0.16f, alloy);
    }
    // spokes: tapered prisms from hub to rim
    for (int sp = 0; sp < spokes; sp++) {
        float a = sp / float(spokes) * 2 * PI;
        float wHub = spokes > 7 ? 0.05f : 0.09f, wRim = spokes > 7 ? 0.035f : 0.075f;
        vec3 d = ring(a), t = ring(a + PI * 0.5f);
        vec3 h0 = d * 0.2f - t * wHub, h1 = d * 0.2f + t * wHub, r0 = d * 0.62f - t * wRim, r1 = d * 0.62f + t * wRim;
        float xf = 0.44f, xb = 0.30f;
        auto X = [&](vec3 p, float x) { return p + vec3(x, 0, 0); };
        b.flatQuad(X(h0, xf), X(h1, xf), X(r1, xf + 0.02f), X(r0, xf + 0.02f), alloy);
        b.flatQuad(X(h1, xf), X(h1, xb), X(r1, xb), X(r1, xf + 0.02f), vec4(0.6f, 0.6f, 0.6f, alloy.w));
        b.flatQuad(X(h0, xb), X(h0, xf), X(r0, xf + 0.02f), X(r0, xb), vec4(0.6f, 0.6f, 0.6f, alloy.w));
        // lug nut
        vec3 ln = ring(a + PI / spokes) * 0.12f + vec3(0.46f, 0, 0);
        b.box(ln, vec3(0.012f, 0.022f, 0.022f), lip);
    }
}

static void buildCaliper(MeshBuilder& b) {
    // a curved block hugging the disc, towards the rear-top
    const int K = 6;
    vec4 red(1, 1, 1, 11);
    for (int k = 0; k < K; k++) {
        float a0 = lerpf(1.95f, 2.75f, k / float(K)), a1 = lerpf(1.95f, 2.75f, (k + 1) / float(K));
        vec3 d0(0, std::cos(a0), std::sin(a0)), d1(0, std::cos(a1), std::sin(a1));
        b.flatQuad(vec3(0.12f, 0, 0) + d0 * 0.58f, vec3(0.12f, 0, 0) + d1 * 0.58f, vec3(0.12f, 0, 0) + d1 * 0.40f, vec3(0.12f, 0, 0) + d0 * 0.40f, red);
        b.flatQuad(vec3(0.12f, 0, 0) + d0 * 0.58f, vec3(-0.08f, 0, 0) + d0 * 0.58f, vec3(-0.08f, 0, 0) + d1 * 0.58f, vec3(0.12f, 0, 0) + d1 * 0.58f, red);
    }
}

// ---------------------------------------------------------------- body
void CarModel::build(const CarSpec& s) {
    dims = vec3(s.length, s.width, s.height);
    deckY = s.height * 0.72f;
    deckZ = -s.length * 0.5f + 0.24f;
    Shape sh = shapeFor(s.body);
    const float L = s.length, W = s.width, H = s.height;
    float zf = L * 0.5f;
    float axF = s.wheelbase * 0.5f, axR = -s.wheelbase * 0.5f;
    if (s.body == BODY_MIDENGINE || s.body == BODY_HYPER) { axF += 0.08f; axR += 0.08f; }
    float sFront = (zf - axF) / L, sRear = (zf - axR) / L;
    wheelR = s.wheelR;
    wheelW = s.body == BODY_TRUCK ? 0.33f : (s.body == BODY_HYPER || s.body == BODY_MIDENGINE ? 0.30f : 0.26f);
    float track = W * 0.5f - wheelW * 0.5f - 0.035f;
    float archR = wheelR * 1.13f;

    auto cf = [&](float t) { return smoothstepf(sh.sA, sh.sB, t) * (1.f - smoothstepf(sh.sC, std::max(sh.sD, sh.sC + 0.01f), t)); };
    auto belt = [&](float t) {
        float u = clampf(t / sh.sA, 0.f, 1.f);
        float y = lerpf(sh.nose, sh.belt, 1.f - (1.f - u) * (1.f - u));
        y = lerpf(y, sh.tail, smoothstepf(0.72f, 1.0f, t));
        return y * H;
    };
    auto gauss = [](float x, float w) { return std::exp(-(x * x) / (w * w)); };
    auto halfW = [&](float t) {
        float e0 = std::sqrt(smoothstepf(0.f, 0.11f, t)), e1 = std::sqrt(smoothstepf(0.f, 0.08f, 1.f - t));
        float w = W * 0.5f * (0.70f + 0.30f * e0) * (0.80f + 0.20f * e1);
        w += sh.hip * W * 0.5f * gauss(t - sRear, 0.1f);
        return w;
    };
    auto endRound = [&](float t) {  // 0 at the very ends, 1 a little way in
        return std::sqrt(smoothstepf(0.f, 0.07f, t)) * std::sqrt(smoothstepf(0.f, 0.05f, 1.f - t));
    };
    auto bottomY = [&](float t) { return (sh.clear + 0.06f * (1.f - smoothstepf(0.f, 0.09f, t)) + 0.07f * smoothstepf(0.9f, 1.f, t)) * H + (1.f - endRound(t)) * 0.05f * H; };
    auto topY = [&](float t) { float b = belt(t) - (1.f - endRound(t)) * 0.06f * H; return b + cf(t) * (sh.roof * H - b); };
    auto archY = [&](float z) {
        float best = -1e9f;
        for (float az : {axF, axR}) {
            float dz = z - az;
            if (std::fabs(dz) < archR) best = std::max(best, wheelR + std::sqrt(archR * archR - dz * dz));
        }
        return best;
    };

    // ---- stations, with thin rows at the door shut lines
    std::vector<float> ts;
    const int NSU = 96;
    for (int i = 0; i < NSU; i++) ts.push_back(i / float(NSU - 1));
    for (float az : {axF, axR}) {
        float tc = (zf - az) / L, span = archR * 1.25f / L;
        for (int k = -40; k <= 40; k++) ts.push_back(tc + span * k / 40.f);
    }
    std::vector<float> seams;
    float seamFront = sh.sA + 0.03f;
    float seamRear = std::min(sh.sC - 0.02f, sRear - archR / L - 0.03f);
    bool doors = s.body != BODY_HYPER;
    if (doors && seamRear > seamFront + 0.15f) { seams.push_back(seamFront); seams.push_back(seamRear); }
    for (float sm : seams) { ts.push_back(sm - 0.0016f); ts.push_back(sm + 0.0016f); }
    std::sort(ts.begin(), ts.end());
    for (auto& t : ts) t = clampf(t, 0.f, 1.f);
    ts.erase(std::unique(ts.begin(), ts.end(), [](float a, float b) { return std::fabs(a - b) < 0.0008f; }), ts.end());
    const int NS = int(ts.size());
    auto isSeam = [&](float t0, float t1) {
        for (float sm : seams) if (t0 >= sm - 0.0017f && t1 <= sm + 0.0017f) return true;
        return false;
    };

    // ---- profile: 9 control points per section, refined with Catmull-Rom
    const int NC = 9;
    const int sub[NC - 1] = {3, 3, 4, 3, 4, 4, 3, 2};
    int NP = 1;
    for (int k : sub) NP += k;
    std::vector<int> segOf(NP, 0);
    {
        int j = 0;
        for (int sgi = 0; sgi < NC - 1; sgi++) for (int k = 0; k < sub[sgi]; k++) segOf[j++] = sgi;
        segOf[NP - 1] = NC - 2;
    }
    std::vector<vec3> P(NS * NP), N(NS * NP, vec3(0, 0, 0));
    std::vector<float> cfs(NS);
    auto at = [&](int i, int j) -> vec3& { return P[i * NP + j]; };
    for (int i = 0; i < NS; i++) {
        float t = ts[i];
        float z = zf - t * L;
        float w = halfW(t), b = belt(t) - (1.f - endRound(t)) * 0.06f * H, top = topY(t), bot = bottomY(t), c = cf(t);
        float wTop = lerpf(w * 0.84f, w * sh.roofRatio, c);
        float crown = (0.03f + 0.025f * c) * H;
        float flare = 1.f + sh.flare * (gauss(t - sFront, 0.075f) + gauss(t - sRear, 0.085f));
        vec2 C[NC] = {
            {0, top + crown},
            {wTop * 0.55f, top + crown * 0.75f},
            {wTop, top - 0.012f},
            {w * 0.96f, b + 0.01f},
            {w * flare, b - 0.09f * H},
            {w * flare * 0.995f, lerpf(b - 0.09f * H, bot, 0.5f)},
            {w * 0.95f, bot + 0.07f * H},
            {w * 0.78f, bot},
            {0, bot},
        };
        int j = 0;
        for (int sgi = 0; sgi < NC - 1; sgi++) {
            vec2 p0 = C[std::max(sgi - 1, 0)], p1 = C[sgi], p2 = C[sgi + 1], p3 = C[std::min(sgi + 2, NC - 1)];
            for (int k = 0; k < sub[sgi]; k++) {
                float u = k / float(sub[sgi]);
                float u2 = u * u, u3 = u2 * u;
                vec2 q = (p1 * 2.f + (p2 - p0) * u + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * u2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * u3) * 0.5f;
                if (sgi == 0 || sgi == NC - 2) q.y = lerpf(p1.y, p2.y, u);  // keep roof/floor centre flat
                at(i, j++) = vec3(std::max(q.x, 0.f), q.y, z);
            }
        }
        at(i, NP - 1) = vec3(0, bot, z);
        cfs[i] = c;
    }
    // smooth normals for the +x half
    for (int i = 0; i + 1 < NS; i++)
        for (int j = 0; j + 1 < NP; j++) {
            vec3 A = at(i, j), B = at(i + 1, j), C = at(i + 1, j + 1), D = at(i, j + 1);
            vec3 n = cross(D - A, C - A) + cross(C - A, B - A);
            N[i * NP + j] += n; N[(i + 1) * NP + j] += n; N[(i + 1) * NP + j + 1] += n; N[i * NP + j + 1] += n;
        }
    for (auto& n : N) n = normalize(n);
    // wheel-arch cut-outs: lift the lower side up around each wheel (normals keep the panel shape)
    for (int i = 0; i < NS; i++) {
        float ay = archY(at(i, 0).z);
        int k = 0;
        for (int jj = 0; jj < NP; jj++) {
            if (segOf[jj] < 4) continue;
            vec3& q = at(i, jj);
            if (ay > q.y) {
                q.y = ay + 0.002f * k++;
                if (segOf[jj] >= 6) N[i * NP + jj] = vec3(0, -1, 0);
            }
        }
    }

    MeshBuilder b;
    float pillarS = (sh.sB + sh.sC) * 0.5f;
    auto aoAt = [&](vec3 p) {
        float ao = lerpf(0.55f, 1.f, smoothstepf(bottomY(0.5f), bottomY(0.5f) + 0.35f * H, p.y));
        if (archY(p.z) > 0) ao *= lerpf(0.6f, 1.f, smoothstepf(archY(p.z) - 0.05f, archY(p.z) + 0.25f, p.y));
        return ao;
    };
    for (int i = 0; i + 1 < NS; i++) {
        float t0 = ts[i], t1 = ts[i + 1];
        float cAvg = (cfs[i] + cfs[i + 1]) * 0.5f;
        bool slope = cfs[i] > 0.03f && cfs[i + 1] > 0.03f && (cfs[i] < 0.97f || cfs[i + 1] < 0.97f);
        bool pillar = std::fabs((t0 + t1) * 0.5f - pillarS) < 0.022f && s.body != BODY_TRUCK;
        bool seam = isSeam(t0, t1);
        for (int j = 0; j + 1 < NP; j++) {
            int sg = segOf[j];
            float matId = 1;
            if (sg <= 1) matId = slope ? 2 : ((sg == 0 && s.stripes) ? 5 : 1);
            else if (sg == 2) matId = cAvg > 0.5f ? (pillar ? 9 : 2) : 1;
            else if (sg == 3 && j == 0 + (sub[0] + sub[1] + sub[2]) && cAvg > 0.5f) matId = 4;  // chrome window trim
            else if (sg >= 7) matId = 9;
            else if (sg == 6 && (s.body == BODY_TRUCK || s.body == BODY_RALLY)) matId = 9;
            if (seam && sg >= 3 && sg <= 6) matId = 6;
            // the underside of an arch lift becomes the dark wheel well
            if (sg >= 4 && archY(at(i, j).z) > 0 && at(i, j + 1).y > bottomY(t0) + 0.15f && at(i, j).y > bottomY(t0) + 0.15f &&
                std::fabs(at(i, j).y - at(i, j + 1).y) < 0.02f) matId = 9;
            for (int side = 0; side < 2; side++) {
                float sx = side == 0 ? 1.f : -1.f;
                auto V = [&](int a, int c) { vec3 q = at(a, c); return vec3(q.x * sx, q.y, q.z); };
                auto Nn = [&](int a, int c) { vec3 q = N[a * NP + c]; return vec3(q.x * sx, q.y, q.z); };
                auto col = [&](int a, int c) { float ao = aoAt(at(a, c)); return vec4(ao, ao, ao, matId); };
                uint32_t A = b.add(V(i, j), Nn(i, j), col(i, j)), B = b.add(V(i + 1, j), Nn(i + 1, j), col(i + 1, j));
                uint32_t C = b.add(V(i + 1, j + 1), Nn(i + 1, j + 1), col(i + 1, j + 1)), D = b.add(V(i, j + 1), Nn(i, j + 1), col(i, j + 1));
                if (side == 0) { b.tri(A, D, C); b.tri(A, C, B); }
                else { b.tri(A, C, D); b.tri(A, B, C); }
            }
        }
    }
    // ---- end caps
    for (int cap = 0; cap < 2; cap++) {
        int i = cap == 0 ? 0 : NS - 1;
        float zc = at(i, 0).z;
        float yc = (at(i, 0).y + at(i, NP - 1).y) * 0.5f;
        vec3 n(0, 0, cap == 0 ? 1.f : -1.f);
        uint32_t c = b.add(vec3(0, yc, zc), n, vec4(1, 1, 1, 1));
        uint32_t first = uint32_t(b.v.size());
        for (int j = 0; j < NP; j++) b.add(at(i, j), n, vec4(1, 1, 1, segOf[j] >= 7 ? 9 : 1));
        for (int j = 0; j < NP; j++) b.add(vec3(-at(i, j).x, at(i, j).y, at(i, j).z), n, vec4(1, 1, 1, segOf[j] >= 7 ? 9 : 1));
        for (int j = 0; j + 1 < NP; j++) {
            uint32_t a = first + j, a1 = first + j + 1, m = first + NP + j, m1 = first + NP + j + 1;
            if (cap == 0) { b.tri(c, a1, a); b.tri(c, m, m1); }
            else { b.tri(c, a, a1); b.tri(c, m1, m); }
        }
    }

    // ---- surface patches (lights) that follow the body
    auto stationAt = [&](float t) {
        for (int i = 0; i + 1 < NS; i++)
            if (t <= ts[i + 1]) return i + (t - ts[i]) / std::max(ts[i + 1] - ts[i], 1e-5f);
        return float(NS - 1);
    };
    auto surf = [&](float fi, float fj, vec3& pos, vec3& nrm) {
        int i0 = std::clamp(int(fi), 0, NS - 2), j0 = std::clamp(int(fj), 0, NP - 2);
        float ti = clampf(fi - i0, 0.f, 1.f), tj = clampf(fj - j0, 0.f, 1.f);
        auto L2 = [&](const std::vector<vec3>& G) {
            return lerp(lerp(G[i0 * NP + j0], G[(i0 + 1) * NP + j0], ti), lerp(G[i0 * NP + j0 + 1], G[(i0 + 1) * NP + j0 + 1], ti), tj);
        };
        pos = L2(P);
        nrm = normalize(L2(N));
    };
    int jBelt = sub[0] + sub[1] + sub[2];
    int jShoulder = jBelt + sub[3];
    auto patch = [&](float t0, float t1, float j0, float j1, float off, vec4 col, int nt, int nu) {
        float i0 = stationAt(t0), i1 = stationAt(t1);
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            std::vector<vec3> pp((nt + 1) * (nu + 1)), nn((nt + 1) * (nu + 1));
            for (int a = 0; a <= nt; a++)
                for (int c = 0; c <= nu; c++) {
                    vec3 p, n;
                    surf(lerpf(i0, i1, a / float(nt)), lerpf(j0, j1, c / float(nu)), p, n);
                    p += n * off;
                    pp[a * (nu + 1) + c] = vec3(p.x * sx, p.y, p.z);
                    nn[a * (nu + 1) + c] = vec3(n.x * sx, n.y, n.z);
                }
            for (int a = 0; a < nt; a++)
                for (int c = 0; c < nu; c++) {
                    int k00 = a * (nu + 1) + c, k10 = (a + 1) * (nu + 1) + c, k11 = k10 + 1, k01 = k00 + 1;
                    uint32_t A = b.add(pp[k00], nn[k00], col), B = b.add(pp[k10], nn[k10], col);
                    uint32_t C = b.add(pp[k11], nn[k11], col), D = b.add(pp[k01], nn[k01], col);
                    if (side == 0) { b.tri(A, D, C); b.tri(A, C, B); }
                    else { b.tri(A, C, D); b.tri(A, B, C); }
                }
        }
    };
    // headlights: lens housing + bright inner element
    patch(0.004f, 0.075f, jBelt - 1.2f, jShoulder + 1.5f, 0.004f, vec4(1, 1, 1, 10), 5, 4);
    patch(0.016f, 0.034f, jBelt + 0.1f, jShoulder - 0.2f, 0.008f, vec4(0.75f, 0.73f, 0.66f, 3), 2, 2);
    patch(0.042f, 0.060f, jBelt + 0.1f, jShoulder - 0.2f, 0.008f, vec4(0.75f, 0.73f, 0.66f, 3), 2, 2);
    // tail lights
    patch(0.935f, 0.997f, jBelt - 0.6f, jShoulder + 1.3f, 0.004f, vec4(0.85f, 0.04f, 0.03f, 3), 4, 4);
    // side indicator + door handles
    if (doors) {
        float th = (seamFront + seamRear) * 0.5f + 0.06f;
        patch(th, th + 0.025f, jShoulder + 0.4f, jShoulder + 0.9f, 0.006f, vec4(1, 1, 1, 4), 1, 1);
    }

    // ---- front & rear faces
    {
        float zF = at(0, 0).z + 0.006f, zR = at(NS - 1, 0).z - 0.006f;
        float w0 = at(0, jShoulder).x, wT = at(NS - 1, jShoulder).x;
        float yTopF = at(0, jBelt).y, yBotF = at(0, NP - 1).y;
        float yTopR = at(NS - 1, jBelt).y, yBotR = at(NS - 1, NP - 1).y;
        vec4 black(1, 1, 1, 9), plate(1, 1, 1, 12), chrome(1, 1, 1, 4);
        auto faceQ = [&](float x0, float x1, float y0, float y1, float z, bool front, vec4 col) {
            if (y1 <= y0 + 0.01f) return;
            if (front) b.flatQuad(vec3(x1, y0, z), vec3(x1, y1, z), vec3(x0, y1, z), vec3(x0, y0, z), col);
            else b.flatQuad(vec3(x0, y0, z), vec3(x0, y1, z), vec3(x1, y1, z), vec3(x1, y0, z), col);
        };
        float fh = yTopF - yBotF;
        bool bigGrille = s.body == BODY_MUSCLE || s.body == BODY_TRUCK || s.body == BODY_GT;
        float gw = w0 * (bigGrille ? 0.55f : 0.36f);
        float gy0 = yBotF + fh * 0.30f, gy1 = yBotF + fh * (bigGrille ? 0.86f : 0.62f);
        faceQ(-gw, gw, gy0, gy1, zF, true, black);
        if (bigGrille) for (int k = 1; k < 4; k++) {
            float yy = lerpf(gy0, gy1, k / 4.f);
            faceQ(-gw, gw, yy - 0.006f, yy + 0.006f, zF + 0.002f, true, chrome);
        }
        faceQ(-w0 * 0.7f, w0 * 0.7f, yBotF + fh * 0.04f, yBotF + fh * 0.2f, zF, true, black);  // lower intake
        float rh = yTopR - yBotR;
        faceQ(-wT * 0.6f, wT * 0.6f, yBotR, yBotR + rh * 0.18f, zR, false, black);           // diffuser
        faceQ(-0.26f, 0.26f, yBotR + rh * 0.3f, yBotR + rh * 0.3f + 0.11f, zR, false, plate);  // plate
        faceQ(-wT * 0.5f, wT * 0.5f, yTopR - rh * 0.22f, yTopR - rh * 0.1f, zR, false, black);
        // exhaust tips
        float ex = s.body == BODY_HATCH ? 0.f : wT * 0.55f;
        for (int k = (s.body == BODY_HATCH ? 1 : -1); k <= 1; k += 2) {
            float x = k * ex + (s.body == BODY_HATCH ? -wT * 0.5f : 0.f);
            vec3 ep(x, yBotR + 0.07f, zR + 0.02f);
            b.tube(ep + vec3(0, 0, 0.12f), ep - vec3(0, 0, 0.06f), 0.045f, 0.05f, 12, chrome);
            b.flatQuad(ep - vec3(0.04f, 0.04f, 0.055f), ep + vec3(-0.04f, 0.04f, -0.055f), ep + vec3(0.04f, 0.04f, -0.055f), ep + vec3(0.04f, -0.04f, -0.055f), vec4(0.02f, 0.02f, 0.02f, 0));
        }
    }

    wheelPos[0] = vec3(track, wheelR, axF);
    wheelPos[1] = vec3(-track, wheelR, axF);
    wheelPos[2] = vec3(track, wheelR, axR);
    wheelPos[3] = vec3(-track, wheelR, axR);

    // ---- mirrors
    {
        float t = sh.sA + 0.05f;
        vec3 p, n;
        surf(stationAt(t), float(jBelt), p, n);
        for (int side = -1; side <= 1; side += 2) {
            vec3 c(side * (p.x + 0.09f), p.y + 0.07f, p.z);
            MeshBuilder pod;
            pod.sphere(vec3(0, 0, 0), 1.f, 2, vec4(1, 1, 1, 1));
            b.append(pod, translate(c) * scale(vec3(0.09f, 0.055f, 0.065f)));
            b.box(c + vec3(0, 0, -0.058f), vec3(0.065f, 0.035f, 0.004f), vec4(1, 1, 1, 4));
            b.box(vec3(side * (p.x + 0.02f), p.y + 0.04f, p.z + 0.01f), vec3(0.03f, 0.02f, 0.03f), vec4(1, 1, 1, 9));
        }
    }
    // ---- spoiler
    if (s.spoiler) {
        float t = 0.955f;
        float z = zf - t * L + 0.06f, y = topY(t) + 0.02f;
        bool tall = s.body == BODY_RALLY || s.body == BODY_HYPER || s.body == BODY_COUPE || s.body == BODY_MIDENGINE;
        float hgt = tall ? 0.22f : 0.06f;
        vec4 wing = (s.body == BODY_HYPER || s.body == BODY_MIDENGINE) ? vec4(1, 1, 1, 9) : vec4(1, 1, 1, 1);
        float span = W * 0.44f;
        b.box(vec3(0, y + hgt + 0.03f, z), vec3(span, 0.022f, 0.17f), wing);
        b.box(vec3(0, y + hgt + 0.06f, z - 0.15f), vec3(span, 0.012f, 0.025f), wing);  // gurney lip
        for (int side = -1; side <= 1; side += 2) {
            b.box(vec3(side * span, y + hgt + 0.04f, z), vec3(0.01f, 0.07f, 0.2f), wing);    // end plate
            b.box(vec3(side * W * 0.27f, y + hgt * 0.5f, z), vec3(0.02f, hgt * 0.5f + 0.02f, 0.05f), vec4(1, 1, 1, 9));
        }
    }
    // ---- body-specific extras
    if (s.body == BODY_MUSCLE) {
        float t = 0.2f;
        vec3 p, n;
        surf(stationAt(t), 0.5f, p, n);
        MeshBuilder bulge;
        bulge.sphere(vec3(0, 0, 0), 1.f, 2, vec4(1, 1, 1, 1));
        b.append(bulge, translate(vec3(0, p.y - 0.01f, p.z)) * scale(vec3(0.32f, 0.06f, 0.55f)));
    }
    if (s.body == BODY_TRUCK) {
        float y = sh.roof * H + 0.07f;
        vec4 rack(1, 1, 1, 9);
        b.box(vec3(0, y, zf - 0.52f * L), vec3(W * 0.42f, 0.025f, 0.03f), rack);
        b.box(vec3(0, y, zf - 0.80f * L), vec3(W * 0.42f, 0.025f, 0.03f), rack);
        b.box(vec3(W * 0.38f, y, zf - 0.66f * L), vec3(0.025f, 0.025f, 0.72f), rack);
        b.box(vec3(-W * 0.38f, y, zf - 0.66f * L), vec3(0.025f, 0.025f, 0.72f), rack);
        b.box(vec3(0, sh.nose * H * 0.45f, zf + 0.09f), vec3(W * 0.38f, 0.1f, 0.04f), rack);
        for (int side = -1; side <= 1; side += 2) b.box(vec3(side * W * 0.3f, sh.nose * H * 0.6f, zf + 0.09f), vec3(0.04f, 0.22f, 0.04f), rack);
    }
    if (s.body == BODY_RALLY) {
        for (int side = -1; side <= 1; side += 2)
            for (float az : {axF, axR})
                b.box(vec3(side * track, 0.17f, az - wheelR - 0.08f), vec3(0.15f, 0.12f, 0.008f), vec4(1, 1, 1, 9));
        float y = sh.roof * H + 0.02f;
        b.box(vec3(0, y + 0.03f, zf - 0.55f * L), vec3(0.18f, 0.04f, 0.16f), vec4(1, 1, 1, 1));  // roof scoop
    }
    if (s.body == BODY_HYPER || s.body == BODY_MIDENGINE) {
        // side intakes ahead of the rear wheels
        float t0 = sRear - archR / L - 0.11f, t1 = sRear - archR / L - 0.015f;
        patch(t0, t1, jShoulder + 0.6f, jShoulder + 3.2f, 0.003f, vec4(1, 1, 1, 9), 3, 3);
    }
    b.build(body);

    // ---------------------------------------------------------------- cockpit
    {
        MeshBuilder in;
        vec4 plastic(0.17f, 0.17f, 0.18f, 0), fabric(0.24f, 0.22f, 0.20f, 0), trim(1, 1, 1, 4), carpet(0.12f, 0.115f, 0.11f, 0);
        float tEye = std::min(sh.sB + 0.10f, std::max(sh.sC - 0.02f, sh.sB + 0.04f));
        float zEye = zf - tEye * L;
        float beltE = belt(tEye), roofY = sh.roof * H;
        float wIn = halfW(tEye) * 0.86f;
        float floorY = bottomY(tEye) + 0.12f;
        eye = vec3(halfW(tEye) * 0.42f, beltE + (roofY - beltE) * 0.55f, zEye);
        if (s.body == BODY_TRUCK) eye.y = beltE + (roofY - beltE) * 0.5f;
        float zWsBase = zf - sh.sA * L;
        float zDashRear = std::min(eye.z + 0.62f, zWsBase - 0.05f);
        float dashTop = beltE + 0.02f;
        // dashboard slab + top pad sloping up to the windscreen
        float dz0 = zDashRear, dz1 = zWsBase + 0.05f;
        in.box(vec3(0, dashTop - 0.17f, (dz0 + dz1) * 0.5f), vec3(wIn, 0.17f, std::max(0.05f, (dz1 - dz0) * 0.5f)), plastic);
        in.box(vec3(0, dashTop - 0.36f, dz0 + 0.06f), vec3(wIn * 0.96f, 0.05f, 0.08f), plastic);
        // instrument hood and two gauges facing the driver
        in.box(vec3(eye.x, dashTop + 0.05f, dz0 + 0.08f), vec3(0.2f, 0.045f, 0.1f), plastic);
        for (int g = -1; g <= 1; g += 2) {
            vec3 gc(eye.x + g * 0.085f, dashTop - 0.03f, dz0 - 0.003f);
            const int K = 20;
            for (int k = 0; k < K; k++) {
                float a0 = k / float(K) * 2 * PI, a1 = (k + 1) / float(K) * 2 * PI;
                vec3 p0 = gc + vec3(std::cos(a0), std::sin(a0), 0) * 0.07f, p1 = gc + vec3(std::cos(a1), std::sin(a1), 0) * 0.07f;
                in.flatTri(gc, p1, p0, vec4(0.05f, 0.05f, 0.06f, 0));
                if (k % 2 == 0 && (a0 < 4.2f || a0 > 5.3f)) {
                    vec3 q0 = gc + vec3(std::cos(a0), std::sin(a0), 0) * 0.058f, q1 = gc + vec3(std::cos(a0), std::sin(a0), 0) * 0.068f;
                    in.flatQuad(q0 + vec3(0, 0, -0.001f), q1 + vec3(0.004f, 0, -0.001f), q1 + vec3(0.004f, 0.004f, -0.001f), q0 + vec3(0, 0.004f, -0.001f), vec4(1.6f, 1.5f, 1.3f, 3));
                }
            }
            // needle
            vec3 n0 = gc + vec3(0, 0, -0.002f), n1 = gc + vec3(-0.045f * g, 0.035f, -0.002f);
            in.flatQuad(n0, n0 + vec3(0.004f, 0, 0), n1 + vec3(0.004f, 0, 0), n1, vec4(1.0f, 0.35f, 0.1f, 3));
            in.flatQuad(n0, n1, n1 + vec3(0.004f, 0, 0), n0 + vec3(0.004f, 0, 0), vec4(1.0f, 0.35f, 0.1f, 3));
        }
        // centre stack with a small screen, vents
        in.box(vec3(0, dashTop - 0.2f, dz0 - 0.02f), vec3(0.14f, 0.16f, 0.04f), plastic);
        in.box(vec3(0, dashTop - 0.1f, dz0 - 0.061f), vec3(0.09f, 0.05f, 0.002f), vec4(0.10f, 0.16f, 0.22f, 3));
        for (int v = -1; v <= 1; v += 2) in.box(vec3(v * wIn * 0.75f, dashTop - 0.08f, dz0 - 0.005f), vec3(0.06f, 0.025f, 0.004f), trim);
        // floor, console, seats
        in.box(vec3(0, floorY, eye.z - 0.2f), vec3(wIn, 0.02f, 0.9f), carpet);
        in.box(vec3(0, floorY + 0.14f, eye.z - 0.15f), vec3(0.09f, 0.14f, 0.45f), plastic);
        in.box(vec3(0, floorY + 0.29f, eye.z + 0.05f), vec3(0.025f, 0.03f, 0.04f), trim);  // gear lever knob
        for (int side = -1; side <= 1; side += 2) {
            float sx = side * std::fabs(eye.x);
            float seatY = std::max(floorY + 0.12f, eye.y - 0.62f);
            in.box(vec3(sx, seatY, eye.z - 0.05f), vec3(0.24f, 0.07f, 0.26f), fabric);
            mat4 back = translate(vec3(sx, seatY + 0.36f, eye.z - 0.34f)) * rotateX(-0.22f);
            in.box(vec3(0, 0, 0), vec3(0.24f, 0.34f, 0.07f), fabric, &back);
            mat4 head = translate(vec3(sx, seatY + 0.78f, eye.z - 0.42f)) * rotateX(-0.22f);
            in.box(vec3(0, 0, 0), vec3(0.13f, 0.09f, 0.06f), fabric, &head);
        }
        // rear-view mirror
        float zMirror = zf - (sh.sB - 0.015f) * L - 0.06f;
        in.box(vec3(0, roofY - 0.1f, zMirror), vec3(0.12f, 0.035f, 0.012f), plastic);
        in.box(vec3(0, roofY - 0.1f, zMirror - 0.013f), vec3(0.11f, 0.028f, 0.001f), trim);
        in.box(vec3(0, roofY - 0.05f, zMirror), vec3(0.01f, 0.03f, 0.01f), plastic);
        in.build(interior);

        // steering wheel: ring + three spokes + hub, axis along local z (facing the driver)
        MeshBuilder sw;
        vec4 leather(0.13f, 0.12f, 0.12f, 0);
        const int K = 28;
        float R = 0.18f;
        for (int k = 0; k < K; k++) {
            float a0 = k / float(K) * 2 * PI, a1 = (k + 1) / float(K) * 2 * PI;
            sw.tube(vec3(std::cos(a0) * R, std::sin(a0) * R, 0), vec3(std::cos(a1) * R, std::sin(a1) * R, 0), 0.02f, 0.02f, 8, leather);
        }
        sw.tube(vec3(0, 0, 0.02f), vec3(-R, 0, 0), 0.016f, 0.014f, 6, leather);
        sw.tube(vec3(0, 0, 0.02f), vec3(R, 0, 0), 0.016f, 0.014f, 6, leather);
        sw.tube(vec3(0, 0, 0.02f), vec3(0, -R, 0), 0.016f, 0.014f, 6, leather);
        sw.box(vec3(0, -0.01f, 0.035f), vec3(0.06f, 0.05f, 0.03f), leather);
        sw.box(vec3(0, -0.01f, 0.004f), vec3(0.025f, 0.018f, 0.002f), trim);
        sw.tube(vec3(0, 0, 0.06f), vec3(0, 0, 0.32f), 0.03f, 0.04f, 8, leather);  // column
        sw.build(steering);
        vec3 swc(eye.x, eye.y - 0.33f, eye.z + 0.44f);
        steeringBase = translate(swc) * rotateX(0.38f);
        // other camera anchors
        float tHood = sh.sA * 0.92f;
        hoodCam = vec3(0, topY(tHood) + 0.16f, zf - tHood * L);
        bumperCam = vec3(0, sh.nose * H * 0.62f, zf + 0.28f);
    }
    MeshBuilder wb;
    buildWheelMesh(wb, sh.spokes, s.body == BODY_TRUCK || s.body == BODY_RALLY);
    wb.build(wheel);
    MeshBuilder cb;
    buildCaliper(cb);
    cb.build(caliper);
}
