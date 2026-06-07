#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "actor.h"

extern Player player;

struct UI {
    bool showStats = false;

    void toggle() { showStats = !showStats; }

    void renderStats(SDL_Renderer* renderer, TTF_Font* font) {
        if (!showStats) return;

        // Фон панелі
        SDL_Rect panel = {50, 50, 300, 400};
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 230);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDrawRect(renderer, &panel);

        // Заголовок
        renderText(renderer, font, "CHARACTER STATS", 70, 70, {255, 215, 0, 255});

        // Лінія
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDrawLine(renderer, 60, 95, 340, 95);

        // HP
        std::string hp = "HP: " + std::to_string(player.hp) + " / " + std::to_string(player.maxHp);
        renderText(renderer, font, hp.c_str(), 70, 110, {255, 80, 80, 255});

        // Швидкість
        std::string spd = "Speed: " + std::to_string(player.speed);
        renderText(renderer, font, spd.c_str(), 70, 135, {255, 255, 255, 255});

        // Лінія
        SDL_RenderDrawLine(renderer, 60, 160, 340, 160);

        // Характеристики
        renderStat(renderer, font, "Strength",     player.strength,     70, 175);
        renderStat(renderer, font, "Dexterity",    player.dexterity,    70, 200);
        renderStat(renderer, font, "Intelligence", player.intelligence, 70, 225);
        renderStat(renderer, font, "Constitution", player.constitution, 70, 250);
        renderStat(renderer, font, "Perception",   player.perception,   70, 275);
        renderStat(renderer, font, "Charisma",     player.charisma,     70, 300);
    }

private:
    void renderText(SDL_Renderer* renderer, TTF_Font* font,
                    const char* text, int x, int y, SDL_Color color) {
        SDL_Surface* s = TTF_RenderText_Solid(font, text, color);
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FreeSurface(s);
        int w, h;
        SDL_QueryTexture(t, NULL, NULL, &w, &h);
        SDL_Rect dest = {x, y, w, h};
        SDL_RenderCopy(renderer, t, NULL, &dest);
        SDL_DestroyTexture(t);
    }

    void renderStat(SDL_Renderer* renderer, TTF_Font* font,
                    const char* name, int value, int x, int y) {
        std::string text = std::string(name) + ": " + std::to_string(value);
        SDL_Color color = value >= 15 ? SDL_Color{100, 255, 100, 255} :
                          value >= 10 ? SDL_Color{255, 255, 255, 255} :
                                        SDL_Color{255, 100, 100, 255};
        renderText(renderer, font, text.c_str(), x, y, color);
    }
};