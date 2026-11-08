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
    // centre of gravity height and aero: tall vans roll and pitch, wings press hypercars down
    hcg = clampf(s->height * 0.38f, 0.38f, 1.1f);
    downK = s->body == BODY_HYPER ? 0.9f : s->body == BODY_MIDENGINE ? 0.6f : s->body == BODY_TRUCK ? 0.05f : 0.3f;
    rollFront = s->drive == FWD ? 0.62f : s->drive == RWD ? 0.5f : 0.56f;
    alphaFs = alphaRs = 0;
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

// Tyres grip less per kilo the harder they are pressed: an axle that takes more load
// gains grip, but not proportionally. Load transfer therefore costs total grip, which
// is what makes weight balance, braking into corners and lift-off oversteer work.
static float loadGrip(float Fz, float Fz0) {
    float r = Fz / std::max(Fz0, 1.f);
    return Fz * (1.08f - 0.08f * r);
}

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
    // only drop into reverse when the car has (nearly) stopped - not while sliding sideways with the brake on
    if (!reversing && in.brake > 0.1f && in.throttle < 0.1f && vF < 0.8f && v < 2.f) reversing = true;
    if (reversing && (in.throttle > 0.1f || vF > 1.5f)) reversing = false;
    if (reversing) { wantThrottle = in.brake; wantBrake = in.throttle; }
    // holding the brake and the throttle at a standstill revs the engine against the brakes (launch / showing off)
    revving = !reversing && v < 1.5f && in.brake > 0.3f && in.throttle > 0.1f;
    if (revving) wantBrake = 1.f;
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
        if (rpmWheel > s.redline * 0.94f && gear < 6) {
            gear++; shiftTimer = 0.22f;
            if (throttle > 0.6f) popCount++;  // ignition cut on the upshift: bang
        }
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
    if (revving) targetRpm = clampf(900.f + throttle * s.redline * 1.05f, 850.f, s.redline);
    rpm += (targetRpm - rpm) * damp(revving ? 9.f : 18.f, dt);
    // rev limiter bounce: the ignition cuts and the needle drops a little
    onLimiter = rpm > s.redline * 0.985f && throttle > 0.5f;
    if (onLimiter) {
        limPh += dt * 30.f;
        rpm -= s.redline * 0.05f * std::fabs(std::sin(limPh));
        popRng = popRng * 1664525u + 1013904223u;
        if ((popRng >> 24) < 6) popCount++;
    }
    // lifting off from high revs: unburnt fuel crackles in the hot exhaust
    if (prevThrottle > 0.6f && throttle < 0.3f && rpm > s.redline * 0.55f) crackle = 0.5f + 0.9f * (rpm / s.redline);
    if (throttle > 0.5f) crackle = std::min(crackle, 0.05f);
    crackle = std::max(0.f, crackle - dt);
    prevThrottle = throttle;

    // ---- ground
    float h = w.heightAt(pos.x, pos.z) + groundBoost;
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
        float down = downK * dragK * v * v;
        float FzF0 = m * G * bRear / wb, FzR0 = m * G * aFront / wb;
        float FzF = (m * G * n.y * bRear / wb) - longAccel * m * hcg / wb + down * 0.45f;
        float FzR = (m * G * n.y * aFront / wb) + longAccel * m * hcg / wb + down * 0.55f;
        FzF = std::max(FzF, 0.15f * m * G) * contact;
        FzR = std::max(FzR, 0.15f * m * G) * contact;
        // lateral load transfer: the outside tyre can't make up what the inside one loses
        float track = std::max(1.2f, s.width * 0.85f);
        float dLat = std::fabs(latAccel) * m * hcg / track;
        float effF = FzF, effR = FzR;
        {
            float tf = std::min(dLat * rollFront, FzF * 0.5f * 0.95f), tr = std::min(dLat * (1.f - rollFront), FzR * 0.5f * 0.95f);
            effF = loadGrip(FzF * 0.5f + tf, FzF0 * 0.5f) + loadGrip(FzF * 0.5f - tf, FzF0 * 0.5f);
            effR = loadGrip(FzR * 0.5f + tr, FzR0 * 0.5f) + loadGrip(FzR * 0.5f - tr, FzR0 * 0.5f);
        }

        float vAbs = std::max(std::fabs(vF), 3.f);
        float alphaF = std::atan2(vR + yawRate * aFront, vAbs) - delta * (vF >= 0 ? 1.f : -1.f);
        float alphaR = std::atan2(vR - yawRate * bRear, vAbs);
        // tyre relaxation: slip builds up over a short rolling distance (calmer at low speed)
        float relax = std::min(1.f, (v + 2.f) * dt / 0.45f);
        alphaFs += (alphaF - alphaFs) * relax;
        alphaRs += (alphaR - alphaRs) * relax;
        alphaF = alphaFs;
        alphaR = alphaRs;
        float muF = mu, muR = mu * (1.f - 0.55f * handbrake);

        // drive and brakes
        float drive = 0;
        if (shiftTimer <= 0 || gear < 0) {
            float tq = engineTorque(s, rpm);
            if (rpm >= s.redline * 0.995f) tq *= 0.2f;  // limiter
            drive = tq * ratio * finalDrive * 0.9f / R * throttle * (gear < 0 ? -0.6f : 1.f);
            if (revving) drive = 0;
        }
        float driveF = 0, driveR = 0;
        if (s.drive == FWD) driveF = drive;
        else if (s.drive == RWD) driveR = drive;
        else { driveF = drive * 0.4f; driveR = drive * 0.6f; }
        if (assists) {
            // traction control: keep the driven tyres just under their grip limit,
            // and back off further when the tyres are already working hard sideways
            float latF = std::fabs(tyreCurve(alphaF)), latR = std::fabs(tyreCurve(alphaR));
            float capF = muF * effF * (0.92f - 0.45f * latF), capR = muR * effR * (0.92f - 0.45f * latR);
            float pre = std::fabs(driveF) + std::fabs(driveR);
            driveF = clampf(driveF, -capF, capF);
            if (handbrake < 0.1f) driveR = clampf(driveR, -capR, capR);
            if (pre > (std::fabs(driveF) + std::fabs(driveR)) * 1.05f && throttle > 0.2f) tcsLight = 1.f;
        }
        float dir = vF > 0.3f ? 1.f : (vF < -0.3f ? -1.f : 0.f);
        float brakeF = brake * m * G * 1.2f * 0.62f * dir;
        float brakeR = brake * m * G * 1.2f * 0.38f * dir + handbrake * m * G * 0.5f * dir;
        // engine braking when you lift
        if (gear > 0 && shiftTimer <= 0 && throttle < 0.05f && std::fabs(vF) > 2.f) {
            float eb = engineTorque(s, s.redline * 0.6f) * 0.16f * (rpm / s.redline) * ratio * finalDrive / R * dir;
            if (s.drive == FWD) brakeF += eb;
            else if (s.drive == RWD) brakeR += eb;
            else { brakeF += eb * 0.4f; brakeR += eb * 0.6f; }
        }
        float FxF = driveF - brakeF, FxR = driveR - brakeR;

        float maxF = muF * effF, maxR = muR * effR;
        if (assists && brake > 0.3f) {
            // ABS: stop just short of locking so the fronts can still steer
            if (std::fabs(FxF) > maxF * 0.88f) { FxF = clampf(FxF, -maxF * 0.88f, maxF * 0.88f); absLight = 1.f; }
            if (std::fabs(FxR) > maxR * 0.88f && handbrake < 0.1f) FxR = clampf(FxR, -maxR * 0.88f, maxR * 0.88f);
        }
        float spinF = 0, spinR = 0;
        if (std::fabs(FxF) > maxF) { spinF = std::fabs(FxF) / maxF - 1.f; FxF = clampf(FxF, -maxF, maxF); }
        if (std::fabs(FxR) > maxR) { spinR = std::fabs(FxR) / maxR - 1.f; FxR = clampf(FxR, -maxR, maxR); }
        float FyF = -maxF * tyreCurve(alphaF);
        float FyR = -maxR * tyreCurve(alphaR);
        float availF = std::sqrt(std::max(maxF * maxF - FxF * FxF, 0.f));
        float availR = std::sqrt(std::max(maxR * maxR - FxR * FxR * 0.85f, 0.f));
        FyF = clampf(FyF, -availF, availF);
        FyR = clampf(FyR, -availR, availR);

        // a sliding (locked or spinning) tyre has much less sideways grip
        if (spinF > 0.02f) FyF *= 0.55f;
        if (spinR > 0.02f) FyR *= 0.6f;
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
        // low-speed sideways scrub - but a car sliding sideways fast keeps sliding (tyre forces slow it)
        float slideK = 1.f - smoothstepf(4.f, 10.f, length(vec3(vel.x, 0, vel.z)));
        float latDamp = (1.f - kb) * slideK * std::min(1.f, 12.f * dt);
        vel -= rgt * (vR2 * latDamp);
        // mild stability assist
        yawRate *= 1.f - std::min(1.f, 0.6f * dt);
        // stop creeping when braking at standstill
        if (brake > 0.5f && length(vec3(vel.x, 0, vel.z)) < 0.6f && !reversing) { vel.x *= 0.8f; vel.z *= 0.8f; }
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
    // a touch more hang time while a stunt is spinning
    vel.y -= G * (stuntDur > 0 ? 0.8f : 1.f) * dt;
    vec3 np = pos + vel * dt;
    float hNew = w.heightAt(np.x, np.z) + groundBoost;
    if (np.y < hNew) {
        float follow = (hNew - hOld) / dt;
        if (!grounded && vel.y < follow - 4.f) impact = std::max(impact, follow - vel.y);
        np.y = hNew;
        vel.y = std::max(vel.y, clampf(follow, -40.f, 40.f));
    }
    float landSpeed = vel.y;  // vertical speed going into this step (for landing squash)
    pos = np;
    bool air = pos.y - hNew > 0.4f;
    airTime = air ? airTime + dt : 0.f;

    // ---- flight presentation
    bool flying = pos.y - hNew > 0.25f;
    if (flying) {
        // the nose follows the arc: up on the way up, dipping as it falls
        float hs = std::max(5.f, std::fabs(dot(vel, fwd)));
        float tgt = clampf(std::atan2(-vel.y, hs) * 0.8f, -0.75f, 0.85f);
        airPitchVel += ((tgt - airPitch) * 16.f - airPitchVel * 6.f) * dt;
        // air control: a little steering, and the nose drifts round to line up with where the car
        // is travelling so it lands pointing the right way
        float hsp = std::hypot(vel.x, vel.z);
        if (hsp > 6.f) {
            float want = wrapAngle(std::atan2(vel.x, vel.z) - yaw);
            float target = -want * 2.2f + steer * 0.9f;
            yawRate += (target - yawRate) * std::min(1.f, 3.f * dt);
        }
        yawRate = clampf(yawRate, -1.6f, 1.6f);
    } else {
        airPitchVel += ((0.f - airPitch) * 160.f - airPitchVel * 20.f) * dt;
    }
    airPitch += airPitchVel * dt;
    if (wasAir && !flying) {
        // touchdown: compress the springs in proportion to how hard we came down, a little speed scrubbed, and the yaw settles instead of spinning out
        float vi = -landSpeed;
        landVel = std::min(landVel, std::max(-4.5f, landSpeed * 0.22f));
        airPitchVel *= 0.3f;
        float scrub = clampf((vi - 6.f) * 0.008f, 0.f, 0.06f);
        vel.x *= 1.f - scrub; vel.z *= 1.f - scrub;
        yawRate *= 0.4f;
        alphaFs *= 0.5f; alphaRs *= 0.5f;
    }
    wasAir = flying;
    landVel += (-landSquash * 170.f - landVel * 12.f) * dt;
    landSquash = clampf(landSquash + landVel * dt, -0.22f, 0.12f);
    // stunt spin: eased so it starts and finishes gently and always lands wheels-down
    if (stuntDur > 0) {
        stuntT += dt * (flying || stuntT < 0.15f ? 1.f : 3.f);  // touched down early: wrap it up quickly
        float u = clampf(stuntT / stuntDur, 0.f, 1.f);
        float e = u - std::sin(2.f * PI * u) / (2.f * PI);
        stuntRoll = stuntRollTurns * 2.f * PI * e;
        stuntYaw = stuntYawTurns * 2.f * PI * e;
        if (u >= 1.f) { stuntDur = 0; stuntRoll = stuntYaw = 0; }
    }

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
    mat4 M = basis(left, u, f, pos);
    if (landSquash != 0.f) M = M * translate(vec3(0, landSquash, 0));
    if (airPitch != 0.f || stuntRoll != 0.f || stuntYaw != 0.f) {
        // rotate about the middle of the car, not its feet
        float hc = spec ? spec->height * 0.5f : 0.6f;
        M = M * translate(vec3(0, hc, 0)) * rotateY(stuntYaw) * rotateZ(stuntRoll) * rotateX(airPitch) * translate(vec3(0, -hc, 0));
    }
    return M;
}

mat4 Vehicle::cabinMatrix() const {
    vec3 f = forward();
    vec3 u = normalize(up);
    f = normalize(f - u * dot(f, u));
    return basis(cross(u, f), u, f, pos) * translate(vec3(0, 0.02f + landSquash, 0)) * rotateX(bodyPitch + airPitch * 0.5f) * rotateZ(bodyRoll);
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
