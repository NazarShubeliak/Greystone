#include "render.h"
#include "map.h"
#include <vector>

extern Tile   map[MAP_HEIGHT][MAP_WIDTH];
extern Player player;
extern std::vector<Enemy> enemies;
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

// ---------------------------------------------------------------- helpers

static SDL_Texture* makeCharTex(SDL_Renderer* r, TTF_Font* f,
                                 const char* sym, SDL_Color col) {
    SDL_Surface* s = TTF_RenderText_Solid(f, sym, col);
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

    SDL_Color yellow = {255, 255, 0, 255};
    texCursor = makeCharTex(r, f, "X", yellow);
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
            if      (t.objectId >= 0) tex = objectTex [t.objectId];
            else if (t.groundId  >= 0) tex = groundTex [t.groundId];
            else                       tex = terrainTex[t.terrainId];

            // Dim explored-but-not-visible tiles
            if (t.visible)
                SDL_SetTextureColorMod(tex, 255, 255, 255);
            else
                SDL_SetTextureColorMod(tex, 70, 70, 70);

            SDL_Rect dst = {x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE};
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

void renderEnemies(SDL_Renderer* r) {
    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        if (!map[e.y][e.x].visible) continue;
        int w, h;
        SDL_QueryTexture(enemyTexture, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {(e.x - cameraX) * TILE_SIZE,
                        (e.y - cameraY) * TILE_SIZE, w, h};
        SDL_RenderCopy(r, enemyTexture, nullptr, &dst);
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
