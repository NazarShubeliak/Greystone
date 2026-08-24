#pragma once
#include "overmap.h"
#include "astar.h"
#include "time_system.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <string>
#include <cstring>

struct CheatConsole {
    bool        visible = false;
    std::string input;
    std::string result;
    bool        resultOk = true;   // true = green, false = red

    // Pending teleport — set by execute(), consumed by main.cpp.
    bool pendingTeleport = false;
    int  tpX = 0, tpY = 0;

    // Pending enemy spawn — set by execute(), consumed by main.cpp.
    std::string pendingSpawn;
    int         pendingSpawnCount = 1;

    // Pending give — item name set by execute(), consumed by main.cpp.
    std::string pendingGive;

    // Pending grow_plants — set by execute(), consumed by main.cpp.
    bool pendingGrowPlants = false;

    // Pending setskill — skill token + target level, consumed by main.cpp.
    std::string pendingSetSkill;
    int         pendingSetSkillLevel = 0;

    // Pending setage — target age, consumed by main.cpp. -1 = none pending
    // (tests the life-stage system, docs/village.md, without waiting the
    // 518400 actions a single in-game year actually takes).
    int pendingSetAge = -1;

    // Pending skipyears — number of in-game years to fast-forward, consumed
    // by main.cpp. 0 = none pending. Tests simulateVillageYear() (marriage,
    // births, growing up, occupation succession) across many generations
    // without waiting the real number of actions that many years would take.
    int pendingSkipYears = 0;

    // Pending simulatedays — number of in-game days to actually tick minute
    // by minute (unlike skipyears, which just jumps the calendar), consumed
    // by main.cpp. 0 = none pending. Runs the same per-minute tick sequence
    // the Wait panel does (needs, plant growth, live village demography) —
    // for testing hunger-gated behavior (NPC-to-NPC trade, the harvest/
    // carry cycle) without manually sitting through real Wait presses.
    int pendingSimulateDays = 0;

    // Pending legends export — filename set by execute(), consumed by
    // main.cpp. Empty string = none pending (a non-empty filename is always
    // needed even for the default, so an explicit bool flag alongside it —
    // rather than checking string-emptiness like pendingGive/pendingUnlockTech
    // do — avoids ambiguity if a filename ever legitimately needed to be "").
    bool        pendingExportLegends = false;
    std::string pendingLegendsFilename;

    // Pending unlocktech — technique token, consumed by main.cpp.
    std::string pendingUnlockTech;

    // Pending unlockspell — spell token, consumed by main.cpp.
    std::string pendingUnlockSpell;

    // Pending lich transformation — consumed by main.cpp (sets player.race =
    // Race::LICH and clears naturalDeathAge, same undead-immortal pattern
    // Vampire/Skeleton/Ghost already use). Cheat-only for now — a real Death
    // school ritual is future work (docs/magic.md); this exists so long
    // multi-day simulatedays/skipyears tests don't require constantly
    // re-feeding/re-watering the player character just to keep testing.
    bool pendingBecomeLich = false;

    // Lowercase, no-space tokens — order MUST match enum Skill (skill.h) so
    // main.cpp can map token index -> Skill index directly.
    static constexpr const char* SKILL_TOKENS[] = {
        "sword","greatsword","axe","greataxe","mace","greatmace",
        "dagger","spear","unarmed","shield","dualwield",
        "fire","water","earth","air","life","death", nullptr
    };
    // Order MUST match enum TechniqueId (combat.h).
    static constexpr const char* TECH_TOKENS[] = { "brutalstrike","lunge","backstab", nullptr };
    // Order MUST match enum SpellId (magic.h).
    static constexpr const char* SPELL_TOKENS[] = {
        "spark","fireball","wallofire","explosion","fireshield",
        "createwater","waterjet","raincall","slowness",
        "stone","stonewall","reclaimwall","wallthrow","architect","skinhardening",
        "gust","vortex","lightness","acceleration",
        "minorheal",
        "withering", nullptr
    };

