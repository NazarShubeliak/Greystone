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
// The field sits beside the house (left or right of the door, chosen at random),
// never in front of it — so reaching the door never means crossing the crop rows.
static BldgRect placeFarmstead(Dir dir, int VCX, int VCY, int secX, int secY, BuildingRole role) {
    int hw = 9, hh = 9;
    int hx, hy, doorX, doorY;
    Dir doorDir;

    switch (dir) {
        case Dir::WEST:
            hx = VCX - 47; hy = VCY - 4;
            doorX = hx + hw - 1; doorY = hy + hh/2; doorDir = Dir::EAST;
            break;
        case Dir::EAST:
            hx = VCX + 39; hy = VCY - 4;
            doorX = hx; doorY = hy + hh/2; doorDir = Dir::WEST;
            break;
        case Dir::NORTH:
            hx = VCX - 4; hy = VCY - 47;
            doorX = hx + hw/2; doorY = hy + hh - 1; doorDir = Dir::SOUTH;
            break;
        default: // SOUTH
            hx = VCX - 4; hy = VCY + 39;
            doorX = hx + hw/2; doorY = hy; doorDir = Dir::NORTH;
            break;
    }

    // Field goes on one of the two sides perpendicular to the door (north/south of
    // an east/west-facing house, or east/west of a north/south-facing one).
    bool sideB = (rand() % 2) != 0;
    int fx, fy, fw, fh;
    if (dir == Dir::WEST || dir == Dir::EAST) {
        fw = hw; fh = 9;
        fx = hx;
        fy = sideB ? (hy + hh + 1) : (hy - fh - 1);
    } else {
        fw = 9; fh = hh;
        fy = hy;
        fx = sideB ? (hx + hw + 1) : (hx - fw - 1);
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

    int minX = std::min(hx, fx), minY = std::min(hy, fy);
    int maxX = std::max(hx+hw, fx+fw), maxY = std::max(hy+hh, fy+fh);
    return {minX, minY, maxX-minX, maxY-minY};
}

// Small household garden — every home grows a little food now, not just
// the farmsteads (docs/village.md; user request after simulatedays
// testing showed non-farm households running out of both their bag and
// their one-time starting gold with no food source of their own at all —
// a farm's field is just the bigger version of the same mechanic, not a
// different one). Same mixed wheat/herb mud patch as placeFarmstead()'s
// field, just much smaller. Best-effort: skips any tile something else
// (a decoration placed earlier, a neighboring building) already claims
// instead of overwriting it, rather than aborting the whole patch.
static void placeGarden(int gx0, int gy0, int gw, int gh, int secX, int secY) {
    for (int y = gy0; y < gy0 + gh; y++)
        for (int x = gx0; x < gx0 + gw; x++) {
            if (!inBounds(x, y)) continue;
            if (map[y][x].objectId >= 0 || !map[y][x].walkable()) continue;
            map[y][x].terrainId = T_MUD;
            map[y][x].groundId  = -1;
            if ((x + y) % 2 == 0) {
                map[y][x].objectId = O_WHEAT;
                map[y][x].objectHp = objectDefs[O_WHEAT].durability;
                map[y][x].plantAge = calcPlantAge(x, y, secX, secY,
                    objectDefs[O_WHEAT].growSeasons, objectDefs[O_WHEAT].daysToMature);
            } else {
                map[y][x].objectId = O_HERB;
                map[y][x].objectHp = objectDefs[O_HERB].durability;
                map[y][x].plantAge = calcPlantAge(x, y, secX, secY,
                    objectDefs[O_HERB].growSeasons, objectDefs[O_HERB].daysToMature);
            }
        }
}

// Places a single-room occupation building (Smithy/Elder/Woodcutter) at a random
// position and distance band around the village center, door facing back toward it,
// rejecting overlaps against everything already placed. Returns false if it couldn't
// find room after several tries (village stays smaller — no building forced in).
static bool placeRoleBuilding(BuildingRole role, int VCX, int VCY, int secX, int secY,
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
        // Unit vector pointing straight out of the door, and its perpendicular —
        // used to place outdoor decoration beside the entrance instead of blocking it.
        int outX=0, outY=0, perpX=0, perpY=0;
        switch (doorDir) {
            case Dir::NORTH: outY=-1; perpX=1; break;
            case Dir::SOUTH: outY= 1; perpX=1; break;
            case Dir::EAST:  outX= 1; perpY=1; break;
            default:         outX=-1; perpY=1; break; // WEST
        }

        switch (role) {
            case BuildingRole::SMITHY: {
                putFurniture(bedX, bedY, O_BED);
                putFurniture(hx+hw-3, hy+hh-2, O_TABLE);

                // Forge yard beside the entrance (left or right, picked at random) —
                // furnace, anvil and supply crates, not crammed into the bedroom.
                int side = (rand() % 2) ? 1 : -1;
                int fyX = doorX + outX*2 + perpX*side*3;
                int fyY = doorY + outY*2 + perpY*side*3;
                putObject(fyX, fyY, O_FIREPLACE);
                putObject(fyX + perpX*side,   fyY + perpY*side,   O_ANVIL);
                putObject(fyX - perpX*side,   fyY - perpY*side,   O_BARREL);
                putObject(fyX + perpX*side*2, fyY + perpY*side*2, O_BARREL);
                break;
            }
            case BuildingRole::ELDER:
                putFurniture(bedX, bedY, O_BED);
                putFurniture(bedX+1, bedY, O_BED);
                putFurniture(hx+2, hy+hh-2, O_TABLE);
                putFurniture(hx+hw-3, hy+hh-2, O_TABLE);
                // Unlike Smithy/Woodcutter, the Elder's hall never had a
                // barrel at all — needed now so this household's granary
                // (spawnVillagers(), main.cpp) has something to attach to,
                // same as every other household.
                putFurniture(hx+hw-3, hy+1, O_BARREL);
                break;
            case BuildingRole::WOODCUTTER: {
                putFurniture(bedX, bedY, O_BED);
                putFurniture(hx+hw-3, hy+hh-2, O_TABLE);
                putFurniture(hx+hw-3, hy+1, O_BARREL);

                // A stack of felled logs beside the entrance, offset to the side
                // so it doesn't block the way in — plus a couple of stumps nearby
                // from whatever he's already cleared.
                int px = doorX + outX + perpX*2, py = doorY + outY + perpY*2;
                putObject(px,             py,             O_FALLEN_LOG);
                putObject(px + perpX,     py + perpY,     O_FALLEN_LOG);
                putObject(px + perpX*2,   py + perpY*2,   O_FALLEN_LOG);
                putObject(px + outX,               py + outY,               O_STUMP);
                putObject(px + perpX*3 + outX,     py + perpY*3 + outY,     O_STUMP);
                break;
            }
            default: break;
        }

        // Small household garden beside the building footprint (not
        // through the door, which pathToPlaza() below routes through) —
        // right edge by default, left if that would run off the map.
        // placeGarden()'s own best-effort tile-skip handles the rest (role
        // decoration placed above, a neighboring building).
        {
            int gw = 3, gh = 3;
            int gx0 = hx + hw + 1, gy0 = hy + (hh - gh) / 2;
            if (gx0 + gw >= MAP_WIDTH - 5) gx0 = hx - gw - 1;
            placeGarden(gx0, gy0, gw, gh, secX, secY);
        }

        pathToPlaza(doorX, doorY, doorDir, VCX, VCY);
        footprints.push_back(candidate);
        villageBuildings.push_back({bedX, bedY, role});
        return true;
    }
    return false;
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

    BldgRect farmA = placeFarmstead(dirs[0], VCX, VCY, secX, secY, BuildingRole::FARM);
    BldgRect farmB = placeFarmstead(dirs[1], VCX, VCY, secX, secY,
                   herbalist ? BuildingRole::HERBALIST_FARM : BuildingRole::FARM);

    std::vector<BldgRect> footprints = {
        farmA, farmB,
        {VCX-6, VCY-6, 13, 13}, // reserve the plaza
    };

    // Elder's hall sits close to the plaza; smithy and woodcutter's lodge range further out.
    placeRoleBuilding(BuildingRole::ELDER,      VCX, VCY, secX, secY, 14, 22, 12, 9, footprints);
    placeRoleBuilding(BuildingRole::SMITHY,     VCX, VCY, secX, secY, 22, 42,  9, 8, footprints);
    placeRoleBuilding(BuildingRole::WOODCUTTER, VCX, VCY, secX, secY, 22, 42,  9, 8, footprints);

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

// Deterministic, order-independent hash of two adjacent overmap sector
// coordinates -> an offset in [lo,hi). Both sectors on either side of a
// shared boundary call this with the same pair (order doesn't matter — the
// hash normalizes it), so they agree on exactly where a river/road crosses
// instead of each independently jittering its own entry/exit point (which
// is what made the same river/road look crooked or jump sideways right at
// a sector seam — user report). `salt` keeps rivers and roads from
// correlating on a sector that happens to have both.
static int crossingCoord(int sxA, int syA, int sxB, int syB, unsigned salt, int lo, int hi) {
    int loX = std::min(sxA, sxB), hiX = std::max(sxA, sxB);
    int loY = std::min(syA, syB), hiY = std::max(syA, syB);
    unsigned h = (unsigned)loX * 73856093u ^ (unsigned)loY * 19349663u
               ^ (unsigned)hiX * 83492791u ^ (unsigned)hiY * 51402917u ^ salt;
    return lo + (int)(h % (unsigned)std::max(1, hi - lo));
}

// Picks the point on THIS sector's map where a river/road crosses toward
// neighbour direction (dX,dY) — a unit step; (0,0) means "no crossing here"
// (a river source/lake, or a road's start/end at a village) and returns the
// map center instead. For a straight (non-diagonal) neighbour the
// perpendicular coordinate comes from crossingCoord(), so it matches
// whatever the neighbour sector computes for the same boundary; a diagonal
// neighbour's crossing is just the fixed corner near that direction — no
// hash needed there, both sides compute the identical corner from the
// direction alone.
static SDL_Point edgePoint(int seedX, int seedY, int dX, int dY, unsigned salt) {
    const int EDGE_MARGIN = 3;
    if (dX == 0 && dY == 0) return SDL_Point{MAP_WIDTH / 2, MAP_HEIGHT / 2};
    int px, py;
    if (dX != 0) px = (dX > 0) ? MAP_WIDTH - EDGE_MARGIN : EDGE_MARGIN;
    else         px = crossingCoord(seedX, seedY, seedX + dX, seedY + dY, salt, EDGE_MARGIN, MAP_WIDTH - EDGE_MARGIN);
    if (dY != 0) py = (dY > 0) ? MAP_HEIGHT - EDGE_MARGIN : EDGE_MARGIN;
    else         py = crossingCoord(seedX, seedY, seedX + dX, seedY + dY, salt, EDGE_MARGIN, MAP_HEIGHT - EDGE_MARGIN);
    return SDL_Point{px, py};
}

static constexpr unsigned RIVER_EDGE_SALT = 0x9E3779B9u;
static constexpr unsigned ROAD_EDGE_SALT  = 0x517CC1B7u;

// Physically carves a river/lake through this sector — called only when this
// sector was traced as isWater in Overmap::traceRivers() (overmap.h). A lake
// terminus (flowDX==0 && flowDY==0) is a big irregular pond near the center;
// a flowing river wanders from its entry point (edgePoint() on the upstream
// side, fromDX/fromDY) to its exit point (edgePoint() on the downstream
// side, flowDX/flowDY) via a handful of overlapping paintPatch() calls along
// a jittered path — both endpoints hashed against the actual neighbour
// sector's coordinates (edgePoint()/crossingCoord()), so this sector's exit
// lines up with the next one's entry instead of each guessing independently.
static void carveRiver(int seedX, int seedY, int flowDX, int flowDY, int fromDX, int fromDY) {
    if (flowDX == 0 && flowDY == 0) {
        int cx = MAP_WIDTH / 2 + (rand() % 21 - 10);
        int cy = MAP_HEIGHT / 2 + (rand() % 21 - 10);
        for (int i = 0; i < 3; i++)
            paintPatch(cx + rand() % 15 - 7, cy + rand() % 15 - 7, 8 + rand() % 6, T_WATER);
        return;
    }

    SDL_Point startP = edgePoint(seedX, seedY, fromDX, fromDY, RIVER_EDGE_SALT);
    SDL_Point endP   = edgePoint(seedX, seedY, flowDX, flowDY, RIVER_EDGE_SALT);

    // Step distance is derived from the path's actual length (not a fixed
    // step COUNT) so consecutive patches always overlap into one continuous
    // band regardless of how long this particular sector's river run is —
    // a fixed 12-step count left wide gaps ("droplets") on longer runs,
    // since 3-5 tile-radius circles spaced ~12+ tiles apart never touch.
    const int   PATCH_R  = 4;
    const float STEP_LEN = 2.5f; // well under PATCH_R*2, guarantees overlap
    float dx = (float)(endP.x - startP.x), dy = (float)(endP.y - startP.y);
    float length = std::sqrt(dx * dx + dy * dy);
    int steps = std::max(1, (int)(length / STEP_LEN));

    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        // Gentle jitter — small enough relative to PATCH_R that consecutive
        // circles still overlap even at their most jittered.
        int px = startP.x + (int)(dx * t) + (rand() % 3 - 1);
        int py = startP.y + (int)(dy * t) + (rand() % 3 - 1);
        paintPatch(px, py, PATCH_R, T_WATER);
    }
}

