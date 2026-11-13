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

int main(int argc, char** argv) {
    d2k::platform::App app(d2k::platform::parseArgs(argc, argv));
    return app.run();
}
