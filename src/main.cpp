// Ridgeline - an open-source open-world festival racer.
// Licensed under the MIT license (see LICENSE).
#include <SDL3/SDL_main.h>
#include "game.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static void usage() {
    std::printf(
        "Ridgeline - open-world festival racing\n\n"
        "Usage: ridgeline [options]\n"
        "  --width N --height N   window size (default 1280x720)\n"
        "  --fullscreen           start fullscreen\n"
        "  --help                 show this help\n\n"
        "Developer options:\n"
        "  --scene NAME           jump to: hub|drive|race|dealer|garage|map|pause|settings\n"
        "  --event N              event index for --scene race\n"
        "  --car N                give and use car N (0-9)\n"
        "  --sim SECONDS          fast-forward the simulation before rendering\n"
        "  --shot FILE.bmp        render, save a screenshot and quit\n"
        "  --frames N             quit after N frames\n");
}

int main(int argc, char** argv) {
    Options o;
    bool fullscreen = false;
    for (int i = 1; i < argc; i++) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(); return 0; }
        else if (!std::strcmp(argv[i], "--width")) o.width = std::atoi(next());
        else if (!std::strcmp(argv[i], "--height")) o.height = std::atoi(next());
        else if (!std::strcmp(argv[i], "--fullscreen")) fullscreen = true;
        else if (!std::strcmp(argv[i], "--scene")) o.scene = next();
        else if (!std::strcmp(argv[i], "--event")) o.event = std::atoi(next());
        else if (!std::strcmp(argv[i], "--car")) o.car = std::atoi(next());
        else if (!std::strcmp(argv[i], "--sim")) o.simSeconds = float(std::atof(next()));
        else if (!std::strcmp(argv[i], "--shot")) o.shot = next();
        else if (!std::strcmp(argv[i], "--frames")) o.frames = std::atoi(next());
    }
    if (o.width < 320) o.width = 1280;
    if (o.height < 240) o.height = 720;
    Game game;
    if (!game.init(o)) {
        if (o.shot.empty())
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ridgeline",
                                 "Could not start the game. Ridgeline needs OpenGL 3.3 support.\nRun from a terminal to see details.", nullptr);
        return 1;
    }
    (void)fullscreen;
    game.run();
    game.shutdown();
    return 0;
}
