#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

struct Actor {
    int x, y;
    const char* symbol;
    SDL_Color color;

    int speed;
    int energy;

    int hp;
    int maxHp;

    int strength;       
    int dexterity;      
    int intelligence;    
    int constitution;    
    int perception;      
    int charisma;        

    bool alive;

    Actor(int x, int y, const char* symbol, SDL_Color color,
      int speed, int hp,
      int strength = 10, int dexterity = 10, int intelligence = 10,
      int constitution = 10, int perception = 10, int charisma = 10)
    : x(x), y(y), symbol(symbol), color(color),
      speed(speed), energy(0),
      hp(hp), maxHp(hp), alive(true),
      strength(strength), dexterity(dexterity), intelligence(intelligence),
      constitution(constitution), perception(perception), charisma(charisma) {}

    void takeDamage(int amount) {
        hp -= amount;
        if (hp <= 0) {
            hp = 0;
            alive = false;
        }
    }

    bool isAlive() const { return alive; }

    void gainEnergy() { energy += speed; }

    bool canAct() const { return energy >= 100; }

    void spendEnergy() { energy -= 100; }
};

struct Player : Actor {
    Player(int x, int y,
           int str = 10, int dex = 10, int intel = 10,
           int con = 10, int per = 10, int cha = 10)
        : Actor(x, y, "@", {255, 255, 255, 255}, 
                100 + (dex - 10) * 2,
                50 + con * 5,
                str, dex, intel, con, per, cha) {}
};

struct Enemy : Actor {
    int aggroRange;

    Enemy(int x, int y, const char* symbol, SDL_Color color,
          int speed, int hp, int aggroRange)
        : Actor(x, y, symbol, color, speed, hp),
          aggroRange(aggroRange) {}
};

struct Animal : Actor {
    bool hostile;
    int fleeRange;

    Animal(int x, int y, const char* symbol, SDL_Color color,
           int speed, int hp, bool hostile, int fleeRange)
        : Actor(x, y, symbol, color, speed, hp),
          hostile(hostile), fleeRange(fleeRange) {}
};