// Sources/Voice.cpp — Complete Hybrid Voice Engine
#include "Voice.h"
#include "Oscillators.h"
#include "Filter.h"
#include "PowerSag.h"
#include <vector>

Voice::Voice(double sr) : sampleRate(sr), invSampleRate(1/sr), filter1(), filter2(), sag(sr) {
    driftGen.seed(42);
    noiseGen.seed(12345);
}

void Voice::processBlock(float** outputs, int numSamples, const Params& p) {
    for (int s = 0; s < numSamples; ++s) {
        float sample = renderVoice(p);
        outputs[0][s] += sample * 0.5f;
        outputs[1][s] += sample * 0.5f;  // Mono to stereo
    }
}

float Voice::renderVoice(const Params& p) {
    // Phase advance
    advancePhases(p);
    
    // 1. VA Oscillators + Unison
    float vaSig = 0;
    int unisonN = std::clamp((int)p.unisonVoices, 1, 8);
    for (int i = 0; i < unisonN; ++i) {
        float detune = (i - unisonN/2.0f) * p.unisonDetune * 0.005f;
        double tempInc = noteFreq * detune * 2 * M_PI * invSampleRate;
        
        float osc1 = getOscillator(0, p) * p.osc1Level;
        float osc2 = getOscillator(1, p) * p.osc2Level;
        float sub = sin(subPhase) * p.subLevel;
        float nse = noiseGen(p);
        
        vaSig += (osc1 + osc2 + sub + nse) / unisonN;
    }
    
    // 2. SOGITEC 4X ADDITIVE LAYER
    float additive = digitalOsc(p);
    
    // 3. HYBRID BLEND
    float source = (1.0f - p.additiveBlend) * vaSig + p.additiveBlend * additive;
    
    // 4. DUAL FILTERS w/ DRIVE
    float f1 = filter1.process(source, p.filter1Cutoff, p.filter1Res, p.filter1Type, p.driveAmount, p.driveCurve);
    float f2 = filter2.process(source, p.filter2Cutoff, p.filter2Res, p.filter2Type, p.driveAmount, p.driveCurve);
    float filtered = p.serialFilters ? f2 : 0.5f * (f1 + f2);
    
    // 5. AMP ENVELOPE (full ADSR)
    float amp = updateAmpEnv(p);
    
    // 6. POWER SAG (final analog character)
    float final = sag.process(filtered * amp);
    
    return final;
}

float Voice::updateAmpEnv(const Params& p) {
    if (gate) {
        if (ampEnv.phase == 0) {  // ATTACK
            ampEnv.level += p.ampAttack * 0.02f;
            if (ampEnv.level >= 1.0f) {
                ampEnv.level = 1.0f;
                ampEnv.phase = 1;  // DECAY
            }
        } else if (ampEnv.phase == 1) {  // DECAY
            ampEnv.level *= (1.0f - p.ampDecay * 0.002f);
            if (ampEnv.level <= p.ampSustain) {
                ampEnv.level = p.ampSustain;
                ampEnv.phase = 2;  // SUSTAIN
            }
        }
    } else {
        ampEnv.phase = 3;  // RELEASE
        ampEnv.level *= (1.0f - p.ampRelease * 0.003f);
        if (ampEnv.level < 0.001f) ampEnv.level = 0;
    }
    return ampEnv.level;
}

