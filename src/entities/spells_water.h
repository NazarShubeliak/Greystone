#pragma once

// ================================================================ Water spells (docs/magic.md)
//
// Fragment file: only ever included from magic.h, right after Spell/SpellShape
// are defined there — split out purely so the spell roster doesn't pile into
// one giant table as more schools arrive (see magic.h's own comment).

// selfCast — see Spell's own comment on that field. main.cpp's useSelfSpell()
// resolves it (a hardcoded thirst-reduction), no shape/range/symbol needed.
inline constexpr Spell CREATE_WATER_SPELL = {
    "Create Water", Skill::WATER, 0, 4, 10.0f, 20, 0, "~",
    {110, 175, 235, 255},
    "Conjures a stream of drinkable water out of thin air, quenching your "
    "thirst on the spot. Requires no training.",
    SpellShape::POINT, 0, false, 0, true
};

inline constexpr Spell WATER_JET_SPELL = {
    "Water Jet", Skill::WATER, 0, 6, 8.0f, 20, 6, "~",
    {60, 130, 210, 255},
    "A weak jet of pressurized water, the first thing any water mage learns. "
    "Douses any fire it touches. Requires no training."
};

// rainCall — see Spell's own comment on that field. Pure utility: douses
// every burning tile and washes scorched ground clean in the area, deals no
// damage to anyone caught in it on purpose.
inline constexpr Spell RAIN_CALL_SPELL = {
    "Rain Call", Skill::WATER, 15, 4, 14.0f, 40, 6, "~",
    {90, 150, 220, 255},
    "Calls down a localized downpour, dousing every flame and washing away "
    "scorched ground in the area. Requires Water 15.",
    SpellShape::BURST, 3, false, 0, false, false, true
};

// slows — see Spell's own comment on that field. Docs/magic.md: "Мокрий
// ворог повільніший" (a wet enemy is slower) — the damage is secondary, the
// point is soaking the target down for buffTurns.
inline constexpr Spell SLOWNESS_SPELL = {
    "Slowness", Skill::WATER, 15, 5, 12.0f, 30, 6, "~",
    {70, 100, 160, 255},
    "A cold, heavy splash that soaks the target down, slowing their steps "
    "for a while. Requires Water 15.",
    SpellShape::POINT, 0, false, 0, false, false, false, false, false, false, false, 20, true
};
