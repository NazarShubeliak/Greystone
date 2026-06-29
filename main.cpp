#include "astar.h"
#include "actor.h"
#include "ui.h"
#include "bottom_panel.h"
#include "body_panel.h"
#include "context_menu.h"
#include "examine_panel.h"
#include "overmap.h"
#include "cheat_console.h"
#include "map.h"
#include "render.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <ctime>
#include <algorithm>

SDL_Color white = {255, 255, 255, 255};
SDL_Color red   = {255,   0,   0, 255};

Tile map[MAP_HEIGHT][MAP_WIDTH];

SDL_Texture* texCursor     = nullptr;
SDL_Texture* playerTexture = nullptr;
SDL_Texture* enemyTexture  = nullptr;

Player player(20, 15);
std::vector<Enemy> enemies;

std::vector<SDL_Point> currentPath;
int pathIndex = 0;
Uint32 lastMoveTime = 0;

int hoverX = 0, hoverY = 0;
int lastHoverX = -1, lastHoverY = -1;
std::vector<SDL_Point> previewPath;

int cameraX = 0, cameraY = 0;
UI ui;
BottomPanel panel;
BodyPanel bodyPanel;
ContextMenu contextMenu;
ExaminePanel examinePanel;
Overmap overmap;
CheatConsole console;
int playerSectorX = 50;
int playerSectorY = 50;

// ------------------------------------------------------------------ helpers

Enemy* getEnemyAt(int x, int y) {
    for (Enemy& e : enemies)
        if (e.isAlive() && e.x == x && e.y == y) return &e;
    return nullptr;
}

bool isTileOccupied(int x, int y) {
    for (Enemy& e : enemies)
        if (e.alive && e.x == x && e.y == y) return true;
    return false;
}

void initEnemy() {
    for (int i = 0; i < 10; i++) {
        int x, y;
        do {
            x = rand() % (MAP_WIDTH - 2) + 1;
            y = rand() % (MAP_HEIGHT - 2) + 1;
        } while (!map[y][x].walkable());
        enemies.push_back(Enemy(x, y, "E", red, 75, Race::HUMAN, 10));
    }
}

// ------------------------------------------------------------------ turn system
//
// One "world tick" = everyone gains speed energy; enemies spend theirs acting.
// The world only ticks when the player chooses to act (world freezes while idle).
// Fast actors (speed > 100) accumulate energy and act multiple times per cycle.
// Slow actors (speed < 100) act less often — multiple ticks pass per their action.

void enemyAct(Enemy& enemy) {
    int dx = player.x - enemy.x;
    int dy = player.y - enemy.y;
    if (dx * dx + dy * dy > enemy.aggroRange * enemy.aggroRange) return;

    std::vector<SDL_Point> path = findPath(enemy.x, enemy.y, player.x, player.y);
    if ((int)path.size() < 2) return;

    SDL_Point next = path[1];
    if (next.x == player.x && next.y == player.y) {
        int damage = 3 + (enemy.strength - 10) / 2;
        player.takeDamage(damage);
        panel.addMessage(enemy.name + " hits you for " + std::to_string(damage) + " damage.");
        if (!player.isAlive())
            panel.addMessage("You have been slain by " + enemy.name + ".");
    } else if (!isTileOccupied(next.x, next.y)) {
        enemy.x = next.x;
        enemy.y = next.y;
    }
}

// One world tick: give everyone energy, then let enemies spend theirs.
void tickWorld() {
    player.energy += player.speed;
    player.tickNeeds();
    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        e.energy += e.speed;
        e.tickNeeds();
        while (e.energy >= 100) {
            enemyAct(e);
            e.energy -= 100;
        }
    }
}

// Called after every player action.
// Spends 100 energy, then ticks the world until the player can act again.
// Result: player.energy >= 100 when this returns.
void onPlayerAct() {
    player.energy -= 100;
    while (player.energy < 100)
        tickWorld();
}

void updateCamera();

// ------------------------------------------------------------------ sector transition

void checkSectorTransition() {
    if (player.x > 0 && player.x < MAP_WIDTH - 1 &&
        player.y > 0 && player.y < MAP_HEIGHT - 1) return;

    int newSX = playerSectorX, newSY = playerSectorY;
    int newPX = player.x,     newPY = player.y;

    // Horizontal transitions take priority over vertical.
    if      (player.x == 0)             { newSX--; newPX = MAP_WIDTH  - 2; }
    else if (player.x == MAP_WIDTH - 1) { newSX++; newPX = 1;              }
    else if (player.y == 0)             { newSY--; newPY = MAP_HEIGHT - 2; }
    else if (player.y == MAP_HEIGHT -1) { newSY++; newPY = 1;              }

    // Clamp at world boundary — push player back inside.
    if (newSX < 0 || newSX >= OVERMAP_W || newSY < 0 || newSY >= OVERMAP_H) {
        player.x = std::max(1, std::min(MAP_WIDTH  - 2, player.x));
        player.y = std::max(1, std::min(MAP_HEIGHT - 2, player.y));
        return;
    }

    playerSectorX = newSX;
    playerSectorY = newSY;
    player.x = newPX;
    player.y = newPY;

    generateSector(overmap.sectors[playerSectorY][playerSectorX].biome,
                   playerSectorX, playerSectorY);
    overmap.reveal(playerSectorX, playerSectorY);

    enemies.clear();
    initEnemy();
    currentPath.clear();
    pathIndex = 0;
    previewPath.clear();
    examinePanel.hide();
    updateVisibility();
    updateCamera();

    int bi = (int)overmap.sectors[playerSectorY][playerSectorX].biome;
    panel.addMessage(std::string("You enter: ") + biomeVisuals[bi].name + ".");
}

