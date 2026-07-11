#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "astar.h"

// Shared visual chrome for popup panels (body/effects/trade/examine/craft/
// pickup/wait/confirm/...). Keeps every panel's background, border and title
// looking like the same UI system instead of each file inventing its own.
// Panel-specific content (lists, ASCII art, buttons, ...) stays in each file
// and is laid out starting from the Y that frame() returns.
namespace PanelStyle {
    const SDL_Color BG     = {10, 10, 14, 248};
    const SDL_Color ACCENT = {110, 100, 60, 255};  // default border/title colour
    const SDL_Color TITLE  = {215, 190, 90, 255};
    const SDL_Color DIVIDER= {70, 65, 45, 255};

    inline SDL_Color dim(SDL_Color c, int div) {
        return SDL_Color{(Uint8)(c.r / div), (Uint8)(c.g / div), (Uint8)(c.b / div), 255};
    }

    // Full-screen translucent scrim, drawn once behind whichever modal panel
    // is open so it doesn't get lost in the much bigger fullscreen map view.
    inline void dimBackdrop(SDL_Renderer* r) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
        SDL_Rect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        SDL_RenderFillRect(r, &full);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    // Background + double border (outer=accent, inner=accent dimmed) +
    // optional left-aligned title + divider line. Pass title=nullptr for a
    // panel with no header (e.g. a confirm popup). Returns the Y just below
    // the divider (or just inside the border, if there's no title) — where
    // the panel's own content should start.
    inline int frame(SDL_Renderer* r, TTF_Font* f, int x, int y, int w, int h,
                      const char* title,
                      SDL_Color accent = ACCENT, SDL_Color titleColor = TITLE) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, BG.a);
        SDL_Rect bg = {x, y, w, h};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 255);
        SDL_RenderDrawRect(r, &bg);
        SDL_Rect inner = {x + 1, y + 1, w - 2, h - 2};
        SDL_Color innerCol = dim(accent, 2);
        SDL_SetRenderDrawColor(r, innerCol.r, innerCol.g, innerCol.b, 255);
        SDL_RenderDrawRect(r, &inner);

        if (!title) return y + 8;

        SDL_Surface* s = TTF_RenderText_Solid(f, title, titleColor);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            int tw, th;
            SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            SDL_Rect dst = {x + 12, y + 7, tw, th};
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        }

        SDL_SetRenderDrawColor(r, DIVIDER.r, DIVIDER.g, DIVIDER.b, 255);
        SDL_RenderDrawLine(r, x + 8, y + 26, x + w - 8, y + 26);

        return y + 34;
    }
}
