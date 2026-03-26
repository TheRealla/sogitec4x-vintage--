// Direct Airwindows port — voltage drop + bloom
class PowerSag {
    float sagMemory = 1.0, loadMemory = 0;
public:
    float process(float in, float depth) {
        float load = fabs(in);
        loadMemory = 0.995 * loadMemory + 0.005 * load;
        sagMemory *= expf(-0.08 * depth * loadMemory / sampleRate);
        return in * fmaxf(0.6f, sagMemory);
    }
};

