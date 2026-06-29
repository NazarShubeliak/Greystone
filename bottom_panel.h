#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <deque>
#include <string>
#include "astar.h"
#include "actor.h"

enum class PanelMode { LOG, DIALOGUE };

struct BottomPanel {
    PanelMode mode = PanelMode::LOG;

    // --- Log ---
    std::deque<std::string> log;
    static const int MAX_LOG = 40;

    void addMessage(const std::string& msg) {
        log.push_front(msg);
        if ((int)log.size() > MAX_LOG) log.pop_back();
    }

    // --- Render ---
    void render(SDL_Renderer* renderer, TTF_Font* font, const Player& player) {
        const int TOP = MAP_VIEW_HEIGHT;
        const int HUD_H = 32;
        const int LOG_H = PANEL_HEIGHT - HUD_H - 1;

        // Background
        SDL_Rect bg = {0, TOP, SCREEN_WIDTH, PANEL_HEIGHT};
        SDL_SetRenderDrawColor(renderer, 10, 10, 10, 255);
        SDL_RenderFillRect(renderer, &bg);

        // Top border
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderDrawLine(renderer, 0, TOP, SCREEN_WIDTH, TOP);

        // Divider above HUD
        SDL_RenderDrawLine(renderer, 0, SCREEN_HEIGHT - HUD_H, SCREEN_WIDTH, SCREEN_HEIGHT - HUD_H);

        switch (mode) {
            case PanelMode::LOG:      renderLog(renderer, font, TOP, LOG_H);   break;
            case PanelMode::DIALOGUE: renderLog(renderer, font, TOP, LOG_H);   break;
        }

        renderHUD(renderer, font, player);
    }

private:
    // ---- log ----------------------------------------------------------------
    void renderLog(SDL_Renderer* r, TTF_Font* f, int top, int logH) {
        const int LINE_H  = 18;
        const int PADDING = 6;
        int maxLines = (logH - PADDING) / LINE_H;
        int y = top + PADDING;

        for (int i = 0; i < (int)log.size() && i < maxLines; i++) {
            // Newest message is brightest; older messages fade.
            int brightness = 255 - i * (160 / maxLines);
            SDL_Color col = {(Uint8)brightness, (Uint8)brightness, (Uint8)brightness, 255};
            renderText(r, f, log[i].c_str(), 8, y, col);
            y += LINE_H;
        }
    }

    // ---- HUD ----------------------------------------------------------------
    void renderHUD(SDL_Renderer* r, TTF_Font* f, const Player& p) {
        const int Y      = SCREEN_HEIGHT - 28;
        const int BAR_X  = 85;
        const int BAR_W  = 130;
        const int BAR_H  = 13;

        // HP label
        std::string hpStr = "HP: " + std::to_string(p.hp) + "/" + std::to_string(p.maxHp);
        renderText(r, f, hpStr.c_str(), 8, Y, {255, 80, 80, 255});

        // HP bar background
        SDL_Rect barBg = {BAR_X, Y + 1, BAR_W, BAR_H};
        SDL_SetRenderDrawColor(r, 60, 0, 0, 255);
        SDL_RenderFillRect(r, &barBg);

        // HP bar fill
        float ratio = (float)p.hp / p.maxHp;
        SDL_Color fillCol = ratio > 0.5f ? SDL_Color{0, 200, 0, 255} :
                            ratio > 0.25f ? SDL_Color{255, 165, 0, 255} :
                                            SDL_Color{220, 0, 0, 255};
        SDL_Rect barFill = {BAR_X, Y + 1, (int)(BAR_W * ratio), BAR_H};
        SDL_SetRenderDrawColor(r, fillCol.r, fillCol.g, fillCol.b, 255);
        SDL_RenderFillRect(r, &barFill);

        // HP bar border
        SDL_SetRenderDrawColor(r, 120, 120, 120, 255);
        SDL_RenderDrawRect(r, &barBg);

        // Speed
        std::string spdStr = "SPD: " + std::to_string(p.speed);
        renderText(r, f, spdStr.c_str(), 230, Y, {180, 180, 180, 255});

        // Energy
        std::string nrgStr = "NRG: " + std::to_string(p.energy);
        renderText(r, f, nrgStr.c_str(), 320, Y, {80, 180, 255, 255});
    }

    // ---- util ---------------------------------------------------------------
    void renderText(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
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
};
