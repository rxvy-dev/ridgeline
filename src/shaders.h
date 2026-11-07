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
float pcf5(sampler2DShadow s, vec3 p, float texel){
    float sum = 0.0;
    for (int x = -2; x <= 2; x++)
        for (int y = -2; y <= 2; y++)
            sum += texture(s, vec3(p.xy + vec2(x, y) * texel * 0.85, p.z));
    return sum / 25.0;
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
        float s0 = pcf5(uShadow0, vec3(pa.xy, pa.z - 0.00010), uShadowTexel.x);
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
uniform float uClipY;
vec3 applyFog(vec3 col, vec3 wp){
    if (wp.y < uClipY) discard;
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
        vec2 auv = vec2(x, v) / 4.0;
        c = texture(tAsphalt, auv).rgb * vec3(1.06, 1.02, 0.97);
        // break up tiling with a second, rotated lookup and large-scale tone variation
        c = mix(c, texture(tAsphalt, vec2(auv.y, -auv.x) * 0.61 + 0.3).rgb, 0.3);
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
        c = mix(c, c * (0.78 + 0.1 * vnoise(vec2(x, v) * 1.3)), inPatch);
        c = mix(c, vec3(0.06), seam * 0.5);
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
        spec = 0.1 + tracks * 0.25 + paint * 0.15; gloss = 26.0 + tracks * 30.0;
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
uniform int uGlassPass;
uniform sampler2D tFlakes;
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
    float spec = 0.06, gloss = 18.0, refl = 0.0, coat = 0.0, sharp = 0.0;
    float fill = mat == 0 ? 0.35 : 0.0;
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
        col = mix(col, env * (mat == 4 ? vec3(1.0) : albedo * 2.0), refl);
    }
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
    vec3 e = uEmis;
    if (uHasEmis == 1) e += texture(tEmis, uv).rgb;
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
uniform sampler2D tHeight, tRefl;
uniform mat4 uReflVP;
uniform int uHasRefl;
uniform float uHalf, uWaterLevel;
out vec4 FragColor;
vec2 waveGrad(vec2 p, float t){
    vec2 g = vec2(0.0);
    // a few directional swells plus noise ripples
    vec2 d1 = normalize(vec2(0.8, 0.6)), d2 = normalize(vec2(-0.5, 0.86)), d3 = normalize(vec2(0.2, -1.0));
    g += d1 * cos(dot(p, d1) * 0.35 + t * 1.1) * 0.06;
    g += d2 * cos(dot(p, d2) * 0.62 + t * 1.6) * 0.04;
    g += d3 * cos(dot(p, d3) * 1.3 + t * 2.3) * 0.025;
    g.x += (vnoise(p * 0.9 + vec2(t * 0.5, 0.0)) - 0.5) * 0.10 + (vnoise(p * 3.1 - t * 0.9) - 0.5) * 0.05;
    g.y += (vnoise(p * 0.9 + vec2(4.0, t * 0.45)) - 0.5) * 0.10 + (vnoise(p * 2.9 + vec2(t * 0.8, 2.0)) - 0.5) * 0.05;
    return g;
}
void main(){
    vec2 p = vPos.xz; float t = uTime;
    float ground = texture(tHeight, (p + uHalf) / (2.0 * uHalf)).r;
    float depth = uWaterLevel - ground;
    if (depth < -0.05) discard;
    vec2 g = waveGrad(p, t);
    float calm = smoothstep(0.0, 1.5, depth);
    vec3 n = normalize(vec3(-g.x * calm, 1.0, -g.y * calm));
    vec3 V = normalize(uCamPos - vPos);
    float dist = length(uCamPos - vPos);
    n = normalize(mix(n, vec3(0, 1, 0), smoothstep(150.0, 900.0, dist) * 0.6));
    float ndv = max(dot(n, V), 0.0);
    float fres = 0.025 + 0.975 * pow(1.0 - ndv, 5.0);
    vec3 R = reflect(-V, n);
    vec3 refl = envColor(R);
    if (uHasRefl == 1) {
        vec4 cp = uReflVP * vec4(vPos, 1.0);
        vec2 ruv = cp.xy / cp.w * 0.5 + 0.5 + n.xz * 0.035;
        vec3 pr = texture(tRefl, clamp(ruv, 0.002, 0.998)).rgb;
        float edge = smoothstep(0.0, 0.04, ruv.x) * smoothstep(1.0, 0.96, ruv.x) * smoothstep(1.0, 0.95, ruv.y);
        refl = mix(refl, pr, edge);
    }
    // water body: turquoise shallows to deep blue-green, lit by sky and sun
    float k = 1.0 - exp(-depth * 0.28);
    vec3 shallow = vec3(0.10, 0.30, 0.27), deepC = vec3(0.012, 0.055, 0.075);
    vec3 body = mix(shallow, deepC, k) * (uSkyAmb * 1.1 + uSunColor * 0.45 * max(uSunDir.y, 0.0));
    float sh = shadowFactor(vPos, vec3(0, 1, 0));
    body *= 0.6 + 0.4 * sh;
    // sun-lit scattering in wave crests
    float sss = pow(max(dot(-V, uSunDir) * 0.5 + 0.5, 0.0), 4.0) * max(g.x + g.y, 0.0) * 4.0;
    body += vec3(0.05, 0.16, 0.13) * sss * sh;
    vec3 col = mix(body, refl, clamp(fres * 1.05, 0.0, 1.0));
    vec3 H = normalize(V + uSunDir);
    float ndh = max(dot(n, H), 0.0);
    col += uSunColor * (pow(ndh, 600.0) * 6.0 + pow(ndh, 80.0) * 0.25) * sh;
    // shoreline foam lapping in and out
    float wave = sin(depth * 6.0 - t * 1.8 + vnoise(p * 0.3) * 4.0) * 0.5 + 0.5;
    float foamN = vnoise(p * 1.8 + vec2(t * 0.3, -t * 0.2)) * 0.6 + vnoise(p * 5.0 - t * 0.5) * 0.4;
    float shore = 1.0 - smoothstep(0.0, 0.9, depth);
    float foam = smoothstep(0.45, 0.7, foamN * (0.6 + 0.6 * wave) + shore * 0.55) * smoothstep(0.0, 0.25, shore + 0.05) * (shore > 0.0 ? 1.0 : 0.0);
    foam *= 1.0 - smoothstep(0.6, 1.4, depth);
    col = mix(col, vec3(0.88, 0.92, 0.93) * (uSkyAmb + uSunColor * 0.6 * sh), foam * 0.85);
    // transparency in the shallows so the lake bed shows through
    float alpha = clamp(max(1.0 - exp(-depth * 1.4), fres) + foam, 0.0, 1.0);
    alpha = max(alpha, smoothstep(60.0, 300.0, dist));
    FragColor = vec4(applyFog(col, vPos), alpha);
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
uniform vec2 uSunUV;
uniform float uSunAmt;
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
