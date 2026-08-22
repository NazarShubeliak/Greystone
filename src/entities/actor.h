#pragma once
#include "item.h"
#include <SDL2/SDL.h>
#include <string>
#include <algorithm>
#include <optional>
#include <cstdlib>

// Monotonically increasing, never reused (even after death) — lets other systems
// (villager goals/quests, main.cpp) reference a specific Actor by a stable id instead
// of a vector index, which shifts whenever a dead Enemy/Villager is erased.
inline int nextActorId = 1;

// ================================================================ Race

enum class Race {
    HUMAN, ELF, DWARF, ORC, HALFLING,
    VAMPIRE, SKELETON, GHOST
};

struct RaceTraits {
    const char* name;
    bool needsFood;
    bool needsWater;
    bool canBleed;
    int  minAge, maxAge;    // 0,0 = ageless / immortal
    int  strMod, dexMod, intMod, conMod, perMod, chaMod;
};

// Indexed by (int)Race
static const RaceTraits raceTraits[] = {
//   name         food   water  bleed  minAge maxAge  str  dex  int  con  per  cha
  { "Human",      true,  true,  true,  18,    80,      0,   0,   0,   0,   0,   0 },
  { "Elf",        true,  true,  true,  100,   700,     0,  +2,   0,   0,  +2,  +1 },
  { "Dwarf",      true,  true,  true,  50,    350,    +2,   0,   0,  +2,   0,  -1 },
  { "Orc",        true,  true,  true,  15,    60,     +3,  -1,   0,  +1,  -1,  -2 },
  { "Halfling",   true,  true,  true,  25,    200,    -2,  +3,   0,   0,  +1,  +1 },
  { "Vampire",    false, false, false,  0,     0,     +2,   0,  +1,  +2,  +1,  +1 },
  { "Skeleton",   false, false, false,  0,     0,     +1,  -1,   0,  +2,   0,  -3 },
  { "Ghost",      false, false, false,  0,     0,      0,   0,  +2,   0,  +2,   0 },
};

// ================================================================ Life stages (docs/village.md "Шар 4: Вік і демографія")
//
// Boundaries are fractions of a race's minAge/maxAge rather than hardcoded
// human years, so the same six-stage shape scales from a 80-year human to a
// 700-year elf per the doc's "для інших рас масштабуються від minAge/maxAge"
// note. minAge already doubles as "age of adulthood" (it's the lower bound
// generateAge() rolls adult villagers from), so Adult begins exactly there.

enum class LifeStage { CHILD, TEEN, YOUNG_ADULT, ADULT, ELDER, OLD };

inline LifeStage lifeStageFor(int age, Race race) {
    const RaceTraits& t = raceTraits[(int)race];
    if (t.minAge == 0 && t.maxAge == 0) return LifeStage::ADULT; // ageless races don't age
    float childEnd = t.minAge * 0.33f;
    float teenEnd  = t.minAge * 0.72f;
    float elderBeg = t.maxAge * 0.625f;
    float oldBeg   = t.maxAge * 0.8f;
    if (age < childEnd)     return LifeStage::CHILD;
    if (age < teenEnd)      return LifeStage::TEEN;
    if (age < t.minAge)     return LifeStage::YOUNG_ADULT;
    if (age < elderBeg)     return LifeStage::ADULT;
    if (age < oldBeg)       return LifeStage::ELDER;
    return LifeStage::OLD;
}

inline const char* lifeStageName(LifeStage s) {
    switch (s) {
        case LifeStage::CHILD:       return "Child";
        case LifeStage::TEEN:        return "Teen";
        case LifeStage::YOUNG_ADULT: return "Young Adult";
        case LifeStage::ADULT:       return "Adult";
        case LifeStage::ELDER:       return "Elder";
        case LifeStage::OLD:         return "Old";
    }
    return "Adult";
}

// ================================================================ Name generation

namespace Names {

    template<size_t N>
    static const char* pick(const char* const (&arr)[N]) {
        return arr[rand() % N];
    }

