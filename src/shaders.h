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
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowMat;
uniform int uShadows;
uniform float uShadowTexel;

float hash12(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p); vec2 u = f*f*(3.0-2.0*f);
    return mix(mix(hash12(i), hash12(i+vec2(1,0)), u.x), mix(hash12(i+vec2(0,1)), hash12(i+vec2(1,1)), u.x), u.y);
}
float fbm2(vec2 p){ float s = 0.0, a = 0.5; for (int i = 0; i < 4; i++){ s += vnoise(p)*a; p = p*2.07 + vec2(1.7, 9.2); a *= 0.5; } return s / 0.9375; }

float shadowFactor(vec3 wp, vec3 n){
    if (uShadows == 0) return 1.0;
    vec4 sp = uShadowMat * vec4(wp + n * 0.12, 1.0);
    vec3 p = sp.xyz / sp.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            s += texture(uShadowMap, vec3(p.xy + vec2(x, y) * uShadowTexel, p.z - 0.0008));
    s /= 9.0;
    vec2 e = abs(p.xy - 0.5) * 2.0;
    return mix(s, 1.0, smoothstep(0.8, 1.0, max(e.x, e.y)));
}
vec3 skyColor(vec3 d){
    float t = clamp(d.y, 0.0, 1.0);
    vec3 c = mix(uSkyHorizon, uSkyTop, pow(t, 0.5));
    if (d.y < 0.0) c = mix(uSkyHorizon, uGroundAmb * 1.1, clamp(-d.y * 4.0, 0.0, 1.0));
    float s = max(dot(d, uSunDir), 0.0);
    c += uSunColor * (pow(s, 900.0) * 8.0 + pow(s, 32.0) * 0.22 + pow(s, 4.0) * 0.07);
    return c;
}
vec3 applyFog(vec3 col, vec3 wp){
    vec3 v = wp - uCamPos; float dist = length(v); vec3 dir = v / max(dist, 0.001);
    float hf = exp(-max(wp.y - 10.0, 0.0) * 0.006);
    float f = 1.0 - exp(-dist * uFogDensity * (0.55 + 0.9 * hf));
    float s = pow(max(dot(dir, uSunDir), 0.0), 6.0);
    vec3 fc = mix(uFogColor, uSunColor * 0.85 + vec3(0.08), s * 0.4);
    return mix(col, fc, clamp(f, 0.0, 1.0));
}
vec3 lighting(vec3 albedo, vec3 n, float shadow){
    float ndl = max(dot(n, uSunDir), 0.0);
    vec3 amb = mix(uGroundAmb, uSkyAmb, n.y * 0.5 + 0.5);
    return albedo * (amb + uSunColor * ndl * shadow);
}
)";

// ---------------------------------------------------------------- static lit geometry
static const char* kLitVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vPos; out vec3 vNrm; out vec4 vCol;
void main(){
    vec4 wp = uModel * vec4(aPos, 1.0);
    vPos = wp.xyz;
    vNrm = mat3(uModel) * aNrm;
    vCol = aCol;
    gl_Position = uViewProj * wp;
}
)";

static const char* kTerrainFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol;
uniform float uWater;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    float slope = 1.0 - n.y;
    vec2 p = vPos.xz;
    float n1 = fbm2(p * 0.012);
    float n2 = vnoise(p * 0.11);
    float n3 = vnoise(p * 1.7);
    float n4 = vnoise(p * 0.45);
    vec3 dryGrass = vec3(0.66, 0.54, 0.29);
    vec3 green    = vec3(0.36, 0.42, 0.18);
    vec3 dirt     = vec3(0.45, 0.35, 0.23);
    vec3 rock     = vec3(0.47, 0.45, 0.42);
    vec3 snow     = vec3(0.93, 0.95, 0.98);
    vec3 c = mix(green, dryGrass, smoothstep(0.3, 0.7, n1));
    c = mix(c, dirt, smoothstep(0.6, 0.85, n2) * 0.55);
    c *= 0.82 + 0.22 * n3 + 0.12 * n4;
    c = mix(c, vCol.rgb, vCol.a);
    float r = smoothstep(0.28, 0.42, slope + (n2 - 0.5) * 0.12);
    vec3 rk = rock * (0.75 + 0.35 * vnoise(vec2(p.x + p.y, vPos.y * 1.5) * 0.35) + 0.15 * n3);
    c = mix(c, rk, r);
    float sn = smoothstep(285.0, 330.0, vPos.y + n1 * 40.0) * (1.0 - r * 0.6);
    c = mix(c, snow, sn);
    c = mix(c, vec3(0.70, 0.64, 0.47), 1.0 - smoothstep(uWater + 0.4, uWater + 2.2, vPos.y + n3 * 0.5));
    float sh = shadowFactor(vPos, n);
    vec3 col = lighting(c, n, sh);
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

