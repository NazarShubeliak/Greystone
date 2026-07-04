#pragma once
#include "item.h"
#include <string>
#include <vector>
#include <SDL2/SDL.h>

// ================================================================ Name tables

static const char* NPC_FIRST_NAMES[] = {
    "Aldric", "Mira",  "Theron", "Sylva",  "Gareth", "Lena",
    "Bram",   "Cora",  "Edric",  "Nessa",  "Finn",   "Wren",
    "Oswin",  "Hilda", "Caius",  "Elara",  "Brand",  "Inga",
    "Rolf",   "Disa",  nullptr
};

static const char* NPC_SURNAMES[] = {
    "Miller", "Fletcher", "Cooper", "Thatcher", "Mason",
    "Baker",  "Smith",    "Weaver", "Carter",   "Turner",
    nullptr
};

static const SDL_Color VILLAGER_COLORS[] = {
    {220, 185,  90, 255},  // amber
    {175, 210, 165, 255},  // sage
    {200, 165, 210, 255},  // lavender
    {155, 205, 215, 255},  // sky
    {215, 175, 145, 255},  // tan
};
static constexpr int VILLAGER_COLOR_COUNT = 5;

// ================================================================ Villager

struct Villager {
    int x = 0, y = 0;           // current tile
    int bedX = 0, bedY = 0;     // home bed object tile (blocksMove=true — not a valid path target)
    int sleepX = 0, sleepY = 0; // walkable tile adjacent to bed — actual path target

    std::string name;
    SDL_Color   color = {220, 185, 90, 255};

    bool alive  = true;
    int  energy = 0;
    int  speed  = 80;   // a bit slower than the player

    enum class State { WANDER, WALK_HOME, SLEEP } state = State::SLEEP;

    // Path used when walking home (recomputed via A* with doors treated as open)
    std::vector<SDL_Point> homePath;
    int homePathIdx    = 0;
    int pathRetryCool  = 0; // ticks to wait before rebuilding a failed path

    // Door the NPC last opened — closed once they step away from it
    int lastDoorX = -1, lastDoorY = -1;

    // Greeting lines used when the player talks to them.
    // Index rotates so households say different things.
    int greetIdx = 0;

    // One villager per village doubles as the general-goods merchant.
    bool isMerchant = false;
    std::vector<Item> shopItems;
};
