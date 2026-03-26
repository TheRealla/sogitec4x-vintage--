// Source/Voice.cpp — Complete Hybrid Engine Implementation
#include "Voice.h"
#include <cmath>
#include <algorithm>
#include <random>

Voice::Voice(double sr) : sampleRate(sr), invSampleRate(1.0/sr) {
    // Init 32-partial additive bank (Sogitec 4X heritage)
    for (int i = 0; i < 32; ++i) {
        partials[i] = 1.0f / (i + 1.0f);
    }
    
    // Unison detunes (pre-calculated)
    for (int i = 0; i < 8; ++i) {
        unisonDetunes[i] = (i - 4.0f) * 0.02f;
    }
    
    // Noise generator
    noiseRng.seed(42);
}

void Voice::startNote(float freqHz, float vel) {
    noteFreq = glideTarget = freqHz;
    velocity = vel;
    gate = true;
    
    // Reset envelopes
    ampADSR.phase = ADSR::ATTACK;
    ampADSR.level = 0;
    modEnv.phase = ADSR::ATTACK;
    modEnv.level = 0;
    
    // Phase reset option
    if (params.phaseReset) {
        osc1Phase = osc2Phase = subPhase = 0;
    }
}

void Voice::stopNote() {
    gate = false;
}

float Voice::processSample(const Params& p) {
    params = p;  // Update params
    
    // 1. GLIDE
    noteFreq = lerp(noteFreq, glideTarget, params.glideTime * 0.001f);
    double phaseInc = 2 * M_PI * noteFreq * invSampleRate;
    
    // 2. UNISON OSCILLATORS (Behringer Vintage style)
    float unisonSum = 0;
    int numVoices = std::min(8, (int)(params.unisonVoices + 0.5f));
    
    for (int v = 0; v < numVoices; ++v) {
        double detune = unisonDetunes[v] * params.unisonDetune;
        double vPhaseInc = phaseInc * (1.0 + detune);
        
        // OSC 1: Multi-waveform with analog drift
        osc1Phase += vPhaseInc * (1.0 + params.driftAmount * 0.01f * sin(osc1Phase * 0.01));
        if (osc1Phase > 2 * M_PI) osc1Phase -= 2 * M_PI;
        float osc1 = getOscWave(osc1Phase, params.osc1Wave);
        
        // OSC 2: Hard sync + PWM
        double pwmWidth = 0.5 + params.pwmAmount * 0.4 * sin(lfo1Phase);
        osc2Phase += vPhaseInc * params.osc2Tune;
        if (params.hardSync) osc2Phase = fmod(osc2Phase, osc1Phase);
        float osc2 = (osc2Phase < 2 * M_PI * pwmWidth) ? 1.0f : -1.0f;
        
        unisonSum += (osc1 * params.osc1Level + osc2 * params.osc2Level) / numVoices;
    }
    
    // 3. SUB OSC + NOISE
    subPhase += phaseInc * 0.5;
    float sub = sin(subPhase) * params.subLevel;
    
    float noise = getColoredNoise();
    
    // 4. HYBRID MIX (VA + SOGITEC ADDITIVE)
    float vaSignal = unisonSum + sub + noise * params.noiseLevel;
    
    float additive = 0;
    for (int k = 1; k < 32; ++k) {
        double harmonicPhase = osc1Phase * k;
        additive += sin(harmonicPhase) * partials[k-1] / float(k);
    }
    
    float sourceSig = lerp(vaSignal, additive, params.additiveBlend);
    
    // 5. DUAL FILTERS
    float filt1 = multimodeFilter(sourceSig, params.filter1Cutoff, params.filter1Res, params.filter1Type);
    float filt2 = multimodeFilter(sourceSig, params.filter2Cutoff, params.filter2Res, params.filter2Type);
    
    float filtered = params.serialFilters ? multimodeFilter(filt1, params.filter2Cutoff, params.filter2Res, params.filter2Type) :
                                        lerp(filt1, filt2, 0.5f);
    
    // 6. DRIVE/SATURATION
    filtered = applyDrive(filtered, params.driveAmount, params.driveCurve);
    
    // 7. ENVELOPES
    float ampEnv = updateADSR(ampADSR, params.ampAttack, params.ampDecay, params.ampSustain);
    float modEnv = updateADSR(modEnv, params.modAttack, params.modDecay, params.modSustain);
    
    // 8. LFO MODULATION (simple destinations)
    lfo1Phase += 2 * M_PI * params.lfo1Rate * invSampleRate;
    float lfo1 = sin(lfo1Phase);
    filtered *= (1.0f + lfo1 * params.lfo1Amount * 0.3f);
    
    // 9. FINAL OUTPUT + POWER SAG
    float finalOut = ampEnv * filtered;
    return applyPowerSag(finalOut);
}

