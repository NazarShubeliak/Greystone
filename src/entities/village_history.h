#pragma once
// Shared map-free village demography engine (docs/world.md's "Шар 3" —
// simulateVillageYear() in isolation). Extracted out of worldgen_sim.cpp so
// both it and main.cpp's pre-history household generation (grave placement
// at village spawn) run the exact same rules instead of drifting into two
// copies — that already happened once (the sibling-marriage bug had to be
// fixed in two places) and is worth avoiding a second time.
//
// Deliberately NOT the same code as main.cpp's live, currently-visited-
// village simulateVillageYear() — that one is map-aware (tile search for
// children, occupation matched to real generated buildings) because it
// drives actual gameplay. This header has no map to be aware of and never
// will; anything here operates purely on Villager records.

#include "npc.h"
#include <algorithm>
#include <string>
#include <vector>

// Namespaced deliberately: main.cpp already has its own giveOccupation()/
// spawnChild()/areRelated()/countStrings() and MARRIAGE_CHANCE_PERCENT-style
// constants for its map-aware live simulateVillageYear() — same concepts,
// different (map-aware) implementations. Without a namespace, #including
// this header from main.cpp would redefine every one of those names in the
// same translation unit. worldgen_sim.cpp has no such conflicts and can
// `using namespace VillageHistory;` for brevity.
namespace VillageHistory {

struct LegendEvent {
    int         year;
    std::string text;
};

inline int countStrings(const char** arr) {
    int n = 0; while (arr[n]) n++; return n;
}

// Picks "First Surname" that doesn't already collide with an existing villager
// sharing that surname — without this, a 20-name first-name pool funneled
// through a handful of surviving surnames produces confusing log lines like
// "Brand Mason was born to Gareth Mason and Gareth Mason" where the two
// parents are genuinely different people who just drew the same name. Only
// checks within the same surname (a same first name across different
// surnames reads fine) and is best-effort: a large enough family eventually
// runs out of fresh combinations from a 20-name pool, so this falls back to
// a possible repeat rather than looping forever or inventing suffixes.
inline std::string pickFirstName(const std::vector<Villager>& vs, const std::string& surname) {
    int nFirstNames = countStrings(NPC_FIRST_NAMES);
    std::string candidate;
    for (int tries = 0; tries < 30; tries++) {
        candidate = std::string(NPC_FIRST_NAMES[rand() % nFirstNames]) + " " + surname;
        bool taken = false;
        for (const Villager& v : vs)
            if (v.name == candidate) { taken = true; break; }
        if (!taken) return candidate;
    }
    return candidate;
}

// No bag/goods here, unlike main.cpp's giveOccupation() — a demography-only
// history log doesn't care what a villager carries.
inline void giveOccupation(Villager& v, Occupation occ) { v.occupation = occ; }

// Creates one child of motherIdx/fatherIdx. Simpler than main.cpp's spawnChild() —
// no map means no tile search, no bag/outfit; just the demographic facts.
// motherIdx/fatherIdx are just consistent slot names, same as main.cpp's version —
// the game has no gender mechanic.
inline int spawnChild(std::vector<Villager>& vs, int motherIdx, int fatherIdx, int ageOverride = 0) {
    const Villager& father = vs[fatherIdx];

    Villager child;
    child.isChild = true;
    child.age     = ageOverride;

    std::string surname = father.name.substr(father.name.find(' ') + 1);
    child.name = pickFirstName(vs, surname);

    child.motherId = motherIdx;
    child.fatherId = fatherIdx;

    int childId = (int)vs.size();
    vs.push_back(child);
    vs[fatherIdx].childIds.push_back(childId);
    vs[motherIdx].childIds.push_back(childId);
    return childId;
}

// Whether aIdx/bIdx are parent-child or share a parent, for the marriage
// eligibility check below. Checks all four motherId/fatherId cross-
// combinations rather than assuming a.motherId lines up with b.motherId
// specifically: seed-generation's initial batch and simulateOneYear()'s
// ongoing births pass call spawnChild() with motherIdx/fatherIdx in opposite
// index order (spouse/primary vs. lower/higher index), so two siblings — one
// from each source — can end up with their shared parents recorded in
// swapped slots. A same-slot-only check missed exactly that case and let
// siblings marry (confirmed in a real 100-year run); this doesn't care which
// slot a shared parent landed in.
inline bool areRelated(const Villager& a, int aIdx, const Villager& b, int bIdx) {
    if (a.motherId == bIdx || a.fatherId == bIdx) return true;
    if (b.motherId == aIdx || b.fatherId == aIdx) return true;
    auto shared = [](int x, int y) { return x >= 0 && x == y; };
    return shared(a.motherId, b.motherId) || shared(a.motherId, b.fatherId)
        || shared(a.fatherId, b.motherId) || shared(a.fatherId, b.fatherId);
}

// A villager has a *reliable* food source if they work one of the village's
// five named trades themselves, or a living spouse/parent/child does (the
// rest of village.md's economic cycle is assumed to flow from there —
// "фермер годує коваля..."). Doesn't reach further than immediate family
// (no sibling/grandparent support) — a scoped simplification.
inline bool hasReliableTrade(const std::vector<Villager>& vs, int i) {
    const Villager& v = vs[i];
    if (v.occupation != Occupation::NONE) return true;
    auto worksAndAlive = [&](int idx) {
        return idx >= 0 && idx < (int)vs.size() && vs[idx].alive && vs[idx].occupation != Occupation::NONE;
    };
    if (worksAndAlive(v.spouseId) || worksAndAlive(v.motherId) || worksAndAlive(v.fatherId)) return true;
    for (int cid : v.childIds)
        if (worksAndAlive(cid)) return true;
    return false;
}

inline constexpr int MARRIAGE_CHANCE_PERCENT        = 20;
inline constexpr int BIRTH_CHANCE_PERCENT           = 15;
inline constexpr int APPRENTICE_CHANCE_PERCENT      = 25;
inline constexpr int GARDEN_FAILURE_CHANCE_PERCENT  = 5;  // per year, per household with no reliable trade
inline constexpr int FAMINE_DEATH_PERCENT_DEPENDENT = 40; // child or Old, during a failed-harvest year
inline constexpr int FAMINE_DEATH_PERCENT_ADULT     = 10; // working-age, same year — lower, but real

// One year of village life — aging/old-age death, growing up, occupation
// succession (with apprenticeship when no heir exists), famine, marriage,
// births. See individual pass comments below for the reasoning behind each
// rule; all of it is confirmed against real multi-generation test runs
// (worldgen_sim.exe), not just written and assumed correct.
inline void simulateOneYear(std::vector<Villager>& vs, int year, std::vector<LegendEvent>& log) {
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
    // Heir order: spouse first, else the first alive child (same as
    // main.cpp's transferGranary()/simulateVillageYear()). If neither exists,
    // docs/village.md's other stated path applies — "йде в учні до майстра
    // без спадкоємця": any unrelated adult with no trade of their own can
    // pick up the vacant one, rolled per year rather than instantly (so a
    // profession dying out reads as an actual gap in village history, not a
    // same-year auto-fill) — this is what stops Blacksmith/Herbalist/etc.
    // from staying vacant forever once their founding line has no children.
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& deceased = vs[i];
        if (deceased.alive || deceased.occupation == Occupation::NONE) continue;

        int heirIdx = -1;
        bool viaFamily = true;
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

        if (heirIdx < 0) {
            viaFamily = false;
            std::vector<int> apprentices;
            for (int k = 0; k < (int)vs.size(); k++)
                if (vs[k].alive && !vs[k].isChild && vs[k].occupation == Occupation::NONE)
                    apprentices.push_back(k);
            if (!apprentices.empty() && (rand() % 100) < APPRENTICE_CHANCE_PERCENT)
                heirIdx = apprentices[rand() % apprentices.size()];
        }

        if (heirIdx >= 0 && vs[heirIdx].occupation == Occupation::NONE) {
            Occupation occ = deceased.occupation;
            giveOccupation(vs[heirIdx], occ);
            deceased.occupation = Occupation::NONE;
            log.push_back({year, viaFamily
                ? vs[heirIdx].name + " takes up the family trade as " + occupationName(occ) + "."
                : vs[heirIdx].name + " apprentices as the village's new " + occupationName(occ) + ", the trade having no heir."});
        }
    }

