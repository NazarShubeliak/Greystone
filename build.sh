#!/bin/bash
set -e

g++ src/main.cpp src/core/astar.cpp src/core/map.cpp src/render/render.cpp \
  -Isrc/core -Isrc/render -Isrc/entities -Isrc/ui \
  -o greystone $(sdl2-config --cflags --libs) -lSDL2_ttf

# Standalone headless Legends/worldgen simulator (src/tools/worldgen_sim.cpp) —
# no SDL2 linking needed, it only uses SDL_Color/SDL_Point as plain structs and
# never calls an actual SDL runtime function. Deliberately NOT using
# sdl2-config here (unlike the build above) — `sdl2-config --cflags` secretly
# adds `-Dmain=SDL_main`, which renames this file's own int main() at the
# preprocessor level regardless of the SDL_MAIN_HANDLED guard in the source
# (that guard only stops SDL_main.h's own macro, not a command-line -D) and
# caused an "undefined reference to WinMain" link error. Point straight at the
# SDL2 headers instead, same path sdl2-config would have resolved to.
g++ src/tools/worldgen_sim.cpp \
  -Isrc/entities -I/mingw64/include/SDL2 \
  -o worldgen_sim

# Standalone Legends Viewer (src/tools/legends_viewer.cpp) — reads the plain-
# text export the real game's `legends` cheat-console command writes. No game
# headers at all, so no -I flags or SDL2 needed here either.
g++ src/tools/legends_viewer.cpp -o legends_viewer
