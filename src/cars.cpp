#include "cars.h"
#include <algorithm>

// All manufacturers and models are invented for this game.
const std::vector<CarSpec> kCars = {
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
struct Shape { float clear, nose, belt, tail, roof, sA, sB, sC, sD, roofRatio, hip; };

static Shape shapeFor(int body) {
    switch (body) {
        case BODY_HATCH:     return {0.11f, 0.43f, 0.63f, 0.66f, 0.98f, 0.22f, 0.42f, 0.86f, 1.00f, 0.82f, 0.00f};
        case BODY_RALLY:     return {0.13f, 0.44f, 0.63f, 0.67f, 0.98f, 0.22f, 0.42f, 0.85f, 1.00f, 0.83f, 0.03f};
        case BODY_MUSCLE:    return {0.10f, 0.55f, 0.66f, 0.64f, 0.98f, 0.40f, 0.53f, 0.70f, 0.87f, 0.74f, 0.03f};
        case BODY_COUPE:     return {0.09f, 0.42f, 0.65f, 0.68f, 0.98f, 0.34f, 0.50f, 0.64f, 0.88f, 0.72f, 0.04f};
        case BODY_MIDENGINE: return {0.08f, 0.36f, 0.64f, 0.76f, 0.98f, 0.20f, 0.40f, 0.52f, 0.80f, 0.66f, 0.05f};
        case BODY_TRUCK:     return {0.17f, 0.57f, 0.68f, 0.68f, 0.98f, 0.28f, 0.40f, 0.93f, 1.00f, 0.88f, 0.02f};
        case BODY_GT:        return {0.08f, 0.46f, 0.66f, 0.70f, 0.98f, 0.42f, 0.56f, 0.68f, 0.90f, 0.68f, 0.04f};
        case BODY_HYPER:     return {0.07f, 0.34f, 0.64f, 0.82f, 0.98f, 0.18f, 0.40f, 0.48f, 0.78f, 0.60f, 0.06f};
    }
    return shapeFor(BODY_COUPE);
}

static void buildWheelMesh(MeshBuilder& b) {
    const int S = 24;
    vec4 rubber(0.06f, 0.06f, 0.065f, 0), dark(0.09f, 0.09f, 0.1f, 0), chrome(0.7f, 0.7f, 0.72f, 4);
    for (int i = 0; i < S; i++) {
        float a0 = i / float(S) * 2 * PI, a1 = (i + 1) / float(S) * 2 * PI;
        vec3 d0(0, std::cos(a0), std::sin(a0)), d1(0, std::cos(a1), std::sin(a1));
        // tread (smooth normals)
        uint32_t i0 = b.add(d0 + vec3(0.5f, 0, 0), d0, rubber), i1 = b.add(d1 + vec3(0.5f, 0, 0), d1, rubber);
        uint32_t i2 = b.add(d1 + vec3(-0.5f, 0, 0), d1, rubber), i3 = b.add(d0 + vec3(-0.5f, 0, 0), d0, rubber);
        b.tri(i0, i2, i1);
        b.tri(i0, i3, i2);
        // outer sidewall (+x)
        b.flatQuad(vec3(0.5f, 0, 0) + d0, vec3(0.5f, 0, 0) + d1, vec3(0.5f, 0, 0) + d1 * 0.7f, vec3(0.5f, 0, 0) + d0 * 0.7f, rubber);
        // inner sidewall (-x)
        b.flatQuad(vec3(-0.5f, 0, 0) + d0, vec3(-0.5f, 0, 0) + d0 * 0.2f, vec3(-0.5f, 0, 0) + d1 * 0.2f, vec3(-0.5f, 0, 0) + d1, dark);
        // rim lip
        b.flatQuad(vec3(0.46f, 0, 0) + d0 * 0.7f, vec3(0.46f, 0, 0) + d1 * 0.7f, vec3(0.46f, 0, 0) + d1 * 0.62f, vec3(0.46f, 0, 0) + d0 * 0.62f, chrome);
        // spokes / recesses
        bool spoke = (i % 4) < 2;
        float x = spoke ? 0.44f : 0.36f;
        b.flatQuad(vec3(x, 0, 0) + d0 * 0.62f, vec3(x, 0, 0) + d1 * 0.62f, vec3(x, 0, 0) + d1 * 0.2f, vec3(x, 0, 0) + d0 * 0.2f, spoke ? chrome : dark);
        // hub
        b.flatTri(vec3(0.45f, 0, 0), vec3(0.45f, 0, 0) + d0 * 0.2f, vec3(0.45f, 0, 0) + d1 * 0.2f, chrome);
    }
}

void CarModel::build(const CarSpec& s) {
    Shape sh = shapeFor(s.body);
    const float L = s.length, W = s.width, H = s.height;
    const int NS = 30, NP = 9;
    float zf = L * 0.5f;
    float axF = s.wheelbase * 0.5f, axR = -s.wheelbase * 0.5f;
    float sRear = (zf - axR) / L;

    auto cf = [&](float t) { return smoothstepf(sh.sA, sh.sB, t) * (1.f - smoothstepf(sh.sC, std::max(sh.sD, sh.sC + 0.01f), t)); };
    auto belt = [&](float t) {
        float u = clampf(t / sh.sA, 0.f, 1.f);
        float y = lerpf(sh.nose, sh.belt, 1.f - (1.f - u) * (1.f - u));
        y = lerpf(y, sh.tail, smoothstepf(0.72f, 1.0f, t));
        return y * H;
    };
    auto halfW = [&](float t) {
        float w = W * 0.5f * (0.80f + 0.20f * std::sqrt(smoothstepf(0.f, 0.14f, t))) * (0.86f + 0.14f * (1.f - smoothstepf(0.88f, 1.f, t)));
        w += sh.hip * W * 0.5f * std::exp(-std::pow((t - sRear) / 0.09f, 2.f));
        return w;
    };
    auto bottomY = [&](float t) { return (sh.clear + 0.05f * (1.f - smoothstepf(0.f, 0.1f, t)) + 0.06f * smoothstepf(0.9f, 1.f, t)) * H; };
    auto topY = [&](float t) { float b = belt(t); return b + cf(t) * (sh.roof * H - b); };

    vec3 P[NS][NP];
    float cfs[NS];
    for (int i = 0; i < NS; i++) {
        float t = i / float(NS - 1);
        float z = zf - t * L;
        float w = halfW(t), b = belt(t), top = topY(t), bot = bottomY(t), c = cf(t);
        float wTop = lerpf(w * 0.86f, w * sh.roofRatio, c);
        float crown = (0.025f + 0.02f * c) * H;
        float stripeX = std::min(0.2f, wTop * 0.32f);
        P[i][0] = vec3(0, top + crown, z);
        P[i][1] = vec3(stripeX, top + crown * 0.8f, z);
        P[i][2] = vec3(wTop, top - 0.01f, z);
        P[i][3] = vec3(w * 0.97f, b, z);
        P[i][4] = vec3(w, b - 0.1f * H, z);
        P[i][5] = vec3(w, lerpf(b - 0.1f * H, bot, 0.55f), z);
        P[i][6] = vec3(w * 0.96f, bot + 0.08f * H, z);
        P[i][7] = vec3(w * 0.82f, bot, z);
        P[i][8] = vec3(0, bot, z);
        cfs[i] = c;
    }
    // smooth normals for the +x half
    vec3 N[NS][NP];
    for (auto& row : N) for (auto& n : row) n = vec3(0, 0, 0);
    for (int i = 0; i + 1 < NS; i++)
        for (int j = 0; j + 1 < NP; j++) {
            vec3 A = P[i][j], B = P[i + 1][j], C = P[i + 1][j + 1], D = P[i][j + 1];
            vec3 n = cross(D - A, C - A) + cross(C - A, B - A);
            N[i][j] += n; N[i + 1][j] += n; N[i + 1][j + 1] += n; N[i][j + 1] += n;
        }
    for (auto& row : N) for (auto& n : row) n = normalize(n);

    MeshBuilder b;
    vec4 paint(1, 1, 1, 1), stripe(1, 1, 1, 5), glass(0, 0, 0, 2), under(0.05f, 0.05f, 0.055f, 0), trim(0.08f, 0.08f, 0.09f, 0);
    float pillarS = (sh.sB + sh.sC) * 0.5f;
    for (int i = 0; i + 1 < NS; i++) {
        float t0 = i / float(NS - 1), t1 = (i + 1) / float(NS - 1);
        float cAvg = (cfs[i] + cfs[i + 1]) * 0.5f;
        bool slope = cfs[i] > 0.03f && cfs[i + 1] > 0.03f && (cfs[i] < 0.97f || cfs[i + 1] < 0.97f);
        bool pillar = std::fabs((t0 + t1) * 0.5f - pillarS) < 0.025f && s.body != BODY_TRUCK;
        for (int j = 0; j + 1 < NP; j++) {
            vec4 m = paint;
            if (j <= 1) m = slope ? glass : ((j == 0 && s.stripes) ? stripe : paint);
            else if (j == 2) m = (cAvg > 0.5f && !pillar) ? glass : paint;
            else if (j == 7) m = under;
            else if (j == 6 && (s.body == BODY_TRUCK || s.body == BODY_RALLY)) m = trim;
            for (int side = 0; side < 2; side++) {
                float sx = side == 0 ? 1.f : -1.f;
                auto V = [&](int a, int c) { return vec3(P[a][c].x * sx, P[a][c].y, P[a][c].z); };
                auto Nn = [&](int a, int c) { return vec3(N[a][c].x * sx, N[a][c].y, N[a][c].z); };
                uint32_t A = b.add(V(i, j), Nn(i, j), m), B = b.add(V(i + 1, j), Nn(i + 1, j), m);
                uint32_t C = b.add(V(i + 1, j + 1), Nn(i + 1, j + 1), m), D = b.add(V(i, j + 1), Nn(i, j + 1), m);
                if (side == 0) { b.tri(A, D, C); b.tri(A, C, B); }
                else { b.tri(A, C, D); b.tri(A, B, C); }
            }
        }
    }
    // end caps
    for (int cap = 0; cap < 2; cap++) {
        int i = cap == 0 ? 0 : NS - 1;
        float zc = P[i][0].z;
        float yc = (P[i][0].y + P[i][8].y) * 0.5f;
        vec3 n(0, 0, cap == 0 ? 1.f : -1.f);
        uint32_t c = b.add(vec3(0, yc, zc), n, paint);
        uint32_t first = uint32_t(b.v.size());
        for (int j = 0; j < NP; j++) b.add(P[i][j], n, paint);
        for (int j = 0; j < NP; j++) b.add(vec3(-P[i][j].x, P[i][j].y, P[i][j].z), n, paint);
        for (int j = 0; j + 1 < NP; j++) {
            uint32_t a = first + j, a1 = first + j + 1, m = first + NP + j, m1 = first + NP + j + 1;
            if (cap == 0) { b.tri(c, a1, a); b.tri(c, m, m1); }
            else { b.tri(c, a, a1); b.tri(c, m1, m); }
        }
    }
    // lights and grille
    {
        float zF = P[0][0].z + 0.012f, zR = P[NS - 1][0].z - 0.012f;
        float w0 = P[0][4].x, wT = P[NS - 1][4].x;
        float yN = P[0][3].y, yT = P[NS - 1][3].y;
        float bN = P[0][8].y, bT = P[NS - 1][8].y;
        vec4 head(1.0f, 0.97f, 0.88f, 3), tail(0.95f, 0.05f, 0.03f, 3), black(0.03f, 0.03f, 0.035f, 0);
        for (int side = -1; side <= 1; side += 2) {
            float x0 = side * w0 * 0.45f, x1 = side * w0 * 0.88f;
            float y0 = yN - 0.13f, y1 = yN - 0.035f;
            if (side > 0) b.flatQuad(vec3(x1, y0, zF), vec3(x1, y1, zF), vec3(x0, y1, zF), vec3(x0, y0, zF), head);
            else b.flatQuad(vec3(x0, y0, zF), vec3(x0, y1, zF), vec3(x1, y1, zF), vec3(x1, y0, zF), head);
            float tx0 = side * wT * 0.5f, tx1 = side * wT * 0.92f;
            float ty0 = yT - 0.13f, ty1 = yT - 0.03f;
            if (side > 0) b.flatQuad(vec3(tx0, ty0, zR), vec3(tx0, ty1, zR), vec3(tx1, ty1, zR), vec3(tx1, ty0, zR), tail);
            else b.flatQuad(vec3(tx1, ty0, zR), vec3(tx1, ty1, zR), vec3(tx0, ty1, zR), vec3(tx0, ty0, zR), tail);
        }
        float gw = w0 * (s.body == BODY_MUSCLE || s.body == BODY_TRUCK ? 0.42f : 0.34f);
        float gy0 = bN + 0.05f, gy1 = std::max(gy0 + 0.06f, yN - 0.15f);
        b.flatQuad(vec3(gw, gy0, zF), vec3(gw, gy1, zF), vec3(-gw, gy1, zF), vec3(-gw, gy0, zF), black);
        b.flatQuad(vec3(-wT * 0.48f, yT - 0.12f, zR), vec3(-wT * 0.48f, yT - 0.04f, zR), vec3(wT * 0.48f, yT - 0.04f, zR), vec3(wT * 0.48f, yT - 0.12f, zR), black);
        // diffuser / bumper shadow
        b.flatQuad(vec3(-wT * 0.6f, bT, zR), vec3(-wT * 0.6f, bT + 0.07f, zR), vec3(wT * 0.6f, bT + 0.07f, zR), vec3(wT * 0.6f, bT, zR), black);
    }
    // wheel arches (dark discs on the body sides; the wheels sit flush over them)
    wheelR = s.wheelR;
    wheelW = s.body == BODY_TRUCK ? 0.32f : 0.26f;
    float track = W * 0.5f - wheelW * 0.5f + 0.02f;
    float zs[2] = {axF, axR};
    for (int a = 0; a < 2; a++) {
        float t = (zf - zs[a]) / L;
        float wx = halfW(t) + 0.006f;
        float bot = bottomY(t);
        float R = wheelR * 1.16f;
        float ths = std::asin(clampf((bot - wheelR) / R, -1.f, 1.f));
        const int K = 14;
        vec4 arch(0.02f, 0.02f, 0.025f, 0);
        for (int side = -1; side <= 1; side += 2) {
            vec3 c(side * wx, wheelR, zs[a]);
            for (int k = 0; k < K; k++) {
                float a0 = lerpf(ths, PI - ths, k / float(K)), a1 = lerpf(ths, PI - ths, (k + 1) / float(K));
                vec3 p0 = c + vec3(0, std::sin(a0) * R, std::cos(a0) * R), p1 = c + vec3(0, std::sin(a1) * R, std::cos(a1) * R);
                if (side > 0) b.flatTri(c, p1, p0, arch);
                else b.flatTri(c, p0, p1, arch);
            }
        }
    }
    wheelPos[0] = vec3(track, wheelR, axF);
    wheelPos[1] = vec3(-track, wheelR, axF);
    wheelPos[2] = vec3(track, wheelR, axR);
    wheelPos[3] = vec3(-track, wheelR, axR);

    // mirrors
    {
        float t = sh.sA + 0.05f;
        float z = zf - t * L, w = halfW(t) * 0.97f, y = belt(t) + 0.05f;
        b.box(vec3(w + 0.06f, y, z), vec3(0.06f, 0.035f, 0.05f), paint);
        b.box(vec3(-w - 0.06f, y, z), vec3(0.06f, 0.035f, 0.05f), paint);
    }
    // spoiler
    if (s.spoiler) {
        float t = 0.95f;
        float z = zf - t * L + 0.05f, y = topY(t) + 0.02f;
        bool tall = s.body == BODY_RALLY || s.body == BODY_HYPER || s.body == BODY_COUPE;
        float hgt = tall ? 0.24f : 0.08f;
        vec4 wing = (s.body == BODY_HYPER || s.body == BODY_MIDENGINE) ? trim : paint;
        b.box(vec3(0, y + hgt + 0.03f, z), vec3(W * 0.43f, 0.025f, 0.16f), wing);
        b.box(vec3(W * 0.28f, y + hgt * 0.5f, z), vec3(0.025f, hgt * 0.5f + 0.02f, 0.06f), trim);
        b.box(vec3(-W * 0.28f, y + hgt * 0.5f, z), vec3(0.025f, hgt * 0.5f + 0.02f, 0.06f), trim);
    }
    // body-specific extras
    if (s.body == BODY_MUSCLE) {
        float t = 0.2f;
        b.box(vec3(0, belt(t) + 0.04f, zf - t * L), vec3(0.28f, 0.05f, 0.35f), paint);
    }
    if (s.body == BODY_TRUCK) {
        float y = sh.roof * H + 0.06f;
        b.box(vec3(0, y, zf - 0.55f * L), vec3(W * 0.42f, 0.03f, 0.03f), trim);
        b.box(vec3(0, y, zf - 0.78f * L), vec3(W * 0.42f, 0.03f, 0.03f), trim);
        b.box(vec3(W * 0.38f, y, zf - 0.66f * L), vec3(0.03f, 0.03f, 0.75f), trim);
        b.box(vec3(-W * 0.38f, y, zf - 0.66f * L), vec3(0.03f, 0.03f, 0.75f), trim);
        b.box(vec3(0, sh.nose * H * 0.55f, zf + 0.08f), vec3(W * 0.4f, 0.12f, 0.05f), trim);
    }
    if (s.body == BODY_RALLY) {
        for (int side = -1; side <= 1; side += 2)
            for (int a = 0; a < 2; a++)
                b.box(vec3(side * track, 0.16f, zs[a] - wheelR - 0.06f), vec3(0.14f, 0.12f, 0.01f), trim);
    }
    if (s.body == BODY_HYPER || s.body == BODY_MIDENGINE) {
        float t = sRear - 0.12f;
        float z = zf - t * L, w = halfW(t) + 0.004f, y = belt(t) - 0.2f * H;
        b.flatQuad(vec3(w, y - 0.12f, z + 0.25f), vec3(w, y - 0.12f, z - 0.25f), vec3(w, y + 0.06f, z - 0.25f), vec3(w, y + 0.06f, z + 0.15f), trim);
        b.flatQuad(vec3(-w, y - 0.12f, z + 0.25f), vec3(-w, y + 0.06f, z + 0.15f), vec3(-w, y + 0.06f, z - 0.25f), vec3(-w, y - 0.12f, z - 0.25f), trim);
    }
    b.build(body);
    MeshBuilder wb;
    buildWheelMesh(wb);
    wb.build(wheel);
}
