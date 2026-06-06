#!/bin/bash
g++ main.cpp astar.cpp map.cpp render.cpp -o greystone $(sdl2-config --cflags --libs) -lSDL2_ttf