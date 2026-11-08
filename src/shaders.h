// GLSL 330 core shader sources.
#pragma once

static const char* kCommonGLSL = R"(
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uSkyAmb;
uniform vec3 uGroundAmb;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uTime;
uniform sampler2DShadow uShadow0;
uniform sampler2DShadow uShadow1;
uniform mat4 uShadowMat0;
uniform mat4 uShadowMat1;
uniform vec2 uShadowTexel;
uniform int uShadows;
uniform sampler2D tSky;   // equirectangular HDRI sky (assets/sky/sky.hdr)
uniform int uHdri;
uniform int uPhoto;       // photo ground textures installed
uniform vec3 uSH[9];      // sky light from the HDRI
uniform float uWet;       // road / ground wetness 0..1
uniform float uRain;      // rain (overcast) amount 0..1
uniform float uHdriW;     // how much of the HDRI sky shows (1 by day, 0 at sunset / night)
uniform float uNight;     // 0 day .. 1 night
uniform vec4 uHL;         // player headlights: position, on (w)
uniform samplerCube tProbe;  // environment captured around the player's car
uniform int uProbe;          // 1 while drawing a car near the probe
uniform float uProbeLod;
uniform vec3 uHLDir;
vec3 procSky(vec3 d){
    float t = clamp(d.y, 0.0, 1.0);
    vec3 c = mix(uSkyHorizon, uSkyTop, pow(t, 0.45));
    if (d.y < 0.0) c = mix(uSkyHorizon, uGroundAmb * 1.1, clamp(-d.y * 4.0, 0.0, 1.0));
    float s = max(dot(d, uSunDir), 0.0);
    c += uSunColor * (pow(s, 1400.0) * 14.0 + pow(s, 48.0) * 0.30 + pow(s, 5.0) * 0.12);
    c += uSunColor * vec3(0.25, 0.12, 0.02) * pow(s, 2.0) * (1.0 - t);
    return c;
}
vec3 shEval(vec3 d){
    return max(uSH[0] * 0.282095 + uSH[1] * 0.488603 * d.y + uSH[2] * 0.488603 * d.z + uSH[3] * 0.488603 * d.x
         + uSH[4] * 1.092548 * d.x * d.y + uSH[5] * 1.092548 * d.y * d.z + uSH[6] * 0.315392 * (3.0 * d.z * d.z - 1.0)
         + uSH[7] * 1.092548 * d.x * d.z + uSH[8] * 0.546274 * (d.x * d.x - d.y * d.y), vec3(0.0));
}
vec3 hdri(vec3 d, float lod){
    float u = atan(d.x, -d.z) * 0.15915494 + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) * 0.31830989;
    vec3 c = textureLod(tSky, vec2(u, v), lod).rgb;
    if (uHdriW < 0.999) c = mix(procSky(d), c, uHdriW);
    if (uRain > 0.0) {
        // overcast: a flat, darker grey sky that hides the sun (and dims with the daylight)
        float l = dot(c, vec3(0.299, 0.587, 0.114));
        float day = clamp(dot(uSkyAmb, vec3(0.333)) / 0.47, 0.04, 1.0);
        vec3 grey = vec3(0.44, 0.47, 0.52) * day * (0.6 + 0.4 * clamp(d.y * 1.6 + 0.25, 0.0, 1.0)) * (0.96 + 0.08 * min(l, 1.0));
        c = mix(c, grey, uRain * 0.97);
    }
    return c;
}

float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p); vec2 u = f*f*(3.0-2.0*f);
    return mix(mix(hash12(i), hash12(i+vec2(1,0)), u.x), mix(hash12(i+vec2(0,1)), hash12(i+vec2(1,1)), u.x), u.y);
}
float fbm2(vec2 p){ float s = 0.0, a = 0.5; for (int i = 0; i < 4; i++){ s += vnoise(p)*a; p = p*2.07 + vec2(1.7, 9.2); a *= 0.5; } return s / 0.9375; }

float pcf(sampler2DShadow s, vec3 p, float texel){
    float sum = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            sum += texture(s, vec3(p.xy + vec2(x, y) * texel, p.z));
    return sum / 9.0;
}
float pcf5(sampler2DShadow s, vec3 p, float texel){
    float sum = 0.0;
    for (int x = -2; x <= 2; x++)
        for (int y = -2; y <= 2; y++)
            sum += texture(s, vec3(p.xy + vec2(x, y) * texel * 0.85, p.z));
    return sum / 25.0;
}
float shadowFactor0(vec3 wp, vec3 n);
float shadowFactor(vec3 wp, vec3 n){ return mix(shadowFactor0(wp, n), 1.0, uRain * 0.8); }
float shadowFactor0(vec3 wp, vec3 n){
    if (uShadows == 0) return 1.0;
    vec4 a = uShadowMat0 * vec4(wp + n * 0.05, 1.0);
    vec3 pa = a.xyz * 0.5 + 0.5;
    vec2 ea = abs(pa.xy - 0.5) * 2.0;
    float ma = max(ea.x, ea.y);
    vec4 b = uShadowMat1 * vec4(wp + n * 0.3, 1.0);
    vec3 pb = b.xyz * 0.5 + 0.5;
    vec2 eb = abs(pb.xy - 0.5) * 2.0;
    float mb = max(eb.x, eb.y);
    float s1 = 1.0;
    if (mb < 1.0 && pb.z < 1.0) s1 = mix(pcf(uShadow1, vec3(pb.xy, pb.z - 0.00025), uShadowTexel.y), 1.0, smoothstep(0.85, 1.0, mb));
    if (ma < 0.98 && pa.z < 1.0) {
        float s0 = pcf5(uShadow0, vec3(pa.xy, pa.z - 0.00010), uShadowTexel.x);
        return mix(s0, s1, smoothstep(0.75, 0.98, ma));
    }
    return s1;
}
vec3 skyColor(vec3 d){
    if (uHdri == 1 && d.y > -0.02) return hdri(vec3(d.x, max(d.y, 0.002), d.z), 0.0);
    return procSky(d);
}
// environment used for reflections (sky above, tree line at the horizon, ground below)
vec3 envColor(vec3 R){
    if (uProbe == 1) return textureLod(tProbe, R, uProbeLod).rgb;
    if (R.y > 0.0) {
        vec3 s = uHdri == 1 ? hdri(R, 1.5) : skyColor(R);
        float az = atan(R.x, R.z);
        float line = 0.035 + 0.05 * vnoise(vec2(az * 9.0, 1.0)) + 0.03 * vnoise(vec2(az * 31.0, 3.0));
        float band = 1.0 - smoothstep(line - 0.01, line + 0.01, R.y);
        vec3 trees = vec3(0.10, 0.11, 0.06) * (uSkyAmb + uSunColor * 0.25);
        return mix(s, trees, band);
    }
    return mix(vec3(0.16, 0.14, 0.10) * (uSkyAmb + uSunColor * 0.4), vec3(0.09, 0.08, 0.06), clamp(-R.y * 2.0, 0.0, 1.0));
}
uniform float uClipY;
vec3 applyFog(vec3 col, vec3 wp){
    if (wp.y < uClipY) discard;
    vec3 v = wp - uCamPos; float dist = length(v); vec3 dir = v / max(dist, 0.001);
    float hf = exp(-max(wp.y - 15.0, 0.0) * 0.005);
    float f = 1.0 - exp(-dist * uFogDensity * (0.5 + 0.9 * hf));
    float s = pow(max(dot(dir, uSunDir), 0.0), 5.0);
    vec3 fogBase = uFogColor;
    if (uHdri == 1) {
        // distance haze takes the colour of the sky behind it (bluer up high, warmer near the sun)
        vec3 hz = hdri(normalize(vec3(dir.x, max(dir.y, 0.03) + 0.05, dir.z)), 6.0);
        fogBase = mix(uFogColor, hz * vec3(0.92, 0.96, 1.04), 0.75);
    }
    vec3 fc = mix(fogBase, uSunColor * vec3(0.85, 0.7, 0.5) + vec3(0.1) * (1.0 - uNight), s * (uHdri == 1 ? 0.3 : 0.55));
    // player headlights: a spot of light thrown down the road ahead
    if (uHL.w > 0.0) {
        vec3 L = wp - uHL.xyz;
        float dl = length(L);
        float cs = dot(L / max(dl, 0.01), uHLDir);
        float spot = smoothstep(0.86, 0.96, cs) * smoothstep(70.0, 6.0, dl) / (1.0 + dl * dl * 0.0025);
        float amb = max(dot(uSkyAmb, vec3(0.333)), 0.03);
        vec3 alb = clamp(col / amb, 0.0, 1.2);
        col += alb * vec3(1.0, 0.94, 0.82) * spot * uHL.w * 1.6;
    }
    return mix(col, fc, clamp(f, 0.0, 1.0));
}
vec3 gradAmb(vec3 n){ return mix(uGroundAmb, uSkyAmb, n.y * 0.5 + 0.5); }
vec3 ambient(vec3 n){
    vec3 a = uHdri == 1 ? mix(gradAmb(n), shEval(n), uHdriW) : gradAmb(n);
    // light bounced off the sunlit ground fills sides and undersides (car flanks, cliffs, trunks)
    a += uSunColor * vec3(0.95, 0.85, 0.7) * max(uSunDir.y, 0.0) * 0.07 * (0.5 - 0.5 * n.y);
    return a;
}
vec3 lighting(vec3 albedo, vec3 n, float shadow){
    float ndl = max(dot(n, uSunDir), 0.0);
    albedo *= 1.0 - 0.3 * uWet * smoothstep(0.4, 0.9, n.y);  // wet ground soaks up light
    return albedo * (ambient(n) + uSunColor * ndl * shadow);
}
// expanding rain-drop rings on standing water: returns a normal offset (xz)
vec2 rainRipples(vec2 p){
    vec2 off = vec2(0.0);
    for (int layer = 0; layer < 2; layer++) {
        vec2 q = p * (layer == 0 ? 2.2 : 3.7) + float(layer) * 17.3;
        vec2 cell = floor(q), f = fract(q);
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                vec2 c = cell + vec2(i, j);
                float h = hash12(c);
                vec2 ctr = vec2(i, j) + vec2(hash12(c + 3.1), hash12(c + 7.7)) * 0.8 + 0.1 - f;
                float t = fract(uTime * 0.9 + h);
                float d = length(ctr);
                float r = t * 0.55;
                float ring = sin((d - r) * 60.0) * exp(-abs(d - r) * 30.0) * (1.0 - t);
                off += (d > 1e-3 ? -ctr / d : vec2(0.0)) * ring;
            }
    }
    return off * 0.25;
}

