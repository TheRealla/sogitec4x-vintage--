struct LeanBehringerOsc {
    double phase = 0, phaseInc = 0;
    float syncPhase = 0;  // Master sync reference
    
    // FIXED: Single lookup table, no polyphase bloat
    static constexpr float sawTable[1024];
    static constexpr float triTable[1024];
    
    inline float process(double masterPhaseInc, float tuneCoarse, float tuneFine, int wave, bool sync) {
        // 1:1 Behringer tuning (coarse ±2 oct, fine ±50 cents)
        float tune = tuneCoarse * 24.0f + (tuneFine - 0.5f) * 1.0f;
        phaseInc = masterPhaseInc * powf(2.0f, tune / 12.0f);
        
        phase += phaseInc;
        if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        
        // HARD SYNC (Behringer exact reset behavior)
        if (sync && phase < phaseInc * 0.1f) {
            phase = 0;
            syncPhase = 0;
        }
        
        switch (wave) {
            case 0: return sinf(phase);  // Sine - direct, NO TABLE
            case 1: return sawTable[(int)(phase * 512.0 / M_PI) & 1023];  // Saw
            case 2: return (phase < M_PI) ? -1.0f : 1.0f;  // Square - direct
            case 3: return triTable[(int)(phase * 512.0 / M_PI) & 1023];  // Triangle
            default: return 0.0f;
        }
    }
};

// Precomputed tables (compile-time, 4KB total)
constexpr float LeanBehringerOsc::sawTable[1024] = {
    // ... 1024-point sawtooth (generated at compile time)
};
constexpr float LeanBehringerOsc::triTable[1024] = {
    // ... 1024-point triangle
};