    inline std::string generate(Race race) {
        static const char* human_first[] = {
            "Aldric","Edwyn","Gareth","Roland","Tomas","Beren","Marcus","Leon","Hugo","Caden",
            "Maren","Lyra","Senna","Elara","Vira","Nora","Clara","Adela","Isara","Mira"
        };
        static const char* human_last[] = {
            "Ashwood","Blackthorn","Coldwater","Ironside","Greywood",
            "Stormwall","Brightfield","Ravenscar","Dunmore","Whitlock"
        };
        static const char* elf_first[] = {
            "Aelindor","Sylvran","Caladorn","Erindel","Faelvyn",
            "Aelindra","Sylvari","Caladwen","Erindele","Faelwyn"
        };
        static const char* elf_last[] = {
            "Moonwhisper","Starweave","Dawnleaf","Silverveil","Nightbloom"
        };
        static const char* dwarf_first[] = {
            "Burdok","Grimthor","Valdak","Dolgin","Thordak",
            "Briga","Helka","Durna","Morda","Inga"
        };
        static const char* dwarf_last[] = {
            "Ironbeard","Stoneback","Copperhelm","Goldvein","Deepdelver"
        };
        static const char* orc_first[] = {
            "Grukk","Vorga","Drakh","Urgok","Skorg",
            "Gorsha","Vraka","Durga","Oksha","Morda"
        };
        static const char* orc_last[] = {
            "Skullbreaker","Bloodaxe","Bonecrusher","Ironfist","Warbrand"
        };
        static const char* halfling_first[] = {
            "Pip","Finn","Cob","Milo","Rufus",
            "Rosie","Daisy","Lila","Bree","Tansy"
        };
        static const char* halfling_last[] = {
            "Goodbarrel","Thistledown","Brandyfoot","Underhill","Hayward"
        };

        switch (race) {
            case Race::HUMAN:
            case Race::VAMPIRE:  // vampires were once human
                return std::string(pick(human_first)) + " " + pick(human_last);
            case Race::ELF:
                return std::string(pick(elf_first)) + " " + pick(elf_last);
            case Race::DWARF:
                return std::string(pick(dwarf_first)) + " " + pick(dwarf_last);
            case Race::ORC:
                return std::string(pick(orc_first)) + " " + pick(orc_last);
            case Race::HALFLING:
                return std::string(pick(halfling_first)) + " " + pick(halfling_last);
            case Race::SKELETON:
                return "Skeleton";
            case Race::GHOST:
                return std::string(pick(human_first)) + "'s Ghost";
            default:
                return "Unknown";
        }
    }

    // minOverride/maxOverride let callers constrain by role (e.g. bandit: 18-45)
    inline int generateAge(Race race, int minOverride = 0, int maxOverride = 0) {
        const RaceTraits& t = raceTraits[(int)race];
        if (t.minAge == 0 && t.maxAge == 0) return 0;  // ageless
        int lo = (minOverride > 0) ? minOverride : t.minAge;
        int hi = (maxOverride > 0) ? maxOverride : t.maxAge;
        if (lo >= hi) return lo;
        return lo + rand() % (hi - lo + 1);
    }

} // namespace Names

// ================================================================ Body parts

enum class PartTarget { HEAD, TORSO, ARM_L, ARM_R, LEG_L, LEG_R };

enum class LimbState { NORMAL, WOUNDED, SEVERE, DESTROYED, SEVERED };

struct BodyPart {
    int   hp, maxHp;
    bool  severed = false;
    bool  broken  = false;
    float bleed   = 0.0f;  // HP drained from torso per world tick

    float ratio() const { return maxHp > 0 ? (float)hp / maxHp : 0.0f; }
    bool  intact() const { return !severed && hp > 0; }

    LimbState state() const {
        if (severed)         return LimbState::SEVERED;
        float r = ratio();
        if (r > 0.60f)       return LimbState::NORMAL;
        if (r > 0.30f)       return LimbState::WOUNDED;
        if (r > 0.10f)       return LimbState::SEVERE;
        return                      LimbState::DESTROYED;
    }