// ---- car liveries, drawn procedurally in body space (x left, y up from the ground, z forward)
uniform int uLivery;       // 0 none
uniform vec3 uLivCol;      // livery colour
uniform vec3 uCarDims;     // length, width, height
uniform int uLivNum;       // race number
uniform float uWinTint;    // window tint 0..1
float lband(float v, float a, float b) {
    float w = fwidth(v) * 0.75 + 1e-4;
    return smoothstep(a - w, a + w, v) * (1.0 - smoothstep(b - w, b + w, v));
}
float lstep(float a, float v) { float w = fwidth(v) * 0.75 + 1e-4; return smoothstep(a - w, a + w, v); }
// seven-segment digit, p in [0,1]x[0,1.8]
float seg7(vec2 p, int d) {
    if (d < 0) return 0.0;
    const int bits[10] = int[10](0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F);
    int b = bits[d];
    float t = 0.2, m = 0.0;
    // a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle
    if ((b & 1) != 0)  m = max(m, lband(p.x, 0.0, 1.0) * lband(p.y, 1.8 - t, 1.8));
    if ((b & 2) != 0)  m = max(m, lband(p.x, 1.0 - t, 1.0) * lband(p.y, 0.9, 1.8));
    if ((b & 4) != 0)  m = max(m, lband(p.x, 1.0 - t, 1.0) * lband(p.y, 0.0, 0.9));
    if ((b & 8) != 0)  m = max(m, lband(p.x, 0.0, 1.0) * lband(p.y, 0.0, t));
    if ((b & 16) != 0) m = max(m, lband(p.x, 0.0, t) * lband(p.y, 0.0, 0.9));
    if ((b & 32) != 0) m = max(m, lband(p.x, 0.0, t) * lband(p.y, 0.9, 1.8));
    if ((b & 64) != 0) m = max(m, lband(p.x, 0.0, 1.0) * lband(p.y, 0.9 - t * 0.5, 0.9 + t * 0.5));
    return m;
}
vec3 carbonWeave(vec3 o) {
    vec2 q = (o.xz + vec2(o.y * 0.3)) * 38.0;
    float tw = step(0.5, fract((floor(q.x) + floor(q.y)) * 0.5));
    float fib = mix(fract(q.x), fract(q.y), tw);
    return vec3(0.022, 0.023, 0.026) * (0.75 + 0.6 * smoothstep(0.1, 0.5, fib) * (1.0 - smoothstep(0.5, 0.9, fib)));
}
vec3 applyLivery(vec3 a, vec3 o, float k) {
    if (uLivery == 0) return a;
    float L = uCarDims.x, W = uCarDims.y, H = uCarDims.z;
    float ax = abs(o.x) / (W * 0.5), zy = o.z / (L * 0.5), hy = o.y / H;
    vec3 lc = uLivCol;
    float m = 0.0;
    if (uLivery == 1) {            // twin racing stripes over the top
        m = lband(abs(o.x), 0.055, 0.215) * lstep(0.32, hy);
    } else if (uLivery == 2) {     // contrast roof
        m = lstep(0.83, hy) * lstep(-0.75, zy) * (1.0 - lstep(0.75, zy));
    } else if (uLivery == 3) {     // two-tone lower body with a pinstripe
        m = 1.0 - lstep(0.36, hy);
        float pin = lband(hy, 0.385, 0.40);
        a = mix(a, vec3(0.9), pin);
    } else if (uLivery == 4) {     // raked shoulder stripes
        float c = 0.55 + 0.05 * zy;
        m = (lband(hy, c - 0.04, c + 0.035) + lband(hy, c - 0.085, c - 0.065)) * lstep(0.62, ax) * lband(zy, -0.97, 0.97);
    } else if (uLivery == 5) {     // race number roundel on the doors + a hood band
        vec2 p = vec2(-(o.z - L * 0.03) * sign(o.x), o.y - H * 0.47);
        float r = length(p);
        float side = lstep(0.6, ax);
        float disc = (1.0 - lstep(0.29, r)) * side;
        float ring = lband(r, 0.29, 0.335) * side;
        int n = clamp(uLivNum, 0, 99);
        int d0 = n / 10, d1 = n - d0 * 10;
        if (d0 == 0) d0 = -1;
        float s = 0.19 / 1.8;
        vec2 q = p / s + vec2(0.0, 0.9);
        float dig = d0 < 0 ? seg7(q + vec2(0.5, 0.0), d1) : max(seg7(q + vec2(1.15, 0.0), d0), seg7(q - vec2(0.15, 0.0), d1));
        a = mix(a, vec3(0.92), disc);
        a = mix(a, vec3(0.02), dig * disc);
        m = ring + lband(abs(o.x), 0.0, 0.14) * lstep(0.5, hy) * lstep(0.35, zy);
    } else if (uLivery == 6) {     // speed slashes down the flanks
        float f = fract((o.z * 1.1 + o.y * 0.9) * 1.25);
        m = lband(f, 0.0, 0.32) * lstep(0.58, ax) * lband(zy, -0.92, 0.05) * lband(hy, 0.2, 0.62);
    } else if (uLivery == 7) {     // carbon bonnet
        float hood = lstep(0.40, zy) * lstep(0.45, hy) * (1.0 - lstep(0.8, ax));
        return mix(a, carbonWeave(o), hood);
    } else if (uLivery == 8) {     // diagonal split fade
        m = lstep(0.0, -zy * 0.9 + (hy - 0.5) * 0.7 - 0.05);
    } else if (uLivery == 9) {     // chequered tail
        vec2 q = ax > 0.6 ? vec2(o.z, o.y) : vec2(o.x, o.z);
        vec2 cq = floor(q / 0.16);
        float chk = mod(cq.x + cq.y, 2.0);
        float zone = 1.0 - lstep(-0.32, zy);
        a = mix(a, vec3(0.02) * k, zone * (1.0 - chk));
        m = zone * chk;
    } else if (uLivery == 10) {    // hot-rod flames licking back from the nose
        float h = 0.62 + (zy - 0.6) * 0.45 + 0.06 * sin(o.z * 15.0 + 1.3) + 0.03 * sin(o.z * 37.0);
        float fl = (1.0 - lstep(h, hy)) * lstep(-0.45, zy) * lstep(0.25, hy);
        float core = (1.0 - lstep(h - 0.09, hy)) * fl;
        a = mix(a, lc * k, fl);
        return mix(a, vec3(1.0, 0.82, 0.18) * k, core);
    } else if (uLivery == 11) {    // three-tone camouflage
        float n = vnoise(o.xz * 1.6 + o.y * 1.3) * 0.65 + vnoise(o.zy * 3.1 + o.x * 2.0) * 0.35;
        a = mix(a, lc * k * 0.55, lstep(0.58, n));
        m = lband(n, 0.36, 0.58);
    } else if (uLivery == 12) {    // hex grid
        vec2 q = (ax > 0.6 ? vec2(o.z, o.y) : vec2(o.x, o.z)) * 5.0;
        vec2 r = vec2(1.0, 1.732);
        vec2 ha = mod(q, r) - r * 0.5, hb = mod(q - r * 0.5, r) - r * 0.5;
        vec2 g = dot(ha, ha) < dot(hb, hb) ? ha : hb;
        vec2 ag = abs(g);
        float hd = max(dot(ag, normalize(vec2(1.0, 1.732))), ag.x);
        m = lstep(0.43, hd) * lstep(0.3, hy) * (1.0 - lstep(0.2, zy) * 0.0);
    } else if (uLivery == 13) {    // colour fade from the sills up
        m = 1.0 - smoothstep(0.12, 0.62, hy);
    } else if (uLivery == 14) {    // Le Mans hoop: a band right round the middle of the car
        m = lband(zy, -0.1, 0.12) + lband(zy, 0.15, 0.18) + lband(zy, -0.16, -0.13);
    }
    return mix(a, lc * k, clamp(m, 0.0, 1.0));
}
)";

// ---------------------------------------------------------------- static lit geometry
static const char* kLitVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vPos; out vec3 vNrm; out vec4 vCol; out vec3 vObj;
void main(){
    vec4 wp = uModel * vec4(aPos, 1.0);
    vPos = wp.xyz;
    vNrm = mat3(uModel) * aNrm;
    vCol = aCol;
    vObj = aPos;
    gl_Position = uViewProj * wp;
}
)";

