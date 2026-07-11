#pragma once
#include "item.h"
#include "astar.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <functional>
#include <string>

struct PickupPanel {
    bool visible = false;
    int tileX = 0, tileY = 0;

    struct Entry {
        Item item;
        bool selected = true;
    };
    std::vector<Entry> entries;
    int cursor = 0;

    // Called with selected[] parallel to entries[].
    std::function<void(const std::vector<bool>&)> onConfirm;

    // ── Layout constants (shared between render + mouse handlers) ─────────
    static constexpr int W      = 400;
    static constexpr int ITEM_H = 22;
    static constexpr int HEADER = 56; // title + hint + gap
    static constexpr int BTN_H  = 28;
    static constexpr int FOOTER = 10; // padding below button

    int panelH()  const { return HEADER + (int)entries.size() * ITEM_H + BTN_H + FOOTER; }
    int panelPx() const { return (SCREEN_WIDTH    - W)       / 2; }
    int panelPy() const { return (MAP_VIEW_HEIGHT - panelH()) / 2; }

    // y-coordinate where item list starts (below header + divider gap)
    int itemsY() const { return panelPy() + HEADER + 8; }

    // Returns item index under screen point (x,y), or -1 if none.
    int itemAt(int x, int y) const {
        int px = panelPx(), iy = itemsY();
        if (x < px || x >= px + W) return -1;
        int idx = (y - iy) / ITEM_H;
        if (idx < 0 || idx >= (int)entries.size()) return -1;
        if (y < iy + idx * ITEM_H || y >= iy + (idx + 1) * ITEM_H) return -1;
        return idx;
    }

    // Returns true if screen point is over the "Pick Up" button.
    bool overButton(int x, int y) const {
        int px = panelPx(), py = panelPy();
        int bx = px + 12,   bw = W - 24;
        int by = py + HEADER + 8 + (int)entries.size() * ITEM_H + 4;
        return x >= bx && x < bx + bw && y >= by && y < by + BTN_H;
    }

    // ── Interface ─────────────────────────────────────────────────────────

    void show(int x, int y, std::vector<Item> items) {
        tileX = x; tileY = y;
        entries.clear();
        cursor = 0;
        for (auto& it : items)
            entries.push_back({std::move(it), true});
        visible = true;
    }

    void hide() { visible = false; }

    void confirm() {
        std::vector<bool> sel;
        sel.reserve(entries.size());
        for (auto& e : entries) sel.push_back(e.selected);
        if (onConfirm) onConfirm(sel);
        hide();
    }

    // Mouse motion: update cursor highlight. Returns true if consumed.
    bool handleMouseMotion(int x, int y) {
        if (!visible) return false;
        int idx = itemAt(x, y);
        if (idx >= 0) cursor = idx;
        return true;
    }

    // Mouse click: toggle item or press button. Returns true if consumed.
    bool handleMouseClick(int x, int y) {
        if (!visible) return false;
        // Click on item row → toggle selection
        int idx = itemAt(x, y);
        if (idx >= 0) {
            entries[idx].selected = !entries[idx].selected;
            cursor = idx;
            return true;
        }
        // Click on "Pick Up" button → confirm
        if (overButton(x, y)) {
            confirm();
            return true;
        }
        // Click outside panel → close without picking up
        int px = panelPx(), py = panelPy();
        bool inside = (x >= px && x < px + W && y >= py && y < py + panelH());
        if (!inside) hide();
        return true;
    }

    // Keyboard: returns true if consumed.
    bool handleKey(SDL_Keycode key) {
        if (!visible) return false;
        if (key == SDLK_ESCAPE)  { hide();    return true; }
        if (key == SDLK_RETURN)  { confirm(); return true; }
        if (key == SDLK_UP)   { if (cursor > 0)                          cursor--; return true; }
        if (key == SDLK_DOWN) { if (cursor < (int)entries.size() - 1)    cursor++; return true; }
        if (key == SDLK_SPACE) {
            if (!entries.empty()) entries[cursor].selected = !entries[cursor].selected;
            return true;
        }
        if (key >= SDLK_a && key <= SDLK_z) {
            int idx = (int)(key - SDLK_a);
            if (idx < (int)entries.size()) { entries[idx].selected = !entries[idx].selected; cursor = idx; }
            return true;
        }
        return true; // consume all keys while open
    }

