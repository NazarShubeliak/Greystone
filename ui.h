#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include "actor.h"

extern Player player;

struct UI {
    bool showStats = false;

    void toggle() { showStats = !showStats; }

    void renderStats(SDL_Renderer* r, TTF_Font* f) {
        if (!showStats) return;

        const int W  = 310;
        const int H  = 460;
        const int X  = 50;
        const int Y  = 50;
        const int LH = 24;
        const int LX = X + 16;

        // Background + border
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 12, 12, 16, 240);
        SDL_Rect bg = {X, Y, W, H};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 130, 110, 50, 255);
        SDL_RenderDrawRect(r, &bg);

        int cy = Y + 10;

        // Title
        txt(r, f, "CHARACTER", LX, cy, {200, 170, 60, 255});
        cy += LH + 2;
        hline(r, X, cy, W);
        cy += 8;

        // HP
        std::string hp = "HP: " + std::to_string(player.hp) + " / " + std::to_string(player.maxHp);
        txt(r, f, hp.c_str(), LX, cy, {220, 70, 70, 255});
        cy += LH;

        // Speed (base minus hunger/thirst penalty — matches actual turn rate in tickWorld)
        {
            int needsPen  = player.needsSpeedPenalty();
            int effSpeed  = std::max(1, player.speed - needsPen);
            std::string spdStr = "Speed:  " + std::to_string(effSpeed);
            txt(r, f, spdStr.c_str(), LX, cy, {190, 190, 190, 255});
            if (needsPen != 0) {
                int cx = LX + (int)spdStr.size() * 9;
                std::string penStr = " (-" + std::to_string(needsPen) + " needs)";
                txt(r, f, penStr.c_str(), cx, cy, {220, 90, 70, 255});
            }
        }
        cy += LH;

        // Race + Age
        std::string meta = std::string(raceTraits[(int)player.race].name)
                         + "  age " + std::to_string(player.age);
        txt(r, f, meta.c_str(), LX, cy, {110, 105, 90, 255});
        cy += LH + 4;
        hline(r, X, cy, W);
        cy += 8;

        // ── Stats with item bonuses ───────────────────────────────────────
        statRow(r, f, "Strength",     player.strength, player.strItemBonus(), player.needsStrPenalty(), LX, cy); cy += LH;
        statRow(r, f, "Dexterity",    player.dexterity, player.dexItemBonus(), 0, LX, cy); cy += LH;
        statRow(r, f, "Intelligence", player.intelligence, 0, 0, LX, cy);                  cy += LH;
        statRow(r, f, "Constitution", player.constitution, player.conItemBonus(), 0, LX, cy); cy += LH;
        statRow(r, f, "Perception",   player.perception,  0, 0, LX, cy);                   cy += LH;
        statRow(r, f, "Charisma",     player.charisma,    0, 0, LX, cy);                   cy += LH + 4;

        hline(r, X, cy, W);
        cy += 8;

        // ── Combat ────────────────────────────────────────────────────────
        txt(r, f, "COMBAT", LX, cy, {180, 155, 55, 255});
        cy += LH;

        // Attack damage breakdown
        int strBonus  = (player.effectiveStr() - 10) / 2;
        int weapDmg   = player.weaponDamage();
        int totalAtk  = 5 + strBonus + weapDmg - 1;
        std::string atkLine = "Attack:   " + std::to_string(totalAtk) + " dmg";
        if (weapDmg > 1)
            atkLine += "  (weapon +" + std::to_string(weapDmg - 1) + ")";
        if (strBonus > 0)
            atkLine += "  (str +" + std::to_string(strBonus) + ")";
        txt(r, f, atkLine.c_str(), LX, cy, {210, 180, 80, 255});
        cy += LH;

        // Defense breakdown
        int def = player.totalDefense();
        std::string defLine = "Defense:  " + std::to_string(def);
        if (def > 0) defLine += "  (reduces incoming dmg)";
        txt(r, f, defLine.c_str(), LX, cy, {80, 175, 210, 255});
        cy += LH;

        // Weapon name
        std::string weapName = "Weapon:   ";
        if (player.worn[(int)EquipSlot::HAND_R].has_value())
            weapName += player.worn[(int)EquipSlot::HAND_R]->name;
        else if (player.worn[(int)EquipSlot::HAND_L].has_value())
            weapName += player.worn[(int)EquipSlot::HAND_L]->name;
        else
            weapName += "Unarmed";
        txt(r, f, weapName.c_str(), LX, cy, {150, 145, 130, 255});
        cy += LH + 4;

        hline(r, X, cy, W);
        cy += 8;

        // ── Needs (compact) ───────────────────────────────────────────────
        needsLine(r, f, "Hunger", player.hunger, LX, cy);  cy += LH;
        needsLine(r, f, "Thirst", player.thirst, LX, cy);
    }

private:
    void txt(SDL_Renderer* r, TTF_Font* f, const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderText_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }

    void hline(SDL_Renderer* r, int x, int y, int w) {
        SDL_SetRenderDrawColor(r, 60, 58, 40, 255);
        SDL_RenderDrawLine(r, x + 8, y, x + w - 8, y);
    }

    // Renders "Name: effective (+itemBonus) (-needsPenalty)".
    // effective = base + bonus - penalty, clamped to >=1 (matches effectiveStr() etc).
    void statRow(SDL_Renderer* r, TTF_Font* f,
                 const char* name, int base, int bonus, int penalty, int x, int y) {
        int effective = std::max(1, base + bonus - penalty);
        SDL_Color valCol = effective >= 15 ? SDL_Color{100, 230, 100, 255}
                         : effective >= 10 ? SDL_Color{210, 210, 200, 255}
                                           : SDL_Color{220,  80,  80, 255};

        // Pad name to fixed width
        std::string label = std::string(name) + ": ";
        while ((int)label.size() < 15) label += ' ';
        txt(r, f, label.c_str(), x, y, {110, 108, 95, 255});

        int labelW = (int)label.size() * 9;
        std::string valStr = std::to_string(effective);
        txt(r, f, valStr.c_str(), x + labelW, y, valCol);
        int cx = x + labelW + (int)valStr.size() * 9;

        if (bonus != 0) {
            std::string bonStr = bonus > 0
                ? " (+" + std::to_string(bonus) + ")"
                : " (" + std::to_string(bonus) + ")";
            SDL_Color bonCol = bonus > 0 ? SDL_Color{200, 175, 55, 255}
                                         : SDL_Color{200,  70,  70, 255};
            txt(r, f, bonStr.c_str(), cx, y, bonCol);
            cx += (int)bonStr.size() * 9;
        }
        if (penalty != 0) {
            std::string penStr = " (-" + std::to_string(penalty) + " needs)";
            txt(r, f, penStr.c_str(), cx, y, {220, 90, 70, 255});
        }
    }

    void needsLine(SDL_Renderer* r, TTF_Font* f,
                   const char* label, float v, int x, int y) {
        SDL_Color col = v < 0.30f ? SDL_Color{90, 175, 65, 255}
                      : v < 0.55f ? SDL_Color{210,185,60,255}
                      : v < 0.80f ? SDL_Color{225,130,45,255}
                                  : SDL_Color{220, 50,50,255};
        const char* state =
            v < 0.10f ? "Fine" : v < 0.30f ? "Peckish" :
            v < 0.55f ? "Hungry" : v < 0.80f ? "Very hungry" : "Starving!";
        std::string line = std::string(label) + ":  " + state
                         + "  (" + std::to_string((int)(v * 100)) + "%)";
        txt(r, f, line.c_str(), x, y, col);
    }
};
