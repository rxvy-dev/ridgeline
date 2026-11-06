// Arcade-sim car physics: bicycle tyre model, gearbox, terrain following, jumps.
#pragma once
#include "cars.h"
#include "world.h"

struct Controls {
    float steer = 0;     // -1 left .. +1 right
    float throttle = 0;  // 0..1
    float brake = 0;     // 0..1 (also reverse when slow)
    float handbrake = 0; // 0..1
};

struct Vehicle {
    const CarSpec* spec = nullptr;
    vec3 pos, vel;
    float yaw = 0;        // heading; forward = (sin yaw, 0, cos yaw)
    float yawRate = 0;    // + = turning right
    bool grounded = true;
    float airTime = 0;
    float steer = 0, throttle = 0, brake = 0, handbrake = 0;
    int gear = 1;         // -1 reverse, 1..6
    float rpm = 900;
    float wheelSpin = 0, wheelSteer = 0;
    float slipAngle = 0;  // body slip (velocity vs heading)
    float tyreSlip = 0;   // 0..1 for audio/smoke
    float bodyPitch = 0, bodyRoll = 0;
    float longAccel = 0, latAccel = 0;
    vec3 up = vec3(0, 1, 0);
    int surface = SURF_ASPHALT;
    float impact = 0;     // last collision strength (m/s), decays
    float waterTime = 0;
    bool reversing = false;
    float shiftTimer = 0;
    bool assists = true;  // traction control + stability assist

    // derived constants
    float finalDrive = 3.5f, dragK = 0.4f, rollRes = 200.f, inertia = 2500.f, aFront = 1.3f, bRear = 1.3f;

    void init(const CarSpec* s, vec3 p, float heading);
    void update(float dt, const World& w, const Controls& in);
    float speed() const { return std::sqrt(vel.x * vel.x + vel.z * vel.z); }
    float speedKmh() const { return speed() * 3.6f; }
    vec3 forward() const { return vec3(std::sin(yaw), 0, std::cos(yaw)); }
    vec3 right() const { return vec3(-std::cos(yaw), 0, std::sin(yaw)); }
    float forwardSpeed() const { return dot(vel, forward()); }
    mat4 chassisMatrix() const;  // ground-aligned frame
    mat4 bodyMatrix() const;     // + roll/pitch
    mat4 wheelMatrix(const CarModel& m, int i, bool spin = true) const;
    void placeAt(vec3 p, float heading, const World& w);
};

// Pushes two cars apart. Returns impact speed.
float collideCars(Vehicle& a, Vehicle& b);
