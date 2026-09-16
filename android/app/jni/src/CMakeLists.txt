cmake_minimum_required(VERSION 3.6)

project(x16emu_android C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The X16-emulator-wifi-support repo root is four levels up from
# android/app/jni/src/. Referenced by path rather than vendored a second
# time, so the Android build always uses the same emulator source as the
# Linux/Windows builds.
get_filename_component(X16_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../x16-emulator" ABSOLUTE)
set(X16_SRC "${X16_ROOT}/src")

add_library(main SHARED
    ${X16_SRC}/cpu/fake6502.c
    ${X16_SRC}/memory.c
    ${X16_SRC}/disasm.c
    ${X16_SRC}/video.c
    ${X16_SRC}/i2c.c
    ${X16_SRC}/smc.c
    ${X16_SRC}/rtc.c
    ${X16_SRC}/via.c
    ${X16_SRC}/serial.c
    ${X16_SRC}/ieee.c
    ${X16_SRC}/vera_spi.c
    ${X16_SRC}/audio.c
    ${X16_SRC}/vera_pcm.c
    ${X16_SRC}/vera_psg.c
    ${X16_SRC}/sdcard.c
    ${X16_SRC}/main.c
    ${X16_SRC}/debugger.c
    ${X16_SRC}/javascript_interface.c
    ${X16_SRC}/joystick.c
    ${X16_SRC}/rendertext.c
    ${X16_SRC}/keyboard.c
    ${X16_SRC}/icon.c
    ${X16_SRC}/timing.c
    ${X16_SRC}/wav_recorder.c
    ${X16_SRC}/testbench.c
    ${X16_SRC}/files.c
    ${X16_SRC}/cartridge.c
    ${X16_SRC}/iso_8859_15.c
    ${X16_SRC}/ymglue.cpp
    ${X16_SRC}/midi.c
    ${X16_SRC}/esp32wifi.c
    ${X16_SRC}/extern/ymfm/src/ymfm_opm.cpp
)

target_include_directories(main PRIVATE
    ${X16_SRC}
    ${X16_SRC}/extern/include
    ${X16_SRC}/extern/ymfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

target_link_libraries(main SDL2 z log)
