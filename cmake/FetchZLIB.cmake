# Optional: zlib built from source for deflate-compressed NSIS installers (D2K_FETCH_ZLIB=ON),
# for platforms without a system zlib (Windows). Static, linked into d2k_core; defines ZLIB::ZLIB.
include(FetchContent)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Linked statically: none of zlib's own install rules (they would write to /usr/local or C:/Program Files
# on `cmake --install build --prefix dist`).
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    URL "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz"
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
# zlib 1.3.1 asks for cmake_minimum_required(2.4.4), which CMake 4 refuses without this.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(zlib)
unset(CMAKE_POLICY_VERSION_MINIMUM)
if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    # zlib 1.3.1 writes zconf.h into its build directory.
    target_include_directories(zlibstatic INTERFACE "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
endif()
set(ZLIB_FOUND TRUE)
message(STATUS "zlib: building 1.3.1 from source (FetchContent)")
