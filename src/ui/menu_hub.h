#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "astar.h"
#include "panel_style.h"

// Which of the eight "reference" screens (Character/Body/Effects/Skills/
// Techniques/Inventory/Craft/Map) is currently showing full-screen. NONE = hub closed.
enum class MenuTab { NONE, CHARACTER, BODY, EFFECTS, SKILLS, TECHNIQUES, INVENTORY, CRAFT, MAP };

// Thin coordinator, not a rendering system of its own: main.cpp still calls
// each screen's existing render()/handleClick()/handleKey() — this only
// tracks which tab is active and draws the shared tab strip on top.
struct MenuHub {
    MenuTab activeTab = MenuTab::NONE;
    static constexpr int TAB_H = 30;

    bool visible() const { return activeTab != MenuTab::NONE; }

    // Solid, opaque background — unlike PanelStyle::dimBackdrop() (used behind
    // small contextual popups, semi-transparent on purpose so the map still
    // shows through), the hub is a full screen in its own right and should
    // fully hide the map, same as Inventory/Map already did on their own.
    void renderBackground(SDL_Renderer* r) const {
        SDL_SetRenderDrawColor(r, PanelStyle::BG.r, PanelStyle::BG.g, PanelStyle::BG.b, 255);
        SDL_Rect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        SDL_RenderFillRect(r, &full);
    }

    struct TabDef { MenuTab tab; const char* label; };
    static constexpr int TAB_COUNT = 8;
    static constexpr TabDef TABS[TAB_COUNT] = {
        {MenuTab::CHARACTER,  "Character"},
        {MenuTab::BODY,       "Body"},
        {MenuTab::EFFECTS,    "Effects"},
        {MenuTab::SKILLS,     "Skills"},
        {MenuTab::TECHNIQUES, "Techniques"},
        {MenuTab::INVENTORY,  "Inventory"},
        {MenuTab::CRAFT,      "Craft"},
        {MenuTab::MAP,        "Map"},
    };

    // Shared geometry for both hit-testing and drawing, so they can never disagree.
    void layout(TTF_Font* f, SDL_Rect out[TAB_COUNT]) const {
        int x = 16;
        for (int i = 0; i < TAB_COUNT; i++) {
            int w, h;
            TTF_SizeText(f, TABS[i].label, &w, &h);
            const int hpad = 18;
            out[i] = {x, 0, w + hpad * 2, TAB_H};
            x += out[i].w;
        }
    }

    MenuTab tabAt(TTF_Font* f, int mx, int my) const {
        if (my < 0 || my >= TAB_H) return MenuTab::NONE;
        SDL_Rect rects[TAB_COUNT];
        layout(f, rects);
        for (int i = 0; i < TAB_COUNT; i++)
            if (mx >= rects[i].x && mx < rects[i].x + rects[i].w) return TABS[i].tab;
        return MenuTab::NONE;
    }

    // Close (X) button pinned to the top-right corner of the tab strip.
    static constexpr int CLOSE_W = 40;
    SDL_Rect closeRect() const { return {SCREEN_WIDTH - CLOSE_W, 0, CLOSE_W, TAB_H}; }
    bool closeButtonAt(int mx, int my) const {
        SDL_Rect cr = closeRect();
        return mx >= cr.x && mx < cr.x + cr.w && my >= cr.y && my < cr.y + cr.h;
    }

    void renderTabBar(SDL_Renderer* r, TTF_Font* f) const {
        SDL_SetRenderDrawColor(r, PanelStyle::BG.r, PanelStyle::BG.g, PanelStyle::BG.b, 255);
        SDL_Rect bar = {0, 0, SCREEN_WIDTH, TAB_H};
        SDL_RenderFillRect(r, &bar);

        SDL_Rect rects[TAB_COUNT];
        layout(f, rects);
        for (int i = 0; i < TAB_COUNT; i++) {
            bool active = (TABS[i].tab == activeTab);
            if (active) {
                SDL_SetRenderDrawColor(r, PanelStyle::ACCENT.r / 3, PanelStyle::ACCENT.g / 3,
                                       PanelStyle::ACCENT.b / 3, 255);
                SDL_RenderFillRect(r, &rects[i]);
                SDL_SetRenderDrawColor(r, PanelStyle::ACCENT.r, PanelStyle::ACCENT.g,
                                       PanelStyle::ACCENT.b, 255);
                SDL_RenderDrawLine(r, rects[i].x, TAB_H - 2, rects[i].x + rects[i].w, TAB_H - 2);
            }
            SDL_Color col = active ? PanelStyle::TITLE : SDL_Color{110, 108, 100, 255};
            SDL_Surface* s = TTF_RenderText_Solid(f, TABS[i].label, col);
            if (s) {
                SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
                int tw, th;
                SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                SDL_FreeSurface(s);
                SDL_Rect dst = {rects[i].x + (rects[i].w - tw) / 2, (TAB_H - th) / 2, tw, th};
                SDL_RenderCopy(r, t, nullptr, &dst);
                SDL_DestroyTexture(t);
            }
        }

        // Close (X) button, top-right corner.
        SDL_Rect cr = closeRect();
        SDL_SetRenderDrawColor(r, 90, 40, 35, 255);
        SDL_RenderFillRect(r, &cr);
        SDL_Surface* xs = TTF_RenderText_Solid(f, "X", {225, 210, 205, 255});
        if (xs) {
            SDL_Texture* xt = SDL_CreateTextureFromSurface(r, xs);
            int xw, xh;
            SDL_QueryTexture(xt, nullptr, nullptr, &xw, &xh);
            SDL_FreeSurface(xs);
            SDL_Rect dst = {cr.x + (cr.w - xw) / 2, (TAB_H - xh) / 2, xw, xh};
            SDL_RenderCopy(r, xt, nullptr, &dst);
            SDL_DestroyTexture(xt);
        }

        SDL_SetRenderDrawColor(r, PanelStyle::DIVIDER.r, PanelStyle::DIVIDER.g,
                               PanelStyle::DIVIDER.b, 255);
        SDL_RenderDrawLine(r, 0, TAB_H, SCREEN_WIDTH, TAB_H);
    }
};
