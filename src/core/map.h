#pragma once
#include "terrain.h"
#include "astar.h"
#include "actor.h"
#include <vector>

// ================================================================ Village buildings
// Records what map.cpp actually built during placeVillage(), so main.cpp can assign
// occupations to whoever's bed ends up in each building instead of guessing from position.
// Kept independent of npc.h/Occupation — map.cpp doesn't need to know about NPCs.

enum class BuildingRole { FARM, HERBALIST_FARM, SMITHY, ELDER, WOODCUTTER };

struct VillageBuildingInfo {
    int          bedX, bedY; // one representative bed tile inside this building
    BuildingRole role;
};

extern std::vector<VillageBuildingInfo> villageBuildings;

// ================================================================ Tile
// Three independent layers: terrain (ground type), ground cover, object.
// Derived properties (walkable, moveCost, etc.) are computed from these.

struct Tile {
    int     terrainId = T_GRASSLAND;
    int     groundId  = -1;   // -1 = none
    int     objectId  = -1;   // -1 = none
    int     objectHp  = 0;    // current HP of the object on this tile
    uint8_t plantAge  = 0;    // growth 0-255 for isPlant objects (170+ = mature)
    bool    visible   = false;
    bool    explored  = false;

    // Is this tile passable at all?
    bool walkable() const {
        if (terrainDefs[terrainId].moveCost == 0) return false;
        if (objectId >= 0 && objectDefs[objectId].blocksMove) return false;
        return true;
    }

    // Does this tile block line-of-sight? Separate from impassability
    // (moveCost==0) — a low Rock blocks movement but not sight (ObjectDef
    // already splits blocksMove/blocksVision the same way), and conversely
    // Water is walkable (swimming — high moveCost, not 0) yet was very
    // nearly opaque too before terrainDefs got its own blocksVision field;
    // only Bedrock actually blocks sight among terrain types.
    bool blocksVision() const {
        if (terrainDefs[terrainId].blocksVision) return true;
        if (objectId >= 0 && objectDefs[objectId].blocksVision) return true;
        return false;
    }

    // Total movement cost (used by A*).
    // Returns 0 if impassable.
    int moveCost() const {
        int cost = terrainDefs[terrainId].moveCost;
        if (cost == 0) return 0;
        if (groundId >= 0) cost += groundDefs[groundId].moveCostMod;
        if (objectId >= 0 && !objectDefs[objectId].blocksMove)
            cost += objectDefs[objectId].moveCostMod;
        return cost;
    }

    // Visual symbol: object > ground cover > terrain
    const char* symbol() const {
        if (objectId >= 0) return objectDefs[objectId].symbol;
        if (groundId  >= 0) return groundDefs[groundId].symbol;
        return terrainDefs[terrainId].symbol;
    }

    // Visual color: same priority as symbol
    SDL_Color color() const {
        if (objectId >= 0) return objectDefs[objectId].color;
        if (groundId  >= 0) return groundDefs[groundId].color;
        return terrainDefs[terrainId].color;
    }
};

// ================================================================ Functions

void generateSector(BiomeType biome = BiomeType::FOREST, int seedX = -1, int seedY = -1, bool isVillage = false,
                     bool hasRiver = false, int flowDX = 0, int flowDY = 0, int riverFromDX = 0, int riverFromDY = 0,
                     bool hasRoad = false, int roadDX = 0, int roadDY = 0, int roadFromDX = 0, int roadFromDY = 0);
bool hasLineOfSight(int x0, int y0, int x1, int y1);
void updateVisibility(int radius = VIEW_RADIUS, int lampRadius = 0);
