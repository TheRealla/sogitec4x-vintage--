// Effects/PowerSag.h
#pragma once
class PowerSag {
public:
    PowerSag(double sr);
    float process(float input);
private:
    double sampleRate;
    float loadMemory = 0, sagMemory = 1.0f, breathPhase = 0;
    Params params;  // Reference to global params
};
