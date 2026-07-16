#pragma once
#include "item.h"
#include "actor.h"
#include <string>
#include <vector>
#include <optional>
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

// ================================================================ Occupation

enum class Occupation { NONE, FARMER, HERBALIST, WOODCUTTER, BLACKSMITH, ELDER, SEAMSTRESS };

// Short label shown in UI (context menu, examine panel). "" for NONE.
inline const char* occupationName(Occupation occ) {
    switch (occ) {
        case Occupation::FARMER:     return "Farmer";
        case Occupation::HERBALIST:  return "Herbalist";
        case Occupation::WOODCUTTER: return "Woodcutter";
        case Occupation::BLACKSMITH: return "Blacksmith";
        case Occupation::ELDER:      return "Elder";
        case Occupation::SEAMSTRESS: return "Seamstress";
        default:                     return "";
    }
}

// One-line flavor description for the villager examine panel (kept short — the
// panel renders it on a single line with no word-wrap).
inline const char* occupationFlavor(Occupation occ) {
    switch (occ) {
        case Occupation::FARMER:     return "Tends the wheat fields west of the village.";
        case Occupation::HERBALIST:  return "Grows medicinal herbs and treats the sick.";
        case Occupation::WOODCUTTER: return "Fells timber in the forest to sell as lumber.";
        case Occupation::BLACKSMITH: return "Forges tools and weapons from raw iron.";
        case Occupation::ELDER:      return "Oversees the village and settles disputes.";
        case Occupation::SEAMSTRESS: return "Sews and mends clothing for the villagers.";
        default:                     return "Helps run the household and tends the garden.";
    }
}

// Goods this occupation sells, if any. Elder and plain helpers (NONE) don't trade.
inline std::vector<Item> goodsFor(Occupation occ) {
    switch (occ) {
        case Occupation::FARMER:
            return { Items::bread(), Items::flatbread(), Items::grain(), Items::roastedBerries() };
        case Occupation::HERBALIST:
            return { Items::herb(), Items::herbalPoultice(), Items::mushroom(), Items::mushroomStew() };
        case Occupation::WOODCUTTER:
            return { Items::woodLog(), Items::branch(), Items::hatchet() };
        case Occupation::BLACKSMITH:
            return { Items::ironSword(), Items::warAxe(), Items::pickaxe(), Items::crudeAxe(), Items::crudeDagger() };
        case Occupation::SEAMSTRESS:
            return { Items::leatherVest(), Items::leatherBoots(), Items::ropeItem() };
        default:
            return {};
    }
}

// ================================================================ Villager
//
// A proper Actor subtype — same stat block, Body (6 parts), hunger/thirst and
// tickNeeds()/takeDamage() as Player/Enemy. Nothing villager-specific duplicates what
// Actor already provides.

struct Villager : Actor {
    int bedX = 0, bedY = 0;     // home bed object tile (blocksMove=true — not a valid path target)
    int sleepX = 0, sleepY = 0; // walkable tile adjacent to bed — actual path target

    enum class State { WANDER, WALK_HOME, SLEEP, EAT, DRINK, FLEE, FIGHT } state = State::SLEEP;

    // Path used when walking home or to the well (recomputed via A* with doors open)
    std::vector<SDL_Point> homePath;
    int homePathIdx    = 0;
    int pathRetryCool  = 0; // ticks to wait before rebuilding a failed path

    // Door the NPC last opened — closed once they step away from it
    int lastDoorX = -1, lastDoorY = -1;

    // Greeting lines used when the player talks to them.
    // Index rotates so households say different things.
    int greetIdx = 0;

    // What this villager does for a living — determines flavor text and bag contents.
    Occupation occupation = Occupation::NONE;

    // Everything they carry — trade goods AND personal food, all physically inside this
    // one real container (no floating/invisible items). Eating pulls a food item out of
    // it; trading reads/writes it directly.
    std::optional<Item> bag;

    // What they're wearing — separate from bag (carried goods), so they're
    // never rendered as "naked with a backpack". Set once at spawn, drops
    // alongside the bag on death.
    std::optional<Item> outfit;

    // Item from a WEAPON-type entry in bag, if any (e.g. Blacksmith stock) —
    // mirrors Enemy::weaponItem(), used when this villager fights back.
    const Item* weaponItem() const {
        if (!bag) return nullptr;
        for (const Item& item : bag->contents)
            if (item.type == ItemType::WEAPON) return &item;
        return nullptr;
    }

    int weaponDmg() const {
        const Item* w = weaponItem();
        return w ? w->damage : 0;
    }

    Villager() : Actor(0, 0, "@", {220, 185, 90, 255}, 80, Race::HUMAN) {
        body = Body(25, 50, 35, 45);
        sync();
    }
};
