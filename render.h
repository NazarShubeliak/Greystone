#pragma once
#include "terrain.h"
#include "astar.h"
#include "actor.h"
#include <SDL2/SDL_ttf.h>

extern SDL_Texture* terrainTex[T_COUNT];
extern SDL_Texture* groundTex [G_COUNT];
extern SDL_Texture* objectTex [O_COUNT];
extern SDL_Texture* sproutTex;

void initTextures(SDL_Renderer* renderer, TTF_Font* font);
void renderMap(SDL_Renderer* renderer);
void renderPlayer(SDL_Renderer* renderer);
void renderEnemies(SDL_Renderer* renderer, TTF_Font* font);
void renderVillagers(SDL_Renderer* renderer, TTF_Font* font);
void updatePreviewPath();
void renderPath(SDL_Renderer* renderer);