    // canSever: true for limbs (not head/torso), sharp = slashing/piercing weapon.
    // forceBleed: sword-type hit — guarantees at least a light bleed on any hit,
    // not just once the part is already badly damaged (docs/weapons.md).
    // severChancePct: sever roll on a killing blow (default 40; axes roll higher).
    // guaranteedDeepWound: Brutal Strike technique — forces the SEVERE-tier bleed
    // and a bone break outright, regardless of how much HP the part has left.
    void applyDamage(int amount, bool sharp = false, bool canSever = false,
                      bool forceBleed = false, int severChancePct = 40,
                      bool guaranteedDeepWound = false) {
        if (severed) return;
        hp = std::max(0, hp - amount);

        float r = ratio();

        // Bleeding starts at SEVERE threshold
        if (r <= 0.30f && r > 0.10f && bleed < 1.0f) bleed = 1.0f;
        if (r <= 0.10f              && bleed < 2.0f)  bleed = 2.0f;
        if (forceBleed              && bleed < 1.0f)  bleed = 1.0f;
        if (guaranteedDeepWound)                      bleed = std::max(bleed, 2.0f);

        // Bone break at SEVERE
        if (!broken && (r <= 0.30f || guaranteedDeepWound)) broken = true;

        // Severing: 0 HP + sharp weapon = severChancePct chance
        if (canSever && hp == 0 && sharp)
            if (rand() % 100 < severChancePct) severed = true;
    }
};

struct Body {
    BodyPart head;
    BodyPart torso;
    BodyPart armL, armR;
    BodyPart legL, legR;

    Body(int headHp  = 30, int torsoHp = 60,
         int armHp   = 40, int legHp   = 50)
        : head {headHp,  headHp},
          torso{torsoHp, torsoHp},
          armL {armHp,   armHp},
          armR {armHp,   armHp},
          legL {legHp,   legHp},
          legR {legHp,   legHp} {}

    BodyPart& get(PartTarget t) {
        switch (t) {
            case PartTarget::HEAD:  return head;
            case PartTarget::TORSO: return torso;
            case PartTarget::ARM_L: return armL;
            case PartTarget::ARM_R: return armR;
            case PartTarget::LEG_L: return legL;
            default:                return legR;
        }
    }

    bool isDead()     const { return head.hp <= 0 || torso.hp <= 0; }
    int  totalHp()    const { return head.hp    + torso.hp; }
    int  totalMaxHp() const { return head.maxHp + torso.maxHp; }

    float totalBleed() const {
        return head.bleed + torso.bleed
             + armL.bleed + armR.bleed
             + legL.bleed + legR.bleed;
    }

    // Only a light cut (bleed<=1.0 — BodyPart::applyDamage()'s WOUNDED-tier
    // threshold, or forceBleed from a sword hit) clots on its own over time;
    // a severe wound (2.0 — SEVERE-tier, or a guaranteedDeepWound technique
    // like Brutal Strike) won't budge without actual treatment (Herbal
    // Poultice) — a deep wound doesn't just stop on its own (user request).
    // Call once per tick, same cadence as tickNeeds()'s hunger/thirst —
    // 0.01/tick means a light bleed (1.0) takes roughly 100 in-game minutes
    // (~1.7h) to stop.
    void clot() {
        constexpr float RATE = 0.01f;
        auto decay = [](BodyPart& p) { if (p.bleed <= 1.0f) p.bleed = std::max(0.0f, p.bleed - RATE); };
        decay(head); decay(torso); decay(armL); decay(armR); decay(legL); decay(legR);
    }

    // Movement penalty from leg damage
    float speedMult() const {
        auto pen = [](const BodyPart& leg) -> float {
            switch (leg.state()) {
                case LimbState::SEVERED:   return 0.30f;
                case LimbState::DESTROYED: return 0.50f;
                case LimbState::SEVERE:    return 0.70f;
                case LimbState::WOUNDED:   return 0.90f;
                default:                   return 1.00f;
            }
        };
        return pen(legL) * pen(legR);
    }