// === HELPER FUNCTIONS ===
float Voice::getOscWave(double phase, float waveType) {
    switch ((int)waveType) {
        case 0: return sin(phase);                    // Sine
        case 1: return phase - M_PI;                  // Saw
        case 2: return phase < M_PI ? -1 : 1;         // Square
        case 3: return 2 * fabs(phase/M_PI - floor(phase/(2*M_PI) + 0.5)) - 1; // Triangle
        default: return 0;
    }
}

float Voice::getColoredNoise() {
    float white = 2.0f * (noiseRng() / float(RAND_MAX)) - 1.0f;
    noiseZ1 = exp(-2 * M_PI * params.noiseColor * 0.1 * invSampleRate) * noiseZ1 + 
              (1 - exp(-2 * M_PI * params.noiseColor * 0.1 * invSampleRate)) * white;
    return noiseZ1;
}

float Voice::multimodeFilter(float input, float cutoff, float res, int type) {
    static float z1 = 0, z2 = 0;
    float fc = cutoff * 0.4;
    float q = res * 4;
    
    float b = 2 * M_PI * fc * invSampleRate;
    float c = 1.0f / (1.0f + res * b + b * b);
    
    float out;
    switch (type) {
        case 0: case 1: // LP12/LP24
            z1 = c * (b * b * input + b * b * z2 - res * b * z1);
            z2 = z1;
            out = z1;
            break;
        case 2: // HP
            z1 = c * ((-b * b * input - b * b * z2) + (1 + res * b) * z1);
            out = z1;
            break;
        case 3: // BP
            out = b * (z1 - z2);
            z1 = c * (b * b * input + b * b * z2 - (1 + res * b) * z1);
            z2 = z1;
            break;
        default:
            out = input;
    }
    return out;
}

float Voice::applyDrive(float x, float drive, float curve) {
    x *= (1 + drive * 3);
    if (curve < 0.33f) return tanh(x);           // Soft tube
    if (curve > 0.66f) return x > 1 ? 1 : (x < -1 ? -1 : x); // Hard clip
    return tanh(x * 1.4f) * 0.85f;               // Asymmetric
}

float Voice::updateADSR(ADSR& env, float attack, float decay, float sustain) {
    if (gate) {
        if (env.phase == ADSR::ATTACK) {
            env.level += attack * 0.02f;
            if (env.level >= 1.0f) {
                env.level = 1.0f;
                env.phase = ADSR::DECAY;
            }
        } else if (env.phase == ADSR::DECAY) {
            env.level += decay * -0.005f;
            if (env.level <= sustain) {
                env.level = sustain;
                env.phase = ADSR::SUSTAIN;
            }
        }
    } else {
        env.phase = ADSR::RELEASE;
        env.level *= 0.998f;  // Release
        if (env.level < 0.001f) env.level = 0;
    }
    return env.level;
}

float Voice::applyPowerSag(float input) {
    static float sagMemory = 1.0f, loadMemory = 0;
    float load = fabs(input);
    loadMemory = 0.995f * loadMemory + 0.005f * load;
    sagMemory *= expf(-params.powerSagDepth * loadMemory * invSampleRate * 10);
    sagMemory = std::max(0.7f, sagMemory);
    return input * sagMemory;
}

float Voice::lerp(float a, float b, float t) {
    return a + t * (b - a);
}
// Add to Voice.h
std::mt19937 noiseRng;
float noiseZ1 = 0;
float unisonDetunes[8];
float partials[32];
double lfo1Phase = 0;
ADSR ampADSR, modEnv;

// In your Voice.cpp process loop:
Filter filt1, filt2;
float sourceSig = hybridOscMix();  // From previous code
float filtOut1 = filt1.process(sourceSig, params.filter1Cutoff, params.filter1Res, params.filter1Type, params.driveAmount, params.driveCurve);
float filtOut2 = filt2.process(sourceSig, params.filter2Cutoff, params.filter2Res, params.filter2Type, params.driveAmount, params.driveCurve);
float filtered = params.serialFilters ? filtOut2 : 0.5f * (filtOut1 + filtOut2);
filtered = powerSag.process(filtered);  // Final analog character
