#include "render.h"
#include "map.h"
#include "npc.h"
#include <vector>

extern Tile   map[MAP_HEIGHT][MAP_WIDTH];
extern Player player;
extern std::vector<Enemy>    enemies;
extern std::vector<Villager> villagers;
extern SDL_Texture* texCursor;
extern SDL_Texture* playerTexture;
extern SDL_Texture* enemyTexture;
extern int cameraX, cameraY;
extern std::vector<SDL_Point> currentPath;
extern std::vector<SDL_Point> previewPath;
extern int pathIndex;
extern int hoverX, hoverY;
extern int lastHoverX, lastHoverY;

// One texture per terrain / ground / object type + cursor.
SDL_Texture* terrainTex[T_COUNT];
SDL_Texture* groundTex [G_COUNT];
SDL_Texture* objectTex [O_COUNT];
SDL_Texture* sproutTex = nullptr;

// ---------------------------------------------------------------- helpers

static SDL_Texture* makeCharTex(SDL_Renderer* r, TTF_Font* f,
                                 const char* sym, SDL_Color col) {
    SDL_Surface* s = TTF_RenderUTF8_Solid(f, sym, col);
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

// ---------------------------------------------------------------- init

void initTextures(SDL_Renderer* r, TTF_Font* f) {
    for (int i = 0; i < T_COUNT; i++)
        terrainTex[i] = makeCharTex(r, f, terrainDefs[i].symbol, terrainDefs[i].color);
    for (int i = 0; i < G_COUNT; i++)
        groundTex[i]  = makeCharTex(r, f, groundDefs[i].symbol,  groundDefs[i].color);
    for (int i = 0; i < O_COUNT; i++)
        objectTex[i]  = makeCharTex(r, f, objectDefs[i].symbol,  objectDefs[i].color);

    SDL_Color yellow      = {255, 255, 0, 255};
    SDL_Color sproutColor = { 80, 160, 60, 255};
    texCursor  = makeCharTex(r, f, "X", yellow);
    sproutTex  = makeCharTex(r, f, ",", sproutColor);
}

// ---------------------------------------------------------------- map

void renderMap(SDL_Renderer* r) {
    int tilesX = SCREEN_WIDTH    / TILE_SIZE;
    int tilesY = MAP_VIEW_HEIGHT / TILE_SIZE;

    for (int y = 0; y < tilesY; y++) {
        for (int x = 0; x < tilesX; x++) {
            int mx = x + cameraX;
            int my = y + cameraY;
            if (mx < 0 || mx >= MAP_WIDTH || my < 0 || my >= MAP_HEIGHT) continue;

            const Tile& t = map[my][mx];
            if (!t.visible && !t.explored) continue;

            // Pick the right texture: object > ground > terrain
            SDL_Texture* tex;
            bool isPlantSprout = false;

            if (t.objectId >= 0) {
                bool isPlant = objectDefs[t.objectId].isPlant;
                if (isPlant && t.plantAge < 85) {
                    tex = sproutTex;
                    isPlantSprout = true;
                } else {
                    tex = objectTex[t.objectId];
                }
            } else if (t.groundId >= 0) {
                tex = groundTex[t.groundId];
            } else {
                tex = terrainTex[t.terrainId];
            }

            // Color mod: visible = full color (plants get stage tint), not visible = dim
            if (t.visible) {
                if (isPlantSprout) {
                    SDL_SetTextureColorMod(tex, 255, 255, 255); // sprout uses its own color
                } else if (t.objectId >= 0 && objectDefs[t.objectId].isPlant) {
                    // Growing (85-169): 70% brightness; Mature (170+): full
                    uint8_t mod = (t.plantAge < 170) ? 178 : 255;
                    SDL_SetTextureColorMod(tex, mod, mod, mod);
                } else {
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                }
            } else {
                SDL_SetTextureColorMod(tex, 70, 70, 70);
            }

            SDL_Rect dst = {x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE};

            // Water is the one terrain type that gets a filled background
            // instead of just a bare glyph — every other terrain (grass,
            // sand, swamp...) stays glyph-only, but water uniquely blocks
            // movement and marks a whole physical feature (river/lake), and
            // a lone dim "~" on the black canvas was unreadable at a glance
            // (user report). Dimmed the same way the glyph itself dims for
            // explored-but-not-currently-visible tiles.
            if (t.terrainId == T_WATER && t.objectId < 0 && t.groundId < 0) {
                SDL_Color wbg = t.visible ? SDL_Color{10, 40, 90, 255} : SDL_Color{5, 16, 32, 255};
                SDL_SetRenderDrawColor(r, wbg.r, wbg.g, wbg.b, 255);
                SDL_RenderFillRect(r, &dst);
            }

            SDL_RenderCopy(r, tex, nullptr, &dst);
        }
    }
}

// ---------------------------------------------------------------- actors

void renderPlayer(SDL_Renderer* r) {
    int w, h;
    SDL_QueryTexture(playerTexture, nullptr, nullptr, &w, &h);
    SDL_Rect dst = {(player.x - cameraX) * TILE_SIZE,
                    (player.y - cameraY) * TILE_SIZE, w, h};
    SDL_RenderCopy(r, playerTexture, nullptr, &dst);
}

void renderEnemies(SDL_Renderer* r, TTF_Font* font) {
    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        if (!map[e.y][e.x].visible) continue;
        int sx = (e.x - cameraX) * TILE_SIZE;
        int sy = (e.y - cameraY) * TILE_SIZE;
        if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;
        SDL_Surface* s = TTF_RenderUTF8_Solid(font, e.symbol, e.color);
        if (!s) continue;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        SDL_Rect dst = {sx, sy, TILE_SIZE, TILE_SIZE};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
}

// ---------------------------------------------------------------- villagers

void renderVillagers(SDL_Renderer* r, TTF_Font* font) {
    for (const Villager& v : villagers) {
        if (!v.alive) continue;
        if (!map[v.y][v.x].visible) continue;
        int sx = (v.x - cameraX) * TILE_SIZE;
        int sy = (v.y - cameraY) * TILE_SIZE;
        if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;
        SDL_Surface* s = TTF_RenderUTF8_Solid(font, v.symbol, v.color);
        if (!s) continue;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        SDL_Rect dst = {sx, sy, TILE_SIZE, TILE_SIZE};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
}

// ---------------------------------------------------------------- path preview

void updatePreviewPath() {
    if (currentPath.empty() && (hoverX != lastHoverX || hoverY != lastHoverY)) {
        lastHoverX = hoverX;
        lastHoverY = hoverY;
        if (hoverX >= 0 && hoverX < MAP_WIDTH &&
            hoverY >= 0 && hoverY < MAP_HEIGHT &&
            map[hoverY][hoverX].walkable()) {
            previewPath = findPath(player.x, player.y, hoverX, hoverY);
        } else {
            previewPath.clear();
        }
    }
}

void renderPath(SDL_Renderer* r) {
    if (hoverX < 0 || hoverX >= MAP_WIDTH ||
        hoverY < 0 || hoverY >= MAP_HEIGHT) return;

    SDL_SetRenderDrawColor(r, 255, 255, 0, 255);

    if (!currentPath.empty()) {
        for (int i = pathIndex; i < (int)currentPath.size(); i++) {
            SDL_Rect dot = {
                (currentPath[i].x - cameraX) * TILE_SIZE + TILE_SIZE / 2 - 2,
                (currentPath[i].y - cameraY) * TILE_SIZE + TILE_SIZE / 2 - 2,
                4, 4
            };
            SDL_RenderFillRect(r, &dot);
        }
        if (map[hoverY][hoverX].visible) {
            SDL_Rect dst = {(hoverX - cameraX) * TILE_SIZE,
                            (hoverY - cameraY) * TILE_SIZE, TILE_SIZE, TILE_SIZE};
            SDL_RenderCopy(r, texCursor, nullptr, &dst);
        }
        return;
    }

    if (previewPath.empty()) return;

    int lastVisIdx = 0;
    for (int i = 1; i < (int)previewPath.size(); i++) {
        if (!map[previewPath[i].y][previewPath[i].x].visible &&
            !map[previewPath[i].y][previewPath[i].x].explored) break;
        lastVisIdx = i;
    }

    for (int i = 1; i <= lastVisIdx; i++) {
        SDL_Rect dot = {
            (previewPath[i].x - cameraX) * TILE_SIZE + TILE_SIZE / 2 - 2,
            (previewPath[i].y - cameraY) * TILE_SIZE + TILE_SIZE / 2 - 2,
            4, 4
        };
        SDL_RenderFillRect(r, &dot);
    }

    if (lastVisIdx > 0) {
        SDL_Point edge = previewPath[lastVisIdx];
        SDL_Rect dst = {(edge.x - cameraX) * TILE_SIZE,
                        (edge.y - cameraY) * TILE_SIZE, TILE_SIZE, TILE_SIZE};
        SDL_RenderCopy(r, texCursor, nullptr, &dst);
    }
}
