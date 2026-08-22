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
    G_SCORCHED,  // fire spells landing on flammable ground swap it to this (cosmetic only)
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
    O_ANVIL,
    O_STUMP,
    O_GRAVE,
    O_COUNT
};

// ================================================================ Material
// What an object is physically made of — determines how much a given school
// of magic actually damages it (see main.cpp's materialResistance()), e.g.
// fire chews through wood fast but barely scratches stone. Ground cover and
// terrain don't have this — only objects are ever "damaged" this way.
enum class Material { NONE, WOOD, STONE, METAL };

// ================================================================ Type definitions

struct TerrainDef {
    const char* name;
    const char* description;
    const char* symbol;
    SDL_Color   color;
    int         moveCost;      // 0 = impassable
    bool        blocksVision = false; // separate from moveCost==0 (ObjectDef already
                                       // splits blocksMove/blocksVision the same way) —
                                       // Water is impassable but see-through; only
                                       // Bedrock (solid rock) actually blocks sight
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
    Material    material    = Material::NONE; // main.cpp's materialResistance()
    bool        isPlant     = false; // uses plantAge for growth, instant harvest
    int         growSeasons = 0;     // bitmask: bit0=spring bit1=summer bit2=autumn bit3=winter
    int         daysToMature= 0;     // in-game days to mature in growing season
};

// ================================================================ Data tables

static const TerrainDef terrainDefs[T_COUNT] = {
  { "Bedrock",      "Solid rock foundation. Completely impassable.",
    "#", {100,100,100,255},  0,   true  },
  { "Grassland",    "Open meadow with firm, even ground.",
    ".", { 34,139, 34,255},  100, false },
  { "Forest floor", "Dark, rich soil beneath a dense forest canopy.",
    ".", { 20, 80, 20,255},  110, false },
  { "Sand",         "Fine desert sand. Soft and tiring to walk through.",
    ".", {210,180,140,255},  150, false },
  { "Swamp",        "Murky, waterlogged ground. Exhausting to traverse.",
    "~", { 47, 79, 47,255},  200, false },
  { "Mud",          "Thick, wet mud that clings to boots.",
    ".", {101, 67, 33,255},  170, false },
  { "Stone",        "Rocky terrain. Firm but uneven underfoot.",
    ".", {120,120,120,255},  120, false },
  { "Snow",         "A blanket of fresh snow muffles all sound.",
    ".", {220,235,255,255},  130, false },
  { "Water",        "Deep water. You can see across it, but crossing means swimming — exhausting, and dangerous if your stamina runs out.",
    "~", { 30, 80,200,255},  280, false },
  { "Stone floor",  "Flat stone paving worn smooth by foot traffic.",
    ".", {160,150,130,255},  100, false },
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
  { "Scorched ground", "Blackened earth, still smoking faintly where fire caught the grass.",
    ".", { 40, 35, 30,255},  15  },
};

static const ObjectDef objectDefs[O_COUNT] = {
//  name          description                                          sym  color               movMod  blkMv  blkVis  dur  material
  { "Tree",       "A tall tree with thick, rough bark.",              "T", {  0,100,  0,255},   0,     true,  true,   100, Material::WOOD },
  { "Dead tree",  "A leafless, rotting tree. Still blocks the way.",  "T", {100, 80, 60,255},   0,     true,  false,   60, Material::WOOD },
  { "Bush",       "A thorny bush. Slows movement but not impassable.","%" ,{  0,130,  0,255},  50,     false, false,   30, Material::WOOD, true, 7, 15 },
  { "Rock",       "A large rock embedded in the ground.",             "o", {140,140,130,255},   0,     true,  false,  200, Material::STONE },
  { "Boulder",    "A massive boulder. Nothing short of a giant moves this.",
                                                                      "O", {110,110,100,255},   0,     true,  true,   500, Material::STONE },
  { "Fallen log", "A fallen tree trunk lying across the ground.",     "=", {120, 80, 40,255},  80,     false, false,   80, Material::WOOD },
  { "Wall",       "A sturdy stone wall.",                            "█", {130,120,100,255},   0,     true,  true,   300, Material::STONE },
  { "Door",        "A wooden door, standing open.",                   "/", {160,110, 50,255},   0, false, false,  50, Material::WOOD },
  { "Closed door", "A sturdy wooden door, closed shut.",             "+", {140, 90, 40,255},   0, true,  true,   50, Material::WOOD },
  { "Window",      "A small window set into the wall.",              "-", {140,200,220,255},   0, true,  false,  20, Material::STONE },
  { "Table",       "A wooden table.",                                "T", {180,130, 60,255},   0, true,  false,  30, Material::WOOD },
  { "Bed",         "A simple straw bed.",                            "b", {200,180,140,255},   0, true,  false,  20, Material::WOOD },
  { "Barrel",      "A wooden barrel.",                               "0", {120, 80, 40,255},   0, true,  false,  40, Material::WOOD },
  { "Fireplace",   "A stone fireplace, warm and crackling.",         "^", {220,100, 30,255},   0, true,  false, 100, Material::STONE },
  { "Lamp post",   "A wooden post with an oil lantern on top.",     "i", {255,210, 60,255},   0, true,  false,  25, Material::WOOD },
  { "Well",        "A stone well. Cold, clear water inside.",       "W", {100,140,180,255},   0, true,  false,   0, Material::STONE },
  { "Wheat",       "Tall stalks of golden wheat, ready to harvest.","\"",{210,185, 60,255},  30, false, false,   5, Material::WOOD, true, 6, 20 },
  { "Herb",        "A cluster of medicinal herbs with a sharp scent.", ":", { 80,160, 60,255},  10, false, false,   8, Material::WOOD, true, 3, 10 },
  { "Mushroom",    "Dark mushrooms growing from the damp soil.",       "n", {160, 90, 40,255},  10, false, false,  10, Material::WOOD, true, 4,  8 },
  { "Anvil",       "A blacksmith's iron anvil, scarred from years of hammering.",
                                                                       "A", {150,150,160,255},   0, true,  false,  400, Material::METAL },
  { "Stump",       "A weathered tree stump, left behind after felling.",
                                                                       "u", {110, 70, 35,255},  20, false, false,   20, Material::WOOD },
  { "Grave",       "A weathered gravestone marking someone's final resting place.",
                                                                       "x", {150,150,150,255},   0, true,  false,    0, Material::STONE },
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
