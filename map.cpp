#include "map.h"
#include "time_system.h"
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <utility>
#include <vector>

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

std::vector<VillageBuildingInfo> villageBuildings;

enum class Dir { NORTH, SOUTH, EAST, WEST };

struct BldgRect { int x, y, w, h; };

static bool rectsClash(const BldgRect& a, const BldgRect& b, int margin) {
    return !(a.x + a.w + margin <= b.x || b.x + b.w + margin <= a.x ||
             a.y + a.h + margin <= b.y || b.y + b.h + margin <= a.y);
}

static void putFurniture(int x, int y, int oid) {
    if (!inBounds(x, y)) return;
    if (map[y][x].terrainId != T_FLOOR) return;
    if (map[y][x].objectId >= 0) return;
    map[y][x].objectId = oid;
    map[y][x].objectHp = objectDefs[oid].durability;
}

// Place a window tile — only if the tile is already a wall.
static void putWindow(int x, int y) {
    if (!inBounds(x, y)) return;
    if (map[y][x].objectId != O_WALL) return;
    map[y][x].objectId = O_WINDOW;
    map[y][x].objectHp = objectDefs[O_WINDOW].durability;
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

// Dirt path from a door to the village plaza, routed as an L-shape whose long
// leg matches whichever way the door faces (so it never cuts through the wall).
static void pathToPlaza(int doorX, int doorY, Dir doorDir, int VCX, int VCY) {
    auto paint = [&](int x, int y) {
        if (!inBounds(x, y)) return;
        Tile& t = map[y][x];
        if (t.objectId < 0 && t.terrainId != T_FLOOR) t.terrainId = T_MUD;
    };

    if (doorDir == Dir::NORTH || doorDir == Dir::SOUTH) {
        int startY = (doorDir == Dir::NORTH) ? doorY - 1 : doorY + 1;
        int stepY  = (VCY >= startY) ? 1 : -1;
        for (int y = startY; y != VCY + stepY; y += stepY) paint(doorX, y);
        int stepX = (VCX >= doorX) ? 1 : -1;
        for (int x = doorX; x != VCX + stepX; x += stepX) paint(x, VCY);
    } else {
        int startX = (doorDir == Dir::WEST) ? doorX - 1 : doorX + 1;
        int stepX  = (VCX >= startX) ? 1 : -1;
        for (int x = startX; x != VCX + stepX; x += stepX) paint(x, doorY);
        int stepY = (VCY >= doorY) ? 1 : -1;
        for (int y = doorY; y != VCY + stepY; y += stepY) paint(VCX, y);
    }
}

// Places a farmhouse + adjacent crop field on the given side of the village.
static void placeFarmstead(Dir dir, int VCX, int VCY, int secX, int secY, BuildingRole role) {
    int hw = 9, hh = 9;
    int hx, hy, fx, fy, fw, fh, doorX, doorY;
    Dir doorDir;

    switch (dir) {
        case Dir::WEST:
            hx = VCX - 47; hy = VCY - 4; fw = 10; fh = 9; fx = VCX - 38; fy = VCY - 4;
            doorX = hx + hw - 1; doorY = hy + hh / 2; doorDir = Dir::EAST;
            break;
        case Dir::EAST:
            hx = VCX + 39; hy = VCY - 4; fw = 10; fh = 9; fx = VCX + 29; fy = VCY - 4;
            doorX = hx; doorY = hy + hh / 2; doorDir = Dir::WEST;
            break;
        case Dir::NORTH:
            hx = VCX - 4; hy = VCY - 47; fw = 9; fh = 10; fx = VCX - 4; fy = VCY - 38;
            doorX = hx + hw / 2; doorY = hy + hh - 1; doorDir = Dir::SOUTH;
            break;
        default: // SOUTH
            hx = VCX - 4; hy = VCY + 39; fw = 9; fh = 10; fx = VCX - 4; fy = VCY + 29;
            doorX = hx + hw / 2; doorY = hy; doorDir = Dir::NORTH;
            break;
    }

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

    // Door facing the field/village
    if (inBounds(doorX, doorY)) {
        map[doorY][doorX].objectId = O_DOOR_CLOSED;
        map[doorY][doorX].objectHp = objectDefs[O_DOOR_CLOSED].durability;
    }

    // Windows: wall opposite the door + the two side walls
    switch (dir) {
        case Dir::WEST:  putWindow(hx, hy+hh/2);      break;
        case Dir::EAST:  putWindow(hx+hw-1, hy+hh/2); break;
        case Dir::NORTH: putWindow(hx+hw/2, hy);      break;
        default:         putWindow(hx+hw/2, hy+hh-1); break;
    }
    putWindow(hx+hw/2, hy);
    putWindow(hx+hw/2, hy+hh-1);

    // Furniture: beds away from door, table in middle, barrel in the back corner
    int bedX, bedY, tableX, tableY, barrelX, barrelY;
    switch (dir) {
        case Dir::WEST: // door on east wall -> back = west side
            bedX=hx+1; bedY=hy+1; tableX=hx+2; tableY=hy+hh/2; barrelX=hx+1; barrelY=hy+hh-2;
            break;
        case Dir::EAST: // door on west wall -> back = east side
            bedX=hx+hw-2; bedY=hy+1; tableX=hx+hw-3; tableY=hy+hh/2; barrelX=hx+hw-2; barrelY=hy+hh-2;
            break;
        case Dir::NORTH: // door on south wall -> back = north side
            bedX=hx+1; bedY=hy+1; tableX=hx+hw/2; tableY=hy+2; barrelX=hx+hw-2; barrelY=hy+1;
            break;
        default: // SOUTH: door on north wall -> back = south side
            bedX=hx+1; bedY=hy+hh-2; tableX=hx+hw/2; tableY=hy+hh-3; barrelX=hx+hw-2; barrelY=hy+hh-2;
            break;
    }
    putFurniture(bedX, bedY, O_BED);
    putFurniture(bedX + (dir==Dir::NORTH||dir==Dir::SOUTH ? 1 : 0),
                 bedY + (dir==Dir::NORTH||dir==Dir::SOUTH ? 0 : 1), O_BED);
    putFurniture(tableX, tableY, O_TABLE);
    putFurniture(barrelX, barrelY, O_BARREL);

    // Crop field: even rows = wheat, odd rows with spacing = herb — everyone plants a
    // little of everything for the table, on top of whatever their specialty is.
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

    pathToPlaza(doorX, doorY, doorDir, VCX, VCY);
    villageBuildings.push_back({bedX, bedY, role});
}

// Places a single-room occupation building (Smithy/Elder/Woodcutter) at a random
// position and distance band around the village center, door facing back toward it,
// rejecting overlaps against everything already placed. Returns false if it couldn't
// find room after several tries (village stays smaller — no building forced in).
static bool placeRoleBuilding(BuildingRole role, int VCX, int VCY,
                              int minDist, int maxDist, int hw, int hh,
                              std::vector<BldgRect>& footprints) {
    for (int attempt = 0; attempt < 30; attempt++) {
        double angle = (rand() % 360) * 3.14159265 / 180.0;
        int    dist  = minDist + rand() % (maxDist - minDist + 1);
        int    cx    = VCX + (int)(std::cos(angle) * dist);
        int    cy    = VCY + (int)(std::sin(angle) * dist);
        int    hx    = cx - hw / 2;
        int    hy    = cy - hh / 2;
        if (hx < 5 || hy < 5 || hx + hw >= MAP_WIDTH - 5 || hy + hh >= MAP_HEIGHT - 5) continue;

        BldgRect candidate = {hx, hy, hw, hh};
        bool clash = false;
        for (const BldgRect& f : footprints)
            if (rectsClash(candidate, f, 3)) { clash = true; break; }
        if (clash) continue;

        // Door goes on whichever wall faces back toward the village center.
        int dxToCenter = VCX - cx, dyToCenter = VCY - cy;
        Dir doorDir = (abs(dxToCenter) > abs(dyToCenter))
                    ? (dxToCenter > 0 ? Dir::EAST : Dir::WEST)
                    : (dyToCenter > 0 ? Dir::SOUTH : Dir::NORTH);

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

        int doorX, doorY;
        switch (doorDir) {
            case Dir::NORTH: doorX = hx+hw/2; doorY = hy;      break;
            case Dir::SOUTH: doorX = hx+hw/2; doorY = hy+hh-1; break;
            case Dir::EAST:  doorX = hx+hw-1; doorY = hy+hh/2; break;
            default:         doorX = hx;      doorY = hy+hh/2; break; // WEST
        }
        if (inBounds(doorX, doorY)) {
            map[doorY][doorX].objectId = O_DOOR_CLOSED;
            map[doorY][doorX].objectHp = objectDefs[O_DOOR_CLOSED].durability;
        }

        // Windows: wall opposite the door + the two side walls
        switch (doorDir) {
            case Dir::NORTH: putWindow(hx+hw/2, hy+hh-1); break;
            case Dir::SOUTH: putWindow(hx+hw/2, hy);      break;
            case Dir::EAST:  putWindow(hx, hy+hh/2);      break;
            default:         putWindow(hx+hw-1, hy+hh/2); break;
        }
        putWindow(hx+hw/2, hy);
        putWindow(hx+hw/2, hy+hh-1);

        // Furniture at fixed interior offsets — safe regardless of door side, since the
        // door always sits on a wall tile, never on these interior ones.
        int bedX = hx+2, bedY = hy+1;
        switch (role) {
            case BuildingRole::SMITHY:
                putFurniture(bedX, bedY, O_BED);
                putFurniture(hx+hw-3, hy+hh-2, O_TABLE);
                putFurniture(hx+hw-3, hy+1, O_ANVIL);
                break;
            case BuildingRole::ELDER:
                putFurniture(bedX, bedY, O_BED);
                putFurniture(bedX+1, bedY, O_BED);
                putFurniture(hx+2, hy+hh-2, O_TABLE);
                putFurniture(hx+hw-3, hy+hh-2, O_TABLE);
                break;
            case BuildingRole::WOODCUTTER:
                putFurniture(bedX, bedY, O_BED);
                putFurniture(hx+hw-3, hy+hh-2, O_TABLE);
                putFurniture(hx+hw-3, hy+1, O_BARREL);
                // A couple of felled logs just outside the door — his own cut stock.
                {
                    int lx = doorX, ly = doorY;
                    switch (doorDir) {
                        case Dir::NORTH: ly -= 2; break;
                        case Dir::SOUTH: ly += 2; break;
                        case Dir::EAST:  lx += 2; break;
                        default:         lx -= 2; break;
                    }
                    putObject(lx, ly, O_FALLEN_LOG);
                    putObject(lx + (doorDir==Dir::NORTH||doorDir==Dir::SOUTH ? 1 : 0),
                              ly + (doorDir==Dir::NORTH||doorDir==Dir::SOUTH ? 0 : 1), O_FALLEN_LOG);
                }
                break;
            default: break;
        }

        pathToPlaza(doorX, doorY, doorDir, VCX, VCY);
        footprints.push_back(candidate);
        villageBuildings.push_back({bedX, bedY, role});
        return true;
    }
    return false;
}

// Approximate bounding box covering a farmstead's house + field, for overlap checks.
static BldgRect farmFootprint(Dir dir, int VCX, int VCY) {
    switch (dir) {
        case Dir::WEST:  return {VCX-47, VCY-4, 19, 9};
        case Dir::EAST:  return {VCX+29, VCY-4, 19, 9};
        case Dir::NORTH: return {VCX-4, VCY-47, 9, 19};
        default:         return {VCX-4, VCY+29, 9, 19}; // SOUTH
    }
}

static void placeVillage(int secX, int secY) {
    const int VCX = MAP_WIDTH  / 2;
    const int VCY = MAP_HEIGHT / 2;

    villageBuildings.clear();

    // Wipe a 50-tile radius clean: remove trees, rocks, ground cover
    for (int y = VCY - 50; y <= VCY + 50; y++)
        for (int x = VCX - 50; x <= VCX + 50; x++)
            if (inBounds(x, y)) {
                map[y][x].objectId  = -1;
                map[y][x].objectHp  = 0;
                map[y][x].groundId  = -1;
                map[y][x].terrainId = T_GRASSLAND;
            }

    // Randomize which 2 of the 4 cardinal directions get a farmstead this village.
    Dir dirs[4] = { Dir::NORTH, Dir::SOUTH, Dir::EAST, Dir::WEST };
    for (int i = 3; i > 0; i--) { int j = rand() % (i + 1); std::swap(dirs[i], dirs[j]); }
    bool herbalist = (rand() % 100) < 45;

    placeFarmstead(dirs[0], VCX, VCY, secX, secY, BuildingRole::FARM);
    placeFarmstead(dirs[1], VCX, VCY, secX, secY,
                   herbalist ? BuildingRole::HERBALIST_FARM : BuildingRole::FARM);

    std::vector<BldgRect> footprints = {
        farmFootprint(dirs[0], VCX, VCY),
        farmFootprint(dirs[1], VCX, VCY),
        {VCX-6, VCY-6, 13, 13}, // reserve the plaza
    };

    // Elder's hall sits close to the plaza; smithy and woodcutter's lodge range further out.
    placeRoleBuilding(BuildingRole::ELDER,      VCX, VCY, 14, 22, 12, 9, footprints);
    placeRoleBuilding(BuildingRole::SMITHY,     VCX, VCY, 22, 42,  9, 8, footprints);
    placeRoleBuilding(BuildingRole::WOODCUTTER, VCX, VCY, 22, 42,  9, 8, footprints);

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
