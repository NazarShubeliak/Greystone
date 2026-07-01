#include "map.h"
#include "time_system.h"
#include <cstdlib>
#include <ctime>
#include <cmath>

extern Tile      map[MAP_HEIGHT][MAP_WIDTH];
extern Player    player;
extern WorldTime worldTime;

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

// Returns growth progress 0-255 for a plant tile, deterministic from coords + current date.
// 170+ = mature (harvestable). Returns 0 in wrong season.
static uint8_t calcPlantAge(int x, int y, int secX, int secY, int growSeasons, int daysToMature) {
    int curSeason = worldTime.season();
    if (!((growSeasons >> curSeason) & 1)) return 0;

    int dayInSeason = (worldTime.month() % 3) * 30 + worldTime.day() - 1;

    unsigned seed = (unsigned)(x * 73856093u ^ y * 19349663u ^
                               secX * 998244353u ^ (unsigned)secY * 83492791u);
    int offset = (int)(seed % (unsigned)std::max(1, daysToMature));

    int daysGrown = dayInSeason - offset;
    if (daysGrown < 0) daysGrown = 0;

    int progress = (daysGrown * 255) / std::max(1, daysToMature);
    return (uint8_t)std::min(255, progress);
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

static void spawnObject(Tile& t, int x, int y, int secX, int secY) {
    int season = worldTime.season();

    switch (t.terrainId) {
        case T_FOREST_FLOOR:
            if      (roll(55)) t.objectId = O_TREE;
            else if (roll(18)) t.objectId = O_BUSH;
            else if (roll(5))  t.objectId = O_ROCK;
            else if (roll(3))  t.objectId = O_FALLEN_LOG;
            else if (season == 2 && roll(12)) t.objectId = O_MUSHROOM; // autumn
            else if (season <= 1 && roll(8))  t.objectId = O_HERB;     // spring/summer
            break;
        case T_GRASSLAND:
            if      (roll(6))  t.objectId = O_TREE;
            else if (roll(8))  t.objectId = O_BUSH;
            else if (roll(3))  t.objectId = O_ROCK;
            else if ((season == 0 || season == 1) && roll(10)) t.objectId = O_HERB;
            break;
        case T_SWAMP:
            if      (roll(22)) t.objectId = O_DEAD_TREE;
            else if (roll(5))  t.objectId = O_FALLEN_LOG;
            else if (season == 2 && roll(8)) t.objectId = O_MUSHROOM;
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
            if      (roll(5))  t.objectId = O_ROCK;
            else if (roll(3))  t.objectId = O_BOULDER;
            break;
        case T_SNOW:
            if (roll(4))  t.objectId = O_ROCK;
            break;
        default: break;
    }
    if (t.objectId >= 0) {
        const ObjectDef& od = objectDefs[t.objectId];
        t.objectHp = od.durability;
        if (od.isPlant) {
            int curSeason = worldTime.season();
            if ((od.growSeasons >> curSeason) & 1) {
                // Wild plants: random age so some are already mature on game start
                unsigned h = (unsigned)(x * 314159265u ^ y * 271828182u ^
                                        secX * 141421356u ^ (unsigned)secY * 173205080u);
                t.plantAge = (uint8_t)(h % 256);
            } else {
                t.plantAge = 0; // wrong season — dormant
            }
        }
    }
}

// ---------------------------------------------------------------- village generation

static void putFurniture(int x, int y, int oid) {
    if (!inBounds(x, y)) return;
    if (map[y][x].terrainId != T_FLOOR) return;
    if (map[y][x].objectId >= 0) return;
    map[y][x].objectId = oid;
    map[y][x].objectHp = objectDefs[oid].durability;
}

// Add a vertical internal wall at bx+divX, with a closed door at mid-height.
static void placeRoomDivider(int bx, int by, int bh, int divX) {
    int doorY = by + bh / 2;
    for (int y = by + 1; y <= by + bh - 2; y++) {
        if (!inBounds(bx + divX, y)) continue;
        Tile& t = map[y][bx + divX];
        t.terrainId = T_FLOOR;
        t.groundId  = -1;
        if (y == doorY) {
            t.objectId = O_DOOR_CLOSED;
            t.objectHp = objectDefs[O_DOOR_CLOSED].durability;
        } else {
            t.objectId = O_WALL;
            t.objectHp = objectDefs[O_WALL].durability;
        }
    }
}

// Place a window tile — only if the tile is already a wall.
static void putWindow(int x, int y) {
    if (!inBounds(x, y)) return;
    if (map[y][x].objectId != O_WALL) return;
    map[y][x].objectId = O_WINDOW;
    map[y][x].objectHp = objectDefs[O_WINDOW].durability;
}

static void placeWindows(int bx, int by, int bw, int bh, bool doorNorth, int doorX) {
    // Opposite wall from exterior door: 1–2 windows
    int oppY = doorNorth ? (by + bh - 1) : by;
    if (bw >= 10) {
        putWindow(bx + bw / 3,     oppY);
        putWindow(bx + 2 * bw / 3, oppY);
    } else {
        putWindow(bx + bw / 2, oppY);
    }
    // Left and right walls: one window at mid-height each
    int midY = by + bh / 2;
    putWindow(bx,           midY);
    putWindow(bx + bw - 1,  midY);
}

enum BType { HOME, TAVERN, STORAGE };

static void placeFurniture(int bx, int by, int bw, int bh, BType type, bool divided) {
    int wallX = bx + bw / 2; // divider column (only used when divided)

    switch (type) {
    case HOME:
        // Main room: bed in corner, table in center
        putFurniture(bx + bw - 2, by + 1,      O_BED);
        putFurniture(bx + bw - 2, by + 2,      O_BED);
        putFurniture(bx + 2,      by + bh / 2, O_TABLE);
        if (divided) {
            // Second room (right of divider): storage barrel
            putFurniture(wallX + 1, by + 1,      O_BARREL);
            putFurniture(wallX + 2, by + bh - 2, O_BARREL);
        }
        break;
    case TAVERN:
        // Main room: tables + fireplace against back wall
        putFurniture(bx + 2,      by + 2,      O_TABLE);
        putFurniture(bx + 2,      by + bh - 2, O_TABLE);
        putFurniture(bx + bw - 2, by + 1,      O_FIREPLACE);
        if (divided) {
            // Second room: more seating + barrel
            putFurniture(wallX + 1, by + 2,      O_TABLE);
            putFurniture(wallX + 1, by + bh - 2, O_BARREL);
            putFurniture(wallX + 2, by + bh - 2, O_BARREL);
        }
        break;
    case STORAGE:
        // Barrels arranged in rows in both rooms
        for (int k = 0; k < (divided ? bw / 2 - 2 : bw - 3); k++) {
            putFurniture(bx + 1 + k, by + 1,      O_BARREL);
            putFurniture(bx + 1 + k, by + bh - 2, O_BARREL);
        }
        if (divided) {
            for (int k = 0; k < bw / 2 - 1; k++) {
                putFurniture(wallX + 1 + k, by + 1,      O_BARREL);
                putFurniture(wallX + 1 + k, by + bh - 2, O_BARREL);
            }
        }
        break;
    }
}

static void putObject(int x, int y, int oid) {
    if (!inBounds(x, y) || map[y][x].objectId >= 0) return;
    map[y][x].objectId = oid;
    map[y][x].objectHp = objectDefs[oid].durability;
}

static void placeLamps(int vcx, int vcy) {
    // Beside the main horizontal road (1 tile south)
    const int road[][2] = { {-22,1},{-13,1},{13,1},{22,1} };
    for (auto& p : road) putObject(vcx+p[0], vcy+p[1], O_LAMP);
    // At plaza corners (just outside the 4×4 stone area)
    const int plaza[][2] = { {-5,-5},{5,-5},{-5,5},{5,5} };
    for (auto& p : plaza) putObject(vcx+p[0], vcy+p[1], O_LAMP);
}

// Places a farmhouse (hx,hy,hw,hh) + adjacent crop field (fx,fy,fw,fh).
// doorFacesWest: door on west wall if true, east wall if false.
static void placeFarmstead(int hx, int hy, int hw, int hh,
                            int fx, int fy, int fw, int fh,
                            bool doorFacesWest, int secX, int secY) {
    // Interior floor
    for (int y = hy+1; y < hy+hh-1; y++)
        for (int x = hx+1; x < hx+hw-1; x++)
            if (inBounds(x,y)) {
                map[y][x].terrainId = T_FLOOR;
                map[y][x].objectId  = -1;
                map[y][x].groundId  = -1;
            }

    // Perimeter walls
    for (int y = hy; y < hy+hh; y++)
        for (int x = hx; x < hx+hw; x++) {
            bool edge = (x==hx || x==hx+hw-1 || y==hy || y==hy+hh-1);
            if (!edge || !inBounds(x,y)) continue;
            map[y][x].terrainId = T_GRASSLAND;
            map[y][x].groundId  = -1;
            map[y][x].objectId  = O_WALL;
            map[y][x].objectHp  = objectDefs[O_WALL].durability;
        }

    // Door on the side facing the field
    int doorX = doorFacesWest ? hx : hx+hw-1;
    int doorY = hy + hh/2;
    if (inBounds(doorX, doorY)) {
        map[doorY][doorX].objectId = O_DOOR_CLOSED;
        map[doorY][doorX].objectHp = objectDefs[O_DOOR_CLOSED].durability;
    }

    // Windows: opposite side + back wall
    putWindow(doorFacesWest ? hx+hw-1 : hx, doorY);
    putWindow(hx+hw/2, hy);
    putWindow(hx+hw/2, hy+hh-1);

    // Furniture: beds away from door, table in middle, barrel in back corner
    if (doorFacesWest) {
        putFurniture(hx+hw-2, hy+1,    O_BED);
        putFurniture(hx+hw-2, hy+2,    O_BED);
        putFurniture(hx+hw-3, hy+hh/2, O_TABLE);
        putFurniture(hx+hw-2, hy+hh-2, O_BARREL);
    } else {
        putFurniture(hx+1, hy+1,    O_BED);
        putFurniture(hx+1, hy+2,    O_BED);
        putFurniture(hx+2, hy+hh/2, O_TABLE);
        putFurniture(hx+1, hy+hh-2, O_BARREL);
    }

    // Crop field: even rows = wheat, odd rows with spacing = herb
    for (int y = fy; y < fy+fh; y++)
        for (int x = fx; x < fx+fw; x++) {
            if (!inBounds(x,y)) continue;
            map[y][x].terrainId = T_MUD;
            map[y][x].groundId  = -1;
            map[y][x].objectId  = -1;
            if (y % 2 == 0) {
                map[y][x].objectId = O_WHEAT;
                map[y][x].objectHp = objectDefs[O_WHEAT].durability;
                map[y][x].plantAge = calcPlantAge(x, y, secX, secY,
                    objectDefs[O_WHEAT].growSeasons, objectDefs[O_WHEAT].daysToMature);
            } else if (x % 3 != 1) {
                map[y][x].objectId = O_HERB;
                map[y][x].objectHp = objectDefs[O_HERB].durability;
                map[y][x].plantAge = calcPlantAge(x, y, secX, secY,
                    objectDefs[O_HERB].growSeasons, objectDefs[O_HERB].daysToMature);
            }
        }
}

static void placeVillage(int secX, int secY) {
    const int VCX = MAP_WIDTH  / 2;
    const int VCY = MAP_HEIGHT / 2;

    // Wipe a 50-tile radius clean: remove trees, rocks, ground cover
    for (int y = VCY - 50; y <= VCY + 50; y++)
        for (int x = VCX - 50; x <= VCX + 50; x++)
            if (inBounds(x, y)) {
                map[y][x].objectId  = -1;
                map[y][x].objectHp  = 0;
                map[y][x].groundId  = -1;
                map[y][x].terrainId = T_GRASSLAND;
            }

    struct Bldg { int rx, ry, w, h; bool doorNorth; BType type; bool divided; };
    static const Bldg bldgs[] = {
        { -26, -21, 11, 8, false, HOME,    false },
        {  15, -21, 11, 8, false, HOME,    false },
        {  -6, -23,  9, 7, false, TAVERN,  false },
        { -28,   8, 14, 9, true,  HOME,    true  },
        {  16,   8, 13, 9, true,  STORAGE, true  },
        {  -6,  15, 14, 8, true,  TAVERN,  true  },
    };

    for (const auto& b : bldgs) {
        int bx = VCX + b.rx;
        int by = VCY + b.ry;

        // Interior floor
        for (int y = by + 1; y < by + b.h - 1; y++)
            for (int x = bx + 1; x < bx + b.w - 1; x++)
                if (inBounds(x, y)) {
                    map[y][x].terrainId = T_FLOOR;
                    map[y][x].objectId  = -1;
                    map[y][x].groundId  = -1;
                }

        // Perimeter walls
        for (int y = by; y < by + b.h; y++)
            for (int x = bx; x < bx + b.w; x++) {
                bool edge = (x == bx || x == bx + b.w - 1 || y == by || y == by + b.h - 1);
                if (!edge || !inBounds(x, y)) continue;
                map[y][x].terrainId = T_GRASSLAND;
                map[y][x].groundId  = -1;
                map[y][x].objectId  = O_WALL;
                map[y][x].objectHp  = objectDefs[O_WALL].durability;
            }

        // Exterior door: for divided buildings, open into the left room (bw/4).
        int doorX = b.divided ? (bx + b.w / 4) : (bx + b.w / 2);
        int doorY = b.doorNorth ? by : (by + b.h - 1);
        if (inBounds(doorX, doorY)) {
            map[doorY][doorX].objectId = O_DOOR_CLOSED;
            map[doorY][doorX].objectHp = objectDefs[O_DOOR_CLOSED].durability;
        }

        // Windows
        placeWindows(bx, by, b.w, b.h, b.doorNorth, doorX);

        // Internal room divider
        if (b.divided) placeRoomDivider(bx, by, b.h, b.w / 2);

        // Furniture
        placeFurniture(bx, by, b.w, b.h, b.type, b.divided);

        // L-shaped dirt path: vertical from door to VCY row, then horizontal to VCX
        int startY = b.doorNorth ? doorY - 1 : doorY + 1;
        int stepY  = (VCY >= startY) ? 1 : -1;
        for (int y = startY; y != VCY + stepY; y += stepY) {
            if (!inBounds(doorX, y)) break;
            Tile& t = map[y][doorX];
            if (t.objectId < 0 && t.terrainId != T_FLOOR)
                t.terrainId = T_MUD;
        }
        int stepX = (VCX >= doorX) ? 1 : -1;
        for (int x = doorX; x != VCX + stepX; x += stepX) {
            if (!inBounds(x, VCY)) break;
            Tile& t = map[VCY][x];
            if (t.objectId < 0 && t.terrainId != T_FLOOR)
                t.terrainId = T_MUD;
        }
    }

    // Central stone plaza
    for (int y = VCY - 4; y <= VCY + 4; y++)
        for (int x = VCX - 4; x <= VCX + 4; x++)
            if (inBounds(x, y)) {
                map[y][x].terrainId = T_STONE;
                map[y][x].groundId  = -1;
                if (map[y][x].objectId != O_WALL)
                    map[y][x].objectId = -1;
            }

    // Well in the center of the plaza
    map[VCY][VCX].objectId = O_WELL;
    map[VCY][VCX].objectHp = 0;  // indestructible

    placeLamps(VCX, VCY);

    // West farmstead: house at far west, field between house and village
    placeFarmstead(VCX-47, VCY-4, 9, 9,   // farmhouse
                   VCX-38, VCY-4, 10, 9,  // field (east of house, door faces east)
                   false, secX, secY);

    // East farmstead: field between village and house, house at far east
    placeFarmstead(VCX+39, VCY-4, 9, 9,  // farmhouse
                   VCX+29, VCY-4, 10, 9, // field (west of house, door faces west)
                   true, secX, secY);
}

// ---------------------------------------------------------------- sector generation

void generateSector(BiomeType biome, int seedX, int seedY, bool isVillage) {
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
            t.plantAge  = 0;
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
            spawnObject(t, x, y, seedX, seedY);
        }
    }

    if (isVillage) placeVillage(seedX, seedY);
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

void updateVisibility(int radius, int lampRadius) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            map[y][x].visible = false;

    // Player FOV
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int tx = player.x + dx;
            int ty = player.y + dy;
            if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT) continue;
            if (dx * dx + dy * dy <= radius * radius) {
                if (hasLineOfSight(player.x, player.y, tx, ty)) {
                    map[ty][tx].visible  = true;
                    map[ty][tx].explored = true;
                }
            }
        }
    }

    // World light pass: each O_LAMP illuminates its own radius
    if (lampRadius <= 0) return;
    for (int ly = 0; ly < MAP_HEIGHT; ly++) {
        for (int lx = 0; lx < MAP_WIDTH; lx++) {
            if (map[ly][lx].objectId != O_LAMP) continue;
            for (int dy = -lampRadius; dy <= lampRadius; dy++) {
                for (int dx = -lampRadius; dx <= lampRadius; dx++) {
                    if (dx * dx + dy * dy > lampRadius * lampRadius) continue;
                    int tx = lx + dx, ty = ly + dy;
                    if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT) continue;
                    if (hasLineOfSight(lx, ly, tx, ty)) {
                        map[ty][tx].visible  = true;
                        map[ty][tx].explored = true;
                    }
                }
            }
        }
    }
}