static const char* kRoadFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    float u = vCol.r, v = vCol.g; int mat = int(vCol.a + 0.5);
    vec2 p = vPos.xz;
    float grain = vnoise(p * 3.1) * 0.5 + vnoise(p * 0.4) * 0.5;
    vec3 c; float spec = 0.0;
    if (mat == 10) {
        c = vec3(0.19, 0.19, 0.2) * (0.85 + 0.25 * grain);
        float wear = exp(-pow((abs(u - 0.5) - 0.24) * 14.0, 2.0));
        c *= 1.0 - wear * 0.18;
        float edge = step(u, 0.04) * step(0.02, u) + step(0.96, u) * step(u, 0.98);
        float center = step(abs(u - 0.5), 0.012) * step(fract(v / 12.0), 0.5);
        c = mix(c, vec3(0.86, 0.85, 0.8), max(edge, center) * (0.85 + 0.15 * grain));
        spec = 0.15;
    } else if (mat == 11) {
        c = vec3(0.50, 0.40, 0.27) * (0.8 + 0.35 * grain);
        float rut = exp(-pow((abs(u - 0.5) - 0.2) * 9.0, 2.0));
        c *= 1.0 - rut * 0.22;
        c = mix(c, vec3(0.45, 0.42, 0.24), smoothstep(0.38, 0.5, abs(u - 0.5)) * 0.6);
    } else {
        c = vec3(0.42, 0.39, 0.33) * (0.8 + 0.3 * grain);
    }
    float sh = shadowFactor(vPos, n);
    vec3 col = lighting(c, n, sh);
    vec3 V = normalize(uCamPos - vPos);
    vec3 H = normalize(V + uSunDir);
    col += uSunColor * pow(max(dot(n, H), 0.0), 40.0) * spec * sh;
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

static const char* kObjectFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol;
uniform vec3 uPaint;
uniform vec3 uStripe;
uniform float uBrake;
uniform float uHeadlights;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    if (!gl_FrontFacing) n = -n;
    int mat = int(vCol.a + 0.5);
    vec3 V = normalize(uCamPos - vPos);
    if (mat == 3) {
        vec3 e = vCol.rgb;
        bool red = e.r > e.g * 2.0;
        e *= red ? (1.1 + uBrake * 2.2) : (1.3 + uHeadlights);
        FragColor = vec4(applyFog(e, vPos), 1.0);
        return;
    }
    vec3 albedo = vCol.rgb; float spec = 0.08; float gloss = 16.0; float refl = 0.0;
    if (mat == 1) { albedo = uPaint; spec = 0.8; gloss = 110.0; refl = 0.07; }
    else if (mat == 5) { albedo = uStripe; spec = 0.8; gloss = 110.0; refl = 0.07; }
    else if (mat == 2) { albedo = vec3(0.02, 0.025, 0.03); spec = 1.0; gloss = 160.0; refl = 0.32; }
    else if (mat == 4) { albedo = vec3(0.6); spec = 1.0; gloss = 60.0; refl = 0.6; }
    float sh = shadowFactor(vPos, n);
    vec3 col = lighting(albedo, n, sh);
    vec3 H = normalize(V + uSunDir);
    col += uSunColor * pow(max(dot(n, H), 0.0), gloss) * spec * sh;
    if (refl > 0.0) {
        vec3 R = reflect(-V, n);
        vec3 env = skyColor(R);
        if (R.y < 0.0) env = mix(uFogColor * 0.5, uGroundAmb * 0.7, clamp(-R.y * 3.0, 0.0, 1.0));
        float f = refl + (1.0 - refl) * pow(1.0 - max(dot(n, V), 0.0), 5.0) * (mat == 2 ? 0.6 : 0.3);
        col = mix(col, env, clamp(f, 0.0, 1.0));
    }
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

// ---------------------------------------------------------------- instanced foliage / rocks
static const char* kTreeVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aCol;
layout(location=3) in vec4 iPosScale;
layout(location=4) in vec4 iTintRot;
uniform mat4 uViewProj;
uniform float uTime;
out vec3 vPos; out vec3 vNrm; out vec4 vCol;
void main(){
    float c = cos(iTintRot.w), s = sin(iTintRot.w);
    vec3 p = aPos * iPosScale.w;
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    vec3 nr = vec3(c * aNrm.x + s * aNrm.z, aNrm.y, -s * aNrm.x + c * aNrm.z);
    if (aCol.a > 0.5) {
        float w = sin(uTime * 1.4 + iPosScale.x * 0.13 + iPosScale.z * 0.07) * 0.035 * p.y;
        p.x += w; p.z += w * 0.6;
    }
    vec3 wp = iPosScale.xyz + p;
    vPos = wp; vNrm = nr;
    vCol = vec4(aCol.a > 0.5 ? aCol.rgb * iTintRot.rgb : aCol.rgb, aCol.a);
    gl_Position = uViewProj * vec4(wp, 1.0);
}
)";

