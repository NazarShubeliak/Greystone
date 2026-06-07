#include "render.h"
#include <vector>

extern SDL_Color white;
extern SDL_Color green;
extern SDL_Color orange;
extern SDL_Color yellow;
extern SDL_Color red;
extern Tile map[MAP_HEIGHT][MAP_WIDTH];
extern Player player;
extern std::vector<Enemy> enemies;
extern SDL_Texture* texWall;
extern SDL_Texture* texFloor;
extern SDL_Texture* texCursor;
extern SDL_Texture* playerTexture;
extern SDL_Texture* enemyTexture;
extern int cameraX;
extern int cameraY;
extern std::vector<SDL_Point> currentPath;
extern std::vector<SDL_Point> previewPath;
extern int pathIndex;
extern int hoverX;
extern int hoverY;
extern int lastHoverX;
extern int lastHoverY;

void initTextures(SDL_Renderer* renderer, TTF_Font* font) {
    SDL_Surface* s = TTF_RenderText_Solid(font, "#", orange);
    texWall = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    s = TTF_RenderText_Solid(font, ".", green);
    texFloor = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    s = TTF_RenderText_Solid(font, "X", yellow);
    texCursor = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
}

void renderMap(SDL_Renderer* renderer) {
    int tilesX = SCREEN_WIDTH / TILE_SIZE;
    int tilesY = SCREEN_HEIGHT / TILE_SIZE;

    for (int y = 0; y < tilesY; y++) {
        for (int x = 0; x < tilesX; x++) {
            int mapX = x + cameraX;
            int mapY = y + cameraY;
            if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) continue;

            SDL_Rect tileDest = {x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE};
            SDL_Texture* tex = (map[mapY][mapX].symbol[0] == '#') ? texWall : texFloor;

            if (map[mapY][mapX].visible) {
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_RenderCopy(renderer, tex, NULL, &tileDest);
            } else if (map[mapY][mapX].explored) {
                SDL_SetTextureColorMod(tex, 80, 80, 80);
                SDL_RenderCopy(renderer, tex, NULL, &tileDest);
            }
        }
    }
}

void renderPlayer(SDL_Renderer* renderer) {
    int w, h;
    SDL_QueryTexture(playerTexture, NULL, NULL, &w, &h);
    SDL_Rect dest = {(player.x - cameraX) * TILE_SIZE, (player.y - cameraY) * TILE_SIZE, w, h};
    SDL_RenderCopy(renderer, playerTexture, NULL, &dest);
}

void renderEnemies(SDL_Renderer* renderer) {
    for (Enemy& enemy : enemies) {
        if (!enemy.alive) continue;
        if (!map[enemy.y][enemy.x].visible) continue;

        int w, h;
        SDL_QueryTexture(enemyTexture, NULL, NULL, &w, &h);
        SDL_Rect dest = {(enemy.x - cameraX) * TILE_SIZE, (enemy.y - cameraY) * TILE_SIZE, w, h};
        SDL_RenderCopy(renderer, enemyTexture, NULL, &dest);
    }
}

void updatePreviewPath() {
    if (currentPath.empty() && (hoverX != lastHoverX || hoverY != lastHoverY)) {
        lastHoverX = hoverX;
        lastHoverY = hoverY;
        if (hoverX >= 0 && hoverX < MAP_WIDTH && hoverY >= 0 && hoverY < MAP_HEIGHT
                && map[hoverY][hoverX].walkable) {
            previewPath = findPath(player.x, player.y, hoverX, hoverY);
        } else {
            previewPath.clear();
        }
    }
}

void renderPath(SDL_Renderer* renderer) {
    if (hoverX < 0 || hoverX >= MAP_WIDTH || hoverY < 0 || hoverY >= MAP_HEIGHT) return;

    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);

    if (!currentPath.empty()) {
        for (int i = pathIndex; i < (int)currentPath.size(); i++) {
            SDL_Rect dot = {
                (currentPath[i].x - cameraX) * TILE_SIZE + TILE_SIZE / 2 - 2,
                (currentPath[i].y - cameraY) * TILE_SIZE + TILE_SIZE / 2 - 2,
                4, 4
            };
            SDL_RenderFillRect(renderer, &dot);
        }
        if (map[hoverY][hoverX].visible) {
            SDL_Rect cursorDest = {(hoverX - cameraX) * TILE_SIZE, (hoverY - cameraY) * TILE_SIZE, TILE_SIZE, TILE_SIZE};
            SDL_RenderCopy(renderer, texCursor, NULL, &cursorDest);
        }
        return;
    }

    if (previewPath.empty()) return;

    int lastVisibleIdx = 0;
    for (int i = 1; i < (int)previewPath.size(); i++) {
        if (!map[previewPath[i].y][previewPath[i].x].visible && !map[previewPath[i].y][previewPath[i].x].explored) break;
        lastVisibleIdx = i;
    }

    for (int i = 1; i <= lastVisibleIdx; i++) {
        SDL_Rect dot = {
            (previewPath[i].x - cameraX) * TILE_SIZE + TILE_SIZE / 2 - 2,
            (previewPath[i].y - cameraY) * TILE_SIZE + TILE_SIZE / 2 - 2,
            4, 4
        };
        SDL_RenderFillRect(renderer, &dot);
    }

    if (lastVisibleIdx > 0) {
        SDL_Point edge = previewPath[lastVisibleIdx];
        SDL_Rect cursorDest = {(edge.x - cameraX) * TILE_SIZE, (edge.y - cameraY) * TILE_SIZE, TILE_SIZE, TILE_SIZE};
        SDL_RenderCopy(renderer, texCursor, NULL, &cursorDest);
    }
}
