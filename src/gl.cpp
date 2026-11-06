#include "gl.h"
#include <SDL3/SDL.h>

#define GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
GL_FUNCS(GL_DEFINE)
#undef GL_DEFINE

bool glLoadAll(const char** missing) {
#define GL_LOAD(ret, name, args)                                         \
    name = (PFN_##name)(void*)SDL_GL_GetProcAddress(#name);              \
    if (!name) { if (missing) *missing = #name; return false; }
    GL_FUNCS(GL_LOAD)
#undef GL_LOAD
    return true;
}
