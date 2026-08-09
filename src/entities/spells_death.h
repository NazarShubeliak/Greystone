#pragma once

// ================================================================ Death spells (docs/magic.md)
//
// Fragment file: only ever included from magic.h, right after Spell/SpellShape
// are defined there — split out purely so the spell roster doesn't pile into
// one giant table as more schools arrive (see magic.h's own comment).
//
// Only the Novice-tier damage spell so far — Sense Death (the other Novice
// spell, seeing corpses/undead through walls) needs the same reveal
// mechanic deferred for Sense Life; not bolted on for one spell.

// docs/magic.md describes this as ignoring armor, which isn't a real
// distinction yet — no spell in the game applies defender defense at all
// (resolveSpellHit() never subtracts it, unlike resolveAttack() for melee),
// so "ignores armor" is already implicitly true for every spell, not just
// this one. Plain POINT damage, same shape as Spark/Water Jet/Stone/Gust.
inline constexpr Spell WITHERING_SPELL = {
    "Withering", Skill::DEATH, 0, 6, 8.0f, 20, 6, ",",
    {130, 100, 150, 255},
    "A weak bolt of necrotic decay, the first thing any death mage learns. "
    "Requires no training."
};
