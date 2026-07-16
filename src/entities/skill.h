#pragma once

// ================================================================ Skill
//
// Shared progression system for weapons AND magic (docs/weapons.md, docs/magic.md).
// Lives in Actor (not Player) — every creature in the world has the same skill set,
// per the world-symmetry rule in CLAUDE.md.

enum class Skill {
    SWORD, GREATSWORD, AXE, GREATAXE, MACE, GREATMACE,
    DAGGER, SPEAR, UNARMED, SHIELD, DUAL_WIELD,
    FIRE, WATER, EARTH, AIR, LIFE, DEATH,
    SKILL_COUNT
};

static const char* skillNames[(int)Skill::SKILL_COUNT] = {
    "Sword", "Greatsword", "Axe", "Greataxe", "Mace", "Greatmace",
    "Dagger", "Spear", "Unarmed", "Shield", "Dual Wield",
    "Fire", "Water", "Earth", "Air", "Life", "Death"
};

inline const char* skillName(Skill s) { return skillNames[(int)s]; }

struct SkillLevel {
    int level = 0;   // 0..100
    int exp   = 0;

    int expToNext() const { return 10 + level * 5; }

    // Returns true if this gain caused at least one level-up.
    bool gain(int amount) {
        if (level >= 100) return false;
        exp += amount;
        bool leveledUp = false;
        while (level < 100 && exp >= expToNext()) {
            exp -= expToNext();
            level++;
            leveledUp = true;
        }
        if (level >= 100) exp = 0;
        return leveledUp;
    }
};