// Physically carves a road through this sector (Overmap::buildRoads()) —
// same shared-boundary-hashed entry/exit approach as carveRiver() above
// (edgePoint()), just narrower (roads aren't rivers) and painting ground
// cover (G_ROAD) instead of swapping terrain, so grass/sand/etc. underneath
// stays visually varied. Clears wilderness objects in its way (trees,
// rocks, ...) via isClearableWildObject() below — a road cutting through a
// forest should actually look cleared, not just painted under the trees
// (user report) — but never touches anything built or farmed (wall, the
// indestructible well, crops...), same tolerance pathToPlaza() already has
// for the in-village dirt paths: route around it, don't bulldoze it. A
// village sector's road aims at the map center (same anchor
// placeVillage()/villageWellX/Y already use) instead of the far edge, so it
// visibly leads to the plaza.
//
// Wilderness objects spawnObject() can actually place (trees, bushes, rocks,
// boulders, fallen logs, wild herbs/mushrooms) — carveRoad() clears these so
// a road doesn't just run underneath a forest looking untouched. Explicitly
// NOT wall/door/table/bed/barrel/well/anvil/grave/stump/wheat — anything
// built or farmed, which a road should route around, not bulldoze.
static bool isClearableWildObject(int objectId) {
    switch (objectId) {
        case O_TREE: case O_DEAD_TREE: case O_BUSH: case O_ROCK:
        case O_BOULDER: case O_FALLEN_LOG: case O_HERB: case O_MUSHROOM:
            return true;
        default:
            return false;
    }
}

