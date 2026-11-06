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
float shadowFactor(vec3 wp, vec3 n){
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
        float s0 = pcf(uShadow0, vec3(pa.xy, pa.z - 0.00012), uShadowTexel.x);
        return mix(s0, s1, smoothstep(0.75, 0.98, ma));
    }
    return s1;
}
vec3 skyColor(vec3 d){
    float t = clamp(d.y, 0.0, 1.0);
    vec3 c = mix(uSkyHorizon, uSkyTop, pow(t, 0.45));
    if (d.y < 0.0) c = mix(uSkyHorizon, uGroundAmb * 1.1, clamp(-d.y * 4.0, 0.0, 1.0));
    float s = max(dot(d, uSunDir), 0.0);
    c += uSunColor * (pow(s, 1400.0) * 14.0 + pow(s, 48.0) * 0.30 + pow(s, 5.0) * 0.12);
    c += uSunColor * vec3(0.25, 0.12, 0.02) * pow(s, 2.0) * (1.0 - t);
    return c;
}
// environment used for reflections (sky above, tree line at the horizon, ground below)
vec3 envColor(vec3 R){
    if (R.y > 0.0) {
        vec3 s = skyColor(R);
        float az = atan(R.x, R.z);
        float line = 0.035 + 0.05 * vnoise(vec2(az * 9.0, 1.0)) + 0.03 * vnoise(vec2(az * 31.0, 3.0));
        float band = 1.0 - smoothstep(line - 0.01, line + 0.01, R.y);
        vec3 trees = vec3(0.10, 0.11, 0.06) * (uSkyAmb + uSunColor * 0.25);
        return mix(s, trees, band);
    }
    return mix(vec3(0.16, 0.14, 0.10) * (uSkyAmb + uSunColor * 0.4), vec3(0.09, 0.08, 0.06), clamp(-R.y * 2.0, 0.0, 1.0));
}
vec3 applyFog(vec3 col, vec3 wp){
    vec3 v = wp - uCamPos; float dist = length(v); vec3 dir = v / max(dist, 0.001);
    float hf = exp(-max(wp.y - 15.0, 0.0) * 0.005);
    float f = 1.0 - exp(-dist * uFogDensity * (0.5 + 0.9 * hf));
    float s = pow(max(dot(dir, uSunDir), 0.0), 5.0);
    vec3 fc = mix(uFogColor, uSunColor * vec3(0.85, 0.7, 0.5) + vec3(0.1), s * 0.55);
    return mix(col, fc, clamp(f, 0.0, 1.0));
}
vec3 ambient(vec3 n){ return mix(uGroundAmb, uSkyAmb, n.y * 0.5 + 0.5); }
vec3 lighting(vec3 albedo, vec3 n, float shadow){
    float ndl = max(dot(n, uSunDir), 0.0);
    return albedo * (ambient(n) + uSunColor * ndl * shadow);
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
out vec4 FragColor;
vec3 tri(sampler2D t, vec3 p, vec3 w, float s){
    return texture(t, p.zy * s).rgb * w.x + texture(t, p.xz * s).rgb * w.y + texture(t, p.xy * s).rgb * w.z;
}
void main(){
    vec3 n = normalize(vNrm);
    vec3 p = vPos;
    float dist = length(p - uCamPos);
    float slope = 1.0 - n.y;
    float macro = fbm2(p.xz * 0.0035);
    float macro2 = vnoise(p.xz * 0.021);
    float macro3 = vnoise(p.xz * 0.11);

    vec3 g1 = texture(tGrass, p.xz / 6.5).rgb;
    vec3 g2 = texture(tGrass, p.xz / 37.0 + 0.37).rgb;
    vec3 grass = mix(g1, g2, 0.35 + 0.25 * smoothstep(60.0, 250.0, dist));
    grass *= mix(vec3(0.86, 0.95, 0.72), vec3(1.10, 0.98, 0.80), smoothstep(0.25, 0.75, macro));
    grass *= 0.82 + 0.3 * macro2;

    vec3 dirt = texture(tDirt, p.xz / 5.0).rgb * (0.9 + 0.2 * macro3);
    float dirtW = smoothstep(0.6, 0.8, fbm2(p.xz * 0.025) + macro3 * 0.15);
    vec3 c = mix(grass, dirt, dirtW * 0.75);
    c = mix(c, vCol.rgb * (0.8 + 0.4 * macro3) * (texture(tDirt, p.xz / 3.0).rgb * 1.6), vCol.a);

    vec3 w = pow(abs(n), vec3(5.0)); w /= (w.x + w.y + w.z);
    const float rs = 1.0 / 11.0;
    vec3 rock = tri(tRock, p, w, rs) * vec3(1.0, 0.97, 0.92);
    vec3 nx = texture(tRockN, p.zy * rs).xyz * 2.0 - 1.0;
    vec3 ny = texture(tRockN, p.xz * rs).xyz * 2.0 - 1.0;
    vec3 nz = texture(tRockN, p.xy * rs).xyz * 2.0 - 1.0;
    vec3 rn = normalize(n + (vec3(0.0, nx.y, nx.x) * w.x + vec3(ny.x, 0.0, ny.y) * w.y + vec3(nz.x, nz.y, 0.0) * w.z) * 0.9);
    float r = smoothstep(0.30, 0.46, slope + (macro3 - 0.5) * 0.12);
    c = mix(c, rock, r);
    n = normalize(mix(n, rn, r));

    float sn = smoothstep(285.0, 330.0, p.y + macro * 50.0) * (1.0 - r * 0.6);
    c = mix(c, vec3(0.92, 0.94, 0.98), sn);
    c = mix(c, vec3(0.70, 0.64, 0.48) * (0.85 + 0.3 * macro3), 1.0 - smoothstep(uWater + 0.4, uWater + 2.4, p.y + macro3));

    float sh = shadowFactor(p, normalize(vNrm));
    vec3 col = lighting(c, n, sh);
    FragColor = vec4(applyFog(col, p), 1.0);
}
)";