static const char* kTerrainFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
uniform float uWater;
uniform sampler2D tGrass, tDirt, tRock, tRockN;
uniform sampler2D tSand, tLeafGround, tGrassN, tForestN;
uniform sampler2D tHeight;
uniform float uHalf;
uniform float uFarCut;  // far-terrain pass: skip what the detailed chunks already cover
uniform vec4 uFuji;     // Fuji: centre x, base height, centre z, skirt radius
out vec4 FragColor;
// caustics: bright wavy lines of sunlight focused by the surface onto the bed
float caustic(vec2 p, float t) {
    float c = 0.0;
    for (int i = 0; i < 2; i++) {
        vec2 q = p * (0.55 + 0.4 * float(i)) + vec2(t * 0.35, t * 0.22) * (i == 0 ? 1.0 : -1.3);
        float n1 = vnoise(q), n2 = vnoise(q * 1.9 + 3.7);
        c += pow(1.0 - abs(n1 + n2 - 1.0), 8.0);
    }
    return c;
}
vec3 tri(sampler2D t, vec3 p, vec3 w, float s){
    return texture(t, p.zy * s).rgb * w.x + texture(t, p.xz * s).rgb * w.y + texture(t, p.xy * s).rgb * w.z;
}
void main(){
    vec3 n = normalize(vNrm);
    vec3 p = vPos;
    if (uFarCut > 0.0 && length(p.xz - uCamPos.xz) < uFarCut) discard;
    float dist = length(p - uCamPos);
    float slope = 1.0 - n.y;
    float macro = fbm2(p.xz * 0.0035);
    float macro2 = vnoise(p.xz * 0.021);
    float macro3 = vnoise(p.xz * 0.11);

    vec3 grass, dirt;
    vec3 detailN = vec3(0.0);
    if (uPhoto == 1) {
        // photo-scanned ground: two scales and a rotation hide the tiling
        mat2 rot = mat2(0.8, -0.6, 0.6, 0.8);
        vec3 g1 = texture(tGrass, p.xz / 7.0).rgb;
        vec3 g2 = texture(tGrass, rot * p.xz / 31.0 + 0.37).rgb;
        grass = mix(g1, g2, 0.3 + 0.35 * smoothstep(40.0, 220.0, dist));
        grass *= mix(vec3(0.95, 1.02, 0.88), vec3(1.08, 1.0, 0.86), smoothstep(0.25, 0.75, macro));
        grass *= (0.88 + 0.22 * macro2) * vec3(0.84, 0.88, 0.80);
        vec3 fg = texture(tDirt, p.xz / 4.0).rgb;
        vec3 lv = texture(tLeafGround, rot * p.xz / 3.5).rgb;
        dirt = mix(fg, lv, smoothstep(0.35, 0.65, vnoise(p.xz * 0.045))) * (0.92 + 0.16 * macro3);
        vec3 gn = texture(tGrassN, p.xz / 7.0).xyz * 2.0 - 1.0;
        detailN = vec3(gn.x, 0.0, -gn.y) * (1.0 - smoothstep(30.0, 120.0, dist)) * 0.6;
    } else {
        vec3 g1 = texture(tGrass, p.xz / 6.5).rgb;
        vec3 g2 = texture(tGrass, p.xz / 37.0 + 0.37).rgb;
        grass = mix(g1, g2, 0.35 + 0.25 * smoothstep(60.0, 250.0, dist));
        grass *= mix(vec3(0.86, 0.95, 0.72), vec3(1.10, 0.98, 0.80), smoothstep(0.25, 0.75, macro));
        grass *= 0.82 + 0.3 * macro2;
        dirt = texture(tDirt, p.xz / 5.0).rgb * (0.9 + 0.2 * macro3);
    }
    float dirtW = smoothstep(0.6, 0.8, fbm2(p.xz * 0.025) + macro3 * 0.15);
    vec3 c = mix(grass, dirt, dirtW * 0.75);
    if (vCol.a > 1.5) {
        // town ground: concrete slabs, kerbs and stains
        bool brick = vCol.r > vCol.b + 0.1;  // shopping-street pavement: small red pavers
        vec2 q = p.xz / (brick ? vec2(0.6, 0.3) : vec2(3.0));
        if (brick) q.x += mod(floor(q.y), 2.0) * 0.5;
        vec2 f = fract(q), id = floor(q);
        float joint = 1.0 - smoothstep(0.0, 0.03, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)));
        float tone = 0.88 + 0.2 * fract(sin(dot(id, vec2(12.9898, 78.233))) * 43758.5453);
        vec3 con = vec3(0.36, 0.355, 0.345) * tone * (1.0 - joint * 0.35) * (0.85 + 0.25 * vnoise(p.xz * 0.6));
        if (uPhoto == 1) con = mix(con, texture(tDirt, p.xz / 6.0).rgb * 0.0 + con, 1.0);
        con *= vCol.rgb / vec3(0.50, 0.49, 0.47);
        c = mix(con, con * 0.7, smoothstep(0.65, 0.85, fbm2(p.xz * 0.08)) * 0.6);
    } else
    c = mix(c, vCol.rgb * (0.8 + 0.4 * macro3) * (texture(tDirt, p.xz / 3.0).rgb * 1.6), vCol.a);

    // rice paddies in the western countryside: level plots of golden autumn rice (some still green,
    // some harvested to stubble, some flooded), separated by narrow grassy dikes
    if (vCol.a < 0.5 && p.x < -1250.0 && p.x > -3350.0 && p.z > -1450.0 && p.z < 470.0 && p.y < 150.0 && n.y > 0.985) {
        float edge = min(min(p.x + 3350.0, -1250.0 - p.x), min(p.z + 1450.0, 470.0 - p.z));
        float pk = smoothstep(0.0, 60.0, edge) * smoothstep(0.985, 0.992, n.y) * (1.0 - smoothstep(0.25, 0.45, vCol.a));
        vec2 q = p.xz / vec2(36.0, 24.0);
        vec2 id = floor(q), f = fract(q);
        float hsh = fract(sin(dot(id, vec2(41.3, 289.1))) * 43758.5);
        float dk = min(min(f.x, 1.0 - f.x) * 36.0, min(f.y, 1.0 - f.y) * 24.0);
        float dike = 1.0 - smoothstep(0.5, 1.1, dk);
        float rows = 0.5 + 0.5 * sin((hsh < 0.5 ? p.x : p.z) * 6.0);
        vec3 golden = vec3(0.62, 0.50, 0.17) * (0.9 + 0.2 * vnoise(p.xz * 0.5));
        vec3 greenR = vec3(0.30, 0.44, 0.13);
        vec3 stubble = vec3(0.42, 0.33, 0.20);
        vec3 water = vec3(0.10, 0.14, 0.18);
        vec3 crop = hsh < 0.55 ? golden : hsh < 0.75 ? greenR : stubble;
        vec3 plot = mix(water * 1.3, crop, hsh > 0.9 ? 0.15 : 0.6 + 0.4 * rows);
        c = mix(c, mix(plot, grass * 0.9, dike), pk);
    }
    n = normalize(n + detailN * (1.0 - slope));
    vec3 w = pow(abs(n), vec3(5.0)); w /= (w.x + w.y + w.z);
    float rs = uPhoto == 1 ? 1.0 / 7.0 : 1.0 / 11.0;
    vec3 rock = tri(tRock, p, w, rs) * vec3(1.0, 0.97, 0.92);
    vec3 nx = texture(tRockN, p.zy * rs).xyz * 2.0 - 1.0;
    vec3 ny = texture(tRockN, p.xz * rs).xyz * 2.0 - 1.0;
    vec3 nz = texture(tRockN, p.xy * rs).xyz * 2.0 - 1.0;
    vec3 rn = normalize(n + (vec3(0.0, nx.y, nx.x) * w.x + vec3(ny.x, 0.0, ny.y) * w.y + vec3(nz.x, nz.y, 0.0) * w.z) * 0.9);
    // landform from the height field: gullies (concave) collect water and loose stones,
    // ridges and spurs (convex) are bare and lighter
    vec2 huv = (p.xz + uHalf) / (2.0 * uHalf);
    float tx = 1.0 / 3200.0;
    float h0 = texture(tHeight, huv).r;
    float lap = texture(tHeight, huv + vec2(tx * 3.0, 0)).r + texture(tHeight, huv - vec2(tx * 3.0, 0)).r
              + texture(tHeight, huv + vec2(0, tx * 3.0)).r + texture(tHeight, huv - vec2(0, tx * 3.0)).r - 4.0 * h0;
    float gully = smoothstep(0.25, 1.6, lap) * smoothstep(0.08, 0.3, slope);
    float ridge = smoothstep(0.25, 1.6, -lap);

    // rock: horizontal strata, warm and cool bands, scree fans below the cliffs
    float warp = vnoise(p.xz * 0.04) * 4.0 + vnoise(p.xz * 0.011) * 9.0;
    float strata = sin(p.y * 0.85 + macro * 9.0 + warp) * 0.65 + sin(p.y * 2.3 + warp * 1.7) * 0.35;
    rock *= mix(vec3(0.88, 0.86, 0.84), vec3(1.05, 1.02, 0.97), smoothstep(-0.5, 0.7, strata));
    rock *= mix(vec3(1.02, 0.98, 0.93), vec3(0.92, 0.96, 1.02), smoothstep(0.3, 0.7, macro2));
    rock *= 0.9 + 0.22 * ridge;
    float r = smoothstep(0.30, 0.46, slope + (macro3 - 0.5) * 0.12 + ridge * 0.06);
    vec3 scree = tri(tRock, p * 3.1, w, rs) * vec3(0.78, 0.76, 0.73);
    float sc = smoothstep(0.16, 0.3, slope) * (1.0 - r) * smoothstep(0.45, 0.75, macro3 + gully * 0.5) * smoothstep(60.0, 140.0, p.y);
    c = mix(c, scree, sc);
    c = mix(c, rock, r);
    c *= 1.0 - gully * 0.22;
    n = normalize(mix(n, rn, r));

    // Fuji: bare red-brown volcanic scoria above the treeline, snow on the upper third streaming
    // down the radial gullies, a flat white summit
    float fujiM = 0.0, fujiSnow = 0.0;
    if (uFuji.w > 0.0) {
        vec2 fv = p.xz - uFuji.xz;
        float fd = length(fv);
        fujiM = 1.0 - smoothstep(uFuji.w * 0.62, uFuji.w * 0.9, fd);
        if (fujiM > 0.0) {
            float hrel = p.y - uFuji.y;
            float ang = atan(fv.y, fv.x);
            float rib = sin(ang * 34.0 + 2.5 * sin(ang * 7.0 + fd * 0.0021) + 1.7 * sin(ang * 3.0 - fd * 0.0013));
            float fine = sin(ang * 140.0 + 3.0 * sin(ang * 23.0 + fd * 0.01)) * 0.5 + 0.5;
            vec3 scoria = mix(vec3(0.20, 0.12, 0.10), vec3(0.33, 0.20, 0.15), vnoise(p.xz * 0.03)) * (0.8 + 0.4 * vnoise(p.xz * 0.35));
            scoria *= 0.75 + 0.5 * tri(tRock, p, w, rs * 0.7).r;
            scoria *= 0.9 + 0.15 * rib;  // ridges a touch lighter, gullies darker
            float bare = smoothstep(220.0, 360.0, hrel + vnoise(p.xz * 0.01) * 60.0);
            c = mix(c, scoria, bare * fujiM);
            n = normalize(mix(n, normalize(n + vec3(rn.x - n.x, 0.0, rn.z - n.z) * 0.4), bare * fujiM));
            // snow: comes lower down the gullies, in fine streaks
            float start = 610.0 - rib * 80.0 - fine * 45.0 - (vnoise(vec2(ang * 9.0, fd * 0.004)) - 0.5) * 70.0;
            fujiSnow = smoothstep(start, start + 25.0, hrel) * fujiM;
            fujiSnow = max(fujiSnow, smoothstep(880.0, 900.0, hrel) * fujiM);  // summit cap
        }
    }
    // snow settles where it can: high up, on flat ground and ledges; steep faces shed it
    float snowLine = 255.0 + macro * 70.0 + (vnoise(p.xz * 0.02) - 0.5) * 40.0;
    float sn = smoothstep(snowLine, snowLine + 35.0, p.y) * smoothstep(0.52, 0.82, n.y + ridge * 0.05 - gully * 0.08);
    sn = max(sn, smoothstep(snowLine + 60.0, snowLine + 120.0, p.y) * smoothstep(0.3, 0.55, n.y));
    // albedo kept below 0.9 so lit snow keeps its shading after tone mapping
    vec3 snowC = vec3(0.68, 0.71, 0.77) * (0.88 + 0.12 * vnoise(p.xz * 0.7)) * (0.94 + 0.06 * macro2);
    sn = mix(sn, fujiSnow, fujiM);  // Fuji has its own snow line
    c = mix(c, snowC, sn);
    // wind-packed ripples and drifts: a little relief so sunlit snow is not a flat sheet
    vec2 sq = p.xz * vec2(0.55, 0.22);
    float s0 = vnoise(sq), sx = vnoise(sq + vec2(0.12, 0.0)), sz = vnoise(sq + vec2(0.0, 0.12));
    float d0 = vnoise(p.xz * 0.06);
    vec3 snN = normalize(n + vec3(s0 - sx, 0.0, s0 - sz) * 3.0 + vec3(d0 - vnoise(p.xz * 0.06 + vec2(0.3, 0.0)), 0.0, d0 - vnoise(p.xz * 0.06 + vec2(0.0, 0.3))) * 1.5);
    n = normalize(mix(n, snN, sn * (1.0 - smoothstep(80.0, 400.0, dist))));

    // beaches: sand, a darker wet band the waves wash over, and the lake/sea bed below
    float hw = p.y - uWater;
    vec3 shore = uPhoto == 1 ? texture(tSand, p.xz / 4.0).rgb * vec3(0.72, 0.68, 0.62) : vec3(0.70, 0.64, 0.48) * (0.85 + 0.3 * macro3);
    c = mix(c, shore, 1.0 - smoothstep(uWater + 0.4, uWater + 2.4, p.y + macro3));
    float swash = 0.35 + 0.25 * sin(uTime * 0.7 + p.x * 0.031 + p.z * 0.023 + vnoise(p.xz * 0.2) * 3.0);
    float wetSand = (1.0 - smoothstep(swash, swash + 0.6, hw)) * step(-0.5, hw);
    float under = smoothstep(0.0, 0.8, -hw);
    c *= mix(1.0, 0.62, max(wetSand, under * 0.7));

    float sh = shadowFactor(p, normalize(vNrm));
    vec3 col = lighting(c, n, sh);
    vec3 V = normalize(uCamPos - p);
    vec3 Hh = normalize(V + uSunDir);
    float ndh = max(dot(n, Hh), 0.0);
    col += uSunColor * pow(ndh, 80.0) * 0.45 * wetSand * sh;                    // wet sand shine
    col += uSunColor * pow(ndh, 400.0) * 1.2 * sn * step(0.93, vnoise(p.xz * 6.0)) * sh;  // snow sparkle
    if (hw < 0.0) {
        float d = -hw;
        col += c * uSunColor * caustic(p.xz, uTime) * 0.35 * exp(-d * 0.3) * smoothstep(0.0, 0.4, d) * sh * max(uSunDir.y, 0.0);
    }
    FragColor = vec4(applyFog(col, p), 1.0);
}
)";

