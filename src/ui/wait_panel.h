#pragma once
#include "time_system.h"
#include "astar.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

struct WaitOption {
    std::string label;
    int         targetMinutes; // -1 = cancel
};

struct WaitPanel {
    bool visible = false;
    int  hovered = -1;

    // Populated in show(). Public so the game loop can read targetMinutes.
    std::vector<WaitOption> options;

    void show(const WorldTime& wt) {
        visible = true;
        hovered = 0;
        options.clear();

        auto next = [&](int h) {
            int base = (wt.minutes / (60*24)) * (60*24) + h * 60;
            if (base <= wt.minutes) base += 60*24;
            return base;
        };

        options.push_back({"1 hour",
                           wt.minutes + 60});
        options.push_back({"Until dawn   (" + hstr((int)wt.sunriseHour()) + ")",
                           next((int)wt.sunriseHour())});
        options.push_back({"Until noon   (12:00)",
                           next(12)});
        options.push_back({"Until evening(18:00)",
                           next(18)});
        options.push_back({"Until night  (22:00)",
                           next(22)});
        options.push_back({"Cancel", -1});
    }

    void hide() { visible = false; hovered = -1; }

    // Keyboard: up/down navigate, Enter selects. Returns targetMinutes or -2 (nothing yet).
    int handleKey(SDL_Keycode key) {
        if (!visible) return -2;
        if (key == SDLK_ESCAPE) { hide(); return -1; }
        if (key == SDLK_UP)   { hovered = (hovered - 1 + (int)options.size()) % (int)options.size(); return -2; }
        if (key == SDLK_DOWN) { hovered = (hovered + 1) % (int)options.size(); return -2; }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (hovered >= 0 && hovered < (int)options.size()) {
                int t = options[hovered].targetMinutes;
                hide();
                return t;
            }
        }
        return -2;
    }

    // Mouse move: update hover row. Returns -2.
    int handleMotion(int mx, int my) {
        if (!visible) return -2;
        auto rects = rowRects();
        for (int i = 0; i < (int)rects.size(); i++)
            if (mx >= rects[i].x && mx < rects[i].x + rects[i].w &&
                my >= rects[i].y && my < rects[i].y + rects[i].h)
                hovered = i;
        return -2;
    }

    // Click: returns targetMinutes, -1 (cancel/outside), or -2 (not visible).
    int handleClick(int mx, int my) {
        if (!visible) return -2;
        auto rects = rowRects();
        for (int i = 0; i < (int)rects.size(); i++) {
            if (mx >= rects[i].x && mx < rects[i].x + rects[i].w &&
                my >= rects[i].y && my < rects[i].y + rects[i].h) {
                int t = options[i].targetMinutes;
                hide();
                return t;
            }
        }
        // Click outside closes the panel
        hide();
        return -1;
    }

    void render(SDL_Renderer* r, TTF_Font* f, const WorldTime& wt) {
        if (!visible) return;

        const int ROWS = (int)options.size();
        const int ROW_H = 28;
        const int W     = 280;
        const int H     = 46 + ROWS * ROW_H + 10;
        const int X     = (SCREEN_WIDTH  - W) / 2;
        const int Y     = (MAP_VIEW_HEIGHT - H) / 2;
        const int PAD   = 14;

        PanelStyle::frame(r, f, X, Y, W, H, nullptr);

        // Header
        int ty = Y + 10;
        std::string header = "Wait  " + wt.timeStr();
        txt(r, f, header.c_str(), X + PAD, ty, {180, 160, 80, 255});
        ty += 22;

        SDL_SetRenderDrawColor(r, 50, 48, 38, 255);
        SDL_RenderDrawLine(r, X + 8, ty, X + W - 8, ty);
        ty += 8;

        // Options
        auto rects = rowRects();
        for (int i = 0; i < ROWS; i++) {
            bool isCancel = (options[i].targetMinutes == -1);
            bool isHover  = (i == hovered);

            if (isHover) {
                SDL_SetRenderDrawColor(r, 50, 45, 20, 255);
                SDL_RenderFillRect(r, &rects[i]);
            }

            SDL_Color col = isCancel   ? SDL_Color{100, 90, 75, 255}
                          : isHover    ? SDL_Color{230, 210, 120, 255}
                          :              SDL_Color{180, 170, 130, 255};

            std::string label = (isHover && !isCancel ? "> " : "  ") + options[i].label;
            txt(r, f, label.c_str(), X + PAD, rects[i].y + 5, col);
        }
    }

private:
    std::vector<SDL_Rect> rowRects() const {
        const int ROW_H = 28;
        const int W     = 280;
        const int X     = (SCREEN_WIDTH  - W) / 2;
        const int Y     = (MAP_VIEW_HEIGHT - W) / 2; // approximate, matches render
        const int topY  = (MAP_VIEW_HEIGHT - (46 + (int)options.size() * ROW_H + 10)) / 2;
        int startY      = topY + 46;

        std::vector<SDL_Rect> rects;
        for (int i = 0; i < (int)options.size(); i++)
            rects.push_back({X, startY + i * ROW_H, W, ROW_H});
        return rects;
    }

    static std::string hstr(int h) {
        char buf[6]; snprintf(buf, sizeof(buf), "%02d:00", h); return buf;
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
