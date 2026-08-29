#pragma once
#include "npc.h"
#include "actor.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

// Debug/diagnostic cheat panel (`census` in the cheat console) — snapshots
// how many creatures tied to the current sector are alive/dead right now.
// Meant for an explicit before/after `simulatedays` comparison, so a real
// die-off (villagers actually dying, log or no log) can be told apart from
// burial simply not keeping up (bodies decay before the player sees them).
// Corpse/Grave counts come in pre-computed (main.cpp) rather than as vectors
// here — both types are defined in main.cpp itself, after the UI headers.
struct CensusPanel {
    bool visible = false;

    int villagersAlive = 0, villagersDead = 0;
    int enemiesAlive    = 0;
    int corpsesHere     = 0;
    int gravesHere      = 0;

    void show(const std::vector<Villager>& villagers, const std::vector<Enemy>& enemies,
              int corpsesHereCount, int gravesHereCount) {
        villagersAlive = villagersDead = enemiesAlive = 0;
        for (const Villager& v : villagers) (v.alive ? villagersAlive : villagersDead)++;
        for (const Enemy& e : enemies) if (e.alive) enemiesAlive++;
        corpsesHere = corpsesHereCount;
        gravesHere  = gravesHereCount;
        visible = true;
    }
    void hide() { visible = false; }

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible) return;

        const int W = 340, H = 208, PAD = 14, LH = 22;
        const int X = (SCREEN_WIDTH     - W) / 2;
        const int Y = (MAP_VIEW_HEIGHT  - H) / 2;

        int ty = PanelStyle::frame(r, f, X, Y, W, H, "Census — current sector");

        txt(r, f, ("Villagers alive:   " + std::to_string(villagersAlive)).c_str(), X + PAD, ty, {110, 190, 110, 255});
        ty += LH;
        txt(r, f, ("Villagers dead:    " + std::to_string(villagersDead)).c_str(), X + PAD, ty, {200, 90, 80, 255});
        ty += LH;
        txt(r, f, ("Total ever spawned: " + std::to_string(villagersAlive + villagersDead)).c_str(),
            X + PAD, ty, {170, 165, 155, 255});
        ty += LH + 8;

        txt(r, f, ("Corpses on ground: " + std::to_string(corpsesHere)).c_str(), X + PAD, ty, {180, 160, 140, 255});
        ty += LH;
        txt(r, f, ("Graves:            " + std::to_string(gravesHere)).c_str(), X + PAD, ty, {160, 160, 160, 255});
        ty += LH + 8;

        txt(r, f, ("Enemies alive:     " + std::to_string(enemiesAlive)).c_str(), X + PAD, ty, {220, 120, 90, 255});
        ty += LH;

        txt(r, f, "Click anywhere to close", X + PAD, Y + H - 20, {55, 55, 50, 255});
    }

private:
    static void txt(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
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
};
