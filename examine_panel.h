#pragma once
#include "map.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

struct ExaminePanel {
    bool visible = false;
    int  tileX = 0, tileY = 0;

    void show(int x, int y) { tileX = x; tileY = y; visible = true; }
    void hide() { visible = false; }

    void render(SDL_Renderer* r, TTF_Font* f, const Tile& tile) {
        if (!visible) return;

        const int W  = 480;
        const int H  = 300;
        const int px = (SCREEN_WIDTH - W) / 2;
        const int py = (MAP_VIEW_HEIGHT - H) / 2;
        const int LX = px + 14;
        int cy = py + 12;

        auto txt = [&](const std::string& text, SDL_Color col) {
            SDL_Surface* s = TTF_RenderText_Solid(f, text.c_str(), col);
            if (!s) return;
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            int w, h;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_Rect dst = {LX, cy, w, h};
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            cy += h + 4;
        };

        auto hline = [&]() {
            SDL_SetRenderDrawColor(r, 65, 65, 60, 255);
            SDL_RenderDrawLine(r, px + 6, cy + 2, px + W - 6, cy + 2);
            cy += 10;
        };

        // Background
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 12, 14, 18, 235);
        SDL_Rect bg = {px, py, W, H};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // Border
        SDL_SetRenderDrawColor(r, 120, 100, 50, 255);
        SDL_RenderDrawRect(r, &bg);

        SDL_Color gold   = {200, 170,  80, 255};
        SDL_Color white  = {215, 215, 205, 255};
        SDL_Color dim    = {145, 145, 130, 255};
        SDL_Color objCol = {180, 200,  70, 255};
        SDL_Color gndCol = { 70, 180,  90, 255};
        SDL_Color terCol = { 70, 145, 185, 255};

        // Title row
        txt("EXAMINE  [" + std::to_string(tileX) + ", " + std::to_string(tileY) + "]", gold);
        hline();

        // ── Object ──────────────────────────────────────────────────────
        if (tile.objectId >= 0) {
            const ObjectDef& od = objectDefs[tile.objectId];
            txt(std::string("[Object]  ") + od.name, objCol);
            txt(std::string("  ") + od.description, dim);

            if (od.durability > 0) {
                txt("  Durability: " + std::to_string(tile.objectHp)
                    + " / " + std::to_string(od.durability), white);
            }

            std::string pass = std::string("  Passable: ") + (od.blocksMove ? "No" : "Yes");
            if (!od.blocksMove && od.moveCostMod > 0)
                pass += "   Penalty: +" + std::to_string(od.moveCostMod);
            pass += std::string("   Blocks sight: ") + (od.blocksVision ? "Yes" : "No");
            txt(pass, dim);
            hline();
        }

        // ── Ground cover ─────────────────────────────────────────────────
        if (tile.groundId >= 0) {
            const GroundDef& gd = groundDefs[tile.groundId];
            txt(std::string("[Ground]  ") + gd.name, gndCol);
            txt(std::string("  ") + gd.description, dim);
            txt("  Move penalty: +" + std::to_string(gd.moveCostMod), white);
            hline();
        }

        // ── Terrain ──────────────────────────────────────────────────────
        const TerrainDef& td = terrainDefs[tile.terrainId];
        txt(std::string("[Terrain] ") + td.name, terCol);
        txt(std::string("  ") + td.description, dim);
        std::string terrStr = (td.moveCost == 0)
            ? "  Passable: No"
            : "  Base move cost: " + std::to_string(td.moveCost);
        txt(terrStr, white);
        hline();

        // ── Summary ──────────────────────────────────────────────────────
        std::string total = tile.walkable()
            ? "Total move cost: " + std::to_string(tile.moveCost())
            : "Total: Impassable";
        txt(total, {220, 220, 200, 255});

        // Close hint
        SDL_Surface* hs = TTF_RenderText_Solid(f, "Click or E to close", {65, 65, 60, 255});
        if (hs) {
            SDL_Texture* ht = SDL_CreateTextureFromSurface(r, hs);
            SDL_FreeSurface(hs);
            int hw, hh;
            SDL_QueryTexture(ht, nullptr, nullptr, &hw, &hh);
            SDL_Rect hdst = {px + W - hw - 10, py + H - hh - 6, hw, hh};
            SDL_RenderCopy(r, ht, nullptr, &hdst);
            SDL_DestroyTexture(ht);
        }
    }
};
