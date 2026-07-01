#pragma once
#include <SDL2/SDL.h>

// ================================================================ Terrain IDs
enum TerrainId {
    T_BEDROCK = 0,
    T_GRASSLAND,
    T_FOREST_FLOOR,
    T_SAND,
    T_SWAMP,
    T_MUD,
    T_STONE,
    T_SNOW,
    T_WATER,
    T_FLOOR,
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
    O_WALL,
    O_DOOR,
    O_DOOR_CLOSED,
    O_WINDOW,
    O_TABLE,
    O_BED,
    O_BARREL,
    O_FIREPLACE,
    O_LAMP,
    O_WELL,
    O_WHEAT,
    O_HERB,
    O_MUSHROOM,
    O_COUNT
};

// ================================================================ Type definitions

struct TerrainDef {
    const char* name;
    const char* description;
    const char* symbol;
    SDL_Color   color;
    int         moveCost;   // 0 = impassable
};

struct GroundDef {
    const char* name;
    const char* description;
    const char* symbol;
    SDL_Color   color;
    int         moveCostMod;
};

struct ObjectDef {
    const char* name;
    const char* description;
    const char* symbol;
    SDL_Color   color;
    int         moveCostMod;  // used only when !blocksMove
    bool        blocksMove;
    bool        blocksVision;
    int         durability;   // max HP; 0 = indestructible
    bool        isPlant     = false; // uses plantAge for growth, instant harvest
    int         growSeasons = 0;     // bitmask: bit0=spring bit1=summer bit2=autumn bit3=winter
    int         daysToMature= 0;     // in-game days to mature in growing season
};

// ================================================================ Data tables

static const TerrainDef terrainDefs[T_COUNT] = {
  { "Bedrock",      "Solid rock foundation. Completely impassable.",
    "#", {100,100,100,255},  0   },
  { "Grassland",    "Open meadow with firm, even ground.",
    ".", { 34,139, 34,255},  100 },
  { "Forest floor", "Dark, rich soil beneath a dense forest canopy.",
    ".", { 20, 80, 20,255},  110 },
  { "Sand",         "Fine desert sand. Soft and tiring to walk through.",
    ".", {210,180,140,255},  150 },
  { "Swamp",        "Murky, waterlogged ground. Exhausting to traverse.",
    "~", { 47, 79, 47,255},  200 },
  { "Mud",          "Thick, wet mud that clings to boots.",
    ".", {101, 67, 33,255},  170 },
  { "Stone",        "Rocky terrain. Firm but uneven underfoot.",
    ".", {120,120,120,255},  120 },
  { "Snow",         "A blanket of fresh snow muffles all sound.",
    ".", {220,235,255,255},  130 },
  { "Water",        "Deep water. Impassable without swimming.",
    "~", { 30, 80,200,255},  0   },
  { "Stone floor",  "Flat stone paving worn smooth by foot traffic.",
    ".", {160,150,130,255},  100 },
};

static const GroundDef groundDefs[G_COUNT] = {
  { "Grass",      "Short, lush green grass.",
    ".", { 60,179, 60,255},  20  },
  { "Tall grass", "Knee-high grass that tangles around your legs.",
    "\"",{ 50,160, 50,255},  100 },
  { "Flower",     "A small wildflower nodding in the breeze.",
    "*", {255,200, 80,255},  10  },
  { "Moss",       "Soft, damp moss covering the ground.",
    ".", { 85,107, 47,255},  30  },
  { "Reeds",      "Tall reeds growing from the wet ground.",
    "|", {107,142, 35,255},  50  },
  { "Leaves",     "A thick carpet of fallen autumn leaves.",
    ".", {160,100, 30,255},  20  },
};

static const ObjectDef objectDefs[O_COUNT] = {
//  name          description                                          sym  color               movMod  blkMv  blkVis  dur
  { "Tree",       "A tall tree with thick, rough bark.",              "T", {  0,100,  0,255},   0,     true,  true,   100 },
  { "Dead tree",  "A leafless, rotting tree. Still blocks the way.",  "T", {100, 80, 60,255},   0,     true,  false,   60 },
  { "Bush",       "A thorny bush. Slows movement but not impassable.","%" ,{  0,130,  0,255},  50,     false, false,   30, true, 7, 15 },
  { "Rock",       "A large rock embedded in the ground.",             "o", {140,140,130,255},   0,     true,  false,  200 },
  { "Boulder",    "A massive boulder. Nothing short of a giant moves this.",
                                                                      "O", {110,110,100,255},   0,     true,  true,   500 },
  { "Fallen log", "A fallen tree trunk lying across the ground.",     "=", {120, 80, 40,255},  80,     false, false,   80 },
  { "Wall",       "A sturdy stone wall.",                            "#", {130,120,100,255},   0,     true,  true,   300 },
  { "Door",        "A wooden door, standing open.",                   "/", {160,110, 50,255},   0, false, false,  50 },
  { "Closed door", "A sturdy wooden door, closed shut.",             "+", {140, 90, 40,255},   0, true,  true,   50 },
  { "Window",      "A small window set into the wall.",              "-", {140,200,220,255},   0, true,  false,  20 },
  { "Table",       "A wooden table.",                                "T", {180,130, 60,255},   0, true,  false,  30 },
  { "Bed",         "A simple straw bed.",                            "b", {200,180,140,255},   0, true,  false,  20 },
  { "Barrel",      "A wooden barrel.",                               "0", {120, 80, 40,255},   0, true,  false,  40 },
  { "Fireplace",   "A stone fireplace, warm and crackling.",         "^", {220,100, 30,255},   0, true,  false, 100 },
  { "Lamp post",   "A wooden post with an oil lantern on top.",     "i", {255,210, 60,255},   0, true,  false,  25 },
  { "Well",        "A stone well. Cold, clear water inside.",       "W", {100,140,180,255},   0, true,  false,   0 },
  { "Wheat",       "Tall stalks of golden wheat, ready to harvest.","\"",{210,185, 60,255},  30, false, false,   5, true, 6, 20 },
  { "Herb",        "A cluster of medicinal herbs with a sharp scent.", ":", { 80,160, 60,255},  10, false, false,   8, true, 3, 10 },
  { "Mushroom",    "Dark mushrooms growing from the damp soil.",       "n", {160, 90, 40,255},  10, false, false,  10, true, 4,  8 },
};

// ================================================================ Biome type

enum class BiomeType {
    PLAINS,
    FOREST,
    SWAMP,
    DESERT,
    TUNDRA,
    CURSED_LANDS,
};