    // ---- Famine: docs/village.md's iron rule, Dwarf Fortress-style — no
    // food means no food, for anyone, not just the young/old. A household
    // without a reliable trade (see hasReliableTrade()) still has its own
    // small garden/plot near the house ("це ж село по-іншому не може
    // бути") — that normally covers them, but can fail in a given year
    // (bad harvest). Checked after occupation succession so a same-year heir
    // or apprentice still counts as a reliable trade in time.
    //
    // Resolved once per married couple (they share the same household/
    // garden, not two independent rolls), then children inherit whichever
    // parent's household they belong to. An earlier version applied this to
    // every unfed adult individually and wiped out ~80% of a 100-year test
    // village, because only 5 named trades exist regardless of population —
    // rolling per household instead of per unemployed relative-chain is what
    // keeps this from re-exploding the same way.
    std::vector<bool> famined(vs.size(), false);
    std::vector<bool> gardenRolled(vs.size(), false);
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& v = vs[i];
        if (!v.alive || v.isChild || gardenRolled[i]) continue;
        gardenRolled[i] = true;
        if (hasReliableTrade(vs, i)) continue;

        bool failedHarvest = (rand() % 100) < GARDEN_FAILURE_CHANCE_PERCENT;
        famined[i] = failedHarvest;
        if (v.spouseId >= 0 && v.spouseId < (int)vs.size() && vs[v.spouseId].alive) {
            famined[v.spouseId]      = failedHarvest;
            gardenRolled[v.spouseId] = true;
        }
    }

    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& v = vs[i];
        if (!v.alive) continue;

        bool inFamine = famined[i];
        if (v.isChild) {
            inFamine = (v.motherId >= 0 && v.motherId < (int)vs.size() && famined[v.motherId])
                    || (v.fatherId >= 0 && v.fatherId < (int)vs.size() && famined[v.fatherId]);
        }
        if (!inFamine) continue;

        bool dependent = v.isChild || lifeStageFor(v.age, v.race) == LifeStage::OLD;
        int deathChance = dependent ? FAMINE_DEATH_PERCENT_DEPENDENT : FAMINE_DEATH_PERCENT_ADULT;
        if ((rand() % 100) < deathChance) {
            v.body.torso.hp = 0;
            v.sync();
            if (!v.alive)
                log.push_back({year, v.name + " has died of starvation after a failed harvest."});
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
            if (areRelated(vs[i], i, vs[j], j)) continue;
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
        // No famine gate here — a bad-harvest year (see the pass above) is a
        // one-year event, not a standing disqualifier, and most couples
        // without one of the five named trades still have a working garden
        // most years; gating births on it would suppress most of a growing
        // population's children for no real reason.

        if ((rand() % 100) < BIRTH_CHANCE_PERCENT) {
            int childId = spawnChild(vs, i, j);
            log.push_back({year, vs[childId].name + " was born to " + vs[i].name + " and " + vs[j].name + "."});
        }
    }
}

