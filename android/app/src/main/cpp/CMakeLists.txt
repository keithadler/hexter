cmake_minimum_required(VERSION 3.22)
project(hexter_android C CXX)

set(HEXTER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The engine, exactly the sources the desktop build uses.
add_library(hexter_engine STATIC
    ${HEXTER_ROOT}/src/dx7_voice.c
    ${HEXTER_ROOT}/src/dx7_voice_data.c
    ${HEXTER_ROOT}/src/dx7_voice_patches.c
    ${HEXTER_ROOT}/src/dx7_voice_render.c
    ${HEXTER_ROOT}/src/dx7_voice_tables.c
    ${HEXTER_ROOT}/src/hexter_synth.c
    ${HEXTER_ROOT}/src/dx7_bank.c
    ${HEXTER_ROOT}/src/dx7_voice_4op.c
    ${HEXTER_ROOT}/src/dx7_voice_fb01.c
    ${HEXTER_ROOT}/src/hexter_engine.c)
target_include_directories(hexter_engine PUBLIC ${HEXTER_ROOT}/src)
file(READ ${HEXTER_ROOT}/CMakeLists.txt HEXTER_TOP)
string(REGEX MATCH "VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${HEXTER_TOP}")
target_compile_definitions(hexter_engine PUBLIC VERSION="${CMAKE_MATCH_1}")
target_compile_options(hexter_engine PRIVATE -O2 -ffp-contract=off)

find_package(oboe REQUIRED CONFIG)

add_library(hexter_jni SHARED hexter_jni.cpp)
target_link_libraries(hexter_jni PRIVATE hexter_engine oboe::oboe log android)