    // Attack strength penalty from arm damage
    float strMult() const {
        auto pen = [](const BodyPart& arm) -> float {
            switch (arm.state()) {
                case LimbState::SEVERED:   return 0.20f;
                case LimbState::DESTROYED: return 0.50f;
                case LimbState::SEVERE:    return 0.70f;
                case LimbState::WOUNDED:   return 0.90f;
                default:                   return 1.00f;
            }
        };
        return pen(armL) * pen(armR);
    }
};

// ================================================================ Actor
//
// Base for every living/undead creature in the world.
// hp, maxHp, speed are cached fields kept in sync via sync().
// Call sync() after any body part change; takeDamage() does it automatically.

struct Actor {
    // --- Identity ---
    int         id     = 0;    // stable, unique, set in the constructor — see nextActorId
    std::string name;
    Race        race   = Race::HUMAN;
    int         age    = 25;
    // Rolled once at construction (docs/village.md: "±10% рандому" around the
    // race's maxAge) — the age tickAgingOneYear() (main.cpp) kills this Actor
    // of old age at, once `age` reaches it. 0 = never (ageless races).
    int         naturalDeathAge = 0;
    float       weight = 70.0f;    // kg
    float       height = 175.0f;   // cm

    // --- Render ---
    const char* symbol;
    SDL_Color   color;

    // --- Position ---
    int x, y;

    // --- Turn system ---
    int baseSpeed;
    int energy = 0;

    // --- Body ---
    Body body;

    // --- Cached derived values (always valid after sync()) ---
    int  hp, maxHp;
    int  speed;
    bool alive = true;

    // --- D&D stats (racial modifiers applied in constructor) ---
    int strength     = 10;
    int dexterity    = 10;
    int intelligence = 10;
    int constitution = 10;
    int perception   = 10;
    int charisma     = 10;

    // --- Survival needs ---
    // Only ticked for races that require them (see raceTraits[].needsFood/needsWater)
    float hunger = 0.0f;    // 0 = full    → 1 = starving
    float thirst = 0.0f;    // 0 = hydrated → 1 = dying

    // --- Stamina ---
    // Resource for techniques and spells (docs/weapons.md, docs/magic.md). Lives here,
    // not on Player, so enemies/villagers can eventually use techniques too (symmetry).
    // Nothing spends it yet — that arrives with the first technique/spell; regen already
    // runs via tickNeeds() so the bar isn't just sitting at max forever in the meantime.
    float stamina    = 100.0f;
    float maxStamina = 100.0f;

    // --- Buffs/debuffs (docs/magic.md self-cast/targeted utility effects) ---
    // Simple duration counters in turns, decremented once per tickNeeds()
    // call — the same "once per actor turn" cadence hunger/thirst/stamina
    // regen already use (see onPlayerAct()/tickEnemyNeeds()/
    // tickVillagerNeeds()). Live here, not on Player, so an NPC mage could
    // eventually buff itself (or debuff someone else) too. Explicit named
    // counters rather than a generic buff-list — there are only a handful of
    // these, a lookup table would be more machinery than the thing it replaces.
    int fireShieldTicks = 0; // Fire Shield: reflects incoming melee damage while > 0 (combat.h)
    int skinHardenTicks = 0; // Skin Hardening: + defense while > 0 (Player::totalDefense())
    int lightnessTicks  = 0; // Lightness: + speed while > 0 (main.cpp's tickWorld())
    int accelTicks  = 0; // Acceleration: bigger + speed while > 0 (main.cpp's tickWorld())
    int slowedTicks = 0; // Slowness (Water, targeted debuff): -speed while > 0 (main.cpp's tickWorld())

    // --- Social ---
    // Disposition to the player: -100 (enemy on sight) .. 0 (neutral) .. +100 (loyal ally)
    // Thresholds: <-50 attack on sight | -50..-20 hostile | -20..+20 neutral
    //             +20..+50 friendly    | >+50 ally (defends player)
    int factionId   = 0;
    int disposition = 0;

