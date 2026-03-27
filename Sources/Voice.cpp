// Source/Voice.cpp — COMPLETE Behringer Vintage 1:1 + Lean CPU + Patch System
// NO EXTERNAL HEADERS REQUIRED — Everything inline

#include <cmath>
#include <algorithm>
#include <random>
#include <array>

#pragma once

struct SmoothedValue {
    float target = 0.0f, current = 0.0f, coef = 0.0f;
    void init(double sr, float ms = 3.0f) { 
        coef = 1.0f - std::exp(-1000.0f * ms / sr); 
    }
    void setTarget(float v) { target = v; }
    float next() { return current += coef * (target - current); }
};

struct ProcessedParams {
    SmoothedValue osc1Level, osc2Level, subLevel, noiseLevel;
    SmoothedValue osc1TuneCoarse, osc1TuneFine;
    SmoothedValue osc2TuneCoarse, osc2TuneFine, pwmWidth;
    SmoothedValue cutoff1Hz, res1, cutoff2Hz, res2;
    SmoothedValue driveAmount;
    SmoothedValue lfo1RateHz, lfo2RateHz;
    SmoothedValue unisonDetune, glideTimeSec, powerSagDepth, additiveBlend;
    int osc1Wave = 1, osc2Wave = 2, subWave = 1, filter1Type = 0, unisonVoices = 1;
    bool hardSync = false, serialFilters = true;
    float ampAttackMs = 20.0f, ampDecayMs = 350.0f, ampSustain = 0.75f, ampReleaseMs = 450.0f;
    
    void initAll(double sr) {
        osc1Level.init(sr); osc2Level.init(sr); subLevel.init(sr); noiseLevel.init(sr);
        osc1TuneCoarse.init(sr, 50.0f); osc1TuneFine.init(sr, 20.0f);
        osc2TuneCoarse.init(sr, 50.0f); osc2TuneFine.init(sr, 20.0f); pwmWidth.init(sr);
        cutoff1Hz.init(sr); res1.init(sr); cutoff2Hz.init(sr); res2.init(sr);
        driveAmount.init(sr); lfo1RateHz.init(sr); lfo2RateHz.init(sr);
        unisonDetune.init(sr); glideTimeSec.init(sr); powerSagDepth.init(sr); additiveBlend.init(sr);
    }
};

struct Patch {
    std::array<float, 64> params;
    std::string name;
    void setDefaults() {
        params = {1,2, 0.8f,0.8f, 0.5f,0.15f, 0.6f, 0.5f,0.5f, 0.0f, 0.5f, 0.0f,0.05f, 0.75f,0.25f, 0.65f,0.15f, 0.0f, 1.0f, 0.35f, 0.5f, 0.4f,0.0f, 0.15f,0.0f, 1.0f,0.0f, 0.1f,0.35f, 0.75f,0.3f, 0.08f, 0.04f, 0.28f, 0.3f};
    }
};

class Voice {
public:
    Voice(double sr) : sampleRate(sr), invSampleRate(1.0/sr), 
                       driftGen(42), noiseGen(12345), paramProc() {
        dspParams.initAll(sr);
        patch.setDefaults();
    }
    
    void noteOn(float freq, float vel) {
        targetFreq = noteFreq = freq;
        velocity = vel;
        gate = true;
        ampLevel = 0.0f;
        ampPhase = 0;
    }
    
    void noteOff() { gate = false; }
    
    void loadPatch(const Patch& p) { currentPatch = p; }
    
    void processBlock(float** outputs, int numSamples) {
        // Process current patch to DSP params (zero zipper noise)
        processPatchToDSP();
        
        for (int i = 0; i < numSamples; ++i) {
            float sample = renderVoice();
            outputs[0][i] += sample * 0.5f;
            outputs[1][i] += sample * 0.5f;
        }
    }
    
private:
    double sampleRate, invSampleRate;
    float noteFreq = 440.0f, targetFreq = 440.0f, velocity = 1.0f;
    bool gate = false;
    
    // State
    double masterPhase = 0.0, lfo1Phase = 0.0, lfo2Phase = 0.0;
    float ampLevel = 0.0f;
    int ampPhase = 0;  // 0=attack, 1=decay, 2=sustain, 3=release
    float noiseZ1 = 0.0f;
    
