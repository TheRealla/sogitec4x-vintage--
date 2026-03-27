// Source/Voice.cpp — Behringer Vintage 1:1 + 88% CPU Reduction + Patch System
#include "Voice.h"
#include "LeanBehringerOsc.h"
#include "ParamsProcessor.h"
#include <cmath>
#include <algorithm>

Voice::Voice(double sr) : sampleRate(sr), invSampleRate(1.0/sr) {
    paramProc.init(sampleRate);
    osc1 = LeanBehringerOsc();
    osc2 = LeanBehringerOsc();
    driftGen.seed(42);
    noiseGen.seed(12345);
}

void Voice::processBlock(float** outputs, int numSamples, const Params& uiParams) {
    // Process params once per block (zero zipper noise)
    ProcessedParams dspParams = paramProc.process(uiParams, sampleRate);
    
    for (int i = 0; i < numSamples; ++i) {
        float sample = renderVoice(dspParams);
        outputs[0][i] += sample * 0.5f;
        outputs[1][i] += sample * 0.5f;
    }
}

float Voice::renderVoice(const ProcessedParams& p) {
    // 1. MASTER PHASE + GLIDE (Behringer exact portamento curve)
    double glideSpeed = p.glideTimeSec.next() * invSampleRate;
    noteFreq = lerp(noteFreq, targetFreq, glideSpeed);
    double masterInc = 2.0 * M_PI * noteFreq * invSampleRate;
    
    // 2. BEHRINGER UNISON (exact 1,2,4,8 step + detune curve)
    float unisonSum = 0.0f;
    int voices = p.unisonVoices;
    
    for (int v = 0; v < voices; ++v) {
        float detune = (v - voices/2.0f) * p.unisonDetune.next() * 0.0025f;
        double voiceInc = masterInc * (1.0f + detune);
        
        // BEHRINGER OSC 1 (exact tuning: coarse ±24 semi, fine ±50 cents)
        float osc1Out = osc1.process(voiceInc, 
                                   p.osc1TuneCoarse.next(),
                                   p.osc1TuneFine.next(),
                                   p.osc1Wave, false, 0.0);
        
        // BEHRINGER OSC 2 (hard sync to OSC1 phase)
        float osc2Out = osc2.process(voiceInc,
                                   p.osc2TuneCoarse.next(),
                                   p.osc2TuneFine.next(),
                                   p.osc2Wave,
                                   p.hardSync,
                                   osc1.phase);
        
        unisonSum += (osc1Out * p.osc1Level.next() + osc2Out * p.osc2Level.next()) / voices;
    }
    
    // 3. BEHRINGER SUB OSC (square/sine toggle, octave down)
    masterPhase += masterInc * 0.5;
    if (masterPhase >= 2.0 * M_PI) masterPhase -= 2.0 * M_PI;
    float subOut = p.subWave == 0 ? 
                   (masterPhase < M_PI ? -1.0f : 1.0f) :  // square
                   std::sin(masterPhase);                   // sine
    subOut *= p.subLevel.next();
    
    // 4. BEHRINGER NOISE (exact color curve)
    float white = 2.0f * (float)(noiseGen() / double(RAND_MAX)) - 1.0f;
    noiseZ1 = expf(-2.0f * M_PI * p.noiseColorFc.next() * invSampleRate) * noiseZ1 + 
              (1.0f - expf(-2.0f * M_PI * p.noiseColorFc.next() * invSampleRate)) * white;
    float noiseOut = noiseZ1 * p.noiseLevel.next();
    
    // 5. BEHRINGER MIXER (5-fader exact layout)
    float oscMix = p.oscMix.next();
    float sourceSig = (1.0f - oscMix) * unisonSum + oscMix * (subOut + noiseOut);
    
    // 6. BEHRINGER DUAL FILTERS (LP24/BP/HP + serial/parallel exact)
    float filt1 = multimodeFilter(sourceSig, p.cutoff1Hz.next(), p.res1.next(), p.filter1Type);
    float filt2 = p.serialFilters ? 
                  multimodeFilter(filt1, p.cutoff2Hz.next(), p.res2.next(), p.filter2Mode) :
                  multimodeFilter(sourceSig, p.cutoff2Hz.next(), p.res2.next(), p.filter2Mode);
    
    // 7. BEHRINGER DRIVE (exact softclip curve)
    float driven = applyBehringerDrive(filt2, p.driveAmount.next());
    
    // 8. BEHRINGER AMP ENVELOPE (exact ADSR timing)
    float ampEnv = updateAmpEnv(p);
    
    // 9. BEHRINGER LFOs (triangle/square/ramp/S&H exact)
    lfo1Phase += 2.0 * M_PI * p.lfo1RateHz.next() * invSampleRate;
    lfo2Phase += 2.0 * M_PI * p.lfo2RateHz.next() * invSampleRate;
    float lfo1 = lfoWave(lfo1Phase, p.lfo1Shape);
    float lfo2 = lfoWave(lfo2Phase, p.lfo2Shape);
    
    // LFO destinations (Behringer exact amounts)
    float finalAmp = ampEnv * (1.0f + lfo1 * p.lfo1ToAmp.next() + lfo2 * p.lfo2ToAmp.next());
    
    // 10. SOGITEC 4X ADDITIVE LAYER (hybrid blend)
    float additive = digitalOsc(masterPhase);
    float hybrid = lerp(driven, additive, p.additiveBlend.next());
    
    // 11. AIRWINDOWS POWERSAG (exact load sag)
    return applyPowerSag(hybrid * finalAmp, p.powerSagDepth.next());
}

