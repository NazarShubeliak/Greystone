#pragma once
#include "actor.h"
#include "combat.h"
#include <SDL2/SDL.h>
#include <cstdlib>
#include <algorithm>

// ================================================================ Magic (docs/magic.md)
//
// Spells reuse the same Skill/SkillLevel progression as weapons (Skill::FIRE etc,
// living on Actor per the world-symmetry rule) and the same 9-slot Hotbar as
// techniques — see hotbar.h's SPELL_SLOT_OFFSET for how one int slot holds either
// a TechniqueId or a SpellId without changing Hotbar's structure. All six base
// schools exist now (docs/magic.md's implementation order: Fireball first as a
// test, round out the rest of Fire, then the other schools one at a time).
//
// Each school's actual spells live in their own spells_<school>.h file (as
// named `inline constexpr Spell` constants), included below right after
// Spell/SpellShape are defined — keeps the growing roster from piling into
// one giant table here. Those files are fragments, not meant to be included
// on their own; spellInfo() further down is what stitches them together by
// SpellId.

enum class SpellId {
    SPARK, FIREBALL, WALL_OF_FIRE, EXPLOSION, FIRE_SHIELD,
    CREATE_WATER, WATER_JET, RAIN_CALL, SLOWNESS,
    STONE, STONE_WALL, RECLAIM_WALL, WALL_THROW, ARCHITECT, SKIN_HARDENING,
    GUST, VORTEX, LIGHTNESS, ACCELERATION,
    MINOR_HEAL,
    WITHERING,
    SPELL_COUNT
};

// Every spell targets a tile, not an actor — no click is ever rejected for
// "nothing standing there" (see main.cpp's useSpellAtTile()). POINT only
// affects the exact impact tile; BURST spreads aoeRadius tiles from it.
// POINT with aoeRadius 0 (Spark) is a precise dart; a POINT/BURST landing on
// an empty tile still spends the cast and, for Fire spells, rolls a chance to
// scorch flammable ground/scenery there instead of just fizzling silently.
enum class SpellShape { POINT, BURST };

struct Spell {
    const char* name;
    Skill       school;
    int         minLevel;
    int         baseDamage;
    float       staminaCost;
    int         extraEnergy;  // added on top of the flat 100 a normal action costs
    int         range;        // Chebyshev tiles from the caster a target may be picked from
    const char* symbol;       // projectile glyph drawn while it flies to the target
    SDL_Color   color;
    const char* description;
    SpellShape  shape      = SpellShape::POINT;
    int         aoeRadius  = 0;      // BURST only — Chebyshev tiles around the impact tile
    // manualArea spells (Wall of Fire) skip shape/aoeRadius entirely: the player
    // hand-picks any number of tiles within range (main.cpp's wallTargeting flow)
    // instead of clicking one impact point. staminaCost becomes a PER-TILE cost
    // in this mode, and hazardTurns tiles catch fire for that many player turns,
    // burning baseDamage into whoever's standing there each turn (see
    // main.cpp's confirmWallTargeting()/tickFireHazards()).
    bool        manualArea  = false;
    int         hazardTurns = 0;
    // selfCast spells (Create Water) skip targeting entirely — no click, no
    // shape, no footprint. useHotbarSlot() resolves them immediately on the
    // caster (main.cpp's useSelfSpell()). shape/range/symbol are irrelevant
    // for these; baseDamage only matters for the backfire self-damage roll.
    bool        selfCast    = false;
    // knockback spells (Gust) shove a hit target one tile directly away from
    // the caster after the normal damage resolves, if that tile is walkable
    // and empty — see main.cpp's tryKnockback(), called from useSpellAtTile()
    // for every Enemy/Villager actually hit when this is set.
    bool        knockback   = false;
    // rainCall spells (Rain Call) skip the normal hit-an-actor resolution
    // entirely — a pure-utility BURST that douses every fireHazards tile and
    // washes G_SCORCHED ground back to grass within aoeRadius of the clicked
    // tile (main.cpp's useSpellAtTile(), checked before the normal footprint
    // logic). Nobody takes damage from it, on purpose.
    bool        rainCall    = false;
    // buildsWall spells (Stone Wall) also skip normal resolution — the
    // clicked POINT tile gets an O_WALL object raised on it if it's currently
    // open ground (main.cpp's useSpellAtTile()), same "edits the map" role
    // docs/magic.md gives Earth. Fails (cast still spent) if the tile isn't
    // clear. No projectile is spawned for this (or reclaimsWall/throwsWall
    // below) — these are ground magic, not something thrown.
    bool        buildsWall   = false;
    // reclaimsWall spells (Reclaim Wall) undo buildsWall — the clicked tile's
    // object is cleared IF AND ONLY IF it's specifically O_WALL (checked by
    // ObjectId, not just Material::STONE, so it can never eat a table/door/
    // anything else that merely happens to share the material) — a Fallen
    // Log or Table being wood, not stone, was the user's original concern
    // for why this needs an explicit check at all. No loot drop — it sinks
    // back into the earth, doesn't shatter.
    bool        reclaimsWall = false;
    // throwsWall spells (Wall Throw) find the nearest O_WALL to the CASTER
    // (not the target) within a generous search radius, un-make it, and hurl
    // it at whatever's on the clicked tile for heavy impact damage — see
    // main.cpp's useSpellAtTile() for the search + resolution. Projectile
    // flies from the wall's position, not the caster's.
    bool        throwsWall   = false;
    // manualBuild spells (Architect) reuse Wall of Fire's exact multi-tile
    // wallTargeting flow (main.cpp's useHotbarSlot()/confirmWallTargeting())
    // but raise a permanent O_WALL on every clear marked tile instead of
    // igniting a FireHazardTile — same UI, construction instead of
    // destruction. staminaCost is per-tile here too.
    bool        manualBuild  = false;
    // buffTurns: how long a selfCast buff (Fire Shield/Skin Hardening/
    // Lightness/Acceleration) or a slows debuff (Slowness) lasts, in turns.
    // useSelfSpell()/the slows branch of useSpellAtTile() pick which Actor
    // counter to set by SpellId, same "no generic effect field for a
    // handful of spells" call as rainCall/buildsWall.
    int         buffTurns   = 0;
    // slows spells (Slowness) set the hit target's slowedTicks to buffTurns
    // after the normal damage resolves — a debuff instead of a self-buff,
    // but reuses the exact same Actor counters/buffTurns field as the
    // selfCast buffs above (main.cpp's useSpellAtTile()). Declared last so
    // adding it never shifts any earlier field's position in existing
    // spells_<school>.h positional initializers.
    bool        slows       = false;
};