    // --- Skills ---
    // Shared progression for weapons AND magic (docs/weapons.md, docs/magic.md).
    // Lives here, not on Player, so every enemy/villager has real skills too.
    SkillLevel skills[(int)Skill::SKILL_COUNT];
    SkillLevel& skill(Skill s) { return skills[(int)s]; }
    const SkillLevel& skill(Skill s) const { return skills[(int)s]; }

    // ---- Constructor ----
    Actor(int x, int y, const char* symbol, SDL_Color color,
          int baseSpeed, Race race = Race::HUMAN,
          int str = 10, int dex = 10, int intel = 10,
          int con = 10, int per = 10, int cha = 10)
        : symbol(symbol), color(color),
          x(x), y(y),
          baseSpeed(baseSpeed), race(race),
          strength(str), dexterity(dex), intelligence(intel),
          constitution(con), perception(per), charisma(cha)
    {
        id = nextActorId++;

        const RaceTraits& rt = raceTraits[(int)race];
        strength     += rt.strMod;
        dexterity    += rt.dexMod;
        intelligence += rt.intMod;
        constitution += rt.conMod;
        perception   += rt.perMod;
        charisma     += rt.chaMod;

        name = Names::generate(race);
        age  = Names::generateAge(race);
        if (rt.minAge != 0 || rt.maxAge != 0)
            naturalDeathAge = (int)(rt.maxAge * (0.90f + (rand() % 21) / 100.0f));

        maxStamina = 50.0f + constitution * 5.0f;
        stamina    = maxStamina;

        sync();
    }

    // ---- Sync cached stats from body ----
    void sync() {
        hp    = body.totalHp();
        maxHp = body.totalMaxHp();
        speed = std::max(1, (int)(baseSpeed * body.speedMult()));
        alive = !body.isDead();
    }

    // ---- Life-stage stat effects (docs/village.md) ----
    // Point-of-use modifiers, same shape as the buff ticks above — nothing
    // caches an "effective" stat, callers ask for the multiplier/modifier
    // where they need it. Skills deliberately do NOT decay with age (doc:
    // old veterans stay skilled, just physically weaker — future teachers).
    float ageStrMult() const {
        switch (lifeStageFor(age, race)) {
            case LifeStage::CHILD:       return 0.4f;
            case LifeStage::TEEN:        return 0.7f;
            case LifeStage::YOUNG_ADULT: return 0.85f;
            case LifeStage::ELDER:       return 0.85f;
            case LifeStage::OLD:         return 0.6f;
            default:                     return 1.0f;
        }
    }
    int ageSpeedMod() const {
        switch (lifeStageFor(age, race)) {
            case LifeStage::CHILD:       return -20;
            case LifeStage::TEEN:        return -10;
            case LifeStage::YOUNG_ADULT: return -5;
            case LifeStage::ELDER:       return -5;
            case LifeStage::OLD:         return -15;
            default:                     return 0;
        }
    }

    // Every speed modifier composed in one place — the single source of truth
    // for "how fast can this Actor currently act." tickWorld() (main.cpp) uses
    // this to grant energy, and UI panels (ui.h's Speed line, bottom_panel.h's
    // SPD:) call it too so what's displayed always matches what's actually
    // applied, instead of each display re-deriving its own partial subset.
    int effectiveSpeed() const {
        return std::max(1, speed - needsSpeedPenalty() + ageSpeedMod()
                            + (lightnessTicks > 0 ? 15 : 0)
                            + (accelTicks     > 0 ? 30 : 0)
                            - (slowedTicks    > 0 ? 15 : 0));
    }

    // ---- Damage ----
    // target: which body part is hit (default torso)
    // sharp:  slashing/piercing weapons can sever limbs
    // forceBleed/severChancePct/guaranteedDeepWound: weapon/technique effects,
    // see BodyPart::applyDamage
    void takeDamage(int amount, PartTarget target = PartTarget::TORSO,
                    bool sharp = false, bool forceBleed = false, int severChancePct = 40,
                    bool guaranteedDeepWound = false) {
        if (!alive) return;
        bool canSever = (target != PartTarget::HEAD && target != PartTarget::TORSO);
        body.get(target).applyDamage(amount, sharp, canSever, forceBleed, severChancePct,
                                      guaranteedDeepWound);
        sync();
    }

