#include "astar.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

SDL_Color white = {255, 255, 255, 255};
SDL_Color green = {0, 255, 0, 255};
SDL_Color orange = {255, 140, 0, 255};

struct Player {
    int x;
    int y;
    const char* symbol;
    SDL_Color color;
};

Tile map[MAP_HEIGHT][MAP_WIDTH];
SDL_Texture* tileTextures[MAP_HEIGHT][MAP_WIDTH];
SDL_Texture* playerTexture = nullptr;
Player player = {20, 15, "@", white};
const int VIEW_RADIUS = 8;

std::vector<SDL_Point> currentPath;
int pathIndex = 0;
Uint32 lastMoveTime = 0;

int hoverX = 0;
int hoverY = 0;
int lastHoverX = -1;
int lastHoverY = -1;
std::vector<SDL_Point> previewPath;

int cameraX = 0;
int cameraY = 0;

void initMap() {
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (x == 0 || x == MAP_WIDTH - 1 || y == 0 || y == MAP_HEIGHT - 1) {
                map[y][x] = {false, "#", orange, false, false};
            } else {
                map[y][x] = {true, ".", green, false, false};
            }
        }
    }
}

void initTextures(SDL_Renderer* renderer, TTF_Font* font) {
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            SDL_Surface* surface = TTF_RenderText_Solid(font, map[y][x].symbol, map[y][x].color);
            tileTextures[y][x] = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }
}

void handleInput(SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) {
        running = false;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            int mouseX = event.button.x / TILE_SIZE + cameraX;
            int mouseY = event.button.y / TILE_SIZE + cameraY;
            if (mouseX >= 0 && mouseX < MAP_WIDTH && mouseY >= 0 && mouseY < MAP_HEIGHT) {
                if (map[mouseY][mouseX].walkable) {
                    currentPath = findPath(player.x, player.y, mouseX, mouseY);
                    pathIndex = 0;
                }
            }
        }

        if (event.button.button == SDL_BUTTON_RIGHT) {
            currentPath.clear();
            pathIndex = 0;
            previewPath.clear();
            lastHoverX = -1;
            lastHoverY = -1;
        }
    }

    if (event.type == SDL_MOUSEMOTION) {
        hoverX = event.motion.x / TILE_SIZE + cameraX;
        hoverY = event.motion.y / TILE_SIZE + cameraY;
    }
}

void updatePlayer() {
    Uint32 currentTime = SDL_GetTicks();
    if (pathIndex < (int)currentPath.size() && currentTime - lastMoveTime > 100) {
        player.x = currentPath[pathIndex].x;
        player.y = currentPath[pathIndex].y;
        pathIndex++;
        lastMoveTime = currentTime;
        if (pathIndex >= (int)currentPath.size()) {
            currentPath.clear();
            pathIndex = 0;
            lastHoverX = -1;
            lastHoverY = -1;
            previewPath.clear();
        }
    }
}

void updateCamera() {
    cameraX = player.x - (SCREEN_WIDTH / TILE_SIZE) / 2;
    cameraY = player.y - (SCREEN_HEIGHT / TILE_SIZE) / 2;

    if (cameraX < 0) cameraX = 0;
    if (cameraY < 0) cameraY = 0;
    if (cameraX > MAP_WIDTH - SCREEN_WIDTH / TILE_SIZE) cameraX = MAP_WIDTH - SCREEN_WIDTH / TILE_SIZE;
    if (cameraY > MAP_HEIGHT - SCREEN_HEIGHT / TILE_SIZE) cameraY = MAP_HEIGHT - SCREEN_HEIGHT / TILE_SIZE;
}

void updateVisibility() {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            map[y][x].visible = false;

    for (int y = -VIEW_RADIUS; y <= VIEW_RADIUS; y++) {
        for (int x = -VIEW_RADIUS; x <= VIEW_RADIUS; x++) {
            int tx = player.x + x;
            int ty = player.y + y;

            if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT) continue;

            if (x * x + y * y <= VIEW_RADIUS * VIEW_RADIUS) {
                map[ty][tx].visible = true;
                map[ty][tx].explored = true;
            }
        }
    }
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

            if (map[mapY][mapX].visible) {
                SDL_SetTextureColorMod(tileTextures[mapY][mapX], 255, 255, 255);
                SDL_RenderCopy(renderer, tileTextures[mapY][mapX], NULL, &tileDest);
            } else if (map[mapY][mapX].explored) {
                SDL_SetTextureColorMod(tileTextures[mapY][mapX], 80, 80, 80);
                SDL_RenderCopy(renderer, tileTextures[mapY][mapX], NULL, &tileDest);
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

void renderPath(SDL_Renderer* renderer) {
    if (!currentPath.empty()) return;

    if (hoverX != lastHoverX || hoverY != lastHoverY) {
        previewPath = findPath(player.x, player.y, hoverX, hoverY);
        lastHoverX = hoverX;
        lastHoverY = hoverY;
    }

    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    for (int i = 0; i < (int)previewPath.size(); i++) {
        SDL_Rect dot = {
            (previewPath[i].x - cameraX) * TILE_SIZE + TILE_SIZE / 2 - 2,
            (previewPath[i].y - cameraY) * TILE_SIZE + TILE_SIZE / 2 - 2,
            4, 4
        };
        SDL_RenderFillRect(renderer, &dot);
    }
}

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    initMap();

    SDL_Window* window = SDL_CreateWindow(
        "Greystone",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    TTF_Font* font = TTF_OpenFont("fonts/DejaVuSansMono.ttf", 16);

    SDL_Surface* playerSurface = TTF_RenderText_Solid(font, player.symbol, player.color);
    playerTexture = SDL_CreateTextureFromSurface(renderer, playerSurface);
    SDL_FreeSurface(playerSurface);

    initTextures(renderer, font);
    updateVisibility();
    updateCamera();

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            handleInput(event, running);
        }

        updatePlayer();
        updateVisibility();
        updateCamera();

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        renderMap(renderer);
        renderPath(renderer);
        renderPlayer(renderer);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(playerTexture);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