static const char* kRoadFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
uniform sampler2D tAsphalt, tAsphaltN, tDirt, tGrass, tGravel, tRefl, tLeafGround;
uniform mat4 uReflVP;
uniform int uHasRefl;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    float u = vCol.r, v = vCol.g, width = vCol.b; int mat = int(vCol.a + 0.5);
    vec3 p = vPos;
    float x = (u - 0.5) * width;
    vec2 uv = vec2(x, v) / 3.5;
    vec3 c; float spec = 0.0, gloss = 30.0, leafM = 0.0;
    vec3 nn = n;
    if (mat == 10) {
        vec2 auv = vec2(x, v) / 4.0;
        c = texture(tAsphalt, auv).rgb * (uPhoto == 1 ? vec3(0.50, 0.50, 0.52) : vec3(1.06, 1.02, 0.97));
        // break up tiling with a second, rotated lookup and large-scale tone variation
        c = mix(c, texture(tAsphalt, vec2(auv.y, -auv.x) * 0.61 + 0.3).rgb * (uPhoto == 1 ? 0.5 : 1.0), 0.3);
        float macro = fbm2(vec2(x, v) * 0.045);
        c *= 0.86 + 0.28 * macro;
        vec3 tn = texture(tAsphaltN, auv).xyz * 2.0 - 1.0;
        float hw = width * 0.5;
        // repair patches: darker, fresher rectangles with a seam
        vec2 cell = vec2(floor(x / 2.6 + 0.5), floor(v / 7.0));
        float ph = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
        vec2 cf = vec2(fract(x / 2.6 + 0.5), fract(v / 7.0));
        float inPatch = step(ph, 0.05) * step(0.12, cf.x) * step(cf.x, 0.88) * step(0.15, cf.y) * step(cf.y, 0.85);
        float seam = inPatch * (1.0 - step(0.15, min(min(cf.x - 0.12, 0.88 - cf.x) * 2.6, min(cf.y - 0.15, 0.85 - cf.y) * 7.0)));
        c = mix(c, c * (0.90 + 0.06 * vnoise(vec2(x, v) * 1.3)), inPatch);
        c = mix(c, c * 0.6, seam * 0.35);
        // polished tyre lines in each lane, lighter grey dust on the crown and edges
        float lane = abs(abs(x) - width * 0.25);
        float tracks = exp(-pow((lane - 0.85) * 2.2, 2.0)) * (0.7 + 0.3 * vnoise(vec2(x * 2.0, v * 0.05)));
        c *= 1.0 - tracks * 0.22;
        float dust = smoothstep(hw - 1.4, hw, abs(x)) + exp(-pow(x * 1.6, 2.0)) * 0.5;
        c = mix(c, c * 1.25 + vec3(0.02, 0.018, 0.012), dust * 0.6);
        // edge crumble
        float crumble = smoothstep(hw - 0.35, hw, abs(x) + (vnoise(vec2(v * 1.7, x)) - 0.5) * 0.5);
        c = mix(c, vec3(0.30, 0.27, 0.22), crumble * 0.7);
        nn = normalize(n + vec3(tn.x, 0.0, tn.y) * (0.45 - tracks * 0.25));
        // markings: double yellow centre, white edge lines, all worn
        float breakup = smoothstep(0.22, 0.55, texture(tAsphalt, auv * 2.3).r + vnoise(vec2(x, v) * 2.5) * 0.45);
        float yellow = step(0.07, abs(x)) * step(abs(x), 0.2);
        float white = step(hw - 0.66, abs(x)) * step(abs(x), hw - 0.48);
        float paint = max(yellow, white) * breakup;
        c = mix(c, yellow > 0.5 ? vec3(0.92, 0.66, 0.12) : vec3(0.90, 0.89, 0.85), paint * 0.92);
        bool town = (p.x > 1700.0 && p.x < 2780.0 && p.z > -970.0 && p.z < 400.0) || (p.x > 130.0 && p.x < 2320.0 && p.z > -2440.0 && p.z < -1930.0);
        if (uPhoto == 1 && !town) {
            // autumn: fallen leaves drift against the edges and scatter thinly across the lanes
            vec3 lv = texture(tLeafGround, p.xz / 3.0).rgb;
            float lum = dot(lv, vec3(0.33));
            float drift = smoothstep(hw - 1.9, hw - 0.3, abs(x)) * smoothstep(0.35, 0.65, fbm2(p.xz * 0.12));
            float scatter = smoothstep(0.78, 0.9, vnoise(p.xz * 1.7) * 0.6 + fbm2(p.xz * 0.2) * 0.5) * (1.0 - tracks);
            leafM = clamp(drift * smoothstep(0.18, 0.32, lum) + scatter * 0.6, 0.0, 1.0);
            c = mix(c, lv * vec3(1.15, 0.95, 0.75), leafM * 0.85);
        }
        spec = (0.1 + tracks * 0.25 + paint * 0.15) * (1.0 - leafM * 0.7); gloss = 26.0 + tracks * 30.0;
    } else if (mat == 11) {
        c = uPhoto == 1 ? texture(tGravel, uv * 1.2).rgb : texture(tDirt, uv * 1.2).rgb * 1.08;
        float rut = exp(-pow((abs(x) - width * 0.2) * 1.4, 2.0));
        c *= 1.0 - rut * 0.18;
        vec3 g = texture(tGrass, p.xz / 6.5).rgb;
        float edge = smoothstep(width * 0.32, width * 0.5, abs(x)) + smoothstep(0.65, 0.8, vnoise(vec2(x * 3.0, v * 0.5))) * step(abs(x), 0.6);
        c = mix(c, g, clamp(edge, 0.0, 1.0) * 0.85);
    } else {
        vec3 gravel = uPhoto == 1 ? texture(tGravel, p.xz / 2.5).rgb : texture(tDirt, p.xz / 2.5).rgb * vec3(1.05, 1.02, 0.98);
        vec3 g = texture(tGrass, p.xz / 6.5).rgb;
        c = mix(gravel, g, smoothstep(0.35, 1.0, u + (vnoise(p.xz * 0.7) - 0.5) * 0.5));
    }
    float sh = shadowFactor(p, n);
    vec3 V = normalize(uCamPos - p);
    float puddle = 0.0;
    float w = clamp(uWet, 0.0, 1.0);
    if (w > 0.01) {
        // wet: the whole surface darkens and turns glossy; standing water collects in the dips,
        // the wheel tracks and the edges as it keeps raining
        float pn = fbm2(p.xz * 0.17) * 0.7 + vnoise(p.xz * 1.1) * 0.3;
        float lane = abs(abs(x) - width * 0.25);
        float rut = mat == 10 ? exp(-pow((lane - 0.85) * 2.0, 2.0)) * 0.22 : 0.15;
        float th = 0.74 - 0.08 * w;
        puddle = smoothstep(th, th + 0.11, pn + rut + smoothstep(width * 0.38, width * 0.5, abs(x)) * 0.12) * smoothstep(0.35, 0.8, w);
        c *= mix(1.0, 0.72, w) * mix(1.0, 0.55, puddle);
        nn = normalize(mix(nn, n, 0.35 * w + 0.65 * puddle));
        if (uRain > 0.05 && puddle > 0.01) {
            vec2 rp = rainRipples(p.xz) * uRain * puddle;
            nn = normalize(nn + vec3(rp.x, 0.0, rp.y));
        }
        spec = mix(spec, 0.7, w); gloss = mix(gloss, 160.0, w);
        gloss = mix(gloss, 1200.0, puddle);
    }
    vec3 col = lighting(c, nn, sh);
    vec3 H = normalize(V + uSunDir);
    col += uSunColor * pow(max(dot(nn, H), 0.0), gloss) * spec * sh;
    if (w > 0.01) {
        float ndv = max(dot(nn, V), 0.0);
        float F = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);
        vec3 R = reflect(-V, nn);
        vec3 treeLine = vec3(0.05, 0.06, 0.04) * (ambient(vec3(0.0, 1.0, 0.0)) + uSunColor * 0.2);
        // damp asphalt: a soft, blurry sheen (no mirror)
        vec3 soft = uHdri == 1 ? hdri(vec3(R.x, max(R.y, 0.02), R.z), 4.5) : envColor(R);
        soft = mix(treeLine, soft * 0.75, smoothstep(0.02, 0.35, R.y));
        col = mix(col, soft, clamp(F * 0.55 * w, 0.0, 0.5));
        // puddles: sharp mirror of the real scene
        if (puddle > 0.01) {
            vec3 env = envColor(R);
            env = mix(treeLine, env * 0.62, smoothstep(0.08, 0.42, R.y));
            if (uHasRefl == 1) {
                vec4 cp = uReflVP * vec4(p, 1.0);
                vec2 ruv = cp.xy / cp.w * 0.5 + 0.5 + (nn.xz - n.xz) * 0.06;
                float edge = smoothstep(0.0, 0.05, ruv.x) * smoothstep(1.0, 0.95, ruv.x) * smoothstep(1.0, 0.95, ruv.y) * smoothstep(0.0, 0.05, ruv.y);
                vec3 pr = texture(tRefl, clamp(ruv, 0.002, 0.998)).rgb;
                env = mix(env, pr * 0.85, edge);
            }
            float Fp = 0.12 + 0.8 * pow(1.0 - ndv, 4.0);
            col = mix(col, env, clamp(puddle * Fp, 0.0, 0.85));
        }
    }
    FragColor = vec4(applyFog(col, p), 1.0);
}
)";

