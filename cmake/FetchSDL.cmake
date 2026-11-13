# Downloads and builds the pinned SDL3 release (the version Homebrew ships and the iPad build
# uses; docs/ios.md) as part of the project. Used when find_package(SDL3) finds nothing or
# D2K_FETCH_SDL=ON. A local source tree can be used instead of the download with
#   -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL3-3.4.20
# Linux needs SDL's build dependencies (X11 / Wayland / audio dev packages; docs/building.md).
include(FetchContent)
set(D2K_SDL_VERSION "3.4.20")
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    URL "https://github.com/libsdl-org/SDL/releases/download/release-${D2K_SDL_VERSION}/SDL3-${D2K_SDL_VERSION}.tar.gz"
    URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(SDL3)
if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "FetchSDL: SDL3::SDL3 target missing after FetchContent")
endif()
message(STATUS "SDL3: building ${D2K_SDL_VERSION} from source (FetchContent)")
