#pragma once
#include "terrain.h"
#include "astar.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdlib>
#include <algorithm>
#include <string>

const int OVERMAP_W = 100;
const int OVERMAP_H = 100;

struct SectorInfo {
    BiomeType biome    = BiomeType::PLAINS;
    bool      explored = false;
};

struct BiomeVisual {
    const char* name;
    const char* description;
    const char* symbol;
    SDL_Color   fg;
    SDL_Color   bg;
};

static const BiomeVisual biomeVisuals[] = {
    { "Plains",       "Open grasslands. Farms and villages are common here.",
      ".",  {100, 180,  60, 255}, { 10,  35,   5, 255} },
    { "Forest",       "Dense woodland. Wolves, bandits and worse lurk in the shadows.",
      "%",  { 20, 130,  20, 255}, {  3,  22,   3, 255} },
    { "Swamp",        "Murky wetlands thick with fog. The ground is treacherous.",
      "~",  { 70, 120,  40, 255}, { 10,  25,   8, 255} },
    { "Desert",       "Scorching sands under a merciless sun. Water is scarce.",
      ".",  {210, 175,  80, 255}, { 45,  35,   5, 255} },
    { "Tundra",       "Frozen wastelands swept by bitter winds. Few survive here.",
      ".",  {180, 205, 225, 255}, { 25,  35,  55, 255} },
    { "Cursed Lands", "Blighted by dark magic. The very ground writhes with evil.",
      "&",  {150,  40, 170, 255}, { 25,   5,  35, 255} },
};

struct Overmap {
    SectorInfo sectors[OVERMAP_H][OVERMAP_W];
    bool       visible = false;
    int        camX = 50, camY = 50;   // overmap camera (independent of player)

    SDL_Texture* biomeTex[6] = {};
    SDL_Texture* unknownTex  = nullptr;
    SDL_Texture* playerTex   = nullptr;

    // ---------------------------------------------------------------- generate

    void generate() {
        for (int y = 0; y < OVERMAP_H; y++)
            for (int x = 0; x < OVERMAP_W; x++)
                sectors[y][x] = { BiomeType::PLAINS, false };

        struct Spec { BiomeType biome; int count, minR, maxR; };
        Spec specs[] = {
            { BiomeType::FOREST,       25,  6, 12 },
            { BiomeType::SWAMP,        10,  4,  8 },
            { BiomeType::DESERT,        8,  5, 10 },
            { BiomeType::TUNDRA,        6,  8, 14 },
            { BiomeType::CURSED_LANDS,  5,  3,  6 },
        };
        for (auto& s : specs) {
            for (int i = 0; i < s.count; i++) {
                int cx = rand() % OVERMAP_W;
                int cy = rand() % OVERMAP_H;
                int r  = s.minR + rand() % (s.maxR - s.minR + 1);
                for (int dy = -r; dy <= r; dy++)
                    for (int dx = -r; dx <= r; dx++) {
                        if (dx*dx + dy*dy > r*r) continue;
                        int nx = cx+dx, ny = cy+dy;
                        if (nx >= 0 && nx < OVERMAP_W && ny >= 0 && ny < OVERMAP_H)
                            sectors[ny][nx].biome = s.biome;
                    }
            }
        }
    }

