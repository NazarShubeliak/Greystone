#pragma once
#include "astar.h"
#include "actor.h"
#include <SDL2/SDL_ttf.h>

void initTextures(SDL_Renderer* renderer, TTF_Font* font);
void renderMap(SDL_Renderer* renderer);
void renderPlayer(SDL_Renderer* renderer);
void renderEnemies(SDL_Renderer* renderer);
void updatePreviewPath();
void renderPath(SDL_Renderer* renderer);