    // ---- Per world-tick updates ----
    // Call once per tick for all living actors. inWater: caller (main.cpp,
    // which has map access this header doesn't) tells us whether this actor
    // is currently standing on a water tile — swimming (moveActorTo(),
    // main.cpp) already spent stamina entering it; here that just means no
    // passive regen while still in the water, and once stamina is fully
    // spent, drowning starts (torso HP drain, same "torso is the death gate"
    // pattern as bleed) — gives a window to reach shore rather than an
    // instant kill the moment the counter hits zero.
    void tickNeeds(bool inWater = false) {
        const RaceTraits& rt = raceTraits[(int)race];
        if (rt.needsFood)  hunger = std::min(1.0f, hunger + 0.000347f); // ~48h to starve
        if (rt.needsWater) thirst = std::min(1.0f, thirst + 0.000694f); // ~24h to dehydrate

        if (inWater) {
            if (stamina <= 0.0f) {
                constexpr int DROWN_DAMAGE = 5;
                body.torso.hp = std::max(0, body.torso.hp - DROWN_DAMAGE);
                sync();
            }
        } else {
            stamina = std::min(maxStamina, stamina + 3.0f);
        }

        if (fireShieldTicks > 0) fireShieldTicks--;
        if (skinHardenTicks > 0) skinHardenTicks--;
        if (lightnessTicks  > 0) lightnessTicks--;
        if (accelTicks  > 0) accelTicks--;
        if (slowedTicks > 0) slowedTicks--;

        // Bleeding drains torso HP
        if (rt.canBleed) {
            float bleed = body.totalBleed();
            if (bleed > 0.0f) {
                body.torso.hp = std::max(0, body.torso.hp - (int)bleed);
                sync();
            }
        }

        body.clot(); // outside the canBleed gate — harmless no-op for races that never bleed in the first place
    }

    // ---- Turn system ----
    void gainEnergy()        { energy += speed; }
    bool canAct()      const { return energy >= 100; }
    void spendEnergy()       { energy -= 100; }
    bool isAlive()     const { return alive; }

    // ---- Stamina helpers (consumers arrive with the first technique/spell) ----
    bool hasStamina(float amount)  const { return stamina >= amount; }
    void spendStamina(float amount)      { stamina = std::max(0.0f, stamina - amount); }

    // ---- Disposition helpers ----
    bool isHostile()   const { return disposition < -20; }
    bool isNeutral()   const { return disposition >= -20 && disposition <= 20; }
    bool isFriendly()  const { return disposition > 20; }
    bool isAlly()      const { return disposition > 50; }

    // ---- Hunger/thirst severity + penalties ----
    // Only touch hunger/thirst (both live on Actor), so this works for every
    // Enemy/Villager too, not just Player — needed by effectiveSpeed() above
    // and combat.h's ageStrMult() sibling. hunger/thirst: 0=full/hydrated,
    // 1=starving/dying. Returns 0-3 severity level for each need.
    int hungerLevel() const {
        if (hunger >= 1.0f) return 4; // dead
        if (hunger >= 0.75f) return 3;
        if (hunger >= 0.50f) return 2;
        if (hunger >= 0.25f) return 1;
        return 0;
    }
    int thirstLevel() const {
        if (thirst >= 1.0f) return 4;
        if (thirst >= 0.75f) return 3;
        if (thirst >= 0.50f) return 2;
        if (thirst >= 0.25f) return 1;
        return 0;
    }

    // Display label for the current hunger/thirst level (0 = no debuff, "").
    const char* hungerLabel() const {
        static const char* n[] = {"", "Peckish", "Hungry", "Very Hungry", "Starving"};
        return n[hungerLevel()];
    }
    const char* thirstLabel() const {
        static const char* n[] = {"", "Thirsty", "Very Thirsty", "Parched", "Dehydrated"};
        return n[thirstLevel()];
    }

