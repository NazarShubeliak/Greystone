// Standalone headless village-history simulator ("Legends mode", docs/world.md).
//
// Runs a village's demography forward N years and prints the event chronicle +
// final family roster to stdout. No SDL window, no player, no map, no game loop —
// just docs/village.md's rules running on plain Villager records. This is
// world.md's "Шар 3" engine in isolation (step 1 of its own Порядок реалізації:
// "Рушій simulateVillageYear(): сім'ї, віки, народження, шлюби, смерті,
// спадщина + лог подій" — nothing else yet).
//
// The engine itself (aging/marriage/births/succession/famine) lives in
// src/entities/village_history.h, shared with main.cpp's pre-history household
// generation (village grave placement at spawn) — this file is now just a
// thin driver: seed a fresh village, run the shared yearly tick N times,
// print the result.
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
#include "village_history.h"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

using namespace VillageHistory; // no conflicting names in this file — safe to pull in wholesale

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

    int nSurnames = countStrings(NPC_SURNAMES);

    for (int r = 0; r < (int)roles.size(); r++) {
        const Role& role = roles[r];
        std::string surname = NPC_SURNAMES[r % nSurnames];

        Villager primary; // ctor already rolls a HUMAN adult age + naturalDeathAge
        primary.name = pickFirstName(vs, surname);
        giveOccupation(primary, role.occ);
        int primaryIdx = (int)vs.size();
        vs.push_back(primary);

        // vs already contains primary at this point, so pickFirstName() naturally
        // avoids handing the spouse the exact same first name too.
        Villager spouse;
        int lo = std::max(raceTraits[(int)Race::HUMAN].minAge, vs[primaryIdx].age - 10);
        int hi = std::min(raceTraits[(int)Race::HUMAN].maxAge, vs[primaryIdx].age + 10);
        spouse.age  = Names::generateAge(Race::HUMAN, lo, hi);
        spouse.name = pickFirstName(vs, surname);
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
