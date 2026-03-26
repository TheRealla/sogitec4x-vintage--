// Effects/PowerSag.cpp — Airwindows-style PSU Simulation
#include "PowerSag.h"
#include <cmath>

PowerSag::PowerSag(double sr) : sampleRate(sr) {}

float PowerSag::process(float input) {
    // 1. Rectified load detection
    float absIn = fabs(input);
    loadMemory = 0.993f * loadMemory + 0.007f * absIn;
    
    // 2. Voltage sag (slow attack, natural recovery)
    float sagAttack = 0.0008f * params.powerSagDepth * loadMemory / sampleRate;
    sagMemory *= expf(-sagAttack);
    sagMemory = std::max(0.65f, sagMemory);
    
    // 3. Subtle breathing (LF modulation under heavy load)
    breathPhase += 2 * M_PI * 0.15f / sampleRate;  // ~0.15Hz
    float breath = 1.0f + 0.03f * sin(breathPhase) * loadMemory;
    
    return input * sagMemory * breath;
}