    // Speed penalty from hunger/thirst (subtracted from speed in tickWorld).
    int hungerSpeedPenalty() const {
        static const int p[] = {0, 5, 15, 30, 40};
        return p[std::min(hungerLevel(), 3)];
    }
    int thirstSpeedPenalty() const {
        static const int p[] = {0, 8, 20, 38, 50};
        return p[std::min(thirstLevel(), 3)];
    }
    int needsSpeedPenalty() const { return hungerSpeedPenalty() + thirstSpeedPenalty(); }

    // STR penalty from hunger/thirst.
    int hungerStrPenalty() const {
        static const int p[] = {0, 1, 2, 4, 6};
        return p[std::min(hungerLevel(), 3)];
    }
    int thirstStrPenalty() const {
        static const int p[] = {0, 1, 3, 5, 7};
        return p[std::min(thirstLevel(), 3)];
    }
    int needsStrPenalty() const { return hungerStrPenalty() + thirstStrPenalty(); }
};

// ================================================================ Player

struct Player : Actor {
    int medicineSkill = 0;

    // Equipment slots — optional so empty slots cost nothing.
    std::optional<Item> worn[(int)EquipSlot::SLOT_COUNT];

    // Which container's contents are shown in the inventory UI (default: BACK).
    int activeContainerSlot = (int)EquipSlot::BACK;

    // ---- Effective stats (base + item bonuses) ----
    // hunger/thirst severity/penalty helpers moved up to Actor (they only
    // touch hunger/thirst, which live there too) — see the block right
    // before Actor's closing brace above.

    int effectiveStr() const {
        int b = 0;
        for (int i = 0; i < (int)EquipSlot::SLOT_COUNT; i++)
            if (worn[i].has_value()) b += worn[i]->strBonus;
        return std::max(1, strength + b - needsStrPenalty());
    }
    int effectiveDex() const {
        int b = 0;
        for (int i = 0; i < (int)EquipSlot::SLOT_COUNT; i++)
            if (worn[i].has_value()) b += worn[i]->dexBonus;
        return dexterity + b;
    }
    int effectiveCon() const {
        int b = 0;
        for (int i = 0; i < (int)EquipSlot::SLOT_COUNT; i++)
            if (worn[i].has_value()) b += worn[i]->conBonus;
        return constitution + b;
    }
    int strItemBonus() const {
        int b = 0;
        for (int i = 0; i < (int)EquipSlot::SLOT_COUNT; i++)
            if (worn[i].has_value()) b += worn[i]->strBonus;
        return b;
    }
    int dexItemBonus() const {
        int b = 0;
        for (int i = 0; i < (int)EquipSlot::SLOT_COUNT; i++)
            if (worn[i].has_value()) b += worn[i]->dexBonus;
        return b;
    }
    int conItemBonus() const {
        int b = 0;
        for (int i = 0; i < (int)EquipSlot::SLOT_COUNT; i++)
            if (worn[i].has_value()) b += worn[i]->conBonus;
        return b;
    }

    // ---- Inventory helpers ----

