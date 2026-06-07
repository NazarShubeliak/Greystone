#include "map.h"
#include <cstdlib>
#include <ctime>

extern SDL_Color white;
extern SDL_Color green;
extern SDL_Color orange;
extern SDL_Color yellow;
extern SDL_Color red;
extern Tile map[MAP_HEIGHT][MAP_WIDTH];
extern Player player;

void initMap() {
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (x == 0 || x == MAP_WIDTH - 1 || y == 0 || y == MAP_HEIGHT - 1)
                map[y][x] = {false, "#", orange, false, false};
            else
                map[y][x] = {true, ".", green, false, false};
        }
    }
    srand(time(nullptr));
    for (int i = 0; i < 200; i++) {
        int x = rand() % (MAP_WIDTH - 2) + 1;
        int y = rand() % (MAP_HEIGHT - 2) + 1;
        map[y][x] = {false, "#", orange, false, false};
    }
}

bool hasLineOfSight(int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        if (x0 == x1 && y0 == y1) return true;
        if (!map[y0][x0].walkable) return false;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void updateVisibility() {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            map[y][x].visible = false;

    for (int y = -VIEW_RADIUS; y <= VIEW_RADIUS; y++) {
        for (int x = -VIEW_RADIUS; x <= VIEW_RADIUS; x++) {
            int tx = player.x + x;
            int ty = player.y + y;
            if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT) continue;
            if (x * x + y * y <= VIEW_RADIUS * VIEW_RADIUS) {
                if (hasLineOfSight(player.x, player.y, tx, ty)) {
                    map[ty][tx].visible = true;
                    map[ty][tx].explored = true;
                }
            }
        }
    }
}
