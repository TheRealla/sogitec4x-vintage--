void Synth::processBlock(float** outputs, int numSamples) {
    for (Voice* voice : activeVoices) {
        voice->processBlock(outputs, numSamples, currentParams);
    }
}