static const char* kRoadFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
uniform sampler2D tAsphalt, tAsphaltN, tDirt, tGrass;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    float u = vCol.r, v = vCol.g, width = vCol.b; int mat = int(vCol.a + 0.5);
    vec3 p = vPos;
    float x = (u - 0.5) * width;
    vec2 uv = vec2(x, v) / 3.5;
    vec3 c; float spec = 0.0, gloss = 30.0;
    vec3 nn = n;
    if (mat == 10) {
        c = texture(tAsphalt, uv).rgb * vec3(1.08, 1.02, 0.94);
        vec3 tn = texture(tAsphaltN, uv).xyz * 2.0 - 1.0;
        nn = normalize(n + vec3(tn.x, 0.0, tn.y) * 0.35);
        float wear = exp(-pow((abs(x) - width * 0.25) * 1.1, 2.0));
        c *= 1.0 + wear * 0.12;
        c *= 0.92 + 0.16 * vnoise(p.xz * 0.05);
        float breakup = smoothstep(0.25, 0.6, texture(tAsphalt, uv * 3.1).r + vnoise(vec2(x, v) * 2.0) * 0.4);
        float yellow = step(0.07, abs(x)) * step(abs(x), 0.19);
        float hw = width * 0.5;
        float white = step(hw - 0.62, abs(x)) * step(abs(x), hw - 0.45);
        c = mix(c, vec3(0.90, 0.68, 0.16), yellow * breakup * 0.95);
        c = mix(c, vec3(0.88, 0.87, 0.82), white * breakup * 0.95);
        spec = 0.12;
    } else if (mat == 11) {
        c = texture(tDirt, uv * 1.2).rgb * 1.08;
        float rut = exp(-pow((abs(x) - width * 0.2) * 1.4, 2.0));
        c *= 1.0 - rut * 0.18;
        vec3 g = texture(tGrass, p.xz / 6.5).rgb;
        float edge = smoothstep(width * 0.32, width * 0.5, abs(x)) + smoothstep(0.65, 0.8, vnoise(vec2(x * 3.0, v * 0.5))) * step(abs(x), 0.6);
        c = mix(c, g, clamp(edge, 0.0, 1.0) * 0.85);
    } else {
        vec3 gravel = texture(tDirt, p.xz / 2.5).rgb * vec3(1.05, 1.02, 0.98);
        vec3 g = texture(tGrass, p.xz / 6.5).rgb;
        c = mix(gravel, g, smoothstep(0.35, 1.0, u + (vnoise(p.xz * 0.7) - 0.5) * 0.5));
    }
    float sh = shadowFactor(p, n);
    vec3 col = lighting(c, nn, sh);
    vec3 V = normalize(uCamPos - p);
    vec3 H = normalize(V + uSunDir);
    col += uSunColor * pow(max(dot(nn, H), 0.0), gloss) * spec * sh;
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
uniform sampler2D tFlakes;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    if (!gl_FrontFacing) n = -n;
    int mat = int(vCol.a + 0.5);
    vec3 V = normalize(uCamPos - vPos);
    float sh = shadowFactor(vPos, n);
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
    float spec = 0.06, gloss = 18.0, refl = 0.0, coat = 0.0, sharp = 0.0;
    if (mat == 1 || mat == 5) {
        albedo = (mat == 1 ? uPaint : uStripe) * vCol.rgb;
        float fl = texture(tFlakes, vObj.xz * 2.7 + vObj.y * 1.3).r;
        albedo *= 0.94 + 0.12 * fl;
        spec = 0.35; gloss = 40.0; coat = 1.0; sharp = 1.0;
    } else if (mat == 2) { albedo = vec3(0.012, 0.014, 0.018); coat = 1.0; sharp = 1.0; refl = 0.06; }
    else if (mat == 4) { albedo = vec3(0.35); refl = 0.75; spec = 1.0; gloss = 90.0; }
    else if (mat == 6) { albedo = uPaint * vCol.rgb * 0.1; }
    else if (mat == 7) { albedo = vec3(0.50, 0.51, 0.50); refl = 0.18; spec = 0.4; gloss = 35.0; }
    else if (mat == 8) { albedo = vec3(0.045, 0.045, 0.05) * vCol.rgb; spec = 0.08; gloss = 12.0; }
    else if (mat == 9) { albedo = vec3(0.035, 0.036, 0.04); spec = 0.25; gloss = 30.0; refl = 0.04; }
    else if (mat == 10) { albedo = vec3(0.30, 0.31, 0.33) * (1.0 + uHeadlights); coat = 1.0; sharp = 1.0; refl = 0.35; }
    else if (mat == 11) { albedo = vec3(0.65, 0.07, 0.05); spec = 0.5; gloss = 50.0; }
    else if (mat == 12) { albedo = vec3(0.86, 0.86, 0.82); spec = 0.2; }
    vec3 col = lighting(albedo, n, sh);
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
        col = mix(col, env * (mat == 4 ? vec3(1.0) : albedo * 2.0), refl);
    }
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
uniform float uRadius;
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
    vec2 xz = cell + (vec2(h1(cell), h1(cell + 17.3)) - 0.5) * 1.6;
    vec2 uv = (xz + uHalf) / (2.0 * uHalf);
    float h = hgt(xz);
    float rd = texture(tRoadDist, uv).r;
    float e = 1.5;
    vec3 gn = normalize(vec3(hgt(xz - vec2(e, 0)) - hgt(xz + vec2(e, 0)), 2.0 * e, hgt(xz - vec2(0, e)) - hgt(xz + vec2(0, e))));
    float d = length(xz - uCamPos.xz);
    float keep = step(1.2, rd) * step(uWaterLevel + 0.8, h) * step(0.82, gn.y) * step(52.0, length(xz - uHub.xz));
    float dens = smoothstep(0.25, 0.55, h1(floor(xz / 9.0)) * 0.6 + h1(cell * 0.37) * 0.4);
    keep *= step(h1(cell + 3.1), 0.35 + dens * 0.65);
    float fade = 1.0 - smoothstep(uRadius * 0.65, uRadius, d);
    float sc = (0.7 + 0.6 * h1(cell + 9.9)) * keep * fade;
    float ang = h1(cell + 5.5) * 6.2831;
    float c = cos(ang), s = sin(ang);
    vec3 p = aPos * vec3(sc, sc * (0.8 + 0.5 * h1(cell + 2.2)), sc);
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    float bend = aPos.y * aPos.y;
    p.x += sin(uTime * 1.8 + xz.x * 0.2 + xz.y * 0.13) * 0.10 * bend;
    p.z += cos(uTime * 1.5 + xz.x * 0.17) * 0.06 * bend;
    vec3 wp = vec3(xz.x, h - 0.04, xz.y) + p;
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
    if (d.y > 0.0) {
        vec2 uv = d.xz / (d.y + 0.12) * 0.9 + vec2(uTime * 0.004, uTime * 0.0015);
        float n = fbm2(uv * 1.3);
        float n2 = fbm2(uv * 4.1 + 4.0);
        float n3 = vnoise(uv * 11.0);
        float dens = n * 0.75 + n2 * 0.3 + n3 * 0.06;
        float cloud = smoothstep(0.52, 0.8, dens) * smoothstep(0.0, 0.2, d.y);
        float thick = smoothstep(0.6, 0.95, dens);
        float sun = max(dot(d, uSunDir), 0.0);
        vec3 lit = mix(vec3(1.0, 0.97, 0.92), uSunColor * vec3(1.0, 0.85, 0.65), 0.35 + 0.4 * pow(sun, 3.0));
        vec3 shade = mix(uSkyHorizon * 0.8, vec3(0.55, 0.57, 0.62), 0.5);
        vec3 cc = mix(lit, shade, thick * 0.6);
        cc += uSunColor * pow(sun, 12.0) * (1.0 - thick) * 0.6;
        c = mix(c, cc, cloud * 0.9);
    }
    float hz = 1.0 - smoothstep(-0.03, 0.10, d.y);
    c = mix(c, uFogColor, hz * 0.7);
    FragColor = vec4(c, 1.0);
}
)";

