#pragma once
#include "item.h"
#include "astar.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

struct ItemExaminePanel {
    bool visible = false;
    Item item;
    bool pendingResumeCraft = false; // set when player clicks "Resume Crafting"

    void show(const Item& i) { item = i; visible = true; pendingResumeCraft = false; }
    void hide()              { visible = false; }

    // Returns true if the click was consumed (resume button hit).
    bool handleClick(int mx, int my) {
        if (!visible) return false;
        if (item.isPartial &&
            mx >= resumeBtn.x && mx <= resumeBtn.x + resumeBtn.w &&
            my >= resumeBtn.y && my <= resumeBtn.y + resumeBtn.h) {
            pendingResumeCraft = true;
            visible = false;
            return true;
        }
        visible = false;
        return false;
    }

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible) return;

        const int W   = 430;
        const int H   = (item.isPartial ? 355 : 295) + (item.count > 1 ? 20 : 0);
        const int X   = (SCREEN_WIDTH  - W) / 2;
        const int Y   = (MAP_VIEW_HEIGHT - H) / 2;
        const int PAD = 14;
        const int LH  = 20;

        // Background + border, tinted by item type
        SDL_Color borderCol = typeVisuals[(int)item.type].color;
        PanelStyle::frame(r, f, X, Y, W, H, nullptr, borderCol);

        int ty = Y + 10;

        // ── Header: symbol + name + type tag ─────────────────────────────
        SDL_Color typeCol = typeVisuals[(int)item.type].color;
        std::string header = std::string("[") + item.groundSymbol() + "] " + item.name;
        txt(r, f, header.c_str(), X + PAD, ty, typeCol);

        // Type label right-aligned
        static const char* typeLabels[] = {
            "WEAPON","ARMOR","FOOD","DRINK","CONTAINER","JEWELRY","BOOK","TOOL","MISC"
        };
        const char* typeLabel = typeLabels[(int)item.type];
        int tlw = textW(f, typeLabel);
        txt(r, f, typeLabel, X + W - tlw - PAD, ty, {borderCol.r, borderCol.g, borderCol.b, 200});

        ty += LH + 2;
        hline(r, X, ty, W);
        ty += 8;

        // ── Description ──────────────────────────────────────────────────
        SDL_Color desc = {155, 150, 135, 255};
        txt(r, f, item.description.c_str(), X + PAD, ty, desc);
        ty += LH + 8;
        hline(r, X, ty, W);
        ty += 10;

        // ── Stats grid (two columns) ──────────────────────────────────────
        const int COL2 = X + W / 2 + 10;
        SDL_Color label = {100, 100,  90, 255};
        SDL_Color value = {210, 210, 200, 255};
        SDL_Color good  = {100, 210,  80, 255};
        SDL_Color warn  = {255, 165,   0, 255};

        // Volume + Weight (always shown, per unit)
        stat(r, f, "Volume:",  std::to_string(item.volume) + " ml",  X+PAD, ty, label, value);
        stat(r, f, "Weight:",  std::to_string(item.weight) + " g",   COL2,  ty, label, value);
        ty += LH;

        if (item.count > 1) {
            stat(r, f, "Count:", std::to_string(item.count), X+PAD, ty, label, {220, 200, 100, 255});
            ty += LH;
        }

        // Slot (if equippable)
        if (item.slot != EquipSlot::NONE) {
            int si = (int)item.slot;
            stat(r, f, "Slot:",   slotNames[si],                          X+PAD, ty, label, value);
        }
        // Value
        if (item.value > 0) {
            stat(r, f, "Value:", std::to_string(item.value) + " gold",    COL2,  ty, label, {200,175,50,255});
        }
        ty += LH + 4;

        // Type-specific stats
        switch (item.type) {
            case ItemType::WEAPON:
                stat(r, f, "Damage:",     std::to_string(item.damage),        X+PAD, ty, label, good);
                stat(r, f, "Two-handed:", item.twoHanded ? "Yes" : "No",      COL2,  ty, label, item.twoHanded ? warn : value);
                ty += LH;
                break;

            case ItemType::ARMOR:
                stat(r, f, "Defense:",    std::to_string(item.defense),       X+PAD, ty, label, good);
                ty += LH;
                break;

            case ItemType::CONTAINER:
                stat(r, f, "Capacity:",   std::to_string(item.maxVolume) + " ml",  X+PAD, ty, label, value);
                stat(r, f, "Max weight:", std::to_string(item.maxWeight) + " g",   COL2,  ty, label, value);
                ty += LH;
                if (!item.contents.empty()) {
                    stat(r, f, "Contains:",   std::to_string(item.contents.size()) + " items",
                         X+PAD, ty, label, value);
                    stat(r, f, "Used:",
                         std::to_string(item.usedVolume()) + " ml / " + std::to_string(item.usedWeight()) + " g",
                         COL2, ty, label, value);
                    ty += LH;
                }
                break;

            case ItemType::FOOD:
                stat(r, f, "Nutrition:", std::to_string(item.nutrition) + "%", X+PAD, ty, label, good);
                ty += LH;
                break;

            case ItemType::DRINK:
                stat(r, f, "Hydration:", std::to_string(item.hydration) + "%", X+PAD, ty, label, {80,160,255,255});
                ty += LH;
                break;

            case ItemType::JEWELRY: {
                std::string bonuses;
                if (item.strBonus) bonuses += "STR+" + std::to_string(item.strBonus) + " ";
                if (item.dexBonus) bonuses += "DEX+" + std::to_string(item.dexBonus) + " ";
                if (item.conBonus) bonuses += "CON+" + std::to_string(item.conBonus) + " ";
                if (!bonuses.empty()) {
                    stat(r, f, "Bonuses:", bonuses.c_str(), X+PAD, ty, label, good);
                    ty += LH;
                }
                break;
            }
            case ItemType::TOOL:
                if (item.lightRadius > 0) {
                    stat(r, f, "Light radius:", std::to_string(item.lightRadius) + " tiles", X+PAD, ty, label, {220, 200, 100, 255});
                    ty += LH;
                }
                break;
            default: break;
        }

        // ── Partial craft: progress bar + Resume button ───────────────────
        if (item.isPartial && item.craftTotalMins > 0) {
            ty += 4;
            hline(r, X, ty, W); ty += 8;

            int pct = item.craftProgress * 100 / item.craftTotalMins;
            std::string pctStr = "Craft progress: " + std::to_string(pct) + "%  ("
                + std::to_string(item.craftProgress) + " / "
                + std::to_string(item.craftTotalMins) + " min)";
            txt(r, f, pctStr.c_str(), X + PAD, ty, {180, 160, 80, 255});
            ty += LH + 4;

            // Progress bar
            const int BW = W - PAD*2, BH = 10;
            SDL_Rect barBg   = {X+PAD, ty, BW, BH};
            SDL_Rect barFill = {X+PAD, ty, BW * pct / 100, BH};
            SDL_SetRenderDrawColor(r, 30, 25, 10, 255);
            SDL_RenderFillRect(r, &barBg);
            SDL_SetRenderDrawColor(r, 160, 130, 40, 255);
            SDL_RenderFillRect(r, &barFill);
            SDL_SetRenderDrawColor(r, 80, 70, 35, 255);
            SDL_RenderDrawRect(r, &barBg);
            ty += BH + 10;

            // Resume button
            resumeBtn = {X + PAD, ty, W - PAD*2, 26};
            SDL_SetRenderDrawColor(r, 45, 80, 35, 255);
            SDL_RenderFillRect(r, &resumeBtn);
            SDL_SetRenderDrawColor(r, 75, 130, 55, 255);
            SDL_RenderDrawRect(r, &resumeBtn);
            int bw = textW(f, "[ Resume Crafting ]");
            txt(r, f, "[ Resume Crafting ]",
                resumeBtn.x + (resumeBtn.w - bw)/2, resumeBtn.y + 4,
                {150, 220, 110, 255});
            ty += 30;
        }

        // ── Footer hint ───────────────────────────────────────────────────
        hline(r, X, Y + H - 24, W);
        txt(r, f, "Click anywhere to close", X + PAD, Y + H - 18, {55, 55, 50, 255});
    }

private:
    SDL_Rect resumeBtn = {0, 0, 0, 0};

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

    static int textW(TTF_Font* f, const char* text) {
        int w = 0, h = 0;
        TTF_SizeText(f, text, &w, &h);
        return w;
    }

    static void stat(SDL_Renderer* r, TTF_Font* f,
                     const char* label, const char* val,
                     int x, int y, SDL_Color lc, SDL_Color vc) {
        txt(r, f, label, x, y, lc);
        int lw = textW(f, label);
        txt(r, f, val, x + lw + 6, y, vc);
    }

    static void stat(SDL_Renderer* r, TTF_Font* f,
                     const char* label, const std::string& val,
                     int x, int y, SDL_Color lc, SDL_Color vc) {
        stat(r, f, label, val.c_str(), x, y, lc, vc);
    }

    static void hline(SDL_Renderer* r, int x, int y, int w) {
        SDL_SetRenderDrawColor(r, 50, 48, 38, 255);
        SDL_RenderDrawLine(r, x + 8, y, x + w - 8, y);
    }
};
