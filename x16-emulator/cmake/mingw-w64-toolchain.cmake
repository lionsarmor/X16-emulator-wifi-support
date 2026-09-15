# CMake toolchain file for cross-compiling x16emu for 64-bit Windows using
# the mingw-w64 compiler on Linux. See tools/fetch-windows-deps.sh for how to
# obtain the SDL2/zlib mingw development files this expects, and
# tools/build-windows.sh for how this file gets used.
#
# Usage:
#   cmake -S . -B build-windows \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
#       -DCMAKE_BUILD_TYPE=Release \
#       -DENABLE_FLUIDSYNTH=OFF

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_TRIPLE x86_64-w64-mingw32)

# Use the "posix" threading-model variant explicitly (rather than the bare
# x86_64-w64-mingw32-gcc, whose system alternative may point at "win32"
# threading, which has no pthread emulation and fails to link anything using
# std::mutex/pthread_mutex_* - which this codebase's esp32wifi.c does).
set(CMAKE_C_COMPILER ${MINGW_TRIPLE}-gcc-posix)
set(CMAKE_CXX_COMPILER ${MINGW_TRIPLE}-g++-posix)
set(CMAKE_RC_COMPILER ${MINGW_TRIPLE}-windres)

# Where to look for the target's libraries/headers (mingw-w64's own sysroot,
# plus the vendored SDL2 mingw devel package fetched by fetch-windows-deps.sh).
set(CROSSDEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../.crossdeps" CACHE PATH "Vendored Windows cross dependencies")

set(CMAKE_FIND_ROOT_PATH
    /usr/${MINGW_TRIPLE}
    ${CROSSDEPS_DIR}/zlib-mingw/usr/${MINGW_TRIPLE}
    ${CROSSDEPS_DIR}/SDL2-mingw/x86_64-w64-mingw32
)

set(CMAKE_PREFIX_PATH ${CMAKE_FIND_ROOT_PATH})

# Only ever look in the target sysroots above for libraries/headers/packages,
# but still use the host's cmake program itself.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
