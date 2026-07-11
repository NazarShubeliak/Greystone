#pragma once
#include <vector>
#include <SDL2/SDL.h>

const int TILE_SIZE       = 24;
const int MAP_WIDTH       = 200;
const int MAP_HEIGHT      = 200;
const int PANEL_HEIGHT    = 150;
const int VIEW_RADIUS     = 16;

// Fallback size when leaving fullscreen (see toggleFullscreen() in main.cpp).
const int WINDOWED_WIDTH  = 1280;
const int WINDOWED_HEIGHT = 800;

// Actual window size — set at startup to the desktop resolution and updated on
// fullscreen/windowed toggle. Everything that lays out UI reads these live (most
// panels recompute their position from these every frame), so updating them here
// is sufficient to make the whole game re-flow to a new size.
inline int SCREEN_WIDTH    = 800;
inline int SCREEN_HEIGHT   = 600;
inline int MAP_VIEW_HEIGHT = SCREEN_HEIGHT - PANEL_HEIGHT;

inline void applyScreenSize(int w, int h) {
    SCREEN_WIDTH    = w;
    SCREEN_HEIGHT   = h;
    MAP_VIEW_HEIGHT = SCREEN_HEIGHT - PANEL_HEIGHT;
}

struct Node {
    int x, y;
    int g, h, f;
    Node* parent;
};

std::vector<SDL_Point> findPath(int startX, int startY, int endX, int endY);
