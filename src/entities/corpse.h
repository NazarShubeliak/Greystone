#pragma once
#include "actor.h"
#include <vector>
#include <string>

static constexpr int CORPSE_DECAY_MINUTES = 5 * 24 * 60; // 5 in-game days
static constexpr int CORPSE_FRESH_MINUTES = 2 * 24 * 60; // fresh for 2 days (necromancy window)

struct Corpse {
    int         x, y;
    std::string name;
    SDL_Color   color;
    int         diedAt;     // worldTime.minutes when killed
    int         decaysAt;   // worldTime.minutes when corpse vanishes

    bool isFresh(int now) const { return now < diedAt + CORPSE_FRESH_MINUTES; }
    bool decayed(int now) const { return now >= decaysAt; }
};

inline Corpse makeCorpse(const Actor& e, int nowMinutes) {
    Corpse c;
    c.x        = e.x;
    c.y        = e.y;
    c.name     = e.name;
    // Dim to ~60% brightness so corpse is visible but clearly "dead".
    c.color    = {(Uint8)(e.color.r * 3 / 5),
                  (Uint8)(e.color.g * 3 / 5),
                  (Uint8)(e.color.b * 3 / 5), 255};
    c.diedAt   = nowMinutes;
    c.decaysAt = nowMinutes + CORPSE_DECAY_MINUTES;
    return c;
}
