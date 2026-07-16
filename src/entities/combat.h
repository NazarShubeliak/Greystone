#pragma once
#include "actor.h"
#include <algorithm>
#include <cstdlib>

// ================================================================ Combat (docs/weapons.md)
//
// One shared resolveAttack() for every attacker/defender pair in the game — player vs
// enemy, enemy vs player, villager vs player, and (later) NPC vs NPC. Symmetry rule:
// it takes Actor&, not Player&/Enemy&, so the same formula and skill-gain hook apply
// to everyone. Callers pass in effective STR/DEX because only Player currently has an
// equipment-bonus layer (effectiveStr()/effectiveDex()) — Enemy/Villager pass their
// base stats directly.

// Weighted random body part hit (CDDA-style distribution).
inline PartTarget randomHitPart() {
    int r = rand() % 100;
    if (r < 10) return PartTarget::HEAD;
    if (r < 55) return PartTarget::TORSO;
    if (r < 67) return PartTarget::ARM_L;
    if (r < 79) return PartTarget::ARM_R;
    if (r < 89) return PartTarget::LEG_L;
                return PartTarget::LEG_R;
}

inline const char* partName(PartTarget t) {
    switch (t) {
        case PartTarget::HEAD:  return "head";
        case PartTarget::TORSO: return "torso";
        case PartTarget::ARM_L: return "left arm";
        case PartTarget::ARM_R: return "right arm";
        case PartTarget::LEG_L: return "left leg";
        case PartTarget::LEG_R: return "right leg";
    }
    return "body";
}

// Weapon-type effects (docs/weapons.md "Типи зброї та їх ефекти"). Deliberately
// NOT covered yet: mace stun (needs a turn-skip mechanic the game doesn't have),
// dagger/weapon action-speed cost (every action currently costs a flat 100
// energy), spear 2-tile reach, unarmed/shield effects.
inline bool isBladed(Skill s) { return s == Skill::SWORD  || s == Skill::GREATSWORD; }
inline bool isAxe(Skill s)    { return s == Skill::AXE    || s == Skill::GREATAXE; }
inline bool isBlunt(Skill s)  { return s == Skill::MACE   || s == Skill::GREATMACE; }
inline bool isDagger(Skill s) { return s == Skill::DAGGER; }

struct AttackResult {
    bool       hit           = false;
    int        damage        = 0;
    PartTarget part          = PartTarget::TORSO;
    bool       killed        = false;
    bool       leveledUp     = false;
    Skill      skillUsed     = Skill::UNARMED;
    int        newSkillLevel = 0;
};

// Resolves one melee attack per the formulas in docs/weapons.md.
// defenderUnaware: true if the defender hasn't spotted the attacker (currently
// only wired for a sleeping Villager target) — only matters for a dagger.
// hitChanceBonus/dmgMultiplier/guaranteedDeepWound: technique hooks (see Technique
// below) — a plain attack leaves them at their no-op defaults.
inline AttackResult resolveAttack(Actor& attacker, Actor& defender,
                                   int attackerEffStr, int attackerEffDex,
                                   int defenderEffDex, int defenderDefense,
                                   const Item* weapon, bool defenderUnaware = false,
                                   int hitChanceBonus = 0, float dmgMultiplier = 1.0f,
                                   bool guaranteedDeepWound = false) {
    AttackResult result;
    result.skillUsed   = weapon ? weapon->weaponSkill : Skill::UNARMED;
    SkillLevel& sk      = attacker.skill(result.skillUsed);

    int chance = 60 + (attackerEffDex - defenderEffDex) * 3 + sk.level / 3 + hitChanceBonus;
    chance = std::max(10, std::min(95, chance));

    if (rand() % 100 >= chance) {
        sk.gain(1);
        result.hit = false;
        return result;
    }

    PartTarget part        = randomHitPart();
    int        weaponDmg   = weapon ? weapon->damage : 1;
    float      skillMult   = 0.5f + sk.level * 0.008f;
    float      base        = weaponDmg * attacker.body.strMult() * skillMult
                              + (attackerEffStr - 10) / 2.0f;
    float      spread      = 0.8f + (rand() % 41) / 100.0f; // ±20%
    int        rawDmg      = std::max(1, (int)(base * spread));
    if (part == PartTarget::HEAD) rawDmg = (int)(rawDmg * 1.5f);

    // Dagger: bonus damage against a target that hasn't noticed the attacker.
    // A technique (Backstab) supplies its own dmgMultiplier instead of stacking on top.
    if (isDagger(result.skillUsed) && defenderUnaware && dmgMultiplier == 1.0f)
        rawDmg = (int)(rawDmg * 1.5f);
    rawDmg = (int)(rawDmg * dmgMultiplier);

    // Mace: crushes straight through half the armor.
    int effDefense = isBlunt(result.skillUsed) ? defenderDefense / 2 : defenderDefense;
    int finalDmg   = std::max(1, rawDmg - effDefense);

    bool wasAlive     = defender.isAlive();
    bool sharp        = weapon && weapon->sharp;
    bool forceBleed   = isBladed(result.skillUsed);            // sword: guaranteed bleed
    int  severChance  = isAxe(result.skillUsed) ? 65 : 40;      // axe: higher sever chance
    defender.takeDamage(finalDmg, part, sharp, forceBleed, severChance, guaranteedDeepWound);

    result.hit    = true;
    result.damage = finalDmg;
    result.part   = part;
    result.killed = wasAlive && !defender.isAlive();

    result.leveledUp = sk.gain(2);
    if (result.killed && sk.gain(5)) result.leveledUp = true;
    result.newSkillLevel = sk.level;

    return result;
}

// ================================================================ Techniques (docs/weapons.md)
//
// Active techniques — like spells for warriors. Gated by weapon skill level and
// stamina, cost extra turn time on top of a normal attack (see the caller's
// onPlayerAct(extraEnergy)). Currently unlocked automatically once the skill
// requirement is met — there's no separate "learned from a teacher" flag yet
// (that arrives once village NPCs can actually run training).

enum class TechniqueId { BRUTAL_STRIKE, LUNGE, BACKSTAB, TECHNIQUE_COUNT };

struct Technique {
    const char* name;
    Skill       skill;
    int         minLevel;
    float       staminaCost;
    int         extraEnergy;   // added on top of the flat 100 a normal action costs
    int         range;         // Chebyshev tiles from the attacker a target may be picked from
    const char* description;
};

inline const Technique& techniqueInfo(TechniqueId id) {
    static const Technique T[(int)TechniqueId::TECHNIQUE_COUNT] = {
        { "Brutal Strike", Skill::AXE,    20, 15.0f, 40, 1,
          "A committed swing that guarantees a deep, bleeding wound. Requires Axe 20." },
        { "Lunge",         Skill::SWORD,  30, 12.0f, 20, 1,
          "A precise thrust, much likelier to connect than a normal swing. Requires Sword 30." },
        { "Backstab",      Skill::DAGGER, 40, 10.0f, 10, 1,
          "A vicious strike against a foe that hasn't noticed you — doubles the damage. "
          "Requires Dagger 40." },
    };
    return T[(int)id];
}

// Skill-gated: whether the attacker even knows this technique yet.
inline bool techniqueUnlocked(const Actor& a, TechniqueId id) {
    const Technique& t = techniqueInfo(id);
    return a.skill(t.skill).level >= t.minLevel;
}

// Unlocked AND enough stamina left to actually use it right now.
inline bool techniqueUsable(const Actor& a, TechniqueId id) {
    const Technique& t = techniqueInfo(id);
    return techniqueUnlocked(a, id) && a.hasStamina(t.staminaCost);
}