// Cars, props and hub objects. Material id in vCol.a.
static const char* kObjectFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
uniform vec3 uPaint;
uniform vec3 uStripe;
uniform float uBrake;
uniform float uHeadlights;
uniform int uGlassPass;
uniform int uBody;
uniform sampler2D tFlakes;
uniform sampler2D tSigns;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    if (!gl_FrontFacing) n = -n;
    int mat = int(vCol.a + 0.5);
    bool glass = mat == 2;
    if ((uGlassPass == 0) == glass) discard;
    vec3 V = normalize(uCamPos - vPos);
    float sh = shadowFactor(vPos, n);
    if (glass) {
        float ndv = max(dot(n, V), 0.0);
        float F = 0.05 + 0.95 * pow(1.0 - ndv, 5.0);
        vec3 env = envColor(reflect(-V, n));
        vec3 H = normalize(V + uSunDir);
        vec3 spec = uSunColor * pow(max(dot(n, H), 0.0), 900.0) * 4.0 * sh;
        float baseA = gl_FrontFacing ? 0.42 : 0.12;
        vec3 tint = vec3(0.025, 0.03, 0.035) * (ambient(n) + uSunColor * 0.3);
        float film = uBody == 1 ? uWinTint : 0.0;
        baseA = mix(baseA, gl_FrontFacing ? 0.94 : 0.6, film);
        tint *= 1.0 - 0.9 * film;
        float a = 1.0 - (1.0 - baseA) * (1.0 - F);
        vec3 c = (tint * baseA * (1.0 - F) + env * F + spec) / max(a, 0.01);
        FragColor = vec4(applyFog(c, vPos), clamp(a, 0.0, 1.0));
        return;
    }
    // inside of the bodywork: door cards, pillars and a lighter headliner
    if (!gl_FrontFacing && (mat == 1 || mat == 5 || mat == 6)) {
        vec3 alb = n.y < -0.55 ? vec3(0.30, 0.29, 0.27) : vec3(0.15, 0.14, 0.13);
        FragColor = vec4(applyFog(alb * (ambient(n) * 1.1 + uSunColor * (0.25 + 0.3 * sh)), vPos), 1.0);
        return;
    }
    if (mat == 3) {
        vec3 e = vCol.rgb;
        bool red = e.r > e.g * 2.0;
        e *= red ? (1.0 + uBrake * 2.6) : (1.4 + uHeadlights);
        float f = pow(1.0 - max(dot(n, V), 0.0), 3.0);
        e += envColor(reflect(-V, n)) * f * 0.5;
        FragColor = vec4(applyFog(e, vPos), 1.0);
        return;
    }
    vec3 albedo = vCol.rgb;
    vec3 col0 = vec3(0.0);  // self-lit (windows)
    float spec = 0.06, gloss = 18.0, refl = 0.0, coat = 0.0, sharp = 0.0;
    float fill = mat == 0 ? 0.35 : 0.0;
    if (mat == 1 || mat == 5) {
        albedo = (mat == 1 ? uPaint : uStripe) * vCol.rgb;
        if (uBody == 1) albedo = applyLivery(albedo, vObj, dot(vCol.rgb, vec3(0.333)));
        float fl = texture(tFlakes, vObj.xz * 2.7 + vObj.y * 1.3).r;
        albedo *= 0.94 + 0.12 * fl;
        spec = 0.35; gloss = 40.0; coat = 1.0; sharp = 1.0;
    } else if (mat == 2) { albedo = vec3(0.012, 0.014, 0.018); coat = 1.0; sharp = 1.0; refl = 0.06; }
    else if (mat == 4) { albedo = vec3(0.35); refl = 0.75; spec = 1.0; gloss = 90.0; }
    else if (mat == 6) { albedo = uPaint * vCol.rgb * 0.1; }
    else if (mat == 7) { albedo = vec3(0.50, 0.51, 0.50); refl = 0.18; spec = 0.4; gloss = 35.0; }
    else if (mat == 8) { albedo = vec3(0.045, 0.045, 0.05) * vCol.rgb; spec = 0.08; gloss = 12.0; }
    else if (mat == 9) { albedo = vec3(0.035, 0.036, 0.04); spec = 0.25; gloss = 30.0; refl = 0.04; }
    else if (mat == 18) { albedo = carbonWeave(vObj); spec = 0.45; gloss = 60.0; coat = 1.0; sharp = 1.0; }
    else if (mat == 19) { albedo = vCol.rgb; spec = 0.55; gloss = 34.0; coat = 0.35; }       // inflatable vinyl
    else if (mat == 20) {                                                                   // steady emissive (signs, lamps)
        FragColor = vec4(applyFog(vCol.rgb * 2.6, vPos), 1.0);
        return;
    }
    else if (mat == 21) { albedo = vCol.rgb; spec = 0.12; gloss = 20.0; }                  // road paint / banners
    else if (mat == 22) {                                                                   // lettered signboards (atlas)
        vec4 sgA = texture(tSigns, vCol.rg);
        if (sgA.a < 0.5) discard;
        vec3 sg = sgA.rgb;
        if (vCol.b > 0.0) { FragColor = vec4(applyFog(sg * (1.1 + 1.9 * vCol.b) * mix(0.8, 1.25, uNight), vPos), 1.0); return; }
        albedo = sg; spec = 0.18; gloss = 28.0;
        col0 = sg * mix(0.08, 0.55, uNight);  // boards are lit after dark
    }
    else if (mat == 10) { albedo = vec3(0.30, 0.31, 0.33) * (1.0 + uHeadlights); coat = 1.0; sharp = 1.0; refl = 0.35; }
    else if (mat == 11) { albedo = vec3(0.65, 0.07, 0.05); spec = 0.5; gloss = 50.0; }
    else if (mat == 12) { albedo = vec3(0.86, 0.86, 0.82); spec = 0.2; }
    else if (mat == 13) {
        // stone paving: running-bond slabs with darker joints and per-slab tone
        vec2 q = vPos.xz / vec2(1.6, 0.8);
        float row = floor(q.y);
        q.x += mod(row, 2.0) * 0.5;
        vec2 f = fract(q), id = floor(q);
        float joint = 1.0 - smoothstep(0.0, 0.035, min(min(f.x, 1.0 - f.x) * 1.6, min(f.y, 1.0 - f.y) * 0.8));
        float tone = 0.86 + 0.26 * fract(sin(dot(id, vec2(12.9898, 78.233))) * 43758.5453);
        albedo = vCol.rgb * tone * (1.0 - joint * 0.55) * (0.88 + 0.24 * vnoise(vPos.xz * 0.35));
        spec = 0.08; gloss = 22.0;
    } else if (mat == 15) {
        // neon signs and street lamps: emissive, a touch of flicker on some
        float fl = 0.85 + 0.15 * step(0.08, fract(sin(dot(floor(vPos.xz * 0.2), vec2(7.1, 3.3))) * 91.7 + uTime * 0.7));
        vec3 e = vCol.rgb * 3.2 * fl;
        FragColor = vec4(applyFog(e + albedo * ambient(n) * 0.2, vPos), 1.0);
        return;
    } else if (mat == 14 || mat == 17) {
        // building facades: floors every 3.6 m, window bays every 2.8 m (towers: a glass curtain wall)
        bool tower = mat == 17;
        float u = abs(n.x) > abs(n.z) ? vPos.z : vPos.x;
        float fy = vPos.y / 3.6, fx = u / (tower ? 1.6 : 2.8);
        vec2 cell = vec2(fract(fx), fract(fy)), id = vec2(floor(fx), floor(fy));
        float rnd = fract(sin(dot(id, vec2(12.9898, 78.233)) + floor(vPos.y / 40.0)) * 43758.5453);
        bool roof = n.y > 0.6;
        float win = roof ? 0.0 : (tower ? step(0.06, cell.x) * step(cell.x, 0.94) * step(0.08, cell.y)
                                        : step(0.18, cell.x) * step(cell.x, 0.82) * step(0.22, cell.y) * step(cell.y, 0.78));
        if (roof) { albedo = vec3(0.32, 0.32, 0.31); }
        else {
            vec3 frame = tower ? vCol.rgb * 0.35 + 0.08 : vCol.rgb * (0.85 + 0.2 * vnoise(vPos.xy * 0.3 + vPos.zy * 0.3));
            vec3 glassC = tower ? vCol.rgb * 0.45 : vec3(0.08, 0.10, 0.12);
            albedo = mix(frame, glassC, win);
            // a few lit windows, warm and cool
            float lit = step(mix(0.86, 0.45, uNight), rnd) * win;
            vec3 interior = mix(vec3(1.0, 0.82, 0.55), vec3(0.75, 0.88, 1.0), step(0.5, fract(rnd * 7.3)));
            col0 = interior * lit * mix(0.12, 1.6, uNight);
            refl = win * (tower ? 0.55 : 0.32); spec = 0.2 + win * 0.6; gloss = 60.0 + win * 300.0;
        }
    } else if (mat == 16) {
        // corrugated steel (containers, sheds): vertical ribs, paint wear, darker towards the bottom
        float u = abs(n.x) > abs(n.z) ? vPos.z : vPos.x;
        float rib = n.y > 0.6 ? 0.0 : smoothstep(0.3, 0.5, abs(fract(u / 0.28) - 0.5));
        float wear = vnoise(vPos.xz * 1.7 + vPos.y * 2.0);
        albedo = vCol.rgb * (0.82 + 0.18 * rib) * (0.85 + 0.25 * wear);
        albedo = mix(albedo, vec3(0.30, 0.22, 0.16), smoothstep(0.78, 0.95, vnoise(vPos.xz * 3.1 + vPos.y * 4.0)) * 0.35);
        spec = 0.25; gloss = 30.0;
    }
    vec3 col = lighting(albedo, n, sh) + albedo * uSunColor * fill * (1.0 - sh) * 0.5;
    vec3 H = normalize(V + uSunDir);
    float ndh = max(dot(n, H), 0.0);
    col += uSunColor * pow(ndh, gloss) * spec * sh;
    vec3 R = reflect(-V, n);
    float ndv = max(dot(n, V), 0.0);
    if (coat > 0.0) {
        float F = 0.07 + 0.93 * pow(1.0 - ndv, 5.0);
        F = max(F, refl);
        vec3 env = envColor(R);
        F = clamp(F * coat, 0.0, 1.0);
        col = col * (1.0 - F) + env * F * 1.25;
        col += uSunColor * pow(ndh, 700.0) * 3.5 * sharp * sh;
    } else if (refl > 0.0) {
        vec3 env = envColor(R);
        float F = mat >= 14 ? (0.25 + 0.75 * pow(1.0 - ndv, 4.0)) : 1.0;
        col = mix(col, env * (mat == 4 || mat >= 14 ? vec3(1.0) : albedo * 2.0), refl * F);
    }
    col += col0;
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

