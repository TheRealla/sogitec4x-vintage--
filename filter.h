// Source/Filter.h
#pragma once
class Filter {
public:
    float process(float input, float cutoff, float res, int type, float drive, float curve);
private:
    float z1 = 0, z2 = 0;
    float sampleRate = 44100;
    float formantFilter(float lp, float bp);
    float applyDrive(float x, float amt, float curve);
};