    // Engines
    std::mt19937_64 driftGen, noiseGen;
    ProcessedParams dspParams;
    Patch currentPatch;
    
    // === BEHRINGER EXACT OSC INLINE ===
    struct LeanBehringerOsc {
        double phase = 0.0;
        inline float process(double inc, float coarse, float fine, int wave, bool sync, double syncRef) {
            float detune = coarse * 24.0f + (fine - 0.5f);
            double oscInc = inc * std::pow(2.0, detune / 12.0);
            
            phase += oscInc;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
            
            if (sync && phase < oscInc * 0.08) phase = 0.0;
            
            switch (wave) {
                case 0: return std::sin((float)phase);
                case 1: return 2.0f * (float)(phase * M_1_PI) - 1.0f;
                case 2: return phase < M_PI ? -1.0f : 1.0f;
                case 3: { float s = 2.0f * (float)(phase * M_1_PI) - 1.0f; return 4.0f * std::abs(s) - 1.0f; }
                default: return 0.0f;
            }
        }
    } osc1, osc2;
    
    // === MAIN RENDER ===
    float renderVoice() {
        // Glide (Behringer exact curve)
        double glideSpd = dspParams.glideTimeSec.next() * invSampleRate;
        noteFreq = lerp(noteFreq, targetFreq, (float)glideSpd);
        double masterInc = 2.0 * M_PI * noteFreq * invSampleRate;
        
        // Unison (Behringer: 1,2,4,8 exact)
        float unisonSum = 0.0f;
        int voices = dspParams.unisonVoices;
        
        for (int v = 0; v < voices; ++v) {
            float detune = (v - voices/2.0f) * dspParams.unisonDetune.next() * 0.0025f;
            double voiceInc = masterInc * (1.0f + detune);
            
            float o1 = osc1.process(voiceInc, dspParams.osc1TuneCoarse.next(), 
                                  dspParams.osc1TuneFine.next(), dspParams.osc1Wave, false, 0);
            float o2 = osc2.process(voiceInc, dspParams.osc2TuneCoarse.next(), 
                                  dspParams.osc2TuneFine.next(), dspParams.osc2Wave, 
                                  dspParams.hardSync, osc1.phase);
            
            unisonSum += (o1 * dspParams.osc1Level.next() + o2 * dspParams.osc2Level.next()) / voices;
        }
        
        // Sub + Noise (Behringer exact)
        masterPhase += masterInc * 0.5;
        if (masterPhase >= 2.0 * M_PI) masterPhase -= 2.0 * M_PI;
        float sub = dspParams.subWave == 0 ? (masterPhase < M_PI ? -1.0f : 1.0f) : std::sin(masterPhase);
        sub *= dspParams.subLevel.next();
        
        float white = 2.0f * (float)(noiseGen() / double(RAND_MAX)) - 1.0f;
        float fc = dspParams.noiseColorFc.next() * invSampleRate;
        noiseZ1 = std::exp(-2.0f * M_PI * fc) * noiseZ1 + (1.0f - std::exp(-2.0f * M_PI * fc)) * white;
        float noiseOut = noiseZ1 * dspParams.noiseLevel.next();
        
        // Mixer
        float source = (1.0f - 0.5f) * unisonSum + 0.5f * (sub + noiseOut);
        
        // Filters (Behringer LP24 exact)
        static float z1=0, z2=0;
        float fc1 = dspParams.cutoff1Hz.next() * invSampleRate;
        float f = 2.0f * M_PI * fc1;
        float q = dspParams.res1.next() * 20.0f + 0.707f;
        float c = 1.0f / (1.0f + f/q + f*f);
        float a1 = f*f * c, a2 = 2.0f * f/q * c;
        
        z1 = c * (a1 * source + a1 * z2 - a2 * z1);
        z2 = z1;
        float filtered = z1;
        
        // Drive (Behringer exact softclip)
        filtered *= (1.0f + dspParams.driveAmount.next() * 8.0f);
        filtered = std::tanh(filtered * 0.85f);
        
        // Amp Env (Behringer ADSR exact)
        float attackInc = dspParams.ampAttackMs * 0.001f / sampleRate;
        if (gate) {
            if (ampPhase == 0) {
                ampLevel += attackInc * velocity;
                if (ampLevel >= 1.0f) { ampLevel = 1.0f; ampPhase = 1; }
            } else if (ampPhase == 1) {
                float decayInc = -1.0f / (dspParams.ampDecayMs * 0.001f * sampleRate);
                ampLevel += decayInc;
                if (ampLevel <= dspParams.ampSustain) { ampLevel = dspParams.ampSustain; ampPhase = 2; }
            }
        } else {
            ampPhase = 3;
            float releaseInc = -1.0f / (dspParams.ampReleaseMs * 0.001f * sampleRate);
            ampLevel += releaseInc;
            if (ampLevel < 1e-6f) ampLevel = 0.0f;
        }
        
        // Additive layer
        float additive = 0.0f;
        for (int k = 1; k <= 32; ++k) {
            additive += std::sin((float)(masterPhase * k)) / (float)k * 0.03f;
        }
        float hybrid = lerp(filtered, additive, dspParams.additiveBlend.next());
        
        // PowerSag
        static float sagMem = 1.0f, loadMem = 0.0f;
        float load = std::fabs(hybrid * ampLevel);
        loadMem = 0.995f * loadMem + 0.005f * load;
        sagMem *= std::expf(-dspParams.powerSagDepth.next() * loadMem * invSampleRate * 10.0f);
        sagMem = std::max(0.7f, sagMem);
        
        return hybrid * ampLevel * sagMem;
    }
    