// Imported (textured) car models. vCol.xy = uv.
static const char* kCarFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
uniform sampler2D tBase, tNormal, tEmis, tFlakes;
uniform vec4 uColor;
uniform int uFlags, uHasBase, uHasNormal, uHasEmis, uGlassPass;
uniform vec3 uEmis, uPaint;
uniform float uBrake;
uniform int uBody;
out vec4 FragColor;
void main(){
    vec2 uv = vCol.xy;
    vec4 base = uColor * (uHasBase == 1 ? texture(tBase, uv) : vec4(1.0));
    bool glass = (uFlags & 2) != 0 || (uFlags & 128) != 0;
    if ((uGlassPass == 1) != glass) discard;
    if (!glass && (uFlags & 8) != 0 && base.a < 0.45) discard;
    vec3 n = normalize(vNrm);
    if (!gl_FrontFacing) n = -n;
    if (uHasNormal == 1) {
        vec3 dp1 = dFdx(vPos), dp2 = dFdy(vPos);
        vec2 du1 = dFdx(uv), du2 = dFdy(uv);
        vec3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
        vec3 T = dp2perp * du1.x + dp1perp * du2.x;
        vec3 B = dp2perp * du1.y + dp1perp * du2.y;
        float inv = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
        vec3 tn = texture(tNormal, uv).xyz * 2.0 - 1.0;
        tn.xy *= 0.8;
        vec3 pn = normalize(mat3(T * inv, B * inv, n) * tn);
        if (!any(isnan(pn))) n = pn;
    }
    vec3 V = normalize(uCamPos - vPos);
    float ndv = max(dot(n, V), 0.0);
    float sh = shadowFactor(vPos, n);
    vec3 R = reflect(-V, n);
    if (glass) {
        float F = 0.05 + 0.95 * pow(1.0 - ndv, 5.0);
        vec3 env = envColor(R);
        vec3 H = normalize(V + uSunDir);
        vec3 spec = uSunColor * pow(max(dot(n, H), 0.0), 900.0) * 4.0 * sh;
        float baseA = gl_FrontFacing ? clamp(base.a * 0.55, 0.25, 0.7) : 0.12;
        if ((uFlags & 4) != 0) baseA = clamp(base.a, 0.2, 0.9);
        vec3 tint = base.rgb * 0.15 * (ambient(n) + uSunColor * 0.3);
        // window film: cabin glass only (lamp lenses sit low or at the very ends)
        float film = (uBody == 1 && (uFlags & 4) == 0 && vObj.y > uCarDims.z * 0.5 && abs(vObj.z) < uCarDims.x * 0.43) ? uWinTint : 0.0;
        baseA = mix(baseA, gl_FrontFacing ? 0.94 : 0.6, film);
        tint *= 1.0 - 0.9 * film;
        float a = 1.0 - (1.0 - baseA) * (1.0 - F);
        vec3 c = (tint * baseA * (1.0 - F) + env * F + spec) / max(a, 0.01);
        FragColor = vec4(applyFog(c, vPos), clamp(a, 0.0, 1.0));
        return;
    }
    vec3 albedo = base.rgb;
    bool paint = (uFlags & 1) != 0;
    float spec = 0.15, gloss = 40.0, coat = 0.0, metal = 0.0;
    if (paint) {
        float l = dot(base.rgb, vec3(0.299, 0.587, 0.114));
        if ((uFlags & 256) != 0) {
            // livery: repaint only the saturated (coloured) areas, keep carbon, black trim and decals
            float mx = max(base.r, max(base.g, base.b)), mn = min(base.r, min(base.g, base.b));
            float sat = (mx - mn) / max(mx, 1e-3);
            float mask = smoothstep(0.35, 0.6, sat) * smoothstep(0.12, 0.3, mx);
            albedo = mix(base.rgb, uPaint * clamp(mx / 0.8, 0.35, 1.25), mask);
        } else
        albedo = uPaint * clamp(mix(1.0, l / 0.72, 0.65), 0.2, 1.3);
        if (uBody == 1) albedo = applyLivery(albedo, vObj, clamp(mix(1.0, l / 0.72, 0.65), 0.2, 1.3));
        float fl = texture(tFlakes, vObj.xz * 3.1 + vObj.y * 1.7).r;
        albedo *= 0.94 + 0.12 * fl;
        coat = 1.0; spec = 0.4;
    } else if ((uFlags & 16) != 0) {
        metal = 0.7; spec = 0.9; gloss = 90.0;
    } else if ((uFlags & 32) != 0) {
        spec = 0.06; gloss = 12.0;
        // rubber: many packs ship a light detail/AO map for tyres - treat it as a mask on dark rubber
        float lum = dot(albedo, vec3(0.333));
        if (lum > 0.25) albedo = vec3(0.035, 0.035, 0.038) * (0.55 + 0.6 * lum);
    }
    vec3 col = lighting(albedo * (1.0 - metal * 0.6), n, sh);
    vec3 H = normalize(V + uSunDir);
    float ndh = max(dot(n, H), 0.0);
    col += uSunColor * pow(ndh, gloss) * spec * sh;
    if (coat > 0.0) {
        float F = 0.06 + 0.94 * pow(1.0 - ndv, 5.0);
        col = col * (1.0 - F) + envColor(R) * F * 1.25;
        col += uSunColor * pow(ndh, 800.0) * 4.0 * sh;
    } else if (metal > 0.0) {
        col = mix(col, envColor(R) * (albedo * 0.7 + 0.3), metal * 0.8);
    }
    // glTF: emission = factor x texture (a white factor with a mostly black map lights only the lamps)
    vec3 e = uEmis;
    if (uHasEmis == 1) e = texture(tEmis, uv).rgb * (dot(uEmis, vec3(1.0)) > 0.001 ? uEmis : vec3(1.0));
    if ((uFlags & 4) != 0) {
        bool red = albedo.r > albedo.g * 1.8 && albedo.r > 0.2;
        if (red) e += albedo * (0.5 + uBrake * 2.5);
        else e += albedo * smoothstep(0.6, 0.95, dot(albedo, vec3(0.33))) * 0.6;
    }
    col += e * 1.5;
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

// ---------------------------------------------------------------- instanced trees / rocks
// vertex colour = (u, v, shade, mode) with mode 1 foliage, 2 bark, 3 rock
static const char* kTreeVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
layout(location=3) in vec4 iPosScale;
layout(location=4) in vec4 iTintRot;
uniform mat4 uViewProj;
uniform float uTime;
out vec3 vPos; out vec3 vNrm; out vec2 vUV; out float vShade; out float vMode; out vec3 vTint;
void main(){
    float c = cos(iTintRot.w), s = sin(iTintRot.w);
    vec3 p = aPos * iPosScale.w;
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    vec3 nr = vec3(c * aNrm.x + s * aNrm.z, aNrm.y, -s * aNrm.x + c * aNrm.z);
    if (aCol.w < 1.5) {
        float ph = uTime * 1.3 + iPosScale.x * 0.11 + iPosScale.z * 0.07;
        float w = (sin(ph) * 0.6 + sin(ph * 2.3 + p.y) * 0.25) * 0.012 * p.y;
        p.x += w; p.z += w * 0.7;
    }
    vec3 wp = iPosScale.xyz + p;
    vPos = wp; vNrm = nr; vUV = aCol.xy; vShade = aCol.z; vMode = aCol.w; vTint = iTintRot.rgb;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

static const char* kTreeFS = R"(
in vec3 vPos; in vec3 vNrm; in vec2 vUV; in float vShade; in float vMode; in vec3 vTint;
uniform sampler2D tFoliage, tBark, tRock;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    vec3 col;
    if (vMode < 1.5) {
        vec4 t = texture(tFoliage, vUV);
        if (t.a < 0.42) discard;
        vec3 albedo = t.rgb * vTint * vShade;
        float sh = shadowFactor(vPos, n);
        float wrap = max((dot(n, uSunDir) + 0.5) / 1.5, 0.0);
        vec3 V = normalize(uCamPos - vPos);
        float trans = pow(max(dot(-V, uSunDir), 0.0), 6.0) * 0.6;
        col = albedo * (ambient(n) * 0.9 + uSunColor * (wrap * 0.85 * sh + trans * (0.4 + 0.6 * sh)));
    } else if (vMode < 2.5) {
        vec3 albedo = texture(tBark, vUV).rgb * vShade;
        col = lighting(albedo, n, shadowFactor(vPos, n));
    } else {
        vec3 w = pow(abs(n), vec3(4.0)); w /= (w.x + w.y + w.z);
        vec3 p = vPos / 4.0;
        vec3 albedo = (texture(tRock, p.zy).rgb * w.x + texture(tRock, p.xz).rgb * w.y + texture(tRock, p.xy).rgb * w.z) * vTint * vShade;
        col = lighting(albedo, n, shadowFactor(vPos, n));
    }
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

// ---------------------------------------------------------------- grass tufts around the camera
static const char* kGrassVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
layout(location=3) in vec2 iOff;
uniform mat4 uViewProj;
uniform float uTime;
uniform vec2 uOrigin;
uniform float uSpacing;
uniform float uInner;
uniform float uOuter;      // negative: last ring, fade out gently
uniform float uScaleMul;
uniform float uHalf;
uniform float uWaterLevel;
uniform vec3 uHub;
uniform vec3 uCamPos;
uniform sampler2D tHeight, tRoadDist;
out vec3 vPos; out vec2 vUV; out float vFade; out vec3 vGround;
float h1(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float hgt(vec2 xz){ return texture(tHeight, (xz + uHalf) / (2.0 * uHalf)).r; }
void main(){
    vec2 cell = uOrigin + iOff;
    vec2 xz = cell + (vec2(h1(cell), h1(cell + 17.3)) - 0.5) * uSpacing * 1.8;
    vec2 uv = (xz + uHalf) / (2.0 * uHalf);
    float h = hgt(xz);
    float rd = texture(tRoadDist, uv).r;
    float e = 1.5;
    vec3 gn = normalize(vec3(hgt(xz - vec2(e, 0)) - hgt(xz + vec2(e, 0)), 2.0 * e, hgt(xz - vec2(0, e)) - hgt(xz + vec2(0, e))));
    float d = length(xz - uCamPos.xz);
    float keep = step(1.0 + 0.6 * uScaleMul, rd) * step(uWaterLevel + 0.8, h) * step(0.82, gn.y) * step(52.0, length(xz - uHub.xz));
    // no wild grass in the rice paddies (same rule as the terrain shader)
    if (xz.x < -1250.0 && xz.x > -3350.0 && xz.y > -1450.0 && xz.y < 470.0 && h < 150.0 && gn.y > 0.988 && rd > 5.0) keep = 0.0;
    float dens = smoothstep(0.25, 0.55, h1(floor(xz / 9.0)) * 0.6 + h1(floor(xz / 2.3)) * 0.4);
    keep *= step(h1(cell + 3.1), 0.35 + dens * 0.65);
    float band = uSpacing * 9.0;
    float outer = abs(uOuter);
    float fadeIn = uInner > 0.0 ? smoothstep(uInner, uInner + band, d) : 1.0;
    float fadeOut = uOuter > 0.0 ? 1.0 - smoothstep(outer - band, outer, d) : 1.0 - smoothstep(outer * 0.7, outer, d);
    float fade = fadeIn * fadeOut;
    float sc = (0.7 + 0.6 * h1(cell + 9.9)) * keep * fade;
    float ang = h1(cell + 5.5) * 6.2831;
    float c = cos(ang), s = sin(ang);
    vec3 p = aPos * vec3(sc * uScaleMul, sc * min(uScaleMul, 1.45) * (0.8 + 0.5 * h1(cell + 2.2)), sc * uScaleMul);
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    float bend = aPos.y * aPos.y;
    p.x += sin(uTime * 1.8 + xz.x * 0.2 + xz.y * 0.13) * 0.10 * bend;
    p.z += cos(uTime * 1.5 + xz.x * 0.17) * 0.06 * bend;
    vec3 wp = vec3(xz.x, h - 0.05 * uScaleMul, xz.y) + p;
    vPos = wp; vUV = aCol.xy; vFade = fade;
    vGround = gn;
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";
static const char* kGrassFS = R"(
in vec3 vPos; in vec2 vUV; in float vFade; in vec3 vGround;
uniform sampler2D tBlades, tGrass;
out vec4 FragColor;
void main(){
    vec4 t = texture(tBlades, vUV);
    if (t.a < 0.5) discard;
    vec3 ground = texture(tGrass, vPos.xz / 37.0 + 0.37).rgb;
    ground *= mix(vec3(0.86, 0.95, 0.72), vec3(1.10, 0.98, 0.80), smoothstep(0.25, 0.75, fbm2(vPos.xz * 0.0035)));
    vec3 albedo = mix(ground * 0.85, t.rgb * ground * 1.7, 0.7) * (0.55 + 0.45 * (1.0 - vUV.y));
    if (uPhoto == 1) albedo = t.rgb * mix(vec3(0.95), ground * 1.6, 0.35) * (0.45 + 0.55 * (1.0 - vUV.y));  // blade colour, rooted in the ground tone
    vec3 n = normalize(vGround + vec3(0.0, 0.6, 0.0));
    float sh = shadowFactor(vPos, vGround);
    vec3 V = normalize(uCamPos - vPos);
    float trans = pow(max(dot(-V, uSunDir), 0.0), 4.0) * 0.5 * (1.0 - vUV.y);
    vec3 col = albedo * (ambient(n) + uSunColor * (max(dot(n, uSunDir), 0.0) + trans) * sh);
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

// ---------------------------------------------------------------- shadow depth passes
static const char* kDepthVS = R"(
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
uniform mat4 uModel;
void main(){ gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }
)";
static const char* kDepthTreeVS = R"(
layout(location=0) in vec3 aPos;
layout(location=2) in vec4 aCol;
layout(location=3) in vec4 iPosScale;
layout(location=4) in vec4 iTintRot;
uniform mat4 uViewProj;
out vec2 vUV; out float vMode;
void main(){
    float c = cos(iTintRot.w), s = sin(iTintRot.w);
    vec3 p = aPos * iPosScale.w;
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    vUV = aCol.xy; vMode = aCol.w;
    gl_Position = uViewProj * vec4(iPosScale.xyz + p, 1.0);
}
)";
static const char* kDepthFS = R"(
out vec4 FragColor;
void main(){ FragColor = vec4(1.0); }
)";
static const char* kDepthTreeFS = R"(
in vec2 vUV; in float vMode;
uniform sampler2D tFoliage;
out vec4 FragColor;
void main(){
    if (vMode < 1.5 && texture(tFoliage, vUV).a < 0.42) discard;
    FragColor = vec4(1.0);
}
)";

// ---------------------------------------------------------------- fullscreen passes
static const char* kFullscreenVS = R"(
out vec2 vUV;
void main(){
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
static const char* kSkyFS = R"(
in vec2 vUV;
uniform mat4 uInvViewProj;
out vec4 FragColor;
void main(){
    vec4 a = uInvViewProj * vec4(vUV * 2.0 - 1.0, -1.0, 1.0);
    vec4 b = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 d = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 c = skyColor(d);
    float cloudW = uHdri == 0 ? 1.0 : 1.0 - uHdriW;
    if (d.y > 0.0 && uNight > 0.0) {
        // stars, twinkling a little, fading near the horizon
        vec3 q = floor(d * 380.0);
        float h = hash12(q.xy + q.z * 17.13);
        float tw = 0.7 + 0.3 * sin(uTime * 3.0 + h * 40.0);
        c += vec3(0.85, 0.9, 1.0) * smoothstep(0.9972, 1.0, h) * tw * uNight * smoothstep(0.02, 0.25, d.y) * (1.0 - uRain);
        // moon glow around the night "sun"
        float m = max(dot(d, uSunDir), 0.0);
        c += vec3(0.75, 0.8, 0.95) * (smoothstep(0.9993, 0.9996, m) * 1.6 + pow(m, 60.0) * 0.08) * uNight;
    }
    if (d.y > 0.0 && cloudW > 0.01) {
        vec2 uv = d.xz / (d.y + 0.12) * 0.9 + vec2(uTime * 0.004, uTime * 0.0015);
        float n = fbm2(uv * 1.3);
        float n2 = fbm2(uv * 4.1 + 4.0);
        float n3 = vnoise(uv * 11.0);
        float dens = n * 0.75 + n2 * 0.3 + n3 * 0.06;
        float cloud = smoothstep(0.52, 0.8, dens) * smoothstep(0.0, 0.2, d.y);
        float thick = smoothstep(0.6, 0.95, dens);
        float sun = max(dot(d, uSunDir), 0.0);
        float dayL = clamp(dot(uSkyAmb, vec3(0.333)) / 0.47, 0.03, 1.0);
        vec3 lit = mix(vec3(1.0, 0.97, 0.92) * dayL, uSunColor * vec3(1.0, 0.85, 0.65), 0.35 + 0.4 * pow(sun, 3.0));
        vec3 shade = mix(uSkyHorizon * 0.8, vec3(0.55, 0.57, 0.62) * dayL, 0.5);
        vec3 cc = mix(lit, shade, thick * 0.6);
        cc += uSunColor * pow(sun, 12.0) * (1.0 - thick) * 0.6;
        c = mix(c, cc, cloud * 0.9 * cloudW);
    }
    float hz = 1.0 - smoothstep(-0.03, 0.10, d.y);
    c = mix(c, uFogColor, hz * 0.7);
    FragColor = vec4(c, 1.0);
}
)";

static const char* kWaterFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
uniform sampler2D tHeight, tRefl;
uniform mat4 uReflVP;
uniform int uHasRefl;
uniform int uPass;          // 0: transmittance (multiplied onto the lake bed), 1: light added on top
uniform float uHalf, uWaterLevel;
out vec4 FragColor;
// sharp-crested travelling waves: h = A * exp(sin(phase) - 1); returns gradient (xy) and height (z)
vec3 waves(vec2 p, float t, float amp) {
    vec3 acc = vec3(0.0);
    float a = 0.30 * amp, k = 0.16, w = 0.9;
    vec2 dir = normalize(vec2(0.82, 0.57));
    for (int i = 0; i < 7; i++) {
        float ph = dot(dir, p) * k + t * w + float(i) * 1.7;
        float e = exp(sin(ph) - 1.0);
        acc.z += a * e;
        acc.xy += a * e * cos(ph) * k * dir;
        // next octave: shorter, faster, turned, smaller
        float ang = 1.13 + float(i) * 0.71;
        dir = normalize(vec2(dir.x * cos(ang) - dir.y * sin(ang), dir.x * sin(ang) + dir.y * cos(ang)));
        a *= 0.62; k *= 1.68; w *= 1.31;
    }
    // capillary ripples on top
    vec2 q = p * 2.3 + vec2(t * 0.6, -t * 0.45);
    acc.x += (vnoise(q) - 0.5) * 0.09 * amp + (vnoise(q * 2.7 + 9.0) - 0.5) * 0.05;
    acc.y += (vnoise(q + vec2(5.2, 1.3)) - 0.5) * 0.09 * amp + (vnoise(q * 2.7 + vec2(1.0, 7.0)) - 0.5) * 0.05;
    return acc;
}
vec4 fogTerms(vec3 wp) {   // fog colour (rgb) and amount (a), as applyFog computes them
    vec3 v = wp - uCamPos; float dist = length(v); vec3 dir = v / max(dist, 0.001);
    float hf = exp(-max(wp.y - 15.0, 0.0) * 0.005);
    float f = 1.0 - exp(-dist * uFogDensity * (0.5 + 0.9 * hf));
    float s = pow(max(dot(dir, uSunDir), 0.0), 5.0);
    vec3 fogBase = uFogColor;
    if (uHdri == 1) {
        vec3 hz = hdri(normalize(vec3(dir.x, max(dir.y, 0.03) + 0.05, dir.z)), 6.0);
        fogBase = mix(uFogColor, hz * vec3(0.92, 0.96, 1.04), 0.75);
    }
    vec3 fc = mix(fogBase, uSunColor * vec3(0.85, 0.7, 0.5) + vec3(0.1) * (1.0 - uNight), s * (uHdri == 1 ? 0.3 : 0.55));
    return vec4(fc, clamp(f, 0.0, 1.0));
}
void main(){
    if (vPos.y < uClipY) discard;
    vec2 p = vPos.xz; float t = uTime;
    float ground = texture(tHeight, (p + uHalf) / (2.0 * uHalf)).r;
    float depth = vPos.y - ground;          // vPos.y: the lakes and sea sit at uWaterLevel, canals higher up
    if (depth < -0.05) discard;
    bool atLevel = abs(vPos.y - uWaterLevel) < 0.3;
    if (uCamPos.y < vPos.y - 0.02) {
        // seen from underneath: a dim teal ceiling instead of a sky mirror
        float s = 0.5 + 0.5 * vnoise(p * 0.6 + t * 0.3);
        if (uPass == 0) FragColor = vec4(vec3(0.18, 0.34, 0.38) * (0.8 + 0.4 * s), 1.0);
        else FragColor = vec4(vec3(0.02, 0.09, 0.10) * (uSkyAmb * 1.5 + uSunColor * 0.3), 1.0);
        return;
    }
    float dist = length(uCamPos - vPos);
    // open, deep water gets the big swell; shallows and small lakes stay calmer
    float amp = mix(0.35, 1.0, smoothstep(1.5, 14.0, depth)) * (1.0 + uRain * 0.6);
    vec3 wv = waves(p, t, amp);
    float calm = smoothstep(0.0, 1.2, depth);
    vec3 n = normalize(vec3(-wv.x * calm, 1.0, -wv.y * calm));
    n = normalize(mix(n, vec3(0, 1, 0), smoothstep(120.0, 1100.0, dist) * 0.75));
    if (uRain > 0.05) {  // drops pocking the surface
        vec2 rp = rainRipples(p) * uRain * 1.4;
        n = normalize(n + vec3(rp.x, 0.0, rp.y));
    }
    vec3 V = normalize(uCamPos - vPos);
    float ndv = max(dot(n, V), 0.0);
    float F = clamp(0.02 + 0.98 * pow(1.0 - ndv, 5.0), 0.0, 1.0);
    vec3 R = reflect(-V, n);
    R.y = abs(R.y);
    vec3 refl = envColor(R);
    if (uHasRefl == 1 && atLevel) {
        vec4 cp = uReflVP * vec4(vPos, 1.0);
        vec2 ruv = cp.xy / cp.w * 0.5 + 0.5 + n.xz * 0.045;
        vec3 pr = texture(tRefl, clamp(ruv, 0.002, 0.998)).rgb;
        float edge = smoothstep(0.0, 0.04, ruv.x) * smoothstep(1.0, 0.96, ruv.x) * smoothstep(1.0, 0.95, ruv.y);
        refl = mix(refl, pr, edge);
    }
    float sh = shadowFactor(vPos, vec3(0, 1, 0));
    // light travelling through the water: red goes first, then green; what's left is blue
    vec3 absorb = vec3(0.42, 0.075, 0.045);
    float path = depth * (1.0 + 1.6 * (1.0 - ndv));          // grazing views see through more water
    vec3 T = exp(-absorb * path);
    T *= 1.0 - smoothstep(80.0, 420.0, dist);                // distant water: no bed visible
    // in-scattered colour of the water itself (what you see where it's deep)
    vec3 scatter = vec3(0.010, 0.050, 0.060) * (uSkyAmb * 1.2 + uSunColor * 0.5 * max(uSunDir.y, 0.0) * (0.6 + 0.4 * sh));
    // sunlight through the crests
    float crest = clamp(wv.z * 2.2 - 0.25, 0.0, 1.0);
    float back = pow(max(dot(-V, uSunDir) * 0.5 + 0.5, 0.0), 3.0);
    scatter += vec3(0.03, 0.16, 0.13) * crest * back * sh * max(uSunDir.y + 0.2, 0.0);
    // foam: breaking lines rolling in onto the shore, plus whitecaps on the open sea
    float shoreBand = 1.0 - smoothstep(0.05, 1.4, depth);
    float roll = sin(depth * 4.5 - t * 1.6 + vnoise(p * 0.12) * 5.0);
    float streak = vnoise(p * vec2(1.1, 3.3) + vec2(t * 0.15, 0.0)) * 0.55 + vnoise(p * 4.7 - t * 0.4) * 0.45;
    float foam = smoothstep(0.55, 0.9, roll) * smoothstep(0.35, 0.7, streak) * shoreBand;
    foam += smoothstep(0.08, 0.0, depth) * smoothstep(0.3, 0.6, streak) * 0.8;  // the swash edge
    float caps = smoothstep(0.62, 0.8, wv.z * amp * 1.6 + streak * 0.35) * smoothstep(6.0, 20.0, depth) * (0.25 + uRain * 0.5);
    foam = clamp(foam + caps, 0.0, 1.0) * (1.0 - smoothstep(250.0, 700.0, dist) * 0.7);
    vec3 foamC = vec3(0.86, 0.9, 0.92) * (ambient(vec3(0.0, 1.0, 0.0)) + uSunColor * max(uSunDir.y, 0.0) * 0.7 * sh);
    vec4 fog = fogTerms(vPos);
    if (uPass == 0) {
        vec3 keep = T * (1.0 - F) * (1.0 - foam) * (1.0 - fog.a);
        FragColor = vec4(keep, 1.0);
        return;
    }
    vec3 H = normalize(V + uSunDir);
    float ndh = max(dot(n, H), 0.0);
    // sun glitter: a broad sheen plus pin-point sparkles on the facets
    float spark = step(0.86, vnoise(p * 9.0 + t * 1.3)) * pow(ndh, 90.0) * 6.0;
    vec3 sun = uSunColor * (pow(ndh, 700.0) * 9.0 + pow(ndh, 90.0) * 0.35 + spark) * sh * (1.0 - uRain * 0.8);
    vec3 col = scatter * (1.0 - T) * (1.0 - F) + refl * F + sun;
    col = mix(col, foamC, foam);
    FragColor = vec4(col * (1.0 - fog.a) + fog.rgb * fog.a, 1.0);
}
)";

