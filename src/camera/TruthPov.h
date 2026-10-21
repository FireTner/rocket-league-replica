// src/camera/TruthPov.h — RL-shaped output of one camera evaluation.
#pragma once

struct TruthPov {
    float focus[3];      // world point the camera looks at (uu)
    float rotation[3];   // UE rotator units: pitch, yaw, roll (65536 = 360 deg)
    float distance;      // uu behind focus
    float fov;           // degrees
    float time;          // seconds (state-local clock, RL keeps one)
};
