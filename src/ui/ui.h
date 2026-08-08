#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include "actor.h"
#include "panel_style.h"
#include "menu_hub.h"

extern Player player;

struct UI {
    bool showStats = false;

    void toggle() { showStats = !showStats; }

    // Two columns filling the hub's full width: vitals+stats on the left,
    // combat+needs on the right. Borderless — the hub already dims the
    // background — this only lays out content within it.
    void renderStats(SDL_Renderer* r, TTF_Font* f) {
        if (!showStats) return;

        const int LH   = 24;
        const int PAD  = 40;
        const int LX   = PAD;
        const int RX   = SCREEN_WIDTH / 2 + PAD;
        const int COLW = SCREEN_WIDTH / 2 - PAD * 2;
        int ly = MenuHub::TAB_H + 24;
        int ry = MenuHub::TAB_H + 24;

        // ── Left column: vitals + stats ─────────────────────────────────
        std::string hp = "HP: " + std::to_string(player.hp) + " / " + std::to_string(player.maxHp);
        txt(r, f, hp.c_str(), LX, ly, {220, 70, 70, 255});
        ly += LH;

        {
            int needsPen  = player.needsSpeedPenalty();
            int effSpeed  = std::max(1, player.speed - needsPen);
            std::string spdStr = "Speed:  " + std::to_string(effSpeed);
            txt(r, f, spdStr.c_str(), LX, ly, {190, 190, 190, 255});
            if (needsPen != 0) {
                int cx = LX + (int)spdStr.size() * 9;
                std::string penStr = " (-" + std::to_string(needsPen) + " needs)";
                txt(r, f, penStr.c_str(), cx, ly, {220, 90, 70, 255});
            }
        }
        ly += LH;

        std::string meta = std::string(raceTraits[(int)player.race].name)
                         + "  age " + std::to_string(player.age);
        txt(r, f, meta.c_str(), LX, ly, {110, 105, 90, 255});
        ly += LH + 4;
        hline(r, LX, ly, COLW);
        ly += 8;

        statRow(r, f, "Strength",     player.strength, player.strItemBonus(), player.needsStrPenalty(), LX, ly); ly += LH;
        statRow(r, f, "Dexterity",    player.dexterity, player.dexItemBonus(), 0, LX, ly); ly += LH;
        statRow(r, f, "Intelligence", player.intelligence, 0, 0, LX, ly);                  ly += LH;
        statRow(r, f, "Constitution", player.constitution, player.conItemBonus(), 0, LX, ly); ly += LH;
        statRow(r, f, "Perception",   player.perception,  0, 0, LX, ly);                   ly += LH;
        statRow(r, f, "Charisma",     player.charisma,    0, 0, LX, ly);                   ly += LH;

        // ── Right column: combat + needs ────────────────────────────────
        txt(r, f, "COMBAT", RX, ry, {180, 155, 55, 255});
        ry += LH;

        int strBonus  = (player.effectiveStr() - 10) / 2;
        int weapDmg   = player.weaponDamage();
        int totalAtk  = 5 + strBonus + weapDmg - 1;
        std::string atkLine = "Attack:   " + std::to_string(totalAtk) + " dmg";
        if (weapDmg > 1)
            atkLine += "  (weapon +" + std::to_string(weapDmg - 1) + ")";
        if (strBonus > 0)
            atkLine += "  (str +" + std::to_string(strBonus) + ")";
        txt(r, f, atkLine.c_str(), RX, ry, {210, 180, 80, 255});
        ry += LH;

        int def = player.totalDefense();
        std::string defLine = "Defense:  " + std::to_string(def);
        if (def > 0) defLine += "  (reduces incoming dmg)";
        txt(r, f, defLine.c_str(), RX, ry, {80, 175, 210, 255});
        ry += LH;

        std::string weapName = "Weapon:   ";
        if (player.worn[(int)EquipSlot::HAND_R].has_value())
            weapName += player.worn[(int)EquipSlot::HAND_R]->name;
        else if (player.worn[(int)EquipSlot::HAND_L].has_value())
            weapName += player.worn[(int)EquipSlot::HAND_L]->name;
        else
            weapName += "Unarmed";
        txt(r, f, weapName.c_str(), RX, ry, {150, 145, 130, 255});
        ry += LH + 4;

        hline(r, RX, ry, COLW);
        ry += 8;

        txt(r, f, "NEEDS", RX, ry, {180, 155, 55, 255});
        ry += LH;
        needsLine(r, f, "Hunger", player.hunger, RX, ry);  ry += LH;
        needsLine(r, f, "Thirst", player.thirst, RX, ry);
    }

private:
    void txt(SDL_Renderer* r, TTF_Font* f, const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderUTF8_Solid(f, text, col);
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
