#pragma once
#include "actor.h"

// ================================================================
// Enemy factory functions.
// Each returns a fully configured Enemy ready to drop into the world.
// Body HP layout: (headHp, torsoHp, armHp, legHp)
// ================================================================

namespace EnemyTypes {

// Adds an item to an enemy's bag if it fits — every enemy that carries anything
// gets a real container, never a bare floating list. Stacks with matching loot.
inline void give(Enemy& e, Item item) {
    if (!e.bag) e.bag = Items::backpack();
    addToContainer(*e.bag, std::move(item));
}

// Fast, fragile. Dangerous only in groups.
inline Enemy goblin(int x, int y) {
    Enemy e(x, y, "g", {60, 170, 60, 255},
            /*speed*/  130,
            /*race*/   Race::HALFLING,
            /*aggro*/  7,
            /*head*/   12, /*torso*/ 22, /*arm*/ 12, /*leg*/ 16);
    e.name        = "Goblin";
    e.strength    = 7;
    e.dexterity   = 14;
    e.disposition = -80;
    e.sync();
    give(e, Items::crudeDagger());
    give(e, Items::mushroom());   // a small ration — goblins forage
    give(e, Items::waterFlask());
    return e;
}

// Slow, heavily armoured bruiser. Hits very hard.
inline Enemy orcWarrior(int x, int y) {
    Enemy e(x, y, "O", {180, 60, 40, 255},
            /*speed*/  75,
            /*race*/   Race::ORC,
            /*aggro*/  12,
            /*head*/   40, /*torso*/ 90, /*arm*/ 55, /*leg*/ 65);
    e.name        = "Orc Warrior";
    e.strength    = 18;
    e.dexterity   = 7;
    e.disposition = -80;
    e.sync();
    give(e, Items::warAxe());
    give(e, Items::ironHelmet());
    give(e, Items::bread());
    give(e, Items::waterFlask());
    return e;
}

// Undead — doesn't bleed, large aggro range, never truly sleeps.
inline Enemy skeleton(int x, int y) {
    Enemy e(x, y, "s", {200, 195, 175, 255},
            /*speed*/  90,
            /*race*/   Race::SKELETON,
            /*aggro*/  15,
            /*head*/   18, /*torso*/ 40, /*arm*/ 22, /*leg*/ 28);
    e.name        = "Skeleton";
    e.strength    = 10;
    e.dexterity   = 10;
    e.disposition = -80;
    e.sync();
    give(e, Items::boneClub());
    // Undead — no food/water ration; Race::SKELETON doesn't need either.
    return e;
}

// Fast predator — flees when badly wounded.
inline Enemy wolf(int x, int y) {
    Enemy e(x, y, "w", {150, 145, 135, 255},
            /*speed*/  140,
            /*race*/   Race::HUMAN,
            /*aggro*/  9,
            /*head*/   18, /*torso*/ 38, /*arm*/ 22, /*leg*/ 28);
    e.name        = "Wolf";
    e.strength    = 11;
    e.dexterity   = 15;
    e.flees       = true;
    e.fleeHpPct   = 0.25f;   // flees below 25% HP
    e.disposition = -60;
    e.sync();
    return e;
}

// Human combatant — medium stats, will carry (and drop) gear eventually.
inline Enemy bandit(int x, int y) {
    Enemy e(x, y, "@", {160, 115, 65, 255},
            /*speed*/  100,
            /*race*/   Race::HUMAN,
            /*aggro*/  10,
            /*head*/   20, /*torso*/ 50, /*arm*/ 30, /*leg*/ 40);
    e.name        = "Bandit";
    e.strength    = 11;
    e.dexterity   = 10;
    e.disposition = -80;
    e.sync();
    give(e, Items::ironSword());
    give(e, Items::leatherVest());
    give(e, Items::bread());
    give(e, Items::waterFlask());
    return e;
}

} // namespace EnemyTypes
