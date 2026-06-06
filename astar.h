#pragma once
#include <vector>
#include <SDL2/SDL.h>

const int TILE_SIZE = 24;
const int MAP_WIDTH = 200;
const int MAP_HEIGHT = 200;
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const int VIEW_RADIUS = 16;

struct Tile {
    bool walkable;
    const char* symbol;
    SDL_Color color;
    bool visible;
    bool explored;
};

struct Node {
    int x, y;
    int g, h, f;
    Node* parent;
};

std::vector<SDL_Point> findPath(int startX, int startY, int endX, int endY);