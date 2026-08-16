// Standalone headless village-history simulator ("Legends mode", docs/world.md).
//
// Runs a village's demography forward N years and prints the event chronicle +
// final family roster to stdout. No SDL window, no player, no map, no game loop —
// just docs/village.md's rules running on plain Villager records. This is
// world.md's "Шар 3" engine in isolation (step 1 of its own Порядок реалізації:
// "Рушій simulateVillageYear(): сім'ї, віки, народження, шлюби, смерті,
// спадщина + лог подій" — nothing else yet).
//
// Deliberately NOT unified with main.cpp's simulateVillageYear(): that version is
// map-aware (places children on real tiles, matches occupations to generated
// buildings) because it drives the live, currently-visited village. This one has
// no map to be aware of. Same rules, separate implementation — see CLAUDE.md/the
// plan for why merging them into one truly engine-agnostic function is deferred
// until both halves have proven themselves on their own.
//
// Build (separate from greystone.exe, no SDL2 linking needed — nothing here calls
// an actual SDL runtime function, only uses SDL_Color/SDL_Point as plain structs):
//   C:/msys64/mingw64/bin/g++.exe -std=c++17 -O2 -o worldgen_sim.exe src/tools/worldgen_sim.cpp -Isrc/core -Isrc/entities -I"C:/msys64/mingw64/include/SDL2"
//
// Usage: worldgen_sim.exe [years=100] [villages=1] [seed=random]

// SDL.h unconditionally #define's main -> SDL_main on Windows (via SDL_main.h)
// and expects libSDL2main to supply a WinMain that calls it — that's the actual
// cause of the "undefined reference to WinMain" link error without this, not a
// missing SDL2 lib. SDL_MAIN_HANDLED turns that redefinition off; nothing here
// calls SDL_Init or needs SDL2's runtime, only SDL_Color/SDL_Point as plain
// structs (pulled in transitively via npc.h), so no SDL2 linking is needed.
#define SDL_MAIN_HANDLED
#include "npc.h"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

struct LegendEvent {
    int         year;
    std::string text;
};

static int countStrings(const char** arr) {
    int n = 0; while (arr[n]) n++; return n;
}

// No bag/goods here, unlike main.cpp's giveOccupation() — a demography-only
// history log doesn't care what a villager carries.
static void giveOccupation(Villager& v, Occupation occ) { v.occupation = occ; }

// Creates one child of motherIdx/fatherIdx. Simpler than main.cpp's spawnChild() —
// no map means no tile search, no bag/outfit; just the demographic facts.
// motherIdx/fatherIdx are just consistent slot names, same as main.cpp's version —
// the game has no gender mechanic.
static int spawnChild(std::vector<Villager>& vs, int motherIdx, int fatherIdx, int ageOverride = 0) {
    const Villager& father = vs[fatherIdx];

    Villager child;
    child.isChild = true;
    child.age     = ageOverride;

    int fnIdx = rand() % countStrings(NPC_FIRST_NAMES);
    std::string surname = father.name.substr(father.name.find(' ') + 1);
    child.name = std::string(NPC_FIRST_NAMES[fnIdx]) + " " + surname;

    child.motherId = motherIdx;
    child.fatherId = fatherIdx;

    int childId = (int)vs.size();
    vs.push_back(child);
    vs[fatherIdx].childIds.push_back(childId);
    vs[motherIdx].childIds.push_back(childId);
    return childId;
}

static constexpr int MARRIAGE_CHANCE_PERCENT = 20;
static constexpr int BIRTH_CHANCE_PERCENT    = 15;

