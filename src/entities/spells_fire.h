#pragma once

// ================================================================ Fire spells (docs/magic.md)
//
// Fragment file: only ever included from magic.h, right after Spell/SpellShape
// are defined there — split out purely so the spell roster doesn't pile into
// one giant table as more schools arrive (see magic.h's own comment).

inline constexpr Spell SPARK_SPELL = {
    "Spark", Skill::FIRE, 0, 6, 8.0f, 20, 6, "*",
    {255, 190, 70, 255},
    "A weak flame dart, the first thing any fire mage learns. Requires no training."
};

inline constexpr Spell FIREBALL_SPELL = {
    "Fireball", Skill::FIRE, 15, 16, 20.0f, 50, 8, "0",
    {255, 100, 30, 255},
    "The classic fire bolt, the workhorse spell of every combat mage — bursts on "
    "impact even if it doesn't land square on a target. Requires Fire 15.",
    SpellShape::BURST, 1
};

inline constexpr Spell WALL_OF_FIRE_SPELL = {
    "Wall of Fire", Skill::FIRE, 20, 8, 6.0f, 30, 5, "=",
    {230, 90, 20, 255},
    "Mark several tiles within reach to raise a wall of flame across all of "
    "them at once, burning for a few turns — costs stamina per tile marked. "
    "Requires Fire 20.",
    SpellShape::POINT, 0, true, 4
};

inline constexpr Spell EXPLOSION_SPELL = {
    "Explosion", Skill::FIRE, 35, 13, 28.0f, 60, 6, "@",
    {255, 60, 10, 255},
    "A bolt that bursts on impact, roasting everything nearby. Requires Fire 35.",
    SpellShape::BURST, 2
};

// selfCast + buffTurns — see Spell's own comments on those fields. Reflects
// through combat.h's resolveAttack(), so it works symmetrically no matter
// who's attacking whom while it's up.
inline constexpr Spell FIRE_SHIELD_SPELL = {
    "Fire Shield", Skill::FIRE, 30, 6, 20.0f, 40, 0, "*",
    {255, 130, 40, 255},
    "Wreathes you in shimmering heat for a while — anyone who strikes you "
    "in melee gets burned right back. Requires Fire 30.",
    SpellShape::POINT, 0, false, 0, true, false, false, false, false, false, false, 20
};
