#include "astar.h"
#include "types.h"
#include "map.h"
#include "render.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>

SDL_Color white  = {255, 255, 255, 255};
SDL_Color green  = {0,   255,   0, 255};
SDL_Color orange = {255, 140,   0, 255};
SDL_Color yellow = {255, 255,   0, 255};
SDL_Color red    = {255,   0,   0, 255};

Tile map[MAP_HEIGHT][MAP_WIDTH];

SDL_Texture* texWall      = nullptr;
SDL_Texture* texFloor     = nullptr;
SDL_Texture* texCursor    = nullptr;
SDL_Texture* playerTexture = nullptr;
SDL_Texture* enemyTexture  = nullptr;

Player player = {20, 15, "@", white, 100, 0};
std::vector<Enemy> enemies;

std::vector<SDL_Point> currentPath;
int pathIndex   = 0;
Uint32 lastMoveTime = 0;

int hoverX = 0, hoverY = 0;
int lastHoverX = -1, lastHoverY = -1;
std::vector<SDL_Point> previewPath;

int cameraX = 0, cameraY = 0;

void initEnemy() {
    for (int i = 0; i < 10; i++) {
        int x, y;
        do {
            x = rand() % (MAP_WIDTH - 2) + 1;
            y = rand() % (MAP_HEIGHT - 2) + 1;
        } while (!map[y][x].walkable);
        enemies.push_back({x, y, "E", red, 75, 75, true});
    }
}

void handleInput(SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) running = false;

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
            int mouseX = event.button.x / TILE_SIZE + cameraX;
            int mouseY = event.button.y / TILE_SIZE + cameraY;
            if (mouseX >= 0 && mouseX < MAP_WIDTH && mouseY >= 0 && mouseY < MAP_HEIGHT) {
                if (map[mouseY][mouseX].walkable) {
                    currentPath = findPath(player.x, player.y, mouseX, mouseY);
                    pathIndex = 1;
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

void updateEnergy() {
    player.energy += player.speed;
}

bool updatePlayer() {
    if (player.energy < 100) return false;

    Uint32 currentTime = SDL_GetTicks();
    if (pathIndex < (int)currentPath.size() && currentTime - lastMoveTime > 100) {
        player.x = currentPath[pathIndex].x;
        player.y = currentPath[pathIndex].y;
        pathIndex++;
        player.energy -= 100;
        lastMoveTime = currentTime;
        if (pathIndex >= (int)currentPath.size()) {
            currentPath.clear();
            pathIndex = 0;
            lastHoverX = -1;
            lastHoverY = -1;
            previewPath.clear();
        }
        return true;
    }
    return false;
}

void updateEnemies() {
    for (Enemy& enemy : enemies) {
        if (!enemy.alive) continue;

        int dx = player.x - enemy.x;
        int dy = player.y - enemy.y;
        if (dx * dx + dy * dy > 10 * 10) continue;

        if (enemy.energy < 100) {
            enemy.energy += enemy.speed;
            continue;
        }

        std::vector<SDL_Point> path = findPath(enemy.x, enemy.y, player.x, player.y);
        int pathIdx = 1;
        int steps = enemy.energy / 100;

        while (steps > 0 && pathIdx < (int)path.size()) {
            SDL_Point next = path[pathIdx++];
            if (next.x != player.x || next.y != player.y) {
                enemy.x = next.x;
                enemy.y = next.y;
            }
            steps--;
        }

        enemy.energy = enemy.speed;
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

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    initMap();
    initEnemy();

    SDL_Window* window = SDL_CreateWindow("Greystone",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    TTF_Font* font = TTF_OpenFont("fonts/DejaVuSansMono.ttf", 16);

    SDL_Surface* playerSurface = TTF_RenderText_Solid(font, player.symbol, player.color);
    playerTexture = SDL_CreateTextureFromSurface(renderer, playerSurface);
    SDL_FreeSurface(playerSurface);

    SDL_Surface* enemySurface = TTF_RenderText_Solid(font, "E", red);
    enemyTexture = SDL_CreateTextureFromSurface(renderer, enemySurface);
    SDL_FreeSurface(enemySurface);

    initTextures(renderer, font);
    updateVisibility();
    updateCamera();

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event))
            handleInput(event, running);

        updateEnergy();
        bool playerMoved = updatePlayer();
        if (playerMoved) updateEnemies();
        updatePreviewPath();
        updateVisibility();
        updateCamera();

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        renderMap(renderer);
        renderPath(renderer);
        renderPlayer(renderer);
        renderEnemies(renderer);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(playerTexture);
    SDL_DestroyTexture(texWall);
    SDL_DestroyTexture(texFloor);
    SDL_DestroyTexture(texCursor);
    SDL_DestroyTexture(enemyTexture);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
