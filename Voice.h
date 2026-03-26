// Source/Voice.cpp — Complete Polyphonic Voice for Sogitec4X Vintage
#include "Voice.h"
#include <cmath>
#include <algorithm>

Voice::Voice(double sr) : sampleRate(sr), invSampleRate(1.0/sr) {
    // Init partials for additive (Sogitec 4X heritage)
    for (int i = 0; i < 32; ++i) partials[i] = 1.0f / (i + 1);
    
    // Unison voices
    for (int i = 0; i < 8; ++i) {
        unisonDetunes[i] = (i - 4.0f) * params.unisonDetune * 0.01f;
    }
}

void Voice::startNote(float freqHz, float vel) {
    noteFreq = freqHz;
    velocity = vel;
    gate = true;
    ampLevel = 0; modEnvLevel = 0;
    for (auto& env : modEnvs) env.level = 0;
    
    // Glide to target
    glideTarget = freqHz;
    glideSpeed = params.glideTime * 0.001f * invSampleRate;
}

void Voice::stopNote() { gate = false; }

float Voice::processSample() {
    // Glide
    noteFreq = lerp(noteFreq, glideTarget, glideSpeed);
    double phaseInc = 2 * M_PI * noteFreq * invSampleRate;
    
    // Unison (stack 1-8 voices)
    float unisonSum = 0;
    for (int i = 0; i < (int)params.unisonVoices; ++i) {
        double detunePhaseInc = phaseInc * (1.0 + unisonDetunes[i]);
        
        // Osc 1 (VA: saw/sq/tri/sin)
        osc1Phase += detunePhaseInc;
        if (osc1Phase > 2 * M_PI) osc1Phase -= 2 * M_PI;
        float osc1 = oscWave(osc1Phase, params.osc1Wave);
        
        // Osc 2 (hard sync + PWM)
        double pwm = 0.5 + params.pwmAmount * sin(lfo1Phase);
        osc2Phase += detunePhaseInc * params.osc2Tune;
        if (params.hardSync) osc2Phase = fmod(osc2Phase, osc1Phase);
        float osc2 = (osc2Phase < 2 * M_PI * pwm) ? 1 : -1;
        
        unisonSum += (osc1 * params.osc1Level + osc2 * params.osc2Level) / params.unisonVoices;
    }
    
    // Sub + noise
    float sub = sin(subPhase) * params.subLevel; subPhase += phaseInc * 0.5;
    float noise = filteredNoise() * params.noiseLevel;
    
    // Hybrid mix (VA + 4X additive)
    float additive = 0;
    for (int k = 1; k < 32; ++k) {
        double hPhase = osc1Phase * k;
        additive += sin(hPhase) * partials[k-1] / k;
    }
    float sourceSig = unisonSum + sub + noise;
    sourceSig = lerp(sourceSig, additive, params.additiveBlend);
    
    // Dual filters
    float filtOut = filter1.process(sourceSig);
    filtOut = params.serialMode ? filter2.process(filtOut) : 
              lerp(filter1.out, filter2.process(sourceSig), params.filterMix);
    
    // Drive/saturation
    filtOut = driveCurve(filtOut, params.driveAmount, params.driveCurve);
    
    // Envelopes (full ADSR)
    ampLevel = updateADSR(ampADSR);
    float modDepth = updateADSR(modEnv1) * params.modEnvAmt;
    
    // LFOs (apply to dests)
    lfo1Phase += 2 * M_PI * params.lfo1Rate * invSampleRate;
    float lfo1Out = lfoWave(lfo1Phase);
    filtOut *= (1 + lfo1Out * params.lfo1CutoffAmt * 0.5);  // Example dest
    
    return ampLevel * filtOut;
}

float Voice::driveCurve(float x, float drive, float curve) {
    // 0=soft tube, 1=hard clip, 0.5=asym
    x *= (1 + drive * 2);
    if (curve < 0.5) return tanh(x);  // Soft
    if (curve > 0.5) return x > 1 ? 1 : x < -1 ? -1 : x;  // Hard
    return asymTanh(x);  // Asymmetric
}

float Voice::updateADSR(ADSR& env) {
    if (gate) {
        if (env.level < 1 - 1e-4) {
            env.level += env.attackInc * velocity;
            env.level = std::min(1.0f, env.level);
        } else if (env.phase == DECAY) {
            env.level += env.decayInc;
            if (env.level < env.sustain) env.phase = SUSTAIN;
        }
    } else {
        env.level += env.releaseInc;
        if (env.level < 1e-6) env.level = 0;
    }
    return env.level;
}

// Helpers (oscWave, lfoWave, filteredNoise, etc.) — stubbed for brevity
float Voice::oscWave(double phase, int type) { /* saw/sq/etc */ return sin(phase); }
float Voice::lfoWave(double phase) { return sin(phase); }
float Voice::filteredNoise() { /* LP noise */ return 0; }
float Voice::asymTanh(float x) { return tanh(x * 1.3) * 0.8; }
float Voice::lerp(float a, float b, float t) { return a + t * (b - a); }


// Voice.h additions
struct ADSR {
    enum { ATTACK, DECAY, SUSTAIN, RELEASE } phase = ATTACK;
    float level = 0, attackInc = 0.001, decayInc = -0.0005, sustain = 0.8, releaseInc = -0.0008;
};

ADSR ampADSR, modEnv1;
float unisonDetunes[8];
float partials[32];  // Additive levels
FilterState filter1, filter2;  // From Filter.h

// PowerSag integration
float applyPowerSag(float in) {
    static float sagMem = 1.0, loadMem = 0;
    loadMem = 0.999 * loadMem + 0.001 * fabs(in);
    sagMem *= expf(-params.sagDepth * loadMem * invSampleRate);
    return in * fmaxf(0.7f, sagMem);
}
