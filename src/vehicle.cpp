#include "vehicle.h"

static const float kGears[6] = {3.30f, 2.25f, 1.68f, 1.32f, 1.06f, 0.86f};
static const float kReverse = 3.0f;
static const float G = 9.81f;
static const float kGripScale = 1.18f;   // arcade friendliness

void Vehicle::init(const CarSpec* s, vec3 p, float heading) {
    spec = s;
    float wb = s->wheelbase;
    bool rearBias = s->body == BODY_MIDENGINE || s->body == BODY_HYPER;
    aFront = wb * (rearBias ? 0.56f : 0.47f);
    bRear = wb - aFront;
    float omegaRed = s->redline * 2 * PI / 60.f;
    float vTop = s->topKmh / 3.6f;
    finalDrive = omegaRed * s->wheelR / (vTop * 1.02f * kGears[5]);
    rollRes = 0.012f * s->massKg * G;
    float P = s->powerKW * 1000.f;
    dragK = std::max(0.1f, (0.9f * P - rollRes * vTop) / (vTop * vTop * vTop));
    inertia = s->massKg * (s->length * s->length + s->width * s->width) / 12.f * 1.1f;
    pos = p;
    yaw = heading;
    vel = vec3(0, 0, 0);
    yawRate = 0;
    gear = 1;
    rpm = 900;
}

void Vehicle::placeAt(vec3 p, float heading, const World& w) {
    pos = p;
    pos.y = w.heightAt(p.x, p.z);
    yaw = heading;
    vel = vec3(0, 0, 0);
    yawRate = 0;
    gear = 1;
    reversing = false;
    up = w.normalAt(p.x, p.z);
    airTime = 0;
    waterTime = 0;
    bodyPitch = bodyRoll = 0;
}

static float engineTorque(const CarSpec& s, float rpm) {
    float x = rpm / s.redline;
    float pf = x < 0.85f ? std::sin(x / 0.85f * PI * 0.5f) : 1.f - (x - 0.85f) * 0.6f;
    pf = std::max(pf, 0.12f);
    float omega = std::max(rpm, 1000.f) * 2 * PI / 60.f;
    return s.powerKW * 1000.f * pf / omega;
}

static float tyreCurve(float a) { return std::sin(1.6f * std::atan(7.5f * a)); }