// ------------------------------------------------------------------ input

void handleInput(SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) running = false;

    // Backtick (~) opens/closes the cheat console from anywhere.
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKQUOTE) {
        if (console.visible) console.close();
        else                 console.open();
        return;
    }

    // Console intercepts all input while open (except the backtick above).
    if (console.handleEvent(event, overmap)) return;

    // Overmap handles arrow keys and M while open.
    if (overmap.visible) {
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_m:     overmap.close(); break;
                case SDLK_UP:    overmap.moveCam( 0, -1); break;
                case SDLK_DOWN:  overmap.moveCam( 0,  1); break;
                case SDLK_LEFT:  overmap.moveCam(-1,  0); break;
                case SDLK_RIGHT: overmap.moveCam( 1,  0); break;
                default: break;
            }
        }
        return;
    }

    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_o)      ui.toggle();
        if (event.key.keysym.sym == SDLK_b)      bodyPanel.toggle();
        if (event.key.keysym.sym == SDLK_e)      examinePanel.hide();
        if (event.key.keysym.sym == SDLK_m)      overmap.open(playerSectorX, playerSectorY);
        if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        int mouseX = event.button.x / TILE_SIZE + cameraX;
        int mouseY = event.button.y / TILE_SIZE + cameraY;

        // Any click closes the examine panel.
        if (examinePanel.visible) {
            examinePanel.hide();
            return;
        }

        if (contextMenu.visible) {
            contextMenu.handleClick(event.button.x, event.button.y);
            return;
        }

        if (event.button.button == SDL_BUTTON_LEFT) {
            if (mouseX >= 0 && mouseX < MAP_WIDTH && mouseY >= 0 && mouseY < MAP_HEIGHT) {
                Enemy* enemy = getEnemyAt(mouseX, mouseY);
                if (enemy && map[mouseY][mouseX].visible) {
                    int damage = 5 + (player.strength - 10) / 2;
                    enemy->takeDamage(damage);
                    panel.addMessage("You hit " + enemy->name + " for " + std::to_string(damage) + " damage.");
                    if (!enemy->isAlive()) panel.addMessage(enemy->name + " dies.");
                    onPlayerAct();
                } else if (map[mouseY][mouseX].walkable()) {
                    currentPath = findPath(player.x, player.y, mouseX, mouseY);
                    pathIndex = 1;
                }
            }
        }

        if (event.button.button == SDL_BUTTON_RIGHT) {
            if (mouseX >= 0 && mouseX < MAP_WIDTH && mouseY >= 0 && mouseY < MAP_HEIGHT) {
                Enemy* enemy = getEnemyAt(mouseX, mouseY);
                if (enemy && map[mouseY][mouseX].visible) {
                    contextMenu.show(event.button.x, event.button.y, {
                        {"Attack", [enemy]() {
                            int damage = 5 + (player.strength - 10) / 2;
                            enemy->takeDamage(damage);
                            panel.addMessage("You hit " + enemy->name + " for " + std::to_string(damage) + " damage.");
                            if (!enemy->isAlive()) panel.addMessage(enemy->name + " dies.");
                            onPlayerAct();
                        }},
                        {"Examine", [enemy]() {}},
                        {"Flee",    []()      {}}
                    });
                } else {
                    // Non-enemy tile: show context menu if explored
                    if (map[mouseY][mouseX].explored) {
                        int mx = mouseX, my = mouseY;
                        std::vector<MenuItem> items;
                        if (map[my][mx].walkable()) {
                            items.push_back({"Move here", [mx, my]() {
                                currentPath = findPath(player.x, player.y, mx, my);
                                pathIndex = 1;
                            }});
                        }
                        items.push_back({"Examine", [mx, my]() {
                            examinePanel.show(mx, my);
                        }});
                        contextMenu.show(event.button.x, event.button.y, items);
                    } else {
                        currentPath.clear();
                        pathIndex = 0;
                        previewPath.clear();
                        lastHoverX = -1;
                        lastHoverY = -1;
                    }
                }
            }
        }
    }

    if (event.type == SDL_MOUSEMOTION) {
        hoverX = event.motion.x / TILE_SIZE + cameraX;
        hoverY = event.motion.y / TILE_SIZE + cameraY;
    }
}

// ------------------------------------------------------------------ update

