#pragma once
#include "terrain.h"
#include "astar.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdlib>
#include <string>

const int OVERMAP_W = 100;
const int OVERMAP_H = 100;

struct SectorInfo {
    BiomeType biome    = BiomeType::PLAINS;
    bool      explored = false;
};

struct BiomeVisual {
    const char* name;
    const char* symbol;
    SDL_Color   fg;
    SDL_Color   bg;
};

static const BiomeVisual biomeVisuals[] = {
    { "Plains",       ".",  {100, 180,  60, 255}, { 10,  35,   5, 255} },
    { "Forest",       "%",  { 20, 130,  20, 255}, {  3,  22,   3, 255} },
    { "Swamp",        "~",  { 70, 120,  40, 255}, { 10,  25,   8, 255} },
    { "Desert",       ".",  {210, 175,  80, 255}, { 45,  35,   5, 255} },
    { "Tundra",       ".",  {180, 205, 225, 255}, { 25,  35,  55, 255} },
    { "Cursed Lands", "&",  {150,  40, 170, 255}, { 25,   5,  35, 255} },
};

struct Overmap {
    SectorInfo sectors[OVERMAP_H][OVERMAP_W];
    bool       visible = false;

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

    void toggle() { visible = !visible; }

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

        const int CS  = TILE_SIZE;
        const int vW  = SCREEN_WIDTH    / CS;
        const int vH  = MAP_VIEW_HEIGHT / CS;
        const int offX = playerSX - vW / 2;
        const int offY = playerSY - vH / 2;

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

        // Player marker
        int pvx = playerSX - offX;
        int pvy = playerSY - offY;
        if (pvx >= 0 && pvx < vW && pvy >= 0 && pvy < vH) {
            SDL_Rect pcell = {pvx * CS, pvy * CS, CS, CS};
            SDL_RenderCopy(r, playerTex, nullptr, &pcell);
        }

        // Title bar (semi-transparent strip)
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
        SDL_Rect titleBar = {0, 0, SCREEN_WIDTH, 26};
        SDL_RenderFillRect(r, &titleBar);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        int bi = (int)sectors[playerSY][playerSX].biome;
        std::string title = std::string("OVERMAP  [")
            + std::to_string(playerSX) + ", " + std::to_string(playerSY) + "]"
            + "   " + biomeVisuals[bi].name
            + "   |   M to close";
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