#include "spells_fire.h"
#include "spells_water.h"
#include "spells_earth.h"
#include "spells_air.h"
#include "spells_life.h"
#include "spells_death.h"

inline const Spell& spellInfo(SpellId id) {
    static const Spell S[(int)SpellId::SPELL_COUNT] = {
        SPARK_SPELL, FIREBALL_SPELL, WALL_OF_FIRE_SPELL, EXPLOSION_SPELL, FIRE_SHIELD_SPELL,
        CREATE_WATER_SPELL, WATER_JET_SPELL, RAIN_CALL_SPELL, SLOWNESS_SPELL,
        STONE_SPELL, STONE_WALL_SPELL, RECLAIM_WALL_SPELL, WALL_THROW_SPELL, ARCHITECT_SPELL, SKIN_HARDENING_SPELL,
        GUST_SPELL, VORTEX_SPELL, LIGHTNESS_SPELL, ACCELERATION_SPELL,
        MINOR_HEAL_SPELL,
        WITHERING_SPELL,
    };
    return S[(int)id];
}

inline bool spellUnlocked(const Actor& a, SpellId id) {
    const Spell& s = spellInfo(id);
    return a.skill(s.school).level >= s.minLevel;
}

inline bool spellUsable(const Actor& a, SpellId id) {
    const Spell& s = spellInfo(id);
    return spellUnlocked(a, id) && a.hasStamina(s.staminaCost);
}

// Chance (percent) that casting the given school backfires and burns the caster
// instead of hitting the target — docs/magic.md "невдалі касти на низькій навичці"
// ("failed casts obtain low skill"). Purely a school-skill risk, not per-spell:
// falls to 0 by skill level 25, so Fireball (unlocks at 15) still carries some
// risk right when a caster first gets access to it.
inline int spellFailChance(const Actor& a, Skill school) {
    return std::max(0, 25 - a.skill(school).level);
}

struct SpellResult {
    bool       killed        = false;
    int        damage        = 0;
    PartTarget part          = PartTarget::TORSO;
};

struct SpellCastRoll {
    bool  backfired     = false;
    int   selfDamage    = 0;
    bool  casterKilled  = false;
    bool  leveledUp     = false;
    Skill skillUsed     = Skill::FIRE;
    int   newSkillLevel = 0;
};

// Rolls one cast's fortune — every spell goes through this exactly once per
// cast, regardless of how many tiles/targets its shape ends up covering (see
// main.cpp's useSpellAtTile()). When this doesn't backfire, call
// resolveSpellHit() once per target actually caught in the shape.
inline SpellCastRoll rollSpellCast(Actor& caster, SpellId id) {
    const Spell& s = spellInfo(id);
    SpellCastRoll result;
    result.skillUsed = s.school;
    SkillLevel& sk = caster.skill(s.school);

    if (rand() % 100 < spellFailChance(caster, s.school)) {
        result.backfired = true;
        int selfDmg = std::max(1, s.baseDamage / 2);
        bool wasAlive = caster.isAlive();
        caster.takeDamage(selfDmg, PartTarget::ARM_R);
        result.selfDamage   = selfDmg;
        result.casterKilled = wasAlive && !caster.isAlive();
        result.leveledUp    = sk.gain(1);
    } else {
        result.leveledUp = sk.gain(2);
    }
    result.newSkillLevel = sk.level;
    return result;
}

// Applies one target's share of an already-resolved (non-backfired) cast.
// No skill roll here — rollSpellCast() already covered that for the whole cast.
inline SpellResult resolveSpellHit(const Actor& caster, Actor& defender, SpellId id) {
    const Spell& s = spellInfo(id);
    SpellResult result;
    float skillMult = 0.6f + caster.skill(s.school).level * 0.007f;
    float spread    = 0.85f + (rand() % 31) / 100.0f; // ±15%
    PartTarget part = randomHitPart();
    int finalDmg    = std::max(1, (int)(s.baseDamage * skillMult * spread));

    bool wasAlive = defender.isAlive();
    defender.takeDamage(finalDmg, part);
    result.damage = finalDmg;
    result.part   = part;
    result.killed = wasAlive && !defender.isAlive();
    return result;
}