    // Reveal the 3x3 area around a sector when the player enters it.
    void reveal(int sx, int sy) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = sx+dx, ny = sy+dy;
                if (nx >= 0 && nx < OVERMAP_W && ny >= 0 && ny < OVERMAP_H)
                    sectors[ny][nx].explored = true;
            }
    }

    // Open overmap and snap camera to player position.
    void open(int playerSX, int playerSY) {
        visible = true;
        camX = playerSX;
        camY = playerSY;
    }
    void close() { visible = false; }

    void moveCam(int dx, int dy) {
        camX = std::max(0, std::min(OVERMAP_W - 1, camX + dx));
        camY = std::max(0, std::min(OVERMAP_H - 1, camY + dy));
    }

    // ---------------------------------------------------------------- textures

    void initTextures(SDL_Renderer* r, TTF_Font* f) {
        auto make = [&](const char* sym, SDL_Color col) -> SDL_Texture* {
            SDL_Surface* s = TTF_RenderText_Solid(f, sym, col);
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            return t;
        };
        for (int i = 0; i < 6; i++)
            biomeTex[i] = make(biomeVisuals[i].symbol, biomeVisuals[i].fg);
        unknownTex = make("?", {38, 38, 35, 255});
        playerTex  = make("@", {255, 255, 180, 255});
    }

    void destroyTextures() {
        for (auto& t : biomeTex) { SDL_DestroyTexture(t); t = nullptr; }
        SDL_DestroyTexture(unknownTex); unknownTex = nullptr;
        SDL_DestroyTexture(playerTex);  playerTex  = nullptr;
    }

    // ---------------------------------------------------------------- render

    void render(SDL_Renderer* r, TTF_Font* f, int playerSX, int playerSY) {
        if (!visible) return;

        const int CS   = TILE_SIZE;
        const int vW   = SCREEN_WIDTH    / CS;
        const int vH   = MAP_VIEW_HEIGHT / CS;
        const int offX = camX - vW / 2;    // viewport centered on camera, not player
        const int offY = camY - vH / 2;

        // Clear map view area
        SDL_SetRenderDrawColor(r, 5, 5, 8, 255);
        SDL_Rect mapArea = {0, 0, SCREEN_WIDTH, MAP_VIEW_HEIGHT};
        SDL_RenderFillRect(r, &mapArea);

        for (int vy = 0; vy < vH; vy++) {
            for (int vx = 0; vx < vW; vx++) {
                int sx = offX + vx;
                int sy = offY + vy;
                SDL_Rect cell = {vx * CS, vy * CS, CS, CS};

                if (sx < 0 || sx >= OVERMAP_W || sy < 0 || sy >= OVERMAP_H)
                    continue; // void outside world bounds

                const SectorInfo& sec = sectors[sy][sx];

                if (!sec.explored) {
                    SDL_SetRenderDrawColor(r, 8, 8, 10, 255);
                    SDL_RenderFillRect(r, &cell);
                    SDL_RenderCopy(r, unknownTex, nullptr, &cell);
                    continue;
                }

                int bi = (int)sec.biome;
                SDL_Color bgc = biomeVisuals[bi].bg;
                SDL_SetRenderDrawColor(r, bgc.r, bgc.g, bgc.b, 255);
                SDL_RenderFillRect(r, &cell);
                SDL_RenderCopy(r, biomeTex[bi], nullptr, &cell);
            }
        }

        // Camera crosshair — gold rectangle around the center cell.
        SDL_SetRenderDrawColor(r, 180, 150, 55, 255);
        SDL_Rect crosshair = {(vW / 2) * CS, (vH / 2) * CS, CS, CS};
        SDL_RenderDrawRect(r, &crosshair);

        // Player marker (@) at actual player sector position.
        int pvx = playerSX - offX;
        int pvy = playerSY - offY;
        if (pvx >= 0 && pvx < vW && pvy >= 0 && pvy < vH) {
            SDL_Rect pcell = {pvx * CS, pvy * CS, CS, CS};
            SDL_RenderCopy(r, playerTex, nullptr, &pcell);
        }

        // ── Sector info box (bottom-left, above legend) ─────────────────
        {
            const int BW = 320;
            const int BH = 74;
            const int BX = 8;
            const int BY = MAP_VIEW_HEIGHT - 22 - BH - 6;
            const SectorInfo& sec = sectors[camY][camX];
            int bi2 = (int)sec.biome;

            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 10, 12, 16, 218);
            SDL_Rect box = {BX, BY, BW, BH};
            SDL_RenderFillRect(r, &box);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

            // Border tinted with biome colour (dimmed)
            SDL_Color bc = sec.explored ? biomeVisuals[bi2].fg : SDL_Color{55,55,50,255};
            SDL_SetRenderDrawColor(r, bc.r / 2, bc.g / 2, bc.b / 2, 255);
            SDL_RenderDrawRect(r, &box);

            int ty = BY + 8;
            auto line = [&](const char* text, SDL_Color col) {
                SDL_Surface* s = TTF_RenderText_Solid(f, text, col);
                if (!s) return;
                SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
                SDL_FreeSurface(s);
                int w, h;
                SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
                SDL_Rect d = {BX + 10, ty, w, h};
                SDL_RenderCopy(r, t, nullptr, &d);
                SDL_DestroyTexture(t);
                ty += h + 4;
            };

            if (!sec.explored) {
                line("Unknown Region",              {85, 85, 80, 255});
                line("This area has not been explored yet.", {60, 60, 55, 255});
                line("Enter the region to reveal it.",       {50, 50, 45, 255});
            } else {
                const BiomeVisual& bv = biomeVisuals[bi2];
                line(bv.name,        bv.fg);
                line(bv.description, {150, 145, 130, 255});
                line("Status: Explored", {75, 160, 75, 255});
            }
        }

        // Title bar (semi-transparent strip)
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 165);
        SDL_Rect titleBar = {0, 0, SCREEN_WIDTH, 26};
        SDL_RenderFillRect(r, &titleBar);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // Show camera coords + biome at camera + player coords.
        int bi = (int)sectors[camY][camX].biome;
        std::string camInfo = std::string("[") + std::to_string(camX) + "," + std::to_string(camY) + "] "
                            + biomeVisuals[bi].name;
        std::string playerInfo = std::string("@:[") + std::to_string(playerSX) + ","
                               + std::to_string(playerSY) + "]";
        std::string title = "OVERMAP  " + camInfo + "   " + playerInfo
                          + "   |   Arrows: pan   M: close";
        SDL_Surface* ts = TTF_RenderText_Solid(f, title.c_str(), {175, 150, 65, 255});
        SDL_Texture* tt = SDL_CreateTextureFromSurface(r, ts);
        SDL_FreeSurface(ts);
        int tw, th;
        SDL_QueryTexture(tt, nullptr, nullptr, &tw, &th);
        SDL_Rect tdst = {8, (26 - th) / 2, tw, th};
        SDL_RenderCopy(r, tt, nullptr, &tdst);
        SDL_DestroyTexture(tt);

        // Legend strip at bottom
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
        SDL_Rect legBar = {0, MAP_VIEW_HEIGHT - 22, SCREEN_WIDTH, 22};
        SDL_RenderFillRect(r, &legBar);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        const char* legend = ".: Plains   %: Forest   ~: Swamp   .: Desert   .: Tundra   &: Cursed Lands   ?: Unexplored";
        SDL_Surface* ls = TTF_RenderText_Solid(f, legend, {75, 75, 70, 255});
        SDL_Texture* lt = SDL_CreateTextureFromSurface(r, ls);
        SDL_FreeSurface(ls);
        int lw, lh;
        SDL_QueryTexture(lt, nullptr, nullptr, &lw, &lh);
        SDL_Rect ldst = {(SCREEN_WIDTH - lw) / 2, MAP_VIEW_HEIGHT - 11 - lh / 2, lw, lh};
        SDL_RenderCopy(r, lt, nullptr, &ldst);
        SDL_DestroyTexture(lt);
    }
};
