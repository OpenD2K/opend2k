// Native Dune 2000 (d2k-mac). Without --map it opens the main menu; with --map NAME it goes
// straight into that mission. Command-line options: see platform/options.hpp; modules:
//   app.*        window, settings, data folder, menu <-> session state machine
//   menu.*       front-end pages (campaign, skirmish, options, data folder)
//   session.*    one running game (world, camera, input, rendering, sound, pause menu)
//   game_data.*  loading the original data for a mission / skirmish map
//   sim_test.*   the headless --simtest checks

#include "platform/app.hpp"
#include "platform/options.hpp"

#include <SDL3/SDL_main.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <mach-o/dyld.h>
#include <unistd.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
// macOS's malloc keeps freed large blocks (MBs each) in a cache for reuse and counts them in the process's
// footprint; a game's load (glTF parsing, decoded images, model vertices, the 3D terrain's build) frees
// ~0.5 GB of them that malloc_zone_pressure_relief does not return. MallocLargeCache=0 turns the cache off
// (no measurable frame-time cost) but is read before main(), so the process starts itself again with it
// set (docs/memory.md). D2K_MALLOC_CACHE=1 keeps it (and the single process, e.g. for a debugger).
static void restartWithoutMallocLargeCache(char** argv) {
    if (std::getenv("MallocLargeCache") || std::getenv("D2K_MALLOC_CACHE")) return;
    char path[PATH_MAX];
    std::uint32_t n = sizeof path;
    if (_NSGetExecutablePath(path, &n) != 0) return;
    setenv("MallocLargeCache", "0", 1);
    execv(path, argv);  // returns only on failure: carry on with the cache
}
#endif
#endif

int main(int argc, char** argv) {
#if defined(__APPLE__) && TARGET_OS_OSX
    restartWithoutMallocLargeCache(argv);
#endif
    d2k::platform::App app(d2k::platform::parseArgs(argc, argv));
    return app.run();
}