// ==================================================================
// Persistent per-village pre-history (docs/world.md step 2: "Worldgen:
// прогін N років для кожного села, spawnVillagers() читає результат").
//
// One simulated household — founder + spouse seeded young, aged forward
// historyYears in isolation (no cross-household marriage; today's
// "2 beds per building" model can't absorb a spouse relocating to a
// different house during pre-history — a documented limit, not an
// oversight). occupation on every member is a placeholder (FARMER for
// farm-type, WOODCUTTER for trade-type) — hasReliableTrade()/succession
// only ever check occupation != NONE, never the specific trade, so the
// exact enum value is inert here. Which *real* trade this household
// actually represents (Farmer/Herbalist/Blacksmith/Elder/Woodcutter)
// isn't knowable map-free — it depends on which building placeVillage()
// (map.cpp, map-dependent, can skip a role after 30 failed placement
// tries) actually generates for a given village. The caller overwrites
// the resolved head-of-household's occupation with the real one once
// the physical building is known.
struct HouseholdHistory {
    bool isFarmHousehold;
    std::string surname;
    std::vector<Villager> hist;   // full simulated roster, dead members kept (alive=false)
    std::vector<int> diedAtYear;  // parallel to hist; -1 = still alive at "now"
    // Deaths with diedAtYear <= this have already had a grave physically
    // placed by spawnVillagers() on some earlier visit. Starts at 0 (nobody
    // placed yet) and advances to VillageHistoryRecord::yearsSimulated each
    // time spawnVillagers() processes this household — so a village ticked
    // forward while the player was elsewhere (advanceVillageOneYear() below)
    // only gets graves for the *newly* dead placed on the next visit, not a
    // second copy of ones already standing in the graveyard.
    int gravesPlacedThroughYear = 0;
};

inline HouseholdHistory simulateHousehold(const std::string& surname, bool isFarmHousehold, int historyYears,
                                           std::vector<LegendEvent>& log) {
    HouseholdHistory h;
    h.isFarmHousehold = isFarmHousehold;
    h.surname         = surname;

    Villager founder;
    founder.name = pickFirstName(h.hist, surname);
    founder.age  = Names::generateAge(Race::HUMAN, raceTraits[(int)Race::HUMAN].minAge,
                                                     raceTraits[(int)Race::HUMAN].minAge + 8);
    giveOccupation(founder, isFarmHousehold ? Occupation::FARMER : Occupation::WOODCUTTER);
    h.hist.push_back(founder);

    Villager foundingSpouse;
    int lo = std::max(raceTraits[(int)Race::HUMAN].minAge, h.hist[0].age - 5);
    int hi = std::min(raceTraits[(int)Race::HUMAN].maxAge, h.hist[0].age + 5);
    foundingSpouse.age  = Names::generateAge(Race::HUMAN, lo, hi);
    foundingSpouse.name = pickFirstName(h.hist, surname);
    h.hist.push_back(foundingSpouse);
    h.hist[0].spouseId = 1;
    h.hist[1].spouseId = 0;
    if (!isFarmHousehold && (rand() % 100) < 20)
        giveOccupation(h.hist[1], Occupation::SEAMSTRESS);

    h.diedAtYear.assign(h.hist.size(), -1);
    for (int year = 1; year <= historyYears; year++) {
        size_t before = h.hist.size();
        std::vector<bool> wasAlive(before);
        for (size_t k = 0; k < before; k++) wasAlive[k] = h.hist[k].alive;

        simulateOneYear(h.hist, year, log);

        h.diedAtYear.resize(h.hist.size(), -1);
        for (size_t k = 0; k < before; k++)
            if (wasAlive[k] && !h.hist[k].alive) h.diedAtYear[k] = year;
    }
    return h;
}

