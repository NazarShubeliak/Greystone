#pragma once
#include "astar.h"
#include "actor.h"
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

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p) {
        if (!visible) return;

        std::vector<Entry> debuffs, buffs;
        collect(p, debuffs, buffs);

        const int W   = 400;
        const int X   = (SCREEN_WIDTH - W) / 2;
        const int PAD = 16;
        const int ROW_H = 36;
        int rows = std::max((int)debuffs.size(), 1) + std::max((int)buffs.size(), 1);
        int H = 90 + rows * ROW_H;
        const int Y = std::max(20, (MAP_VIEW_HEIGHT - H) / 2);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 8, 8, 12, 248);
        SDL_Rect bg = {X, Y, W, H};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        SDL_SetRenderDrawColor(r, 90, 80, 50, 255);
        SDL_RenderDrawRect(r, &bg);
        SDL_Rect inner = {X + 1, Y + 1, W - 2, H - 2};
        SDL_SetRenderDrawColor(r, 45, 40, 25, 255);
        SDL_RenderDrawRect(r, &inner);

        int ty = Y + 10;
        txt(r, f, "Effects", X + PAD, ty, {200, 190, 150, 255});
        ty += 24;
        hline(r, X, ty, W);
        ty += 10;

        txt(r, f, "Debuffs", X + PAD, ty, {215, 100, 80, 255});
        ty += 22;
        if (debuffs.empty()) {
            txt(r, f, "(none)", X + PAD + 8, ty, {110, 110, 110, 255});
            ty += ROW_H;
        } else {
            for (const Entry& e : debuffs) ty = drawEntry(r, f, e, X + PAD, ty, W - PAD * 2);
        }

        ty += 4;
        txt(r, f, "Buffs", X + PAD, ty, {100, 200, 120, 255});
        ty += 22;
        if (buffs.empty()) {
            txt(r, f, "(none)", X + PAD + 8, ty, {110, 110, 110, 255});
        } else {
            for (const Entry& e : buffs) ty = drawEntry(r, f, e, X + PAD, ty, W - PAD * 2);
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

    static void hline(SDL_Renderer* r, int x, int y, int w) {
        SDL_SetRenderDrawColor(r, 50, 48, 38, 255);
        SDL_RenderDrawLine(r, x + 8, y, x + w - 8, y);
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