static void carveRoad(int seedX, int seedY, bool isVillage, int toDX, int toDY, int fromDX, int fromDY) {
    SDL_Point startP = edgePoint(seedX, seedY, fromDX, fromDY, ROAD_EDGE_SALT);
    SDL_Point endP   = isVillage ? SDL_Point{MAP_WIDTH / 2, MAP_HEIGHT / 2}
                                  : edgePoint(seedX, seedY, toDX, toDY, ROAD_EDGE_SALT);

    const int   PATCH_R  = 2;
    const float STEP_LEN = 1.5f;
    float dx = (float)(endP.x - startP.x), dy = (float)(endP.y - startP.y);
    float length = std::sqrt(dx * dx + dy * dy);
    int steps = std::max(1, (int)(length / STEP_LEN));

    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        int cx = startP.x + (int)(dx * t) + (rand() % 3 - 1);
        int cy = startP.y + (int)(dy * t) + (rand() % 3 - 1);
        for (int py = cy - PATCH_R; py <= cy + PATCH_R; py++) {
            for (int px = cx - PATCH_R; px <= cx + PATCH_R; px++) {
                if ((px - cx) * (px - cx) + (py - cy) * (py - cy) > PATCH_R * PATCH_R) continue;
                if (px <= 0 || px >= MAP_WIDTH - 1 || py <= 0 || py >= MAP_HEIGHT - 1) continue;
                Tile& tile = map[py][px];
                if (tile.terrainId == T_BEDROCK || tile.terrainId == T_WATER || tile.terrainId == T_FLOOR) continue;
                if (tile.objectId >= 0) {
                    if (!isClearableWildObject(tile.objectId)) continue; // building, well, crop, etc. — route around it, don't touch
                    tile.objectId = -1;
                    tile.objectHp = 0;
                }
                tile.groundId = G_ROAD;
            }
        }
    }
}

// ---------------------------------------------------------------- sector generation

void generateSector(BiomeType biome, int seedX, int seedY, bool isVillage,
                     bool hasRiver, int flowDX, int flowDY, int riverFromDX, int riverFromDY,
                     bool hasRoad, int roadDX, int roadDY, int roadFromDX, int roadFromDY) {
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

    // River/lake carved from the overmap's hydrology (Overmap::traceRivers()).
    if (hasRiver) carveRiver(seedX, seedY, flowDX, flowDY, riverFromDX, riverFromDY);

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

    // Road carved last, after village buildings — so it can be routed
    // around whatever's already there instead of the other way around.
    if (hasRoad) carveRoad(seedX, seedY, isVillage, roadDX, roadDY, roadFromDX, roadFromDY);
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
