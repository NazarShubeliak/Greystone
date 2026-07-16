#pragma once
#include "astar.h"
#include "actor.h"
#include "panel_style.h"
#include "menu_hub.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

// Read-only panel listing every Skill (weapons + magic schools, docs/weapons.md /
// docs/magic.md) with level and progress toward the next one. Values are pulled
// live from Player.skills[], same pattern as EffectsPanel. Magic schools show at
// level 0 until spellcasting exists — that's expected, not a bug.
struct SkillsPanel {
    bool visible = false;

    void toggle() { visible = !visible; }
    void hide()   { visible = false; }

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p) {
        if (!visible) return;

        const int PAD  = 40;
        const int GAP  = 40;
        const int COLW = (SCREEN_WIDTH - PAD * 2 - GAP) / 2;
        const int LX   = PAD;
        const int RX   = PAD + COLW + GAP;
        int ty = MenuHub::TAB_H + 24;

        txt(r, f, "Weapons", LX, ty, PanelStyle::TITLE);
        int ly = ty + 26;
        for (int i = (int)Skill::SWORD; i <= (int)Skill::DUAL_WIELD; i++)
            ly = drawSkill(r, f, p, (Skill)i, LX, ly, COLW);

        txt(r, f, "Magic", RX, ty, PanelStyle::TITLE);
        int ry = ty + 26;
        for (int i = (int)Skill::FIRE; i <= (int)Skill::DEATH; i++)
            ry = drawSkill(r, f, p, (Skill)i, RX, ry, COLW);
    }

private:
    int drawSkill(SDL_Renderer* r, TTF_Font* f, const Player& p, Skill s, int x, int y, int maxW) {
        const SkillLevel& sk = p.skills[(int)s];
        std::string label = std::string(skillName(s)) + "  " + std::to_string(sk.level);
        SDL_Color labelCol = sk.level == 0 ? SDL_Color{110, 108, 100, 255} : PanelStyle::TITLE;
        txt(r, f, label.c_str(), x + 8, y, labelCol);

        int barW = maxW - 16;
        int barX = x + 8, barY = y + 18;
        SDL_SetRenderDrawColor(r, 55, 52, 45, 255);
        SDL_Rect bg = {barX, barY, barW, 6};
        SDL_RenderFillRect(r, &bg);
        if (sk.level < 100 && sk.expToNext() > 0) {
            float frac = (float)sk.exp / sk.expToNext();
            SDL_Rect fg = {barX, barY, (int)(barW * frac), 6};
            SDL_SetRenderDrawColor(r, PanelStyle::ACCENT.r, PanelStyle::ACCENT.g,
                                   PanelStyle::ACCENT.b, 255);
            SDL_RenderFillRect(r, &fg);
        }
        return y + 32;
    }

    static void txt(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderText_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
};
