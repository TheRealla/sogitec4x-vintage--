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