    void open() {
        visible = true;
        input.clear();
        result.clear();
        SDL_StartTextInput();
    }

    void close() {
        visible = false;
        SDL_StopTextInput();
    }

    // Returns true if the event was consumed.
    bool handleEvent(SDL_Event& e, Overmap& overmap, WorldTime& wt) {
        if (!visible) return false;

        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE:
                    close();
                    return true;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    execute(overmap, wt);
                    return true;
                case SDLK_BACKSPACE:
                    if (!input.empty()) input.pop_back();
                    result.clear();
                    return true;
                case SDLK_TAB:
                    tabComplete();
                    result.clear();
                    return true;
                default:
                    return true;  // consume all other keys
            }
        }

        if (e.type == SDL_TEXTINPUT) {
            input += e.text.text;
            result.clear();
            return true;
        }

        return false;
    }

    void execute(Overmap& overmap, WorldTime& wt) {
        std::string cmd = input;
        input.clear();

        if (cmd.empty()) return;

        // ── Commands ─────────────────────────────────────────────────────
        if (cmd == "grow_plants" || cmd == "gp") {
            pendingGrowPlants = true;
            result   = "All plants matured.";
            resultOk = true;

        } else if (cmd == "reveal_map" || cmd == "rm") {
            for (int y = 0; y < OVERMAP_H; y++)
                for (int x = 0; x < OVERMAP_W; x++)
                    overmap.sectors[y][x].explored = true;
            result  = "All sectors revealed.";
            resultOk = true;

        } else if (cmd == "hide_map" || cmd == "hm") {
            for (int y = 0; y < OVERMAP_H; y++)
                for (int x = 0; x < OVERMAP_W; x++)
                    overmap.sectors[y][x].explored = false;
            result  = "Map hidden.";
            resultOk = true;

        } else if (cmd == "help") {
            result   = "Commands: rm  hm  tp X Y  time HH:MM  season <s>  spawn <type> [N]  give <item>  "
                       "setskill <skill> <lvl>  setage <age>  skipyears <n>  simulatedays <n>  unlocktech <name>  unlockspell <name>  "
                       "legends [file]  lich  gp  help";
            resultOk = true;

        } else if (cmd == "lich") {
            pendingBecomeLich = true;
            result   = "You feel your heart stop. Hunger and thirst no longer touch you.";
            resultOk = true;

        } else {
            // tp X Y
            int x, y;
            if (sscanf(cmd.c_str(), "tp %d %d", &x, &y) == 2) {
                if (x >= 0 && x < OVERMAP_W && y >= 0 && y < OVERMAP_H) {
                    pendingTeleport = true;
                    tpX = x; tpY = y;
                    result   = "Teleporting to [" + std::to_string(x) + ", " + std::to_string(y) + "]...";
                    resultOk = true;
                } else {
                    result   = "Out of range. Valid: 0-" + std::to_string(OVERMAP_W-1) + " / 0-" + std::to_string(OVERMAP_H-1);
                    resultOk = false;
                }
                return;
            }

            // time HH:MM  or  time HH MM
            int th, tm;
            if (sscanf(cmd.c_str(), "time %d:%d", &th, &tm) == 2 ||
                sscanf(cmd.c_str(), "time %d %d", &th, &tm) == 2) {
                if (th >= 0 && th < 24 && tm >= 0 && tm < 60) {
                    int dayBase = (wt.minutes / (60 * 24)) * (60 * 24);
                    wt.minutes  = dayBase + th * 60 + tm;
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%02d:%02d", th, tm);
                    result   = std::string("Time set to ") + buf + ".";
                    resultOk = true;
                } else {
                    result   = "Invalid time. Use: time HH:MM  (0-23 : 0-59)";
                    resultOk = false;
                }
                return;
            }

            // season <name>
            char sname[32];
            if (sscanf(cmd.c_str(), "season %31s", sname) == 1) {
                // Lowercase comparison
                for (int i = 0; sname[i]; i++)
                    sname[i] = (char)tolower((unsigned char)sname[i]);

                int targetSeason = -1;
                if (strcmp(sname, "spring") == 0) targetSeason = 0;
                else if (strcmp(sname, "summer") == 0) targetSeason = 1;
                else if (strcmp(sname, "autumn") == 0 || strcmp(sname, "fall") == 0) targetSeason = 2;
                else if (strcmp(sname, "winter") == 0) targetSeason = 3;

                if (targetSeason >= 0) {
                    int timeOfDay = wt.minutes % (60 * 24);
                    int targetMonth = targetSeason * 3; // first month of the season
                    wt.minutes = targetMonth * 30 * 24 * 60 + timeOfDay;
                    result   = std::string("Season set to ") + wt.seasonName() + " (" + wt.monthName() + ", Day 1).";
                    resultOk = true;
                } else {
                    result   = "Unknown season. Use: spring  summer  autumn  winter";
                    resultOk = false;
                }
                return;
            }

            // spawn <type> [count]
            char etype[32]; int count = 1;
            if (sscanf(cmd.c_str(), "spawn %31s %d", etype, &count) >= 1) {
                for (int i = 0; etype[i]; i++)
                    etype[i] = (char)tolower((unsigned char)etype[i]);
                count = std::max(1, std::min(count, 20));

                static const char* valid[] = {
                    "goblin","orc","skeleton","wolf","bandit", nullptr
                };
                bool ok = false;
                for (int i = 0; valid[i]; i++) if (strcmp(etype, valid[i]) == 0) { ok = true; break; }

                if (ok) {
                    pendingSpawn      = etype;
                    pendingSpawnCount = count;
                    result   = "Spawning " + std::to_string(count) + "x " + etype + ".";
                    resultOk = true;
                } else {
                    result   = "Unknown type. Use: goblin  orc  skeleton  wolf  bandit";
                    resultOk = false;
                }
                return;
            }

            // give <item>
            char gitem[32];
            if (sscanf(cmd.c_str(), "give %31s", gitem) == 1) {
                for (int i = 0; gitem[i]; i++) gitem[i] = (char)tolower((unsigned char)gitem[i]);
                static const char* validItems[] = {
                    "sword","dagger","axe","club","boneclub",
                    "helmet","vest","boots","backpack","pouch",
                    "hatchet","pickaxe",
                    "torch","lantern","bread","water",
                    "ring","amulet","log","stone","branch", nullptr
                };
                bool ok = false;
                for (int i = 0; validItems[i]; i++) if (strcmp(gitem, validItems[i]) == 0) { ok = true; break; }
                if (ok) {
                    pendingGive = gitem;
                    result   = std::string("Added ") + gitem + " to inventory.";
                    resultOk = true;
                } else {
                    result   = "Unknown item. Tab-complete or type 'give' for list.";
                    resultOk = false;
                }
                return;
            }

            // setskill <skill> <level>
            char skname[32]; int slevel;
            if (sscanf(cmd.c_str(), "setskill %31s %d", skname, &slevel) == 2) {
                for (int i = 0; skname[i]; i++) skname[i] = (char)tolower((unsigned char)skname[i]);
                bool ok = false;
                for (int i = 0; SKILL_TOKENS[i]; i++) if (strcmp(skname, SKILL_TOKENS[i]) == 0) { ok = true; break; }
                if (!ok) {
                    result   = "Unknown skill. Tab-complete or type 'setskill' for list.";
                    resultOk = false;
                } else if (slevel < 0 || slevel > 100) {
                    result   = "Level must be 0-100.";
                    resultOk = false;
                } else {
                    pendingSetSkill      = skname;
                    pendingSetSkillLevel = slevel;
                    result   = std::string("Set ") + skname + " to level " + std::to_string(slevel) + ".";
                    resultOk = true;
                }
                return;
            }

            // setage <age>
            int newAge;
            if (sscanf(cmd.c_str(), "setage %d", &newAge) == 1) {
                if (newAge < 0 || newAge > 999) {
                    result   = "Age must be 0-999.";
                    resultOk = false;
                } else {
                    pendingSetAge = newAge;
                    result   = "Set age to " + std::to_string(newAge) + ".";
                    resultOk = true;
                }
                return;
            }

            // skipyears <n>
            int nYears;
            if (sscanf(cmd.c_str(), "skipyears %d", &nYears) == 1) {
                if (nYears < 1 || nYears > 1000) {
                    result   = "Years must be 1-1000.";
                    resultOk = false;
                } else {
                    pendingSkipYears = nYears;
                    result   = "Fast-forwarding " + std::to_string(nYears) + " year(s)...";
                    resultOk = true;
                }
                return;
            }

            // simulatedays <n>
            int nDays;
            if (sscanf(cmd.c_str(), "simulatedays %d", &nDays) == 1) {
                if (nDays < 1 || nDays > 3650) {
                    result   = "Days must be 1-3650.";
                    resultOk = false;
                } else {
                    pendingSimulateDays = nDays;
                    result   = "Simulating " + std::to_string(nDays) + " day(s) minute by minute...";
                    resultOk = true;
                }
                return;
            }

            // legends [filename] — export every cached village's chronicle +
            // roster (docs/world.md "Legends-лог подій") to a text file, read
            // by the standalone legends_viewer.exe. No filename = "legends.txt".
            if (cmd == "legends" || cmd.rfind("legends ", 0) == 0) {
                std::string fname = cmd.size() > 8 ? cmd.substr(8) : "legends.txt";
                if (fname.empty()) fname = "legends.txt";
                pendingExportLegends  = true;
                pendingLegendsFilename = fname;
                result   = "Exporting legends to " + fname + "...";
                resultOk = true;
                return;
            }

            // unlocktech <name>
            char tname[32];
            if (sscanf(cmd.c_str(), "unlocktech %31s", tname) == 1) {
                for (int i = 0; tname[i]; i++) tname[i] = (char)tolower((unsigned char)tname[i]);
                bool ok = false;
                for (int i = 0; TECH_TOKENS[i]; i++) if (strcmp(tname, TECH_TOKENS[i]) == 0) { ok = true; break; }
                if (ok) {
                    pendingUnlockTech = tname;
                    result   = std::string("Unlocked ") + tname + " (skill raised to its required level if needed).";
                    resultOk = true;
                } else {
                    result   = "Unknown technique. Use: brutalstrike  lunge  backstab";
                    resultOk = false;
                }
                return;
            }

            // unlockspell <name>
            char spname[32];
            if (sscanf(cmd.c_str(), "unlockspell %31s", spname) == 1) {
                for (int i = 0; spname[i]; i++) spname[i] = (char)tolower((unsigned char)spname[i]);
                bool ok = false;
                for (int i = 0; SPELL_TOKENS[i]; i++) if (strcmp(spname, SPELL_TOKENS[i]) == 0) { ok = true; break; }
                if (ok) {
                    pendingUnlockSpell = spname;
                    result   = std::string("Unlocked ") + spname + " (skill raised to its required level if needed).";
                    resultOk = true;
                } else {
                    result   = "Unknown spell. Use: spark  fireball  wallofire  explosion  fireshield  createwater  "
                               "waterjet  raincall  slowness  stone  stonewall  reclaimwall  wallthrow  architect  "
                               "skinhardening  gust  vortex  lightness  acceleration  minorheal  withering";
                    resultOk = false;
                }
                return;
            }

            result  = "Unknown: " + cmd + "   (type 'help')";
            resultOk = false;
        }
    }

    // Returns contextual hint text based on what's been typed so far.
    std::string getHints() const {
        auto starts = [&](const char* prefix) {
            return input.rfind(prefix, 0) == 0;
        };
        if (input.empty())
            return "Commands: rm  hm  tp  time  season  spawn  give  setskill  setage  skipyears  unlocktech  unlockspell  legends  lich  gp  help    (Tab autocompletes)";
        if (input == "spawn" || starts("spawn "))
            return "spawn: goblin  orc  skeleton  wolf  bandit   e.g. spawn goblin 3";
        if (input == "give" || starts("give "))
            return "give: sword  dagger  axe  club  boneclub  helmet  vest  boots  backpack  pouch  hatchet  pickaxe  torch  lantern  bread  water  ring  amulet  log  stone  branch";
        if (input == "season" || starts("season "))
            return "season: spring  summer  autumn  winter";
        if (input == "time" || starts("time "))
            return "time HH:MM   e.g. time 14:30";
        if (input == "tp" || starts("tp "))
            return "tp X Y   (sector coords 0-99)";
        if (input == "setskill" || starts("setskill "))
            return "setskill <skill> <0-100>   skills: sword greatsword axe greataxe mace greatmace "
                   "dagger spear unarmed shield dualwield fire water earth air life death   e.g. setskill axe 30";
        if (input == "setage" || starts("setage "))
            return "setage <age>   sets your age directly — tests life-stage speed/STR effects "
                   "and old-age death without waiting real in-game years. e.g. setage 70";
        if (input == "skipyears" || starts("skipyears "))
            return "skipyears <n>   fast-forwards n in-game years — tests marriage/births/growing up/"
                   "occupation succession in the current village. e.g. skipyears 20";
        if (input == "simulatedays" || starts("simulatedays "))
            return "simulatedays <n>   actually ticks n days minute-by-minute (needs, plant growth, "
                   "village demography) — unlike skipyears, hunger/harvest/trade states fire for real. e.g. simulatedays 10";
        if (input == "unlocktech" || starts("unlocktech "))
            return "unlocktech: brutalstrike  lunge  backstab";
        if (input == "unlockspell" || starts("unlockspell "))
            return "unlockspell: spark  fireball  wallofire  explosion  fireshield  createwater  "
                   "waterjet  raincall  slowness  stone  stonewall  reclaimwall  wallthrow  architect  "
                   "skinhardening  gust  vortex  lightness  acceleration  minorheal  withering";
        if (input == "legends" || starts("legends "))
            return "legends [filename]   exports every cached village's chronicle + roster to a text "
                   "file (default legends.txt) — read it with legends_viewer.exe. e.g. legends";
        if (input == "lich")
            return "lich   transforms you into a Lich (cheat-only for now) — no more hunger/thirst/aging, "
                   "for long simulatedays/skipyears tests without needing to eat/drink.";
        // Partial command suggestions
        static const char* cmds[] = {
            "reveal_map","rm","hide_map","hm","tp","time","season","spawn","give",
            "setskill","setage","skipyears","simulatedays","unlocktech","unlockspell","legends","lich","help", nullptr
        };
        std::string sugg;
        for (int i = 0; cmds[i]; i++) {
            if (std::string(cmds[i]).rfind(input, 0) == 0) {
                if (!sugg.empty()) sugg += "  ";
                sugg += cmds[i];
            }
        }
        return sugg.empty() ? "" : "  " + sugg;
    }

    void tabComplete() {
        // Complete top-level command
        static const char* cmds[] = {
            "reveal_map","hide_map","tp","time","season","spawn","give",
            "setskill","setage","skipyears","simulatedays","unlocktech","unlockspell","legends","lich","help", nullptr
        };
        if (input.find(' ') == std::string::npos) {
            for (int i = 0; cmds[i]; i++) {
                if (!input.empty() && std::string(cmds[i]).rfind(input, 0) == 0) {
                    input = std::string(cmds[i]) + " ";
                    return;
                }
            }
            return;
        }
        // Complete spawn argument
        if (input.rfind("spawn ", 0) == 0) {
            std::string arg = input.substr(6);
            static const char* types[] = {"goblin","orc","skeleton","wolf","bandit", nullptr};
            for (int i = 0; types[i]; i++)
                if (std::string(types[i]).rfind(arg, 0) == 0) { input = "spawn " + std::string(types[i]); return; }
        }
        // Complete give argument
        if (input.rfind("give ", 0) == 0) {
            std::string arg = input.substr(5);
            static const char* items[] = {
                "sword","dagger","axe","club","boneclub","helmet","vest","boots",
                "backpack","pouch","hatchet","pickaxe","torch","lantern","bread","water",
                "ring","amulet","log","stone","branch", nullptr
            };
            for (int i = 0; items[i]; i++)
                if (std::string(items[i]).rfind(arg, 0) == 0) { input = "give " + std::string(items[i]); return; }
        }
        // Complete season argument
        if (input.rfind("season ", 0) == 0) {
            std::string arg = input.substr(7);
            static const char* seasons[] = {"spring","summer","autumn","winter", nullptr};
            for (int i = 0; seasons[i]; i++)
                if (std::string(seasons[i]).rfind(arg, 0) == 0) { input = "season " + std::string(seasons[i]); return; }
        }
        // Complete setskill argument (skill name only — leave the level for the user to type)
        if (input.rfind("setskill ", 0) == 0) {
            std::string arg = input.substr(9);
            for (int i = 0; SKILL_TOKENS[i]; i++)
                if (std::string(SKILL_TOKENS[i]).rfind(arg, 0) == 0) {
                    input = "setskill " + std::string(SKILL_TOKENS[i]) + " ";
                    return;
                }
        }
        // Complete unlocktech argument
        if (input.rfind("unlocktech ", 0) == 0) {
            std::string arg = input.substr(11);
            for (int i = 0; TECH_TOKENS[i]; i++)
                if (std::string(TECH_TOKENS[i]).rfind(arg, 0) == 0) {
                    input = "unlocktech " + std::string(TECH_TOKENS[i]);
                    return;
                }
        }
        // Complete unlockspell argument
        if (input.rfind("unlockspell ", 0) == 0) {
            std::string arg = input.substr(12);
            for (int i = 0; SPELL_TOKENS[i]; i++)
                if (std::string(SPELL_TOKENS[i]).rfind(arg, 0) == 0) {
                    input = "unlockspell " + std::string(SPELL_TOKENS[i]);
                    return;
                }
        }
    }

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible) return;

        const int H  = 28;
        const int Y  = MAP_VIEW_HEIGHT - H;

        // Background strip
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 8, 10, 14, 225);
        SDL_Rect bg = {0, Y, SCREEN_WIDTH, H};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // Top border line
        SDL_SetRenderDrawColor(r, 100, 90, 45, 255);
        SDL_RenderDrawLine(r, 0, Y, SCREEN_WIDTH, Y);

        // Prompt + typed text + blinking cursor
        std::string display = "> " + input + "_";
        SDL_Surface* s = TTF_RenderUTF8_Solid(f, display.c_str(), {210, 195, 105, 255});
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            int w, h;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_Rect td = {8, Y + (H - h) / 2, w, h};
            SDL_RenderCopy(r, t, nullptr, &td);
            SDL_DestroyTexture(t);
        }

        // Above the input bar: result (after Enter) OR dynamic hints (while typing)
        {
            std::string aboveText = result.empty() ? getHints() : result;
            SDL_Color aboveCol = result.empty()
                ? SDL_Color{65, 62, 48, 255}
                : (resultOk ? SDL_Color{90, 200, 90, 255} : SDL_Color{220, 80, 80, 255});

            if (!aboveText.empty()) {
                SDL_Surface* as = TTF_RenderUTF8_Solid(f, aboveText.c_str(), aboveCol);
                if (as) {
                    SDL_Texture* at = SDL_CreateTextureFromSurface(r, as);
                    SDL_FreeSurface(as);
                    int aw, ah;
                    SDL_QueryTexture(at, nullptr, nullptr, &aw, &ah);
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
                    SDL_Rect abg = {0, Y - ah - 6, SCREEN_WIDTH, ah + 6};
                    SDL_RenderFillRect(r, &abg);
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                    SDL_Rect ad = {8, Y - ah - 3, aw, ah};
                    SDL_RenderCopy(r, at, nullptr, &ad);
                    SDL_DestroyTexture(at);
                }
            }
        }
    }
};