// One year of village life — same four passes and tuning constants as main.cpp's
// simulateVillageYear() (plus the old-age death main.cpp keeps in a separate
// tickAgingOneYear()), reimplemented without any map dependency.
static void simulateOneYear(std::vector<Villager>& vs, int year, std::vector<LegendEvent>& log) {
    // ---- Aging + old-age death ----
    for (Villager& v : vs) {
        if (!v.alive) continue;
        v.age++;
        if (v.naturalDeathAge > 0 && v.age >= v.naturalDeathAge) {
            v.body.torso.hp = 0;
            v.sync();
            if (!v.alive)
                log.push_back({year, v.name + " has died of old age."});
        }
    }

    // ---- Growing up: child -> adult at the race's age of adulthood ----------
    for (Villager& v : vs) {
        if (!v.alive || !v.isChild) continue;
        if (v.age >= raceTraits[(int)v.race].minAge) {
            v.isChild = false;
            log.push_back({year, v.name + " has grown into an adult."});
        }
    }

    // ---- Occupation succession: a dead worker's job passes to an heir -------
    // Same heir order as main.cpp's transferGranary()/simulateVillageYear() —
    // spouse first, else the first alive child — retried yearly until someone
    // old enough qualifies.
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& deceased = vs[i];
        if (deceased.alive || deceased.occupation == Occupation::NONE) continue;

        int heirIdx = -1;
        if (deceased.spouseId >= 0 && deceased.spouseId < (int)vs.size()
            && vs[deceased.spouseId].alive && !vs[deceased.spouseId].isChild) {
            heirIdx = deceased.spouseId;
        } else {
            for (int cid : deceased.childIds) {
                if (cid >= 0 && cid < (int)vs.size() && vs[cid].alive && !vs[cid].isChild) {
                    heirIdx = cid;
                    break;
                }
            }
        }

        if (heirIdx >= 0 && vs[heirIdx].occupation == Occupation::NONE) {
            Occupation occ = deceased.occupation;
            giveOccupation(vs[heirIdx], occ);
            deceased.occupation = Occupation::NONE;
            log.push_back({year, vs[heirIdx].name + " takes up the family trade as " + occupationName(occ) + "."});
        }
    }

    // ---- Marriage: pair up eligible unmarried/widowed adults -----------------
    std::vector<int> eligible;
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& v = vs[i];
        if (!v.alive || v.isChild) continue;
        if (v.age < raceTraits[(int)v.race].minAge) continue;
        bool free = v.spouseId < 0 || v.spouseId >= (int)vs.size() || !vs[v.spouseId].alive;
        if (free) eligible.push_back(i);
    }
    std::vector<bool> matchedThisYear(vs.size(), false);
    for (int i : eligible) {
        if (matchedThisYear[i]) continue;
        if ((rand() % 100) >= MARRIAGE_CHANCE_PERCENT) continue;

        std::vector<int> candidates;
        for (int j : eligible) {
            if (j == i || matchedThisYear[j]) continue;
            const Villager& a = vs[i];
            const Villager& b = vs[j];
            bool related = (a.motherId >= 0 && (a.motherId == b.motherId || a.motherId == j))
                         || (a.fatherId >= 0 && (a.fatherId == b.fatherId || a.fatherId == j))
                         || (b.motherId == i) || (b.fatherId == i);
            if (related) continue;
            candidates.push_back(j);
        }
        if (candidates.empty()) continue;

        int j = candidates[rand() % candidates.size()];
        vs[i].spouseId = j;
        vs[j].spouseId = i;
        matchedThisYear[i] = matchedThisYear[j] = true;
        log.push_back({year, vs[i].name + " and " + vs[j].name + " are married."});
    }

    // ---- Births: married couples at full adulthood might have a child --------
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& a = vs[i];
        if (!a.alive || a.isChild || a.spouseId < 0) continue;
        int j = a.spouseId;
        if (j <= i || j >= (int)vs.size()) continue; // handle each pair once
        Villager& b = vs[j];
        if (!b.alive) continue;

        if (lifeStageFor(a.age, a.race) != LifeStage::ADULT) continue;
        if (lifeStageFor(b.age, b.race) != LifeStage::ADULT) continue;

        if ((rand() % 100) < BIRTH_CHANCE_PERCENT) {
            int childId = spawnChild(vs, i, j);
            log.push_back({year, vs[childId].name + " was born to " + vs[i].name + " and " + vs[j].name + "."});
        }
    }
}

