#pragma once
#include "astar.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

// Generic modal Yes/No confirmation popup.
struct ConfirmPanel {
    bool        visible = false;
    int         hovered = 0; // 0 = Continue (yes), 1 = Stop (no)
    std::string message;

    void show(const std::string& msg) {
        visible = true;
        hovered = 0;
        message = msg;
    }

    void hide() { visible = false; }

    // Returns 1 = continue, 0 = stop, -2 = no decision yet.
    int handleKey(SDL_Keycode key) {
        if (!visible) return -2;
        if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_TAB) {
            hovered = 1 - hovered;
            return -2;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            int r = (hovered == 0) ? 1 : 0;
            hide();
            return r;
        }
        if (key == SDLK_y) { hide(); return 1; }
        if (key == SDLK_n || key == SDLK_ESCAPE) { hide(); return 0; }
        return -2;
    }

    int handleMotion(int mx, int my) {
        if (!visible) return -2;
        auto rects = btnRects();
        for (int i = 0; i < 2; i++)
            if (mx >= rects[i].x && mx < rects[i].x + rects[i].w &&
                my >= rects[i].y && my < rects[i].y + rects[i].h)
                hovered = i;
        return -2;
    }

    int handleClick(int mx, int my) {
        if (!visible) return -2;
        auto rects = btnRects();
        for (int i = 0; i < 2; i++)
            if (mx >= rects[i].x && mx < rects[i].x + rects[i].w &&
                my >= rects[i].y && my < rects[i].y + rects[i].h) {
                int r = (i == 0) ? 1 : 0;
                hide();
                return r;
            }
        // Click outside does nothing — this is a modal that requires a real choice.
        return -2;
    }

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible) return;

        const int W    = 360;
        const int H    = 110;
        const int X    = (SCREEN_WIDTH - W) / 2;
        const int Y    = (MAP_VIEW_HEIGHT - H) / 2;
        const int PAD  = 14;

        // Warning tone (reddish accent) — this is always an interrupt/danger prompt.
        PanelStyle::frame(r, f, X, Y, W, H, nullptr, {120, 60, 50, 255});

        wrapText(r, f, message, X + PAD, Y + 12, W - PAD * 2, {215, 190, 150, 255});

        auto rects = btnRects();
        const char* labels[2] = {"Continue", "Stop"};
        for (int i = 0; i < 2; i++) {
            bool isHover = (i == hovered);
            if (isHover) {
                SDL_SetRenderDrawColor(r, 55, 40, 25, 255);
                SDL_RenderFillRect(r, &rects[i]);
            }
            SDL_SetRenderDrawColor(r, 90, 80, 60, 255);
            SDL_RenderDrawRect(r, &rects[i]);

            SDL_Color col = isHover ? SDL_Color{230, 210, 120, 255} : SDL_Color{180, 170, 130, 255};
            SDL_Surface* s = TTF_RenderText_Solid(f, labels[i], col);
            if (!s) continue;
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            int tw, th; SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            SDL_FreeSurface(s);
            SDL_Rect dst = {rects[i].x + (rects[i].w - tw) / 2, rects[i].y + (rects[i].h - th) / 2, tw, th};
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        }
    }

private:
    std::vector<SDL_Rect> btnRects() const {
        const int W      = 360;
        const int H      = 110;
        const int X      = (SCREEN_WIDTH - W) / 2;
        const int Y      = (MAP_VIEW_HEIGHT - H) / 2;
        const int BTN_W  = 130;
        const int BTN_H  = 30;
        const int gap    = 20;
        int totalW       = BTN_W * 2 + gap;
        int startX       = X + (W - totalW) / 2;
        int by           = Y + H - BTN_H - 14;
        return {
            {startX, by, BTN_W, BTN_H},
            {startX + BTN_W + gap, by, BTN_W, BTN_H}
        };
    }

    // Simple word-wrap to fit within maxW, one TTF_RenderText_Solid call per line.
    static void wrapText(SDL_Renderer* r, TTF_Font* f, const std::string& text,
                         int x, int y, int maxW, SDL_Color col) {
        std::vector<std::string> words;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t sp = text.find(' ', pos);
            if (sp == std::string::npos) sp = text.size();
            words.push_back(text.substr(pos, sp - pos));
            pos = sp + 1;
        }

        std::string line;
        int ty = y;
        for (const std::string& w : words) {
            std::string candidate = line.empty() ? w : line + " " + w;
            int tw = 0, th = 0;
            TTF_SizeText(f, candidate.c_str(), &tw, &th);
            if (tw > maxW && !line.empty()) {
                drawLine(r, f, line, x, ty, col);
                ty += th + 4;
                line = w;
            } else {
                line = candidate;
            }
        }
        if (!line.empty()) drawLine(r, f, line, x, ty, col);
    }

    static void drawLine(SDL_Renderer* r, TTF_Font* f, const std::string& line,
                        int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderText_Solid(f, line.c_str(), col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
};