// Advances the player one step along currentPath (with visual pacing).
// If the player has extra energy (speed > 100), the next call will act again
// before the world gets another tick — producing CDDA-style multi-actions.
bool updatePlayer() {
    if (pathIndex >= (int)currentPath.size()) return false;

    // Visual pacing: one rendered step per 100 ms regardless of game speed.
    Uint32 now = SDL_GetTicks();
    if (now - lastMoveTime < 100) return false;
    lastMoveTime = now;

    // World ticks until player has enough energy to act (handles speed < 100).
    while (player.energy < 100) tickWorld();

    SDL_Point next = currentPath[pathIndex];

    // Attack enemy blocking the path instead of moving.
    Enemy* blocker = getEnemyAt(next.x, next.y);
    if (blocker) {
        int damage = 5 + (player.strength - 10) / 2;
        blocker->takeDamage(damage);
        panel.addMessage("You hit " + blocker->name + " for " + std::to_string(damage) + " damage.");
        if (!blocker->isAlive()) panel.addMessage(blocker->name + " dies.");
        currentPath.clear();
        pathIndex = 0;
        onPlayerAct();
        return true;
    }

    // Tile became unwalkable (wall spawned, etc.).
    if (!map[next.y][next.x].walkable()) {
        currentPath.clear();
        pathIndex = 0;
        return false;
    }

    player.x = next.x;
    player.y = next.y;
    pathIndex++;
    onPlayerAct();

    if (pathIndex >= (int)currentPath.size()) {
        currentPath.clear();
        pathIndex = 0;
        lastHoverX = -1;
        lastHoverY = -1;
        previewPath.clear();
    }

    return true;
}

void renderDeathScreen(SDL_Renderer* renderer, TTF_Font* font) {
    // Dark overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // "YOU DIED" text — rendered twice for a shadow effect
    TTF_Font* bigFont = TTF_OpenFont("fonts/DejaVuSansMono.ttf", 48);
    if (bigFont) {
        auto renderCentered = [&](const char* text, int y, SDL_Color col) {
            SDL_Surface* s = TTF_RenderText_Solid(bigFont, text, col);
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
            int w, h;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_Rect dst = {(SCREEN_WIDTH - w) / 2, y, w, h};
            SDL_RenderCopy(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        };
        renderCentered("YOU DIED", MAP_VIEW_HEIGHT / 2 - 60, {80, 0, 0, 255});
        renderCentered("YOU DIED", MAP_VIEW_HEIGHT / 2 - 62, {200, 0, 0, 255});
        TTF_CloseFont(bigFont);
    }

    // Subtitle
    SDL_Surface* s = TTF_RenderText_Solid(font, "Press Escape to quit", {150, 150, 150, 255});
    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    int w, h;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    SDL_Rect dst = {(SCREEN_WIDTH - w) / 2, MAP_VIEW_HEIGHT / 2, w, h};
    SDL_RenderCopy(renderer, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

void updateCamera() {
    const int tilesX = SCREEN_WIDTH    / TILE_SIZE;
    const int tilesY = MAP_VIEW_HEIGHT / TILE_SIZE;
    cameraX = player.x - tilesX / 2;
    cameraY = player.y - tilesY / 2;
    if (cameraX < 0) cameraX = 0;
    if (cameraY < 0) cameraY = 0;
    if (cameraX > MAP_WIDTH  - tilesX) cameraX = MAP_WIDTH  - tilesX;
    if (cameraY > MAP_HEIGHT - tilesY) cameraY = MAP_HEIGHT - tilesY;
}

// ------------------------------------------------------------------ main

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    srand((unsigned int)time(nullptr));
    overmap.generate();
    overmap.reveal(playerSectorX, playerSectorY);
    generateSector(overmap.sectors[playerSectorY][playerSectorX].biome,
                   playerSectorX, playerSectorY);
    initEnemy();

    // Give everyone starting energy so they're ready to act immediately.
    player.energy = player.speed;
    for (Enemy& e : enemies) e.energy = e.speed;

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
    overmap.initTextures(renderer, font);
    updateVisibility();
    updateCamera();

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event))
            handleInput(event, running);

        updatePlayer();
        checkSectorTransition();
        updatePreviewPath();
        updateVisibility();
        updateCamera();

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        renderMap(renderer);
        renderPath(renderer);
        renderPlayer(renderer);
        renderEnemies(renderer);
        ui.renderStats(renderer, font);
        panel.render(renderer, font, player);
        bodyPanel.render(renderer, font, player);
        if (examinePanel.visible)
            examinePanel.render(renderer, font, map[examinePanel.tileY][examinePanel.tileX]);
        contextMenu.render(renderer, font);
        overmap.render(renderer, font, playerSectorX, playerSectorY);
        console.render(renderer, font);

        if (!player.isAlive()) {
            renderDeathScreen(renderer, font);
        }

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(playerTexture);
    SDL_DestroyTexture(texCursor);
    SDL_DestroyTexture(enemyTexture);
    for (int i = 0; i < T_COUNT; i++) SDL_DestroyTexture(terrainTex[i]);
    for (int i = 0; i < G_COUNT; i++) SDL_DestroyTexture(groundTex[i]);
    for (int i = 0; i < O_COUNT; i++) SDL_DestroyTexture(objectTex[i]);
    overmap.destroyTextures();
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