static const char* kTreeFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol;
out vec4 FragColor;
void main(){
    vec3 n = normalize(vNrm);
    float sh = shadowFactor(vPos, n);
    vec3 col;
    if (vCol.a > 0.5) {
        float wrap = max((dot(n, uSunDir) + 0.45) / 1.45, 0.0);
        vec3 V = normalize(uCamPos - vPos);
        float trans = pow(max(dot(-V, uSunDir), 0.0), 4.0) * 0.35;
        vec3 amb = mix(uGroundAmb, uSkyAmb, n.y * 0.5 + 0.5);
        float var = 0.85 + 0.3 * vnoise(vPos.xz * 0.8 + vPos.y);
        col = vCol.rgb * var * (amb * 0.95 + uSunColor * (wrap * 0.9 + trans) * sh);
    } else {
        col = lighting(vCol.rgb, n, sh);
    }
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
layout(location=3) in vec4 iPosScale;
layout(location=4) in vec4 iTintRot;
uniform mat4 uViewProj;
void main(){
    float c = cos(iTintRot.w), s = sin(iTintRot.w);
    vec3 p = aPos * iPosScale.w;
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    gl_Position = uViewProj * vec4(iPosScale.xyz + p, 1.0);
}
)";
static const char* kDepthFS = R"(
out vec4 FragColor;
void main(){ FragColor = vec4(1.0); }
)";

// ---------------------------------------------------------------- sky (fullscreen triangle)
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
        vec2 uv = d.xz / (d.y + 0.15) * 1.3 + vec2(uTime * 0.006, uTime * 0.002);
        float n = fbm2(uv * 1.1);
        float n2 = fbm2(uv * 3.1 + 4.0);
        float cloud = smoothstep(0.5, 0.78, n * 0.8 + n2 * 0.3) * smoothstep(0.0, 0.18, d.y);
        float lit = 0.8 + 0.35 * max(dot(d, uSunDir), 0.0);
        vec3 cc = mix(vec3(1.0, 0.98, 0.95), uSunColor * 0.8, 0.25) * lit * (0.85 + 0.2 * n2);
        c = mix(c, cc, cloud * 0.85);
    }
    float hz = 1.0 - smoothstep(-0.02, 0.12, d.y);
    c = mix(c, uFogColor, hz * 0.85);
    FragColor = vec4(c, 1.0);
}
)";

// ---------------------------------------------------------------- water
static const char* kWaterFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol;
out vec4 FragColor;
void main(){
    vec2 p = vPos.xz; float t = uTime;
    float nx = (vnoise(p * 0.12 + vec2(t * 0.25, 0.0)) - 0.5) * 0.22 + (vnoise(p * 0.7 + vec2(0.0, -t * 0.6)) - 0.5) * 0.12;
    float nz = (vnoise(p * 0.12 + vec2(7.0, t * 0.2)) - 0.5) * 0.22 + (vnoise(p * 0.7 + vec2(t * 0.5, 3.0)) - 0.5) * 0.12;
    vec3 n = normalize(vec3(nx, 1.0, nz));
    vec3 V = normalize(uCamPos - vPos);
    vec3 R = reflect(-V, n);
    vec3 refl = skyColor(R);
    float fres = 0.03 + 0.97 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 deep = vec3(0.04, 0.13, 0.15) * (uSkyAmb + uSunColor * 0.35);
    vec3 col = mix(deep, refl, clamp(fres, 0.0, 1.0));
    vec3 H = normalize(V + uSunDir);
    col += uSunColor * pow(max(dot(n, H), 0.0), 220.0) * 2.5 * shadowFactor(vPos, vec3(0, 1, 0));
    FragColor = vec4(applyFog(col, vPos), 1.0);
}
)";

// ---------------------------------------------------------------- unlit / blended (beacons, blobs, checkpoints, skids)
static const char* kUnlitFS = R"(
in vec3 vPos; in vec3 vNrm; in vec4 vCol;
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

// ---------------------------------------------------------------- post processing
static const char* kPostFS = R"(
in vec2 vUV;
uniform sampler2D uScene;
uniform float uBlur;
uniform float uFlash;
out vec4 FragColor;
vec3 aces(vec3 x){ return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
void main(){
    vec2 uv = vUV;
    vec3 c = texture(uScene, uv).rgb;
    if (uBlur > 0.001) {
        vec2 dir = uv - vec2(0.5, 0.48);
        float amt = uBlur * smoothstep(0.08, 0.65, length(dir));
        vec3 acc = c; float w = 1.0;
        for (int i = 1; i < 12; i++) {
            float t = float(i) / 12.0;
            acc += texture(uScene, uv - dir * amt * t * 0.16).rgb;
            w += 1.0;
        }
        c = acc / w;
    }
    c = aces(c * 1.08);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 1.14);
    c = pow(c, vec3(0.96, 0.98, 1.03));
    vec2 q = uv - 0.5;
    c *= 1.0 - dot(q, q) * 0.6;
    c = mix(c, vec3(1.0), uFlash);
    FragColor = vec4(c, 1.0);
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
