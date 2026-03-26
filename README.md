This skeleton faithfully blends:Sogitec 4X heritage: Multi-DSP/partial additive capability (you already had the 32-harmonic digitalOsc stub — keep/enhance it for IRCAM-style additive layers on top of the VA engine).

Behringer Vintage: Two main oscillators + sub + noise, hard sync, dual filters with drive, two LFOs, unison, glide/portamento, ~100-preset spirit (you'd add a preset system separately).

Airwindows PowerSag: Dynamic, load-dependent voltage sag that makes the whole thing feel more "alive" and analog under heavy output.



The code is still a skeleton — many sections (full ADSR, better filter implementations, modulation matrix, preset loading) are left as stubs for you to flesh out.


cmake_minimum_required(VERSION 3.15)
project(Sogitec4X_Vintage)

set(CMAKE_CXX_STANDARD 17)

# Source files
file(GLOB SOURCES "Sources/*.cpp" "Effects/*.cpp")

add_executable(sogitec4x ${SOURCES} Sources/Voice.h Params.h)

# Math library
target_link_libraries(sogitec4x m)

# Optimization
target_compile_options(sogitec4x PRIVATE -O3 -march=native -ffast-math)

# Usage: ./sogitec4x test.wav  # Generates test audio
## Quick Build & Test
```bash
mkdir build && cd build
cmake .. && make -j
./sogitec4x test.wav  # Plays hybrid tones to WAV

# 1. Cmajor Player (instant)
cmajor-player sol-dpkva.cmajor

# 2. VS Code → Run → Hear keytracking magic

# 3. Export WAV test
cmajor-render sol-dpkva.cmajor -duration 10s -outfile demo.wav
# 1. Cmajor Player (instant)
cmajor-player sol-dpkva.cmajor

# 2. VS Code → Run → Hear keytracking magic

# 3. Export WAV test
cmajor-render sol-dpkva.cmajor -duration 10s -outfile demo.wav