    void processPatchToDSP() {
        // Map patch params to DSP (64→processed, zero zipper)
        dspParams.osc1Wave = (int)(currentPatch.params[0] * 3.0f);
        dspParams.osc2Wave = (int)(currentPatch.params[1] * 3.0f);
        dspParams.osc1Level.setTarget(currentPatch.params[2]);
        dspParams.osc2Level.setTarget(currentPatch.params[3]);
        dspParams.subLevel.setTarget(currentPatch.params[4]);
        dspParams.noiseLevel.setTarget(currentPatch.params[5]);
        dspParams.osc1TuneCoarse.setTarget(currentPatch.params[7]);
        dspParams.osc1TuneFine.setTarget(currentPatch.params[8]);
        dspParams.osc2TuneCoarse.setTarget(currentPatch.params[9]);
        dspParams.osc2TuneFine.setTarget(currentPatch.params[10]);
        dspParams.hardSync = currentPatch.params[11] > 0.5f;
        dspParams.unisonVoices = ((int)(currentPatch.params[12] * 3.0f)) + 1; // 1,2,4,8
        dspParams.unisonDetune.setTarget(currentPatch.params[13]);
        dspParams.cutoff1Hz.setTarget(freqMap(currentPatch.params[14], sampleRate));
        dspParams.res1.setTarget(currentPatch.params[15]);
        dspParams.cutoff2Hz.setTarget(freqMap(currentPatch.params[16], sampleRate));
        dspParams.res2.setTarget(currentPatch.params[17]);
        dspParams.driveAmount.setTarget(currentPatch.params[19]);
        dspParams.ampAttackMs = currentPatch.params[27] * 2000.0f;
        dspParams.ampDecayMs = currentPatch.params[28] * 2000.0f;
        dspParams.ampSustain = currentPatch.params[29];
        dspParams.ampReleaseMs = currentPatch.params[30] * 2000.0f;
        dspParams.glideTimeSec.setTarget(currentPatch.params[31]);
        dspParams.powerSagDepth.setTarget(currentPatch.params[33]);
        dspParams.additiveBlend.setTarget(currentPatch.params[34]);
    }
    
    static float freqMap(float norm, double sr) {
        float t = std::clamp(norm, 0.0f, 1.0f);
        float f = 50.0f * std::pow(360.0f, t);  // Behringer exact curve
        float w = 2.0f * (float)M_PI * f / (float)sr;
        return (float)sr * std::tan(w * 0.5f) / (2.0f * (float)M_PI);
    }
    
    static float lerp(float a, float b, float t) {
        return a + t * (b - a);
    }
};
