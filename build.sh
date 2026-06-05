#!/bin/bash
g++ main.cpp astar.cpp -o greystone $(sdl2-config --cflags --libs) -lSDL2_ttf