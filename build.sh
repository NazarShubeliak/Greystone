#!/bin/bash
set -e

g++ src/main.cpp src/core/astar.cpp src/core/map.cpp src/render/render.cpp \
  -Isrc/core -Isrc/render -Isrc/entities -Isrc/ui \
  -o greystone $(sdl2-config --cflags --libs) -lSDL2_ttf

# Standalone headless Legends/worldgen simulator (src/tools/worldgen_sim.cpp) —
# no SDL2 linking needed, it only uses SDL_Color/SDL_Point as plain structs and
# never calls an actual SDL runtime function. sdl2-config --cflags still gives
# it the header path.
g++ src/tools/worldgen_sim.cpp \
  -Isrc/entities \
  -o worldgen_sim $(sdl2-config --cflags)
