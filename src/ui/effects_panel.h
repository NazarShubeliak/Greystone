#pragma once
#include "astar.h"
#include "actor.h"
#include "panel_style.h"
#include "menu_hub.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

// Read-only panel listing every currently active buff and debuff with a
// short explanation of what it does. Values are pulled live from Player, so
// nothing here needs to be kept in sync with a separate "effect" system.
struct EffectsPanel {
    bool visible = false;

    void toggle() { visible = !visible; }
    void hide()   { visible = false; }

    // Debuffs and buffs sit side by side across the full hub width instead
    // of stacked in a small box.
    void render(SDL_Renderer* r, TTF_Font* f, const Player& p) {
        if (!visible) return;

        std::vector<Entry> debuffs, buffs;
        collect(p, debuffs, buffs);

        const int PAD  = 40;
        const int GAP  = 40;
        const int COLW = (SCREEN_WIDTH - PAD * 2 - GAP) / 2;
        const int LX   = PAD;
        const int RX   = PAD + COLW + GAP;
        int ty = MenuHub::TAB_H + 24;

        txt(r, f, "Debuffs", LX, ty, {215, 100, 80, 255});
        int ly = ty + 26;
        if (debuffs.empty()) {
            txt(r, f, "(none)", LX + 8, ly, {110, 110, 110, 255});
        } else {
            for (const Entry& e : debuffs) ly = drawEntry(r, f, e, LX, ly, COLW);
        }

        txt(r, f, "Buffs", RX, ty, {100, 200, 120, 255});
        int ry = ty + 26;
        if (buffs.empty()) {
            txt(r, f, "(none)", RX + 8, ry, {110, 110, 110, 255});
        } else {
            for (const Entry& e : buffs) ry = drawEntry(r, f, e, RX, ry, COLW);
        }
    }

private:
    struct Entry { std::string name, desc; SDL_Color color; };

    void collect(const Player& p, std::vector<Entry>& debuffs, std::vector<Entry>& buffs) {
        // Hunger
        if (p.hungerLevel() > 0) {
            std::string d = "-" + std::to_string(p.hungerSpeedPenalty()) + " Speed, -"
                           + std::to_string(p.hungerStrPenalty()) + " STR";
            if (p.hungerLevel() >= 4) d += "  (losing HP)";
            debuffs.push_back({p.hungerLabel(), d, levelColor(p.hungerLevel())});
        }
        // Thirst
        if (p.thirstLevel() > 0) {
            std::string d = "-" + std::to_string(p.thirstSpeedPenalty()) + " Speed, -"
                           + std::to_string(p.thirstStrPenalty()) + " STR";
            if (p.thirstLevel() >= 4) d += "  (losing HP)";
            debuffs.push_back({p.thirstLabel(), d, levelColor(p.thirstLevel())});
        }
        // Bleeding
        float bleed = p.body.totalBleed();
        if (bleed > 0.0f) {
            std::string d = "Losing ~" + std::to_string((int)bleed) + " HP per turn from wounds";
            debuffs.push_back({"Bleeding", d, {220, 50, 50, 255}});
        }
        // Spell buffs (Fire Shield/Skin Hardening/Lightness) — plain turn counters
        // on Actor, ticked down in tickNeeds(); shown here so the player can see
        // how much longer they have left instead of guessing.
        if (p.fireShieldTicks > 0) {
            std::string d = "Reflects melee damage back at attackers — "
                           + std::to_string(p.fireShieldTicks) + " turns left";
            buffs.push_back({"Fire Shield", d, {255, 130, 40, 255}});
        }
        if (p.skinHardenTicks > 0) {
            std::string d = "+8 Defense — " + std::to_string(p.skinHardenTicks) + " turns left";
            buffs.push_back({"Skin Hardening", d, {160, 140, 110, 255}});
        }
        if (p.lightnessTicks > 0) {
            std::string d = "+15 effective Speed — " + std::to_string(p.lightnessTicks) + " turns left";
            buffs.push_back({"Lightness", d, {200, 235, 245, 255}});
        }
        if (p.accelTicks > 0) {
            std::string d = "+30 effective Speed — " + std::to_string(p.accelTicks) + " turns left";
            buffs.push_back({"Acceleration", d, {255, 225, 130, 255}});
        }
        // Slowness (Water) — normally landed on an enemy/villager, not the
        // player, but the counter lives on every Actor symmetrically, so a
        // stray self-hit (clicking your own tile) shows up here too instead
        // of silently doing nothing.
        if (p.slowedTicks > 0) {
            std::string d = "-15 effective Speed — " + std::to_string(p.slowedTicks) + " turns left";
            debuffs.push_back({"Slowed", d, {90, 130, 190, 255}});
        }
        // Life stage (docs/village.md) — age-based speed/strength modifier,
        // same point-of-use pattern as the buff ticks above. Every non-Adult
        // stage is a net penalty (even Elder), so it always lands in debuffs.
        LifeStage stage = lifeStageFor(p.age, p.race);
        if (stage != LifeStage::ADULT) {
            std::string d = std::to_string(p.ageSpeedMod()) + " Speed, "
                           + std::to_string((int)(p.ageStrMult() * 100)) + "% Strength — age "
                           + std::to_string(p.age);
            debuffs.push_back({lifeStageName(stage), d, {180, 160, 140, 255}});
        }
        // Item stat bonuses (jewelry etc.) — can be negative, so sort by sign.
        addStatBonus(p.strItemBonus(), "Strength", debuffs, buffs);
        addStatBonus(p.dexItemBonus(), "Dexterity", debuffs, buffs);
        addStatBonus(p.conItemBonus(), "Constitution", debuffs, buffs);
        // Light source
        int light = p.totalLightRadius();
        if (light > 0) {
            std::string d = "+" + std::to_string(light) + " vision radius in darkness";
            buffs.push_back({"Light Source", d, {220, 200, 100, 255}});
        }
    }

    static void addStatBonus(int bonus, const char* statName,
                              std::vector<Entry>& debuffs, std::vector<Entry>& buffs) {
        if (bonus == 0) return;
        std::string name = std::string(statName) + " Bonus";
        std::string d = (bonus > 0 ? "+" : "") + std::to_string(bonus) + " " + statName + " from worn items";
        if (bonus > 0) buffs.push_back({name, d, {100, 200, 120, 255}});
        else           debuffs.push_back({name, d, {220, 90, 70, 255}});
    }

    static SDL_Color levelColor(int lvl) {
        switch (lvl) {
            case 1:  return {210, 185,  60, 255};
            case 2:  return {225, 130,  45, 255};
            case 3:  return {235,  90,  40, 255};
            default: return {220,  50,  50, 255};
        }
    }

    int drawEntry(SDL_Renderer* r, TTF_Font* f, const Entry& e, int x, int y, int maxW) {
        txt(r, f, e.name.c_str(), x + 8, y, e.color);
        txt(r, f, e.desc.c_str(), x + 16, y + 17, {170, 165, 155, 255});
        return y + 36;
    }

    static void txt(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderUTF8_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
};