void Vehicle::update(float dt, const World& w, const Controls& in) {
    const CarSpec& s = *spec;
    const float m = s.massKg, wb = s.wheelbase, R = s.wheelR;
    vec3 fwd = forward(), rgt = right();
    vec3 vh(vel.x, 0, vel.z);
    float vF = dot(vh, fwd), vR = dot(vh, rgt);
    float v = length(vh);

    // ---- inputs
    float steerRate = std::fabs(in.steer) > std::fabs(steer) ? 3.2f : 5.5f;
    steer += clampf(in.steer - steer, -steerRate * dt, steerRate * dt);
    handbrake += clampf(in.handbrake - handbrake, -8 * dt, 8 * dt);
    float wantThrottle = in.throttle, wantBrake = in.brake;
    if (!reversing && in.brake > 0.1f && in.throttle < 0.1f && vF < 0.8f) reversing = true;
    if (reversing && (in.throttle > 0.1f || vF > 1.5f)) reversing = false;
    if (reversing) { wantThrottle = in.brake; wantBrake = in.throttle; }
    throttle += clampf(wantThrottle - throttle, -10 * dt, 10 * dt);
    brake += clampf(wantBrake - brake, -10 * dt, 10 * dt);

    // ---- gearbox
    shiftTimer = std::max(0.f, shiftTimer - dt);
    if (reversing) gear = -1;
    else if (gear < 1) gear = 1;
    float ratio = gear < 0 ? kReverse : kGears[gear - 1];
    float wheelRpm = std::fabs(vF) / R * 60.f / (2 * PI);
    float rpmWheel = wheelRpm * ratio * finalDrive;
    if (gear > 0 && shiftTimer <= 0) {
        if (rpmWheel > s.redline * 0.94f && gear < 6) { gear++; shiftTimer = 0.22f; }
        else if (gear > 1 && rpmWheel < s.redline * 0.42f) {
            float lower = wheelRpm * kGears[gear - 2] * finalDrive;
            if (lower < s.redline * 0.88f) { gear--; shiftTimer = 0.15f; }
        }
        ratio = kGears[gear - 1];
        rpmWheel = wheelRpm * ratio * finalDrive;
    }
    float launch = 900.f + throttle * s.redline * 0.55f * (1.f - smoothstepf(2.f, 10.f, std::fabs(vF)));
    float targetRpm = clampf(std::max(rpmWheel, launch), 850.f, s.redline);
    if (!grounded) targetRpm = clampf(900.f + throttle * s.redline, 850.f, s.redline);
    rpm += (targetRpm - rpm) * damp(18.f, dt);

    // ---- ground
    float h = w.heightAt(pos.x, pos.z);
    float clearance = pos.y - h;
    float contact = 1.f - smoothstepf(0.08f, 0.4f, clearance);
    grounded = contact > 0.01f;
    surface = w.surfaceAt(pos.x, pos.z);
    vec3 n = w.normalAt(pos.x, pos.z);

    float surfMu = 1.f, surfRoll = 1.f;
    switch (surface) {
        case SURF_DIRT: surfMu = lerpf(0.66f, 0.96f, s.offroad); surfRoll = lerpf(2.2f, 1.2f, s.offroad); break;
        case SURF_GRASS: surfMu = lerpf(0.55f, 0.92f, s.offroad); surfRoll = lerpf(4.0f, 1.5f, s.offroad); break;
        case SURF_WATER: surfMu = 0.3f; surfRoll = 8.f; break;
        default: break;
    }
    float mu = s.grip * surfMu * kGripScale;

    float maxSteer = 0.6f / (1.f + v * 0.055f);
    // let the player counter-steer into a slide
    float delta = steer * maxSteer;
    if (v > 5 && std::fabs(slipAngle) > 0.1f && signf(steer) == -signf(slipAngle)) delta = steer * std::max(maxSteer, std::fabs(slipAngle) * 0.9f);
    wheelSteer = delta;

    float FxTot = 0, FyTot = 0, torque = 0;
    tyreSlip = 0;
    if (grounded) {
        float hcg = 0.5f;
        float down = 0.35f * dragK * v * v;
        float FzF = (m * G * n.y * bRear / wb) - longAccel * m * hcg / wb + down * 0.45f;
        float FzR = (m * G * n.y * aFront / wb) + longAccel * m * hcg / wb + down * 0.55f;
        FzF = std::max(FzF, 0.15f * m * G) * contact;
        FzR = std::max(FzR, 0.15f * m * G) * contact;

        float vAbs = std::max(std::fabs(vF), 3.f);
        float alphaF = std::atan2(vR + yawRate * aFront, vAbs) - delta * (vF >= 0 ? 1.f : -1.f);
        float alphaR = std::atan2(vR - yawRate * bRear, vAbs);
        float muF = mu, muR = mu * (1.f - 0.55f * handbrake);

        // drive and brakes
        float drive = 0;
        if (shiftTimer <= 0 || gear < 0) {
            float tq = engineTorque(s, rpm);
            if (rpm >= s.redline * 0.995f) tq *= 0.2f;  // limiter
            drive = tq * ratio * finalDrive * 0.9f / R * throttle * (gear < 0 ? -0.6f : 1.f);
        }
        float driveF = 0, driveR = 0;
        if (s.drive == FWD) driveF = drive;
        else if (s.drive == RWD) driveR = drive;
        else { driveF = drive * 0.4f; driveR = drive * 0.6f; }
        if (assists) {
            // traction control: keep the driven tyres just under their grip limit,
            // and back off further when the tyres are already working hard sideways
            float latF = std::fabs(tyreCurve(alphaF)), latR = std::fabs(tyreCurve(alphaR));
            float capF = muF * FzF * (0.92f - 0.45f * latF), capR = muR * FzR * (0.92f - 0.45f * latR);
            float pre = std::fabs(driveF) + std::fabs(driveR);
            driveF = clampf(driveF, -capF, capF);
            if (handbrake < 0.1f) driveR = clampf(driveR, -capR, capR);
            if (pre > (std::fabs(driveF) + std::fabs(driveR)) * 1.05f && throttle > 0.2f) tcsLight = 1.f;
            if (brake > 0.7f && v > 6.f) absLight = 1.f;
        }
        float dir = vF > 0.3f ? 1.f : (vF < -0.3f ? -1.f : 0.f);
        float brakeF = brake * m * G * 1.15f * 0.6f * dir;
        float brakeR = brake * m * G * 1.15f * 0.4f * dir + handbrake * m * G * 0.5f * dir;
        float FxF = driveF - brakeF, FxR = driveR - brakeR;

        float maxF = muF * FzF, maxR = muR * FzR;
        float spinF = 0, spinR = 0;
        if (std::fabs(FxF) > maxF) { spinF = std::fabs(FxF) / maxF - 1.f; FxF = clampf(FxF, -maxF, maxF); }
        if (std::fabs(FxR) > maxR) { spinR = std::fabs(FxR) / maxR - 1.f; FxR = clampf(FxR, -maxR, maxR); }
        float FyF = -maxF * tyreCurve(alphaF);
        float FyR = -maxR * tyreCurve(alphaR);
        float availF = std::sqrt(std::max(maxF * maxF - FxF * FxF, 0.f));
        float availR = std::sqrt(std::max(maxR * maxR - FxR * FxR * 0.85f, 0.f));
        FyF = clampf(FyF, -availF, availF);
        FyR = clampf(FyR, -availR, availR);

        float cd = std::cos(delta), sd = std::sin(delta);
        float FxFc = FxF * cd - FyF * sd;
        float FyFc = FxF * sd + FyF * cd;

        float drag = dragK * vF * std::fabs(vF) + rollRes * surfRoll * clampf(vF * 2.f, -1.f, 1.f);
        FxTot = FxFc + FxR - drag;
        FyTot = FyFc + FyR - dragK * 2.f * vR * std::fabs(vR);
        torque = FyFc * aFront - FyR * bRear;

        tyreSlip = clampf((std::fabs(alphaR) - 0.12f) * 2.5f, 0.f, 1.f);
        tyreSlip = std::max(tyreSlip, clampf((std::fabs(alphaF) - 0.16f) * 2.5f, 0.f, 1.f));
        tyreSlip = std::max(tyreSlip, clampf(std::max(spinF, spinR) * 1.5f, 0.f, 1.f) * smoothstepf(1.f, 4.f, v));
        if (brake > 0.6f && v > 8 && (spinF > 0.05f || spinR > 0.05f)) tyreSlip = std::max(tyreSlip, 0.7f);
        tyreSlip *= smoothstepf(2.f, 8.f, v);
        if (surface != SURF_ASPHALT) tyreSlip *= 0.6f;

        // gravity along the slope
        vec3 gPlane(n.x * n.y * G, 0, n.z * n.y * G);
        vel.x += gPlane.x * dt * contact;
        vel.z += gPlane.z * dt * contact;
    }

    // stability assist: gently rotate the nose back towards the direction of travel
    if (assists && grounded && v > 8.f && handbrake < 0.1f) {
        float sa = std::atan2(vR, std::fabs(vF));
        float excess = std::fabs(sa) - 0.16f;
        if (excess > 0) { torque += signf(sa) * excess * inertia * 9.f; scLight = 1.f; }
    }
    tcsLight = std::max(0.f, tcsLight - dt * 3.f);
    absLight = std::max(0.f, absLight - dt * 3.f);
    scLight = std::max(0.f, scLight - dt * 3.f);
    vec3 accel = fwd * (FxTot / m) + rgt * (FyTot / m);
    vel.x += accel.x * dt;
    vel.z += accel.z * dt;
    yawRate += torque / inertia * dt;

    // low-speed kinematic steering + lateral grip
    if (grounded) {
        float kb = smoothstepf(1.5f, 6.f, vF);
        float vF2 = dot(vec3(vel.x, 0, vel.z), fwd);
        float yawKin = vF2 * std::tan(delta) / wb;
        yawRate = lerpf(yawKin, yawRate, kb);
        float vR2 = dot(vec3(vel.x, 0, vel.z), rgt);
        float latDamp = (1.f - kb) * std::min(1.f, 12.f * dt);
        vel -= rgt * (vR2 * latDamp);
        // mild stability assist
        yawRate *= 1.f - std::min(1.f, 0.6f * dt);
        // stop creeping when braking at standstill
        if (brake > 0.5f && std::fabs(vF2) < 0.4f && !reversing) { vel.x *= 0.8f; vel.z *= 0.8f; }
    } else {
        yawRate *= 1.f - std::min(1.f, 0.8f * dt);
    }
    yaw -= yawRate * dt;
    yaw = wrapAngle(yaw);

    // accelerations for weight transfer & body motion (smoothed)
    longAccel += (FxTot / m - longAccel) * damp(8.f, dt);
    latAccel += (FyTot / m - latAccel) * damp(8.f, dt);

    // ---- vertical / terrain following
    float hOld = h;
    vel.y -= G * dt;
    vec3 np = pos + vel * dt;
    float hNew = w.heightAt(np.x, np.z);
    if (np.y < hNew) {
        float follow = (hNew - hOld) / dt;
        if (!grounded && vel.y < follow - 4.f) impact = std::max(impact, follow - vel.y);
        np.y = hNew;
        vel.y = std::max(vel.y, clampf(follow, -40.f, 40.f));
    }
    pos = np;
    bool air = pos.y - hNew > 0.4f;
    airTime = air ? airTime + dt : 0.f;

    // world bounds
    const float B = World::HALF - 40.f;
    if (pos.x < -B) { pos.x = -B; vel.x = std::fabs(vel.x) * 0.3f; }
    if (pos.x > B) { pos.x = B; vel.x = -std::fabs(vel.x) * 0.3f; }
    if (pos.z < -B) { pos.z = -B; vel.z = std::fabs(vel.z) * 0.3f; }
    if (pos.z > B) { pos.z = B; vel.z = -std::fabs(vel.z) * 0.3f; }

    // trees & rocks
    vec3 tn;
    for (int c = -1; c <= 1; c += 2) {
        vec3 cp = pos + fwd * (c * s.length * 0.28f);
        vec3 before = cp;
        if (w.collideTrees(cp, s.width * 0.48f, tn)) {
            pos += cp - before;
            float vn = dot(vel, tn);
            if (vn < 0) {
                impact = std::max(impact, -vn);
                vel -= tn * (vn * 1.35f);
                vel.x *= 0.7f; vel.z *= 0.7f;
                yawRate += c * dot(tn, rgt) * -vn * 0.08f;
            }
        }
    }

    // water
    if (surface == SURF_WATER && pos.y < w.waterLevel) {
        waterTime += dt;
        vel.x *= 1.f - std::min(1.f, 2.5f * dt);
        vel.z *= 1.f - std::min(1.f, 2.5f * dt);
    } else {
        waterTime = 0;
    }

    // ---- presentation
    vec3 nn = grounded ? n : vec3(0, 1, 0);
    up = normalize(lerp(up, nn, damp(grounded ? 12.f : 1.5f, dt)));
    float tp = clampf(-longAccel * 0.007f, -0.06f, 0.06f), tr = clampf(-latAccel * 0.009f, -0.08f, 0.08f);
    bodyPitch += (tp - bodyPitch) * damp(6.f, dt);
    bodyRoll += (tr - bodyRoll) * damp(6.f, dt);
    wheelSpin += dot(vel, fwd) / R * dt;
    if (wheelSpin > 1000) wheelSpin -= 2 * PI * 150;
    if (wheelSpin < -1000) wheelSpin += 2 * PI * 150;
    slipAngle = v > 3.f ? std::atan2(dot(vel, rgt), std::fabs(dot(vel, fwd))) : 0.f;
    impact = std::max(0.f, impact - 30.f * dt);
}

