#pragma once

// ================================================================ Air spells (docs/magic.md)
//
// Fragment file: only ever included from magic.h, right after Spell/SpellShape
// are defined there — split out purely so the spell roster doesn't pile into
// one giant table as more schools arrive (see magic.h's own comment).
//
// knockback — see Spell's own comment on that field. Weakest raw damage of
// any spell in the game so far, matching docs/magic.md's "найслабший урон
// але найкращий контроль" (weakest damage, best control) framing for Air.
// Symbol/color deliberately far from Vortex's below — the two were too easy
// to mix up when both were near-identical pale blue ")".
inline constexpr Spell GUST_SPELL = {
    "Gust", Skill::AIR, 0, 3, 6.0f, 20, 4, ">",
    {170, 220, 255, 255},
    "A blast of wind that shoves whatever it hits backward. Requires no training.",
    SpellShape::POINT, 0, false, 0, false, true
};

// BURST + knockback together, docs/magic.md's Адепт-tier "Вихор — Відкинути
// всіх навколо" (Vortex — throws everyone nearby back). Needed zero new code
// on top of Gust's knockback — just a wider, weaker blast that hits everyone
// in range instead of one target.
inline constexpr Spell VORTEX_SPELL = {
    "Vortex", Skill::AIR, 30, 5, 20.0f, 50, 5, "@",
    {225, 210, 160, 255},
    "A spinning blast of wind that throws everyone nearby off their feet. "
    "Requires Air 30.",
    SpellShape::BURST, 2, false, 0, false, true
};

// selfCast + buffTurns — see Spell's own comments on those fields. Reflected
// through tickWorld()'s energy-gain step for the player and enemies; no
// equivalent hook for villagers yet (they don't use the same speed/energy
// turn system).
inline constexpr Spell LIGHTNESS_SPELL = {
    "Lightness", Skill::AIR, 0, 3, 10.0f, 20, 0, ")",
    {200, 235, 245, 255},
    "Lightens your step for a while, making you noticeably faster. "
    "Requires no training.",
    SpellShape::POINT, 0, false, 0, true, false, false, false, false, false, false, 25
};

// selfCast + buffTurns, same pattern as Lightness — a stronger, shorter,
// higher-tier version (docs/magic.md's Адепт-tier "Прискорення"). Docs frame
// it as an outright extra turn; that would need restructuring the turn loop
// itself, so this is a much bigger speed bonus instead — same idea, cheaper
// to build honestly.
inline constexpr Spell ACCELERATION_SPELL = {
    "Acceleration", Skill::AIR, 30, 5, 18.0f, 30, 0, ")",
    {255, 225, 130, 255},
    "A surge of wind at your back — you move noticeably faster than "
    "Lightness alone can manage, for a shorter while. Requires Air 30.",
    SpellShape::POINT, 0, false, 0, true, false, false, false, false, false, false, 15
};
