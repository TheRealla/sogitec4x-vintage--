// Serial/Parallel + Formant/Notch modes
enum FilterMode { LP12, LP24, HP, BP, Notch, Formant };
float dualFilter(float in) {
    float f1 = ladderFilter(in, params.cutoff1, mode1);
    float f2 = ladderFilter(in, params.cutoff2, mode2);
    return serial ? f2 : lerp(f1, f2, mix);
}