// Approximates the composition spawnVillagers()/placeVillage() (main.cpp, map.cpp)
// tend to produce: 2 farm households (one has a 45% chance to roll Herbalist
// instead, matching map.cpp's actual roll) plus one household each for
// Blacksmith/Elder/Woodcutter. The real map-based generator doesn't actually
// guarantee the latter three (placeRoleBuilding() can fail to find room after 30
// tries and silently skip that building) — that's a physical-placement detail
// with no map here to make it apply, so this always seeds all five.
static std::vector<Villager> seedVillage() {
    std::vector<Villager> vs;

    struct Role { Occupation occ; bool canHaveKids; };
    std::vector<Role> roles = {
        { (rand() % 100) < 45 ? Occupation::HERBALIST : Occupation::FARMER, true  },
        { Occupation::FARMER,     true  },
        { Occupation::BLACKSMITH, false },
        { Occupation::ELDER,      false },
        { Occupation::WOODCUTTER, false },
    };

    int nFirstNames = countStrings(NPC_FIRST_NAMES);
    int nSurnames   = countStrings(NPC_SURNAMES);

    for (int r = 0; r < (int)roles.size(); r++) {
        const Role& role = roles[r];
        const char* surname = NPC_SURNAMES[r % nSurnames];

        Villager primary; // ctor already rolls a HUMAN adult age + naturalDeathAge
        primary.name = std::string(NPC_FIRST_NAMES[rand() % nFirstNames]) + " " + surname;
        giveOccupation(primary, role.occ);
        int primaryIdx = (int)vs.size();
        vs.push_back(primary);

        Villager spouse;
        int lo = std::max(raceTraits[(int)Race::HUMAN].minAge, vs[primaryIdx].age - 10);
        int hi = std::min(raceTraits[(int)Race::HUMAN].maxAge, vs[primaryIdx].age + 10);
        spouse.age  = Names::generateAge(Race::HUMAN, lo, hi);
        spouse.name = std::string(NPC_FIRST_NAMES[rand() % nFirstNames]) + " " + surname;
        int spouseIdx = (int)vs.size();
        vs.push_back(spouse);

        vs[primaryIdx].spouseId = spouseIdx;
        vs[spouseIdx].spouseId  = primaryIdx;

        bool isFarmHousehold = (role.occ == Occupation::FARMER || role.occ == Occupation::HERBALIST);
        if (!isFarmHousehold && (rand() % 100) < 20)
            giveOccupation(vs[spouseIdx], Occupation::SEAMSTRESS);

        if (role.canHaveKids && (rand() % 100) < 60) {
            int youngerParentAge = std::min(vs[primaryIdx].age, vs[spouseIdx].age);
            int maxChildAge      = youngerParentAge - 16; // parent was at least 16 at birth
            if (maxChildAge >= 1) {
                int nKids = 1 + rand() % 3;
                for (int k = 0; k < nKids; k++)
                    spawnChild(vs, spouseIdx, primaryIdx, Names::generateAge(Race::HUMAN, 1, std::min(17, maxChildAge)));
            }
        }
    }

    return vs;
}

int main(int argc, char** argv) {
    int      years    = argc > 1 ? std::atoi(argv[1]) : 100;
    int      villages  = argc > 2 ? std::atoi(argv[2]) : 1;
    unsigned seed      = argc > 3 ? (unsigned)std::atoi(argv[3]) : (unsigned)time(nullptr);

    std::cout << "Greystone worldgen simulator -- " << years << " years, "
              << villages << " village(s), seed " << seed << "\n\n";

    for (int vi = 0; vi < villages; vi++) {
        srand(seed + (unsigned)vi * 7919u); // distinct but reproducible per village

        std::vector<Villager>   vs = seedVillage();
        std::vector<LegendEvent> log;

        for (int year = 1; year <= years; year++)
            simulateOneYear(vs, year, log);

        std::cout << "==================== Village " << (vi + 1) << " ====================\n";
        std::cout << "--- Chronicle (" << log.size() << " events) ---\n";
        for (const LegendEvent& e : log)
            std::cout << "Year " << e.year << ": " << e.text << "\n";

        int aliveCount = 0;
        for (const Villager& v : vs) if (v.alive) aliveCount++;

        std::cout << "\n--- Final roster after " << years << " years (" << aliveCount << " living, "
                  << ((int)vs.size() - aliveCount) << " deceased, " << vs.size()
                  << " total ever founded/born) ---\n";
        for (const Villager& v : vs) {
            std::cout << (v.alive ? "  " : "  [dead] ")
                      << v.name << ", age " << v.age
                      << " (" << lifeStageName(lifeStageFor(v.age, v.race)) << ")";
            if (v.occupation != Occupation::NONE)
                std::cout << ", " << occupationName(v.occupation);
            if (v.spouseId >= 0 && v.spouseId < (int)vs.size())
                std::cout << ", spouse of " << vs[v.spouseId].name;
            if (v.motherId >= 0 || v.fatherId >= 0) {
                std::cout << ", child of ";
                if (v.motherId >= 0 && v.motherId < (int)vs.size()) std::cout << vs[v.motherId].name;
                if (v.motherId >= 0 && v.fatherId >= 0)             std::cout << " and ";
                if (v.fatherId >= 0 && v.fatherId < (int)vs.size()) std::cout << vs[v.fatherId].name;
            }
            if (!v.childIds.empty())
                std::cout << ", parent of " << v.childIds.size() << " child(ren)";
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    return 0;
}