    // ── Render ────────────────────────────────────────────────────────────

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible || entries.empty()) return;

        const int px = panelPx(), py = panelPy(), H = panelH();

        // Background + border
        PanelStyle::frame(r, f, px, py, W, H, nullptr);

        SDL_Color gold  = PanelStyle::TITLE;
        SDL_Color white = {215, 215, 205, 255};
        SDL_Color dim   = {110, 110, 100, 255};
        SDL_Color green = { 80, 200,  80, 255};
        SDL_Color grey  = {140, 140, 130, 255};

        auto drawText = [&](const std::string& text, SDL_Color col, int x, int y) {
            if (text.empty()) return;
            SDL_Surface* s = TTF_RenderText_Solid(f, text.c_str(), col);
            if (!s) return;
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_Rect dst = {x, y, w, h};
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        };

        // Header
        int cy = py + 10;
        drawText("Pick up items:", gold, px + 12, cy); cy += 22;
        drawText("Click item to toggle   a-z/Space=toggle   Enter=confirm   Esc=cancel", dim, px + 12, cy); cy += 18;

        // Divider
        SDL_SetRenderDrawColor(r, 65, 65, 60, 255);
        SDL_RenderDrawLine(r, px + 6, cy + 2, px + W - 6, cy + 2);
        cy += 8;

        // Item list
        for (int i = 0; i < (int)entries.size(); i++) {
            const Entry& e = entries[i];

            // Row highlight on cursor
            if (i == cursor) {
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(r, 40, 65, 40, 180);
                SDL_Rect row = {px + 4, cy - 1, W - 8, ITEM_H};
                SDL_RenderFillRect(r, &row);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            }

            // Letter
            std::string letter = (i < 26) ? std::string(1, 'a' + i) : " ";
            drawText(letter, e.selected ? green : dim, px + 10, cy + 2);

            // Checkbox
            drawText(e.selected ? "[x]" : "[ ]", e.selected ? green : grey, px + 26, cy + 2);

            // Name (+ count if a stack) + weight
            std::string label = e.item.name;
            if (e.item.count > 1) label += " x" + std::to_string(e.item.count);
            label += "  (" + std::to_string(e.item.weight * e.item.count) + "g)";
            drawText(label, e.selected ? white : dim, px + 68, cy + 2);

            cy += ITEM_H;
        }

        // "Pick Up" button
        cy += 4;
        bool btnHover = false;
        {
            int mx, my; SDL_GetMouseState(&mx, &my);
            btnHover = overButton(mx, my);
        }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, btnHover ? 60 : 30, btnHover ? 90 : 60, btnHover ? 60 : 30, 220);
        SDL_Rect btnRect = {px + 12, cy, W - 24, BTN_H};
        SDL_RenderFillRect(r, &btnRect);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, btnHover ? 100 : 80, btnHover ? 160 : 120, btnHover ? 100 : 80, 255);
        SDL_RenderDrawRect(r, &btnRect);

        // Count selected
        int selCount = 0;
        for (auto& e : entries) if (e.selected) selCount++;
        std::string btnLabel = "Pick Up (" + std::to_string(selCount) + " selected)";
        // Center label in button
        {
            SDL_Surface* s = TTF_RenderText_Solid(f, btnLabel.c_str(), btnHover ? white : SDL_Color{160, 200, 160, 255});
            if (s) {
                SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
                SDL_FreeSurface(s);
                int tw, th; SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
                SDL_Rect dst = {px + 12 + (W - 24 - tw) / 2, cy + (BTN_H - th) / 2, tw, th};
                SDL_RenderCopy(r, t, nullptr, &dst);
                SDL_DestroyTexture(t);
            }
        }
    }
};
