#pragma once

// ================================================================ Earth spells (docs/magic.md)
//
// Fragment file: only ever included from magic.h, right after Spell/SpellShape
// are defined there — split out purely so the spell roster doesn't pile into
// one giant table as more schools arrive (see magic.h's own comment).
//
// Skin Hardening (the other Novice spell, a temporary defense buff) needs a
// buff-duration system nothing in the game has yet, deliberately deferred
// rather than bolted on for one spell.

inline constexpr Spell STONE_SPELL = {
    "Stone", Skill::EARTH, 0, 6, 8.0f, 20, 6, "o",
    {150, 130, 100, 255},
    "A stone hurled by force of will, the first thing any earth mage learns. "
    "Requires no training."
};

// buildsWall — see Spell's own comment on that field. The only spell in the
// game that edits the map itself, matching docs/magic.md's "єдина школа що
// редагує тайли карти" (the only school that edits map tiles) framing for Earth.
inline constexpr Spell STONE_WALL_SPELL = {
    "Stone Wall", Skill::EARTH, 15, 6, 15.0f, 60, 5, "#",
    {130, 120, 100, 255},
    "Raises a slab of solid stone from the earth on an open tile, blocking "
    "the way. Requires Earth 15.",
    SpellShape::POINT, 0, false, 0, false, false, false, true
};

// reclaimsWall — see Spell's own comment on that field. Only ever checks for
// O_WALL specifically, so it can't do anything to a wooden door/table/log
// that happens to share Material::STONE with nothing — an earth spell has no
// business unmaking something that isn't stone.
inline constexpr Spell RECLAIM_WALL_SPELL = {
    "Reclaim Wall", Skill::EARTH, 20, 4, 10.0f, 30, 5, "#",
    {130, 120, 100, 255},
    "Undoes a stone wall, sinking it back into the earth it came from. Only "
    "works on stone — a wooden wall shrugs this off. Requires Earth 20.",
    SpellShape::POINT, 0, false, 0, false, false, false, false, true
};

// throwsWall — see Spell's own comment on that field. knockback stacks on
// top of the direct hit — getting hit by a flying wall should knock you flat.
inline constexpr Spell WALL_THROW_SPELL = {
    "Wall Throw", Skill::EARTH, 25, 18, 18.0f, 40, 6, "#",
    {150, 120, 100, 255},
    "Tears a nearby stone wall from the ground and hurls it at the target — "
    "needs a wall standing somewhere close to you first. Requires Earth 25.",
    SpellShape::POINT, 0, false, 0, false, true, false, false, false, true
};

// manualBuild — see Spell's own comment on that field. Master-tier: "єдина
// школа що редагує тайли карти" taken to its conclusion — hand-build an
// entire structure one tile at a time, same UI as Wall of Fire.
inline constexpr Spell ARCHITECT_SPELL = {
    "Architect", Skill::EARTH, 50, 10, 20.0f, 80, 6, "#",
    {130, 120, 100, 255},
    "Mark out a structure's walls tile by tile and raise all of them from "
    "the earth at once. Costs stamina per tile marked. Requires Earth 50.",
    SpellShape::POINT, 0, false, 0, false, false, false, false, false, false, true
};
