#pragma once
#include "astar.h"
#include "actor.h"
#include "combat.h"
#include "panel_style.h"
#include "menu_hub.h"
#include "hotbar.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

// Lists every Technique the game knows about (locked ones shown grayed out) and
// lets the player bind an unlocked one to any of the 9 hotbar slots by clicking
// its numbered mini-box — click the slot it's already in to unbind. Pure UI: the
// actual "use a technique" logic lives in main.cpp (useTechniqueOnEnemy/Villager),
// this panel only edits Hotbar::slots.
struct TechniquesPanel {
    bool visible = false;

    void toggle() { visible = !visible; }
    void hide()   { visible = false; }

    static constexpr int ROW_H  = 96;
    static constexpr int BOX    = 20;
    static constexpr int BOX_GAP = 4;

    int rowTop(int row) const { return MenuHub::TAB_H + 20 + row * ROW_H; }

    SDL_Rect miniBoxRect(int row, int slot) const {
        int x = 40 + slot * (BOX + BOX_GAP);
        int y = rowTop(row) + 68;
        return { x, y, BOX, BOX };
    }

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p, const Hotbar& hotbar) const {
        if (!visible) return;

        for (int i = 0; i < (int)TechniqueId::TECHNIQUE_COUNT; i++) {
            TechniqueId id = (TechniqueId)i;
            const Technique& t = techniqueInfo(id);
            bool unlocked = techniqueUnlocked(p, id);
            bool usable   = techniqueUsable(p, id);
            int y = rowTop(i);

            SDL_Color nameCol = unlocked ? PanelStyle::TITLE : SDL_Color{110, 108, 100, 255};
            txt(r, f, t.name, 40, y, nameCol);

            std::string req = std::string("  Requires ") + skillName(t.skill) + " "
                             + std::to_string(t.minLevel)
                             + " (currently " + std::to_string(p.skill(t.skill).level) + ")";
            SDL_Color reqCol = unlocked ? SDL_Color{110, 190, 110, 255} : SDL_Color{190, 110, 90, 255};
            txt(r, f, req.c_str(), 40 + textW(f, t.name), y, reqCol);

            txt(r, f, t.description, 40, y + 18, {170, 165, 155, 255});

            std::string cost = "Stamina: " + std::to_string((int)t.staminaCost)
                              + "   Extra time: +" + std::to_string(t.extraEnergy)
                              + (unlocked && !usable ? "   (not enough stamina right now)" : "");
            txt(r, f, cost.c_str(), 40, y + 36, {130, 150, 190, 255});

            txt(r, f, "Bind to slot:", 40, y + 54, {130, 125, 115, 255});
            for (int s = 0; s < Hotbar::SLOT_COUNT; s++) {
                SDL_Rect box = miniBoxRect(i, s);
                bool bound = hotbar.slots[s] == (int)id;

                SDL_SetRenderDrawColor(r, bound ? PanelStyle::ACCENT.r : 30,
                                       bound ? PanelStyle::ACCENT.g : 28,
                                       bound ? PanelStyle::ACCENT.b : 24, 255);
                SDL_RenderFillRect(r, &box);
                SDL_SetRenderDrawColor(r, unlocked ? 150 : 70, unlocked ? 145 : 68,
                                       unlocked ? 130 : 60, 255);
                SDL_RenderDrawRect(r, &box);

                std::string label = std::to_string((s + 1) % 10);
                txt(r, f, label.c_str(), box.x + 6, box.y + 2,
                    unlocked ? SDL_Color{220, 210, 190, 255} : SDL_Color{90, 88, 80, 255});
            }
        }
    }

    bool handleClick(int mx, int my, const Player& p, Hotbar& hotbar) {
        if (!visible) return false;

        for (int i = 0; i < (int)TechniqueId::TECHNIQUE_COUNT; i++) {
            TechniqueId id = (TechniqueId)i;
            if (!techniqueUnlocked(p, id)) continue;
            for (int s = 0; s < Hotbar::SLOT_COUNT; s++) {
                SDL_Rect box = miniBoxRect(i, s);
                if (mx >= box.x && mx < box.x + box.w && my >= box.y && my < box.y + box.h) {
                    hotbar.assign(s, id);
                    return true;
                }
            }
        }
        return true; // swallow anyway — this tab has no other clickable content
    }

private:
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

    static int textW(TTF_Font* f, const char* text) {
        int w = 0, h = 0; TTF_SizeText(f, text, &w, &h); return w;
    }
};
