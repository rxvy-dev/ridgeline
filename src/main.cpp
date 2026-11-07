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
        "  --scene NAME           jump to: hub|drive|race|dealer|garage|map|pause|settings|\n                         wheelspin|upgrade|photo|keyart|trap|donut|spot\n"
        "  --event N              event index for --scene race\n"
        "  --car N                give and use car N\n  --at X,Z,YAW           spawn point for --scene spot\n"
        "  --sim SECONDS          fast-forward the simulation before rendering\n"
        "  --shot FILE.bmp        render, save a screenshot and quit\n"
        "  --frames N             quit after N frames\n"
        "  --record 'CMD'         pipe raw RGBA frames at a fixed 30 fps into CMD (e.g. ffmpeg)\n"
        "  --record-seconds S     length of the recording (default 3)\n"
        "  --record-audio FILE    also write the game audio (f32le stereo 48 kHz)\n"
        "  --orbit RAD_PER_S      auto-orbit the camera in photo/keyart scenes\n");
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
        else if (!std::strcmp(argv[i], "--look")) o.look = float(std::atof(next()));
        else if (!std::strcmp(argv[i], "--cam")) o.cam = std::atoi(next());
        else if (!std::strcmp(argv[i], "--at")) o.at = next();
        else if (!std::strcmp(argv[i], "--record")) { o.recordCmd = next(); o.noHints = true; }
        else if (!std::strcmp(argv[i], "--record-audio")) o.recordAudio = next();
        else if (!std::strcmp(argv[i], "--record-seconds")) o.recordSec = float(std::atof(next()));
        else if (!std::strcmp(argv[i], "--orbit")) o.orbit = float(std::atof(next()));
        else if (!std::strcmp(argv[i], "--no-hud")) o.noHud = true;
        else if (!std::strcmp(argv[i], "--wet")) o.wet = true;
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
