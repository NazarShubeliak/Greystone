#pragma once

// ================================================================ Life spells (docs/magic.md)
//
// Fragment file: only ever included from magic.h, right after Spell/SpellShape
// are defined there — split out purely so the spell roster doesn't pile into
// one giant table as more schools arrive (see magic.h's own comment).
//
// Only the Novice-tier self-heal so far — Sense Life (the other Novice
// spell, seeing living creatures through walls) needs a reveal mechanic
// nothing in the game has yet, deliberately deferred rather than bolted on
// for one spell.

// selfCast — see Spell's own comment on that field. main.cpp's useSelfSpell()
// resolves it (restores torso HP directly, same "always torso" convention
// starvation/dehydration/bleed already use for undirected HP loss/gain).
inline constexpr Spell MINOR_HEAL_SPELL = {
    "Minor Heal", Skill::LIFE, 0, 4, 10.0f, 20, 0, "+",
    {130, 220, 140, 255},
    "Channels a little life energy to knit your own wounds. Requires no training.",
    SpellShape::POINT, 0, false, 0, true
};
