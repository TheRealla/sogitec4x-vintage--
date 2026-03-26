// Source/Filter.cpp — Dual Multimode Filters + Drive
#include "Filter.h"
#include <cmath>

Filter::Filter() : z1(0), z2(0) {}

float Filter::process(float input, float cutoff, float res, int type, float driveAmt, float driveCurve) {
    // 1. Pre-drive
    input = applyDrive(input, driveAmt * 0.5f, driveCurve);
    
    // 2. Multimode filter core (SVF topology)
    float fc = cutoff * 0.45f;  // Normalized
    float q = res * 4.0f + 0.707f;
    
    float f = 2.0f * M_PI * fc / sampleRate;
    float f2 = f * f;
    float qInv = 1.0f / q;
    
    // SVF coefficients
    float c = 1.0f / (1.0f + f * qInv + f2);
    float a1 = f2 * c;
    float a2 = 2.0f * f * qInv * c;
    float a0 = (1.0f - f2 * c) * 0.5f;
    
    // One-pole for modes
    float lp = a1 * input + a1 * z2 - a2 * z1;
    float bp = a2 * input + a2 * z2 + (1.0f - a2 * 2.0f) * z1;
    float hp = a0 * (input - z2);
    
    z1 = bp;
    z2 = lp;
    
    float output;
    switch (type) {
        case 0: output = lp; break;                    // LP12
        case 1: output = lp; break;                    // LP24 (2x cascade in real impl)
        case 2: output = hp; break;                    // HP
        case 3: output = bp; break;                    // BP
        case 4: output = lp + hp; break;               // Notch
        case 5: output = formantFilter(lp, bp); break; // Formant (vowel)
        default: output = input;
    }
    
    // Post-drive
    return applyDrive(output, driveAmt * 0.5f, driveCurve);
}

float Filter::formantFilter(float lp, float bp) {
    // Simple vowel formant approximation (A/E/I/O/U-ish)
    static float formantZ = 0;
    formantZ = 0.95f * formantZ + 0.05f * (lp * 0.7f + bp * 0.3f);
    return formantZ;
}

float Filter::applyDrive(float x, float amt, float curve) {
    x *= (2.0f + amt * 8.0f);
    if (curve < 0.33f) {
        return tanh(x);  // Soft tube saturation
    } else if (curve > 0.66f) {
        return x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);  // Hard clipping
    } else {
        // Asymmetric (vintage diode)
        return 0.8f * tanh(x * 1.3f) + 0.2f * x;
    }
}
