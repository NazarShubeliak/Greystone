#pragma once
#include <SDL2/SDL.h>

// ================================================================ Terrain IDs
enum TerrainId {
    T_BEDROCK = 0,   // impassable map border / solid rock
    T_GRASSLAND,
    T_FOREST_FLOOR,
    T_SAND,
    T_SWAMP,
    T_MUD,
    T_STONE,
    T_SNOW,
    T_WATER,
    T_COUNT
};

// ================================================================ Ground cover IDs
enum GroundId {
    G_GRASS = 0,
    G_TALL_GRASS,
    G_FLOWER,
    G_MOSS,
    G_REEDS,
    G_LEAVES,
    G_COUNT
};

// ================================================================ Object IDs
enum ObjectId {
    O_TREE = 0,
    O_DEAD_TREE,
    O_BUSH,
    O_ROCK,
    O_BOULDER,
    O_FALLEN_LOG,
    O_COUNT
};

// ================================================================ Type definitions

struct TerrainDef {
    const char* name;
    const char* symbol;
    SDL_Color   color;
    int         moveCost;   // 0 = impassable
};

struct GroundDef {
    const char* name;
    const char* symbol;
    SDL_Color   color;
    int         moveCostMod;  // added to terrain cost
};

struct ObjectDef {
    const char* name;
    const char* symbol;
    SDL_Color   color;
    int         moveCostMod;  // used only when !blocksMove
    bool        blocksMove;
    bool        blocksVision;
};

// ================================================================ Data tables

static const TerrainDef terrainDefs[T_COUNT] = {
//   name             sym  color (R,G,B,A)                 moveCost
  { "Bedrock",        "#", {100, 100, 100, 255},            0   },
  { "Grassland",      ".", { 34, 139,  34, 255},            100 },
  { "Forest floor",   ".", { 20,  80,  20, 255},            110 },
  { "Sand",           ".", {210, 180, 140, 255},            150 },
  { "Swamp",          "~", { 47,  79,  47, 255},            200 },
  { "Mud",            ".", {101,  67,  33, 255},            170 },
  { "Stone",          ".", {120, 120, 120, 255},            120 },
  { "Snow",           ".", {220, 235, 255, 255},            130 },
  { "Water",          "~", { 30,  80, 200, 255},            0   },
};

static const GroundDef groundDefs[G_COUNT] = {
//   name            sym  color                              moveCostMod
  { "Grass",         ".", { 60, 179,  60, 255},             20  },
  { "Tall grass",    "\"",{ 50, 160,  50, 255},             100 },
  { "Flower",        "*", {255, 200,  80, 255},             10  },
  { "Moss",          ".", { 85, 107,  47, 255},             30  },
  { "Reeds",         "|", {107, 142,  35, 255},             50  },
  { "Leaves",        ".", {160, 100,  30, 255},             20  },
};

static const ObjectDef objectDefs[O_COUNT] = {
//   name           sym  color                    moveMod  blockMove  blockVis
  { "Tree",         "T", { 0, 100,   0, 255},     0,       true,      true  },
  { "Dead tree",    "T", {100,  80,  60, 255},     0,       true,      false },
  { "Bush",         "%", { 0, 130,   0, 255},      50,      false,     false },
  { "Rock",         "o", {140, 140, 130, 255},     0,       true,      false },
  { "Boulder",      "O", {110, 110, 100, 255},     0,       true,      true  },
  { "Fallen log",   "=", {120,  80,  40, 255},     80,      false,     false },
};

// ================================================================ Biome type
// Controls which terrains and objects get generated in a sector.

enum class BiomeType {
    PLAINS,
    FOREST,
    SWAMP,
    DESERT,
    TUNDRA,
    CURSED_LANDS,
};
