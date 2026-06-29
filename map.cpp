#include "map.h"
#include <cstdlib>
#include <ctime>
#include <cmath>

extern Tile   map[MAP_HEIGHT][MAP_WIDTH];
extern Player player;

// ---------------------------------------------------------------- helpers

static bool inBounds(int x, int y) {
    return x > 0 && x < MAP_WIDTH - 1 && y > 0 && y < MAP_HEIGHT - 1;
}

// Paint a circular patch of terrain at (cx, cy) with given radius.
static void paintPatch(int cx, int cy, int radius, int terrainId) {
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int tx = cx + dx, ty = cy + dy;
            if (!inBounds(tx, ty)) continue;
            map[ty][tx].terrainId = terrainId;
        }
    }
}

// Roll a random object/ground spawn.
// Returns true if roll < chancePercent (0-100).
static bool roll(int chancePercent) {
    return (rand() % 100) < chancePercent;
}

// ---------------------------------------------------------------- ground cover / object spawners

static void spawnGround(Tile& t) {
    switch (t.terrainId) {
        case T_GRASSLAND:
            if      (roll(40)) t.groundId = G_GRASS;
            else if (roll(20)) t.groundId = G_TALL_GRASS;
            else if (roll(8))  t.groundId = G_FLOWER;
            break;
        case T_FOREST_FLOOR:
            if      (roll(45)) t.groundId = G_MOSS;
            else if (roll(25)) t.groundId = G_LEAVES;
            break;
        case T_SWAMP:
            if      (roll(35)) t.groundId = G_REEDS;
            else if (roll(15)) t.groundId = G_MOSS;
            break;
        case T_MUD:
            if (roll(20)) t.groundId = G_REEDS;
            break;
        case T_STONE:
            if (roll(8))  t.groundId = G_MOSS;
            break;
        // Sand, Snow, Water: no ground cover
        default: break;
    }
}

static void spawnObject(Tile& t) {
    switch (t.terrainId) {
        case T_FOREST_FLOOR:
            if      (roll(55)) t.objectId = O_TREE;
            else if (roll(18)) t.objectId = O_BUSH;
            else if (roll(5))  t.objectId = O_ROCK;
            else if (roll(3))  t.objectId = O_FALLEN_LOG;
            break;
        case T_GRASSLAND:
            if      (roll(6))  t.objectId = O_TREE;
            else if (roll(8))  t.objectId = O_BUSH;
            else if (roll(3))  t.objectId = O_ROCK;
            break;
        case T_SWAMP:
            if      (roll(22)) t.objectId = O_DEAD_TREE;
            else if (roll(5))  t.objectId = O_FALLEN_LOG;
            break;
        case T_MUD:
            if      (roll(12)) t.objectId = O_DEAD_TREE;
            else if (roll(4))  t.objectId = O_ROCK;
            break;
        case T_STONE:
            if      (roll(25)) t.objectId = O_ROCK;
            else if (roll(10)) t.objectId = O_BOULDER;
            break;
        case T_SAND:
            if (roll(5))  t.objectId = O_ROCK;
            else if (roll(3)) t.objectId = O_BOULDER;
            break;
        case T_SNOW:
            if (roll(4))  t.objectId = O_ROCK;
            break;
        default: break;
    }
    if (t.objectId >= 0)
        t.objectHp = objectDefs[t.objectId].durability;
}

// ---------------------------------------------------------------- sector generation

void generateSector(BiomeType biome, int seedX, int seedY) {
    // Deterministic seed from sector coords so revisiting gives same layout.
    unsigned int seed = (seedX >= 0)
        ? (unsigned int)(seedX * 73856093u ^ seedY * 19349663u)
        : (unsigned int)time(nullptr);
    srand(seed);

    // Step 1: fill entire map with base terrain (no bedrock border —
    // reaching the edge triggers a sector transition instead).
    int baseTerrain   = T_GRASSLAND;
    int altTerrain    = T_FOREST_FLOOR;
    int altCount      = 0;
    int altRadius     = 8;

    switch (biome) {
        case BiomeType::FOREST:
            baseTerrain = T_FOREST_FLOOR;
            altTerrain  = T_GRASSLAND;
            altCount    = 18;   // clearings
            altRadius   = 10;
            break;
        case BiomeType::PLAINS:
            baseTerrain = T_GRASSLAND;
            altTerrain  = T_FOREST_FLOOR;
            altCount    = 10;   // tree clusters
            altRadius   = 14;
            break;
        case BiomeType::SWAMP:
            baseTerrain = T_SWAMP;
            altTerrain  = T_MUD;
            altCount    = 20;
            altRadius   = 8;
            break;
        case BiomeType::DESERT:
            baseTerrain = T_SAND;
            altTerrain  = T_STONE;
            altCount    = 12;
            altRadius   = 6;
            break;
        case BiomeType::TUNDRA:
            baseTerrain = T_SNOW;
            altTerrain  = T_STONE;
            altCount    = 10;
            altRadius   = 8;
            break;
        case BiomeType::CURSED_LANDS:
            baseTerrain = T_MUD;
            altTerrain  = T_SWAMP;
            altCount    = 25;
            altRadius   = 7;
            break;
    }

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            Tile& t = map[y][x];
            t.terrainId = baseTerrain;
            t.groundId  = -1;
            t.objectId  = -1;
            t.objectHp  = 0;
            t.visible   = false;
            t.explored  = false;
        }
    }

    // Step 2: scatter alternate terrain patches
    for (int p = 0; p < altCount; p++) {
        int cx = 3 + rand() % (MAP_WIDTH  - 6);
        int cy = 3 + rand() % (MAP_HEIGHT - 6);
        int r  = altRadius / 2 + rand() % (altRadius / 2 + 1);
        paintPatch(cx, cy, r, altTerrain);
    }

    // Add occasional water patches for variety
    if (biome == BiomeType::SWAMP || biome == BiomeType::CURSED_LANDS) {
        for (int p = 0; p < 6; p++) {
            int cx = 3 + rand() % (MAP_WIDTH  - 6);
            int cy = 3 + rand() % (MAP_HEIGHT - 6);
            paintPatch(cx, cy, 3 + rand() % 4, T_WATER);
        }
    }

    // Step 3 & 4: ground cover and objects per tile
    for (int y = 1; y < MAP_HEIGHT - 1; y++) {
        for (int x = 1; x < MAP_WIDTH - 1; x++) {
            Tile& t = map[y][x];
            if (t.terrainId == T_BEDROCK || t.terrainId == T_WATER) continue;
            spawnGround(t);
            spawnObject(t);
        }
    }
}

// ---------------------------------------------------------------- visibility

bool hasLineOfSight(int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        if (x0 == x1 && y0 == y1) return true;
        if (map[y0][x0].blocksVision()) return false;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void updateVisibility() {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            map[y][x].visible = false;

    for (int dy = -VIEW_RADIUS; dy <= VIEW_RADIUS; dy++) {
        for (int dx = -VIEW_RADIUS; dx <= VIEW_RADIUS; dx++) {
            int tx = player.x + dx;
            int ty = player.y + dy;
            if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT) continue;
            if (dx * dx + dy * dy <= VIEW_RADIUS * VIEW_RADIUS) {
                if (hasLineOfSight(player.x, player.y, tx, ty)) {
                    map[ty][tx].visible  = true;
                    map[ty][tx].explored = true;
                }
            }
        }
    }
}