mat4 Vehicle::chassisMatrix() const {
    vec3 f = forward();
    vec3 u = normalize(up);
    f = normalize(f - u * dot(f, u));
    vec3 left = cross(u, f);
    return basis(left, u, f, pos);
}

mat4 Vehicle::bodyMatrix() const {
    return chassisMatrix() * translate(vec3(0, 0.02f, 0)) * rotateX(bodyPitch) * rotateZ(bodyRoll);
}

mat4 Vehicle::wheelMatrix(const CarModel& m, int i, bool spin) const {
    bool front = i < 2, rightSide = (i % 2) == 1;
    mat4 M = chassisMatrix() * translate(m.wheelPos[i]);
    if (front) M = M * rotateY(-wheelSteer);
    float a = spin ? wheelSpin : 0.f;
    if (rightSide) M = M * rotateY(PI) * rotateX(-a);
    else M = M * rotateX(a);
    return M * scale(vec3(m.wheelW, m.wheelR, m.wheelR));
}

float collideCars(Vehicle& a, Vehicle& b) {
    float best = 0;
    vec3 da = a.pos - b.pos;
    if (da.x * da.x + da.z * da.z > 100.f) return 0;
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2) {
            vec3 pa = a.pos + a.forward() * (i * a.spec->length * 0.28f);
            vec3 pb = b.pos + b.forward() * (j * b.spec->length * 0.28f);
            float ra = a.spec->width * 0.55f, rb = b.spec->width * 0.55f;
            vec3 d = pa - pb;
            d.y = 0;
            float dist = length(d);
            if (dist >= ra + rb || dist < 1e-4f) continue;
            vec3 n = d / dist;
            float pen = ra + rb - dist;
            float ma = a.spec->massKg, mb = b.spec->massKg;
            a.pos += n * (pen * mb / (ma + mb));
            b.pos -= n * (pen * ma / (ma + mb));
            float vrel = dot(a.vel - b.vel, n);
            if (vrel < 0) {
                float jimp = -(1.f + 0.25f) * vrel / (1.f / ma + 1.f / mb);
                a.vel += n * (jimp / ma);
                b.vel -= n * (jimp / mb);
                a.yawRate += i * dot(n, a.right()) * -vrel * 0.05f;
                b.yawRate -= j * dot(n, b.right()) * -vrel * 0.05f;
                best = std::max(best, -vrel);
            }
        }
    a.impact = std::max(a.impact, best);
    b.impact = std::max(b.impact, best);
    return best;
}
