#!/bin/bash
g++ src/main.cpp src/core/astar.cpp src/core/map.cpp src/render/render.cpp \
  -Isrc/core -Isrc/render -Isrc/entities -Isrc/ui \
  -o greystone $(sdl2-config --cflags --libs) -lSDL2_ttf
