# CMake toolchain: Windows x64 with MinGW-w64 (cross-compiling from macOS / Linux).
# macOS: brew install mingw-w64   Ubuntu: apt install g++-mingw-w64-x86-64-posix
# Used by tools/release/package_windows.sh.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(_triple x86_64-w64-mingw32)
find_program(CMAKE_C_COMPILER NAMES ${_triple}-gcc-posix ${_triple}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${_triple}-g++-posix ${_triple}-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES ${_triple}-windres REQUIRED)
# Only the cross toolchain's own headers / libraries; programs (cmake, ninja, dxc) from the host.
get_filename_component(_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(CMAKE_FIND_ROOT_PATH "${_bin}/../${_triple}" "/usr/${_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
