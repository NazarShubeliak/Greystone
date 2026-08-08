#pragma once

// ================================================================ Air spells (docs/magic.md)
//
// Fragment file: only ever included from magic.h, right after Spell/SpellShape
// are defined there — split out purely so the spell roster doesn't pile into
// one giant table as more schools arrive (see magic.h's own comment).
//
// Only the Novice-tier control spell (Gust) so far — Lightness (the other
// Novice spell, a temporary speed buff) needs the same buff-duration system
// deferred for Fire Shield/Skin Hardening; not bolted on for one spell.

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
