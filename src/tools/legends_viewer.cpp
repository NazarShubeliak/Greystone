// Standalone "Legends Viewer" (docs/world.md "Legends-лог подій"), Dwarf
// Fortress-style — reads the plain-text export produced by the real game's
// `legends` cheat-console command (cheat_console.h / main.cpp::exportLegends())
// and lets you interactively browse every cached village's chronicle
// (year-by-year events: marriages, births, deaths, occupation succession)
// and current household roster.
//
// Deliberately reads the actual running game's exported data, not a fresh
// simulation of its own (unlike worldgen_sim.exe) — the point is to see what
// happened in the world you're actually playing, not a disconnected sample.
// Fully self-contained: no game headers, no SDL2, nothing but the standard
// library — the export format is plain text, so there's nothing to link
// against.
//
// Build (no -I flags needed at all — see worldgen_sim.exe's build command
// for comparison, which does need one for Villager/SDL_Color):
//   C:/msys64/mingw64/bin/g++.exe -std=c++17 -O2 -o legends_viewer.exe src/tools/legends_viewer.cpp
//
// Usage: legends_viewer.exe [legends.txt]

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct LegendEventEntry {
    int         year;
    std::string text;
};

struct RosterMember {
    std::string name;
    int         age    = 0;
    bool        alive  = true;
    std::string occupation;
    bool        isChild = false;
};

struct Household {
    bool                       isFarm = false;
    std::vector<RosterMember>  members;
};

struct VillageRecord {
    int                             secX = 0, secY = 0, yearsSimulated = 0;
    std::vector<LegendEventEntry>   chronicle;
    std::vector<Household>          households;
};

// Splits a "|"-delimited line into fields. The export format never puts a
// literal "|" inside a field (names/event text come from fixed word pools
// and templates that don't use it), so a plain split is safe.
static std::vector<std::string> splitPipe(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, '|')) fields.push_back(field);
    return fields;
}

static std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

// Parses main.cpp::exportLegends()'s format. Permissive: if a header/count
// line doesn't parse (unexpected file, truncated export), the affected
// section just comes out empty rather than crashing — same
// graceful-degradation spirit the rest of the codebase uses for worldgen
// edge cases.
static std::vector<VillageRecord> loadLegends(const std::string& filename) {
    std::vector<VillageRecord> villages;
    std::ifstream f(filename);
    if (!f) {
        std::cerr << "Could not open " << filename << "\n";
        return villages;
    }

    std::string line;
    std::getline(f, line); // "GREYSTONE LEGENDS EXPORT 1" — not validated, just skipped
    std::getline(f, line);
    int nVillages = 0;
    std::sscanf(line.c_str(), "VILLAGES %d", &nVillages);

    for (int i = 0; i < nVillages && std::getline(f, line); i++) {
        VillageRecord v;
        std::sscanf(line.c_str(), "VILLAGE %d %d %d", &v.secX, &v.secY, &v.yearsSimulated);

        std::getline(f, line);
        int nEvents = 0;
        std::sscanf(line.c_str(), "CHRONICLE %d", &nEvents);
        for (int e = 0; e < nEvents && std::getline(f, line); e++) {
            size_t bar = line.find('|');
            LegendEventEntry ev;
            ev.year = std::atoi(line.substr(0, bar).c_str());
            ev.text = (bar != std::string::npos) ? line.substr(bar + 1) : line;
            v.chronicle.push_back(ev);
        }

        std::getline(f, line);
        int nHouseholds = 0;
        std::sscanf(line.c_str(), "ROSTER %d", &nHouseholds);
        for (int h = 0; h < nHouseholds; h++) {
            std::getline(f, line);
            int isFarm = 0, nMembers = 0;
            std::sscanf(line.c_str(), "HOUSEHOLD %d %d", &isFarm, &nMembers);
            Household hh;
            hh.isFarm = isFarm != 0;
            for (int m = 0; m < nMembers && std::getline(f, line); m++) {
                std::vector<std::string> fields = splitPipe(line);
                RosterMember rm;
                if (fields.size() >= 5) {
                    rm.name       = fields[0];
                    rm.age        = std::atoi(fields[1].c_str());
                    rm.alive      = fields[2] == "1";
                    rm.occupation = fields[3];
                    rm.isChild    = fields[4] == "1";
                }
                hh.members.push_back(rm);
            }
            std::getline(f, line); // ENDHOUSEHOLD
            v.households.push_back(hh);
        }
        std::getline(f, line); // ENDVILLAGE
        villages.push_back(v);
    }

    return villages;
}