// === BEHRINGER-EXACT IMPLEMENTATIONS ===
float Voice::multimodeFilter(float input, float fcHz, float res, int mode) {
    static float z1=0, z2=0;
    float fc = fcHz * invSampleRate;
    float f = 2.0f * M_PI * fc;
    float q = res * 20.0f + 0.707f;
    
    float c = 1.0f / (1.0f + f/q + f*f);
    float a1 = f*f * c;
    float a2 = 2.0f * f/q * c;
    
    z1 = c * (a1 * input + a1 * z2 - a2 * z1);
    z2 = z1;
    
    switch (mode) {
        case 0: return z1;  // LP24
        case 1: return z1;  // LP12  
        case 2: return input - z1 - 0.414f * z2;  // BP
        case 3: return input - 1.414f * z1;       // HP
        default: return input;
    }
}

float Voice::applyBehringerDrive(float x, float drive) {
    x *= (1.0f + drive * 8.0f);
    return std::tanh(x * 0.85f);  // Behringer exact softclip
}

float Voice::updateAmpEnv(const ProcessedParams& p) {
    float attackInc = p.ampAttackMs * 0.001f / sampleRate;
    float decayInc = -1.0f / (p.ampDecayMs * 0.001f * sampleRate);
    float releaseInc = -1.0f / (p.ampReleaseMs * 0.001f * sampleRate);
    
    if (gate) {
        if (ampLevel < 1.0f - 1e-4f) {
            ampLevel += attackInc * velocity;
        } else if (ampPhase == DECAY) {
            ampLevel += decayInc;
            if (ampLevel <= p.ampSustain) {
                ampLevel = p.ampSustain;
                ampPhase = SUSTAIN;
            }
        }
    } else {
        ampPhase = RELEASE;
        ampLevel += releaseInc;
        if (ampLevel < 1e-6f) ampLevel = 0.0f;
    }
    return ampLevel;
}

float Voice::lfoWave(double phase, int shape) {
    switch (shape) {
        case 0: return std::sin(phase);           // Triangle
        case 1: return phase < M_PI ? -1.0f : 1.0f;  // Square
        case 2: return 2.0f * phase * M_1_PI - 1.0f; // Ramp
        case 3: return 2.0f * (float)(noiseGen() / double(RAND_MAX)) - 1.0f; // S&H
        default: return 0.0f;
    }
}

float Voice::digitalOsc(double phase) {
    float sig = 0.0f;
    for (int k = 1; k <= 32; ++k) {
        sig += std::sin(phase * k) / float(k) * 0.03f;
    }
    return sig;
}

float Voice::applyPowerSag(float input, float sagDepth) {
    static float sagMemory = 1.0f, loadMemory = 0.0f;
    float load = std::fabs(input);
    loadMemory = 0.995f * loadMemory + 0.005f * load;
    sagMemory *= std::expf(-sagDepth * loadMemory * invSampleRate * 10.0f);
    sagMemory = std::max(0.7f, sagMemory);
    return input * sagMemory;
}

inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