static const char* kUnlitFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
uniform vec4 uTint;
uniform int uFog;
out vec4 FragColor;
void main(){
    vec4 c = vCol * uTint;
    if (uFog == 1) c.rgb = applyFog(c.rgb, vPos);
    else {
        float d = length(vPos - uCamPos);
        c.a *= 1.0 - smoothstep(900.0, 1400.0, d);
    }
    FragColor = c;
}
)";

// ---------------------------------------------------------------- bloom + post
static const char* kBrightFS = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uTexel;
out vec4 FragColor;
void main(){
    vec3 c = vec3(0.0);
    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) c += texture(uSrc, vUV + vec2(x, y) * uTexel).rgb;
    c /= 9.0;
    float l = max(c.r, max(c.g, c.b));
    FragColor = vec4(c * smoothstep(1.0, 2.2, l), 1.0);
}
)";
static const char* kBlurFS = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uDir;
out vec4 FragColor;
void main(){
    float w[5] = float[](0.227, 0.194, 0.121, 0.054, 0.016);
    vec3 c = texture(uSrc, vUV).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        c += texture(uSrc, vUV + uDir * float(i) * 1.5).rgb * w[i];
        c += texture(uSrc, vUV - uDir * float(i) * 1.5).rgb * w[i];
    }
    FragColor = vec4(c, 1.0);
}
)";
static const char* kSsaoFS = R"(
in vec2 vUV;
uniform sampler2D uDepth;
uniform vec4 uProj;   // near, far, tan(fovy/2), aspect
uniform vec2 uTexel;  // full-res texel
out vec4 FragColor;
float linZ(float d){ float z = d * 2.0 - 1.0; return 2.0 * uProj.x * uProj.y / (uProj.y + uProj.x - z * (uProj.y - uProj.x)); }
vec3 viewPos(vec2 uv){
    float l = linZ(texture(uDepth, uv).r);
    return vec3((uv.x * 2.0 - 1.0) * uProj.z * uProj.w * l, (uv.y * 2.0 - 1.0) * uProj.z * l, -l);
}
void main(){
    float d = texture(uDepth, vUV).r;
    if (d >= 0.99999) { FragColor = vec4(1.0); return; }
    vec3 P = viewPos(vUV);
    float dist = -P.z;
    if (dist > 180.0) { FragColor = vec4(1.0); return; }
    // normal from the nearer neighbour on each axis (keeps silhouettes clean)
    vec3 px = viewPos(vUV + vec2(uTexel.x, 0.0)), nx = viewPos(vUV - vec2(uTexel.x, 0.0));
    vec3 py = viewPos(vUV + vec2(0.0, uTexel.y)), ny = viewPos(vUV - vec2(0.0, uTexel.y));
    vec3 dx = abs(px.z - P.z) < abs(nx.z - P.z) ? px - P : P - nx;
    vec3 dy = abs(py.z - P.z) < abs(ny.z - P.z) ? py - P : P - ny;
    vec3 N = normalize(cross(dx, dy));
    float radius = mix(0.55, 2.2, smoothstep(10.0, 120.0, dist));
    float rUV = radius / (dist * uProj.z * 2.0);
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float ao = 0.0;
    const int N_S = 12;
    for (int i = 0; i < N_S; i++) {
        float a = (float(i) + noise) * 2.39996;
        float r = sqrt((float(i) + 0.5) / float(N_S)) * rUV;
        vec2 o = vec2(cos(a) / uProj.w, sin(a)) * r;
        vec3 S = viewPos(vUV + o);
        vec3 v = S - P;
        float l = length(v);
        float occ = max(dot(N, v / max(l, 1e-4)) - 0.08, 0.0);
        ao += occ * (1.0 - smoothstep(radius * 0.6, radius * 1.6, l));
    }
    ao = 1.0 - ao / float(N_S) * 1.6;
    ao = mix(clamp(ao, 0.0, 1.0), 1.0, smoothstep(120.0, 180.0, dist));
    FragColor = vec4(ao, ao, ao, 1.0);
}
)";
static const char* kPostFS = R"(
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBlur;
uniform float uFlash;
uniform float uBloomAmt;
uniform vec2 uSunUV;
uniform float uSunAmt;
uniform sampler2D uAO;
uniform float uAOAmt;
uniform vec2 uAOTexel;
uniform vec2 uTexelS;
uniform float uExposure, uContrast, uSat, uWarmth;
uniform int uCB;
// colour-blind correction (daltonization): simulate the deficiency, then shift the
// colour difference it loses into channels that can still be seen
vec3 daltonize(vec3 c, int mode) {
    mat3 toLMS = mat3(17.8824, 3.45565, 0.0299566, 43.5161, 27.1554, 0.184309, 4.11935, 3.86714, 1.46709);
    mat3 toRGB = mat3(0.0809444479, -0.0102485335, -0.000365296938, -0.130504409, 0.0540193266, -0.00412161469, 0.116721066, -0.113614708, 0.693511405);
    vec3 lms = toLMS * c;
    vec3 s;
    if (mode == 1) s = vec3(2.02344 * lms.y - 2.52581 * lms.z, lms.y, lms.z);
    else if (mode == 2) s = vec3(lms.x, 0.494207 * lms.x + 1.24827 * lms.z, lms.z);
    else s = vec3(lms.x, lms.y, -0.395913 * lms.x + 0.801109 * lms.y);
    vec3 sim = toRGB * s;
    vec3 err = c - sim;
    vec3 shift = vec3(0.0, err.r * 0.7 + err.g, err.r * 0.7 + err.b);
    return clamp(c + shift, 0.0, 1.0);
}
uniform float uGrainT;
uniform float uMB;
uniform sampler2D uDepthP;
uniform mat4 uInvVP, uPrevVP;
uniform vec3 uCamPosP;
out vec4 FragColor;
vec3 aces(vec3 x){ return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main(){
    vec2 uv = vUV;
    vec3 c = texture(uScene, uv).rgb;
    {
        // contrast-adaptive sharpening: win back the detail MSAA and upscaled textures soften
        vec3 n = texture(uScene, uv + vec2(0.0, uTexelS.y)).rgb, s2 = texture(uScene, uv - vec2(0.0, uTexelS.y)).rgb;
        vec3 e = texture(uScene, uv + vec2(uTexelS.x, 0.0)).rgb, w = texture(uScene, uv - vec2(uTexelS.x, 0.0)).rgb;
        vec3 mn = min(c, min(min(n, s2), min(e, w))), mx = max(c, max(max(n, s2), max(e, w)));
        vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
        vec3 wgt = -amp * 0.16;
        c = clamp((c + (n + s2 + e + w) * wgt) / (1.0 + 4.0 * wgt), 0.0, 64.0);
    }
    if (uMB > 0.001) {
        // per-pixel camera motion blur from depth reprojection (the car you drive stays sharp)
        float d = texture(uDepthP, uv).r;
        vec4 wp = uInvVP * vec4(uv * 2.0 - 1.0, min(d, 0.99999) * 2.0 - 1.0, 1.0);
        wp /= wp.w;
        vec4 pp = uPrevVP * vec4(wp.xyz, 1.0);
        vec2 puv = pp.xy / pp.w * 0.5 + 0.5;
        vec2 vel = (uv - puv) * uMB;
        float dist = length(wp.xyz - uCamPosP);
        vel *= smoothstep(7.0, 16.0, dist);
        float l = length(vel);
        if (l > 0.06) vel *= 0.06 / l;
        if (l > 0.0015) {
            vec3 acc = c;
            for (int i = 1; i <= 8; i++) {
                float t = float(i) / 8.0 - 0.5;
                acc += texture(uScene, uv + vel * t).rgb;
            }
            c = acc / 9.0;
        }
    }
    if (uBlur > 0.001) {
        vec2 dir = uv - vec2(0.5, 0.45);
        float amt = uBlur * smoothstep(0.06, 0.6, length(dir));
        vec3 acc = c; float w = 1.0;
        for (int i = 1; i < 14; i++) {
            float t = float(i) / 14.0;
            acc += texture(uScene, uv - dir * amt * t * 0.17).rgb;
            w += 1.0;
        }
        c = acc / w;
    }
    if (uAOAmt > 0.0) {
        // ambient occlusion: contact shadows under cars, in wheel arches, at tree roots
        float ao = 0.0;
        ao += texture(uAO, uv + uAOTexel * vec2(-0.75, -0.75)).r;
        ao += texture(uAO, uv + uAOTexel * vec2(0.75, -0.75)).r;
        ao += texture(uAO, uv + uAOTexel * vec2(-0.75, 0.75)).r;
        ao += texture(uAO, uv + uAOTexel * vec2(0.75, 0.75)).r;
        c *= mix(1.0, ao * 0.25, uAOAmt);
    }
    c += texture(uBloom, uv).rgb * uBloomAmt;
    if (uSunAmt > 0.001) {
        // light shafts: march the bloom buffer towards the sun
        vec2 d = (uSunUV - uv) / 40.0;
        vec2 p = uv;
        float decay = 1.0;
        vec3 acc = vec3(0.0);
        for (int i = 0; i < 40; i++) {
            p += d;
            acc += texture(uBloom, clamp(p, 0.0, 1.0)).rgb * decay;
            decay *= 0.955;
        }
        c += acc / 40.0 * uSunAmt * 2.2 * vec3(1.0, 0.85, 0.65);
    }
    c = aces(c * 1.12 * uExposure);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c *= mix(vec3(0.93, 0.99, 1.06), vec3(1.07, 1.0, 0.88), smoothstep(0.15, 0.75, l));
    l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.2);
    c = c * c * (3.0 - 2.0 * c) * 0.35 + c * 0.65;
    vec2 q = uv - 0.5;
    c *= 1.0 - dot(q, q) * 0.7;
    {
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(vec3(l), c, 1.10);
        c = clamp(c, 0.0, 1.0);
        c = c * c * (3.0 - 2.0 * c) * 0.22 + c * 0.78;  // gentle S-curve
    }
    // fine film grain (also hides banding in the sky gradients)
    float g = fract(sin(dot(gl_FragCoord.xy + uGrainT * 61.7, vec2(12.9898, 78.233))) * 43758.5453);
    c += (g - 0.5) * 0.018;
    // player display settings: contrast, saturation, colour temperature, colour-blind aid
    c = (c - 0.5) * uContrast + 0.5;
    float lg = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(lg), c, uSat);
    c *= vec3(1.0 + 0.06 * uWarmth, 1.0 + 0.01 * uWarmth, 1.0 - 0.07 * uWarmth);
    c = clamp(c, 0.0, 1.0);
    if (uCB > 0) c = daltonize(c, uCB);
    c = mix(c, vec3(1.0), uFlash);
    FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// ---------------------------------------------------------------- HUD
