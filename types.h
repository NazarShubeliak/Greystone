#pragma once
#include <SDL2/SDL.h>

extern SDL_Color white;
extern SDL_Color green;
extern SDL_Color orange;
extern SDL_Color yellow;
extern SDL_Color red;

struct Player {
    int x;
    int y;
    const char* symbol;
    SDL_Color color;
    int speed;
    int energy;
};

struct Enemy {
    int x;
    int y;
    const char* symbol;
    SDL_Color color;
    int speed;
    int energy;
    bool alive;
};