static const char* kWaterFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol; in vec3 vObj;
out vec4 FragColor;
void main(){
    vec2 p = vPos.xz; float t = uTime;
    float nx = (vnoise(p * 0.12 + vec2(t * 0.25, 0.0)) - 0.5) * 0.18 + (vnoise(p * 0.7 + vec2(0.0, -t * 0.6)) - 0.5) * 0.10 + (vnoise(p * 2.3 + t) - 0.5) * 0.05;
    float nz = (vnoise(p * 0.12 + vec2(7.0, t * 0.2)) - 0.5) * 0.18 + (vnoise(p * 0.7 + vec2(t * 0.5, 3.0)) - 0.5) * 0.10 + (vnoise(p * 2.1 - t) - 0.5) * 0.05;
    vec3 n = normalize(vec3(nx, 1.0, nz));
    vec3 V = normalize(uCamPos - vPos);
    vec3 R = reflect(-V, n);
    vec3 refl = envColor(R);
    float fres = 0.03 + 0.97 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 deep = vec3(0.03, 0.10, 0.11) * (uSkyAmb + uSunColor * 0.35);
    vec3 col = mix(deep, refl, clamp(fres, 0.0, 1.0));
    vec3 H = normalize(V + uSunDir);
    col += uSunColor * pow(max(dot(n, H), 0.0), 300.0) * 4.0 * shadowFactor(vPos, vec3(0, 1, 0));
    FragColor = vec4(applyFog(col, vPos), 1.0);
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
static const char* kPostFS = R"(
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBlur;
uniform float uFlash;
uniform float uBloomAmt;
out vec4 FragColor;
vec3 aces(vec3 x){ return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main(){
    vec2 uv = vUV;
    vec3 c = texture(uScene, uv).rgb;
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
    c += texture(uBloom, uv).rgb * uBloomAmt;
    c = aces(c * 1.12);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c *= mix(vec3(0.93, 0.99, 1.06), vec3(1.07, 1.0, 0.88), smoothstep(0.15, 0.75, l));
    l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.2);
    c = c * c * (3.0 - 2.0 * c) * 0.35 + c * 0.65;
    vec2 q = uv - 0.5;
    c *= 1.0 - dot(q, q) * 0.7;
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
uniform vec3 uClip;
out vec4 FragColor;
void main(){
    vec4 c = vCol;
    if (vMode > 0.5 && vMode < 1.5) c.a *= texture(uFont, vUV).r;
    else if (vMode > 1.5) c *= vec4(texture(uMap, vUV).rgb, 1.0);
    if (uClip.z > 0.0) {
        float d = length(vPix - uClip.xy);
        c.a *= 1.0 - smoothstep(uClip.z - 1.5, uClip.z, d);
    }
    FragColor = c;
}
)";