static const char* kHudVS = R"(
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
layout(location=3) in float aMode;
uniform vec2 uScreen;
out vec2 vUV; out vec4 vCol; out float vMode; out vec2 vPix;
void main(){
    vUV = aUV; vCol = aCol; vMode = aMode; vPix = aPos;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";
static const char* kHudFS = R"(
in vec2 vUV; in vec4 vCol; in float vMode; in vec2 vPix;
uniform sampler2D uFont;
uniform sampler2D uMap;
uniform sampler2D uImg;
uniform vec3 uClip;
out vec4 FragColor;
void main(){
    vec4 c = vCol;
    if (vMode > 0.5 && vMode < 1.5) c.a *= texture(uFont, vUV).r;
    else if (vMode > 2.5) c *= texture(uImg, vUV);
    else if (vMode > 1.5) c *= vec4(texture(uMap, vUV).rgb, 1.0);
    if (uClip.z > 0.0) {
        float d = length(vPix - uClip.xy);
        c.a *= 1.0 - smoothstep(uClip.z - 1.5, uClip.z, d);
    }
    FragColor = c;
}
)";

// ---------------------------------------------------------------- rain streaks
static const char* kRainVS = R"(
layout(location=0) in vec3 aPos;   // seed in [0,1)^3
layout(location=1) in vec3 aNrm;   // x: side -1/1, y: 0 head / 1 tail, z: random
uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform float uTime;
uniform vec3 uWind;
out float vA; out float vT;
void main(){
    vec3 box = vec3(34.0, 24.0, 34.0);
    float speed = 9.5 + aNrm.z * 3.5;
    vec3 vel = vec3(uWind.x, -speed, uWind.z);
    vec3 p = aPos * box + vel * uTime;
    vec3 rel = mod(p - uCamPos, box) - box * 0.5;
    vec3 c = uCamPos + rel;
    vec3 dir = normalize(vel);
    vec3 toCam = normalize(uCamPos - c);
    vec3 side = normalize(cross(dir, toCam)) * (0.003 + 0.003 * aNrm.z);
    vec3 pos = c + side * aNrm.x - dir * (aNrm.y * (0.3 + 0.25 * aNrm.z));
    gl_Position = uViewProj * vec4(pos, 1.0);
    float d = length(rel);
    vA = smoothstep(17.0, 5.0, d) * smoothstep(1.5, 4.0, d);
    vT = aNrm.y;
}
)";
static const char* kRainFS = R"(
uniform float uRain;
uniform vec3 uTint;
in float vA; in float vT;
out vec4 FragColor;
void main(){
    if (vA < 0.01) discard;
    FragColor = vec4(uTint, vA * uRain * 0.3 * (1.0 - vT * 0.7));
}
)";