    // Total carry volume across all worn containers.
    int totalCarryVolume() const {
        int v = 500; // bare hands / pockets
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++)
            if (worn[s].has_value() && worn[s]->isContainer())
                v += worn[s]->maxVolume;
        return v;
    }

    int usedCarryVolume() const {
        int v = 0;
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++)
            if (worn[s].has_value() && worn[s]->isContainer())
                v += worn[s]->usedVolume();
        return v;
    }

    // Total armor defense from all worn armor pieces.
    int totalDefense() const {
        int d = 0;
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++)
            if (worn[s].has_value()) d += worn[s]->defense;
        if (skinHardenTicks > 0) d += 8; // Skin Hardening buff
        return d;
    }

    // Light radius from any equipped item (torch, lantern, etc.).
    int totalLightRadius() const {
        int best = 0;
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++)
            if (worn[s].has_value()) best = std::max(best, worn[s]->lightRadius);
        return best;
    }

    // Item in the attacking hand (HAND_R, or HAND_L as fallback), or nullptr if unarmed.
    const Item* weaponItem() const {
        if (worn[(int)EquipSlot::HAND_R].has_value()) return &*worn[(int)EquipSlot::HAND_R];
        if (worn[(int)EquipSlot::HAND_L].has_value()) return &*worn[(int)EquipSlot::HAND_L];
        return nullptr;
    }

    // Weapon damage (HAND_R, or HAND_L as fallback, or base 1).
    int weaponDamage() const {
        const Item* w = weaponItem();
        return w ? w->damage : 1; // unarmed
    }

    bool hasChopTool() const {
        int hands[] = {(int)EquipSlot::HAND_R, (int)EquipSlot::HAND_L};
        for (int s : hands)
            if (worn[s].has_value() && worn[s]->canChop) return true;
        return false;
    }

    bool hasMineTool() const {
        int hands[] = {(int)EquipSlot::HAND_R, (int)EquipSlot::HAND_L};
        for (int s : hands)
            if (worn[s].has_value() && worn[s]->canMine) return true;
        return false;
    }

    // Try to add item to the best available container (BACK first, then WAIST, etc.).
    // Stacks into an existing matching stack when possible. Returns true on success.
    bool addToContainer(Item item) {
        int order[] = {
            (int)EquipSlot::BACK,  (int)EquipSlot::WAIST,
            (int)EquipSlot::HANDS, (int)EquipSlot::HAND_L
        };
        for (int s : order)
            if (worn[s].has_value() && ::addToContainer(*worn[s], item)) return true;
        // Try any remaining container slot
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++)
            if (worn[s].has_value() && ::addToContainer(*worn[s], item)) return true;
        return false;
    }

    Player(int x, int y,
           int str = 10, int dex = 10, int intel = 10,
           int con = 10, int per = 10, int cha = 10)
        : Actor(x, y, "@", {255, 255, 255, 255},
                100 + (dex - 10) * 2,
                Race::HUMAN, str, dex, intel, con, per, cha)
    {
        name = "Player";
        age  = 25;
        body = Body(35, 70, 45, 55);
        sync();
    }
};

// ================================================================ Enemy
// Temporary class — will be merged into NPC when the full NPC/AI system is built.

struct Enemy : Actor {
    int   aggroRange;
    bool  flees     = false;
    float fleeHpPct = 0.0f;   // flee when hp/maxHp drops below this fraction

    // Everything they have — weapon, armor, loot, rations — lives inside this one
    // real container (no floating/invisible items). Dropped in full on death.
    std::optional<Item> bag;

    const Item* weaponItem() const {
        if (!bag) return nullptr;
        for (const Item& item : bag->contents)
            if (item.type == ItemType::WEAPON) return &item;
        return nullptr;
    }

    int weaponDmg() const {
        const Item* w = weaponItem();
        return w ? w->damage : 0;
    }

    Enemy(int x, int y, const char* sym, SDL_Color col,
          int speed, Race race, int aggroRange,
          int headHp = 25, int torsoHp = 50, int armHp = 35, int legHp = 45)
        : Actor(x, y, sym, col, speed, race),
          aggroRange(aggroRange)
    {
        body        = Body(headHp, torsoHp, armHp, legHp);
        disposition = -80;
        sync();
    }

    bool wantsToFlee() const {
        return flees && maxHp > 0 && (float)hp / maxHp < fleeHpPct;
    }
};

// ================================================================ Animal

struct Animal : Actor {
    bool hostile;
    int  fleeRange;

    Animal(int x, int y, const char* sym, SDL_Color col,
           int speed, Race race, bool hostile, int fleeRange)
        : Actor(x, y, sym, col, speed, race),
          hostile(hostile), fleeRange(fleeRange)
    {
        disposition = hostile ? -60 : 0;
    }
};