static void waitForEnter() {
    std::cout << "\nPress Enter to continue...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

static void printVillageList(const std::vector<VillageRecord>& villages, const std::string& filename) {
    std::cout << "\n=== Greystone Legends ===\n";
    std::cout << "Loaded " << villages.size() << " village(s) from " << filename << "\n\n";
    for (size_t i = 0; i < villages.size(); i++) {
        const VillageRecord& v = villages[i];
        int living = 0, total = 0;
        for (const Household& h : v.households)
            for (const RosterMember& m : h.members) { total++; if (m.alive) living++; }
        std::cout << "  " << (i + 1) << ". Village (" << v.secX << "," << v.secY << ") -- "
                   << v.yearsSimulated << " yrs, " << v.chronicle.size() << " events, "
                   << living << "/" << total << " living\n";
    }
    std::cout << "\n  s. Search by name\n  q. Quit\n\n> ";
}

static void printVillageDetail(const VillageRecord& v) {
    std::cout << "\n=== Village (" << v.secX << "," << v.secY << ") -- "
               << v.yearsSimulated << " years of history ===\n\n";

    std::cout << "--- Chronicle (" << v.chronicle.size() << " events) ---\n";
    if (v.chronicle.empty()) std::cout << "  (nothing happened yet)\n";
    for (const LegendEventEntry& e : v.chronicle)
        std::cout << "Year " << e.year << ": " << e.text << "\n";

    std::cout << "\n--- Households ---\n";
    for (const Household& h : v.households) {
        std::cout << "\n[" << (h.isFarm ? "Farm" : "Trade") << " household]\n";
        for (const RosterMember& m : h.members) {
            std::cout << "  " << (m.alive ? "  " : "[dead] ") << m.name << ", age " << m.age;
            if (!m.occupation.empty()) std::cout << ", " << m.occupation;
            if (m.isChild) std::cout << " (child)";
            std::cout << "\n";
        }
    }
    waitForEnter();
}

static void searchByName(const std::vector<VillageRecord>& villages) {
    std::cout << "Search name: ";
    std::string query;
    std::getline(std::cin, query);
    if (query.empty()) return;
    std::string qLower = toLower(query);

    std::cout << "\n";
    bool found = false;
    for (const VillageRecord& v : villages) {
        for (const Household& h : v.households) {
            for (const RosterMember& m : h.members) {
                if (toLower(m.name).find(qLower) == std::string::npos) continue;
                found = true;
                std::cout << "  " << m.name << " -- Village (" << v.secX << "," << v.secY << "), "
                          << (m.alive ? "alive, age " + std::to_string(m.age) : "deceased");
                if (!m.occupation.empty()) std::cout << ", " << m.occupation;
                std::cout << "\n";
            }
        }
    }
    if (!found) std::cout << "  No matches.\n";
    waitForEnter();
}

int main(int argc, char** argv) {
    std::string filename = argc > 1 ? argv[1] : "legends.txt";
    std::vector<VillageRecord> villages = loadLegends(filename);
    if (villages.empty()) {
        std::cout << "No villages loaded from " << filename
                   << " — run the game, use the `legends` cheat-console command, then re-run this tool.\n";
        return 1;
    }

    std::string choice;
    while (true) {
        printVillageList(villages, filename);
        if (!std::getline(std::cin, choice)) break;
        if (choice == "q" || choice == "Q") break;
        if (choice == "s" || choice == "S") { searchByName(villages); continue; }

        int idx = std::atoi(choice.c_str());
        if (idx >= 1 && idx <= (int)villages.size())
            printVillageDetail(villages[idx - 1]);
    }

    return 0;
}