// Advances one already-simulated household by exactly one more year (docs/
// world.md step 5: "Подієва симуляція віддалених сіл під час гри"). Same
// mechanics as the yearly loop inside simulateHousehold() above, just for a
// single `year` number handed in by the caller instead of an internal 1..N
// loop — used to keep ticking a village that already has a founding history
// but isn't the one currently loaded on the map.
inline void advanceHousehold(HouseholdHistory& h, int year, std::vector<LegendEvent>& log) {
    size_t before = h.hist.size();
    std::vector<bool> wasAlive(before);
    for (size_t k = 0; k < before; k++) wasAlive[k] = h.hist[k].alive;

    simulateOneYear(h.hist, year, log);

    h.diedAtYear.resize(h.hist.size(), -1);
    for (size_t k = 0; k < before; k++)
        if (wasAlive[k] && !h.hist[k].alive) h.diedAtYear[k] = year;
}

// One village's cached pre-history: 2 farm-type + 3 trade-type households,
// matching worldgen_sim.cpp's seedVillage() role count (2 farms always
// guaranteed by placeVillage(); Smith/Elder/Woodcutter may or may not
// actually get placed on the map — unused cached households are simply
// never consumed by the caller). yearsSimulated is the total number of
// years this record has been advanced since founding (starts at
// simulateVillageHistory()'s historyYears, +1 per advanceVillageOneYear()
// call) — it's the "year" argument fed to advanceHousehold() and also what
// a grave's diedYearsAgo is computed relative to, since pre-history and
// later distant-tick years share one continuous numbering. `log` is the
// merged, year-sorted chronicle across all 5 households (docs/world.md
// "Legends-лог подій") — read by the `legends` cheat-console export
// (main.cpp) for the standalone legends_viewer.exe tool.
struct VillageHistoryRecord {
    std::vector<HouseholdHistory> farmHouseholds;
    std::vector<HouseholdHistory> tradeHouseholds;
    int yearsSimulated = 0;
    std::vector<LegendEvent> log;
};

inline VillageHistoryRecord simulateVillageHistory(int villageSurnameSeed, int historyYears = 60) {
    VillageHistoryRecord rec;
    int nSurnames = countStrings(NPC_SURNAMES);
    auto surnameFor = [&](int slot) { return std::string(NPC_SURNAMES[(villageSurnameSeed + slot) % nSurnames]); };

    rec.farmHouseholds.push_back(simulateHousehold(surnameFor(0), true,  historyYears, rec.log));
    rec.farmHouseholds.push_back(simulateHousehold(surnameFor(1), true,  historyYears, rec.log));
    rec.tradeHouseholds.push_back(simulateHousehold(surnameFor(2), false, historyYears, rec.log)); // Smith
    rec.tradeHouseholds.push_back(simulateHousehold(surnameFor(3), false, historyYears, rec.log)); // Elder
    rec.tradeHouseholds.push_back(simulateHousehold(surnameFor(4), false, historyYears, rec.log)); // Woodcutter
    rec.yearsSimulated = historyYears;

    // Each of the 5 calls above ran its own full 1..historyYears loop one
    // after another, so rec.log is 5 chronologically-sorted runs concatenated
    // back-to-back rather than one — sort once here so the merged chronicle
    // reads in actual year order.
    std::stable_sort(rec.log.begin(), rec.log.end(),
                      [](const LegendEvent& a, const LegendEvent& b) { return a.year < b.year; });
    return rec;
}

// Ticks every household of one village forward by one year — called for
// every village *other* than the one currently loaded on the map (that one
// advances live instead, via main.cpp's tickAgingOneYear()/
// simulateVillageYear() acting on the real placed Villager objects). All 5
// households advance the same `rec.yearsSimulated` in this one call, so
// appending straight to rec.log (no re-sort needed) keeps it in order.
inline void advanceVillageOneYear(VillageHistoryRecord& rec) {
    rec.yearsSimulated++;
    for (HouseholdHistory& h : rec.farmHouseholds)  advanceHousehold(h, rec.yearsSimulated, rec.log);
    for (HouseholdHistory& h : rec.tradeHouseholds) advanceHousehold(h, rec.yearsSimulated, rec.log);
}

} // namespace VillageHistory
