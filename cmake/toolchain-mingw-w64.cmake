# CMake toolchain file for cross-compiling to Windows x86_64 from Linux,
# using the Debian/Ubuntu mingw-w64 cross-compiler against the Qt6 binaries
# fetched by aqtinstall (see ~/qt-win/6.5.3/mingw_64).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Use POSIX threads + SEH exception model — matches Qt's MinGW build.
set(CMAKE_CXX_FLAGS_INIT "-static-libgcc -static-libstdc++")

# Roots for find_package / find_library / etc.
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 $ENV{HOME}/qt-win/6.5.3/mingw_64)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
