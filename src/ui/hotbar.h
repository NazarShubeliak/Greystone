#pragma once
#include "astar.h"
#include "actor.h"
#include "combat.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

// Classic-RPG action bar: 9 slots, keys 1-9 or a click use whatever technique
// (later: spell too) is bound to that slot. Purely a UI/input surface — it only
// holds which TechniqueId sits in each slot and draws itself; main.cpp's
// useHotbarSlot() does the actual "find a target and attack" work, reusing
// useTechniqueOnEnemy()/useTechniqueOnVillager() (it needs the enemies/villagers
// globals, which this header has no business knowing about).
struct Hotbar {
    static constexpr int SLOT_COUNT = 9;
    static constexpr int SLOT_SIZE  = 42;
    static constexpr int SLOT_GAP   = 6;

    int slots[SLOT_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1 }; // -1 = empty, else (int)TechniqueId

    bool assign(int slot, TechniqueId id) {
        if (slot < 0 || slot >= SLOT_COUNT) return false;
        // Clicking the same technique into the slot it's already in clears it —
        // this is the panel's "toggle" affordance, no separate clear button needed.
        if (slots[slot] == (int)id) slots[slot] = -1;
        else                        slots[slot] = (int)id;
        return true;
    }

    void clear(int slot) {
        if (slot >= 0 && slot < SLOT_COUNT) slots[slot] = -1;
    }

    static int totalWidth() { return SLOT_COUNT * SLOT_SIZE + (SLOT_COUNT - 1) * SLOT_GAP; }
    static int originX()    { return (SCREEN_WIDTH - totalWidth()) / 2; }
    static int originY()    { return MAP_VIEW_HEIGHT - SLOT_SIZE - 10; }

    SDL_Rect slotRect(int i) const {
        return { originX() + i * (SLOT_SIZE + SLOT_GAP), originY(), SLOT_SIZE, SLOT_SIZE };
    }

    // Returns slot index under (mx,my) in screen pixels, or -1.
    int slotAt(int mx, int my) const {
        for (int i = 0; i < SLOT_COUNT; i++) {
            SDL_Rect r = slotRect(i);
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) return i;
        }
        return -1;
    }

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p) const {
        for (int i = 0; i < SLOT_COUNT; i++) {
            SDL_Rect rect = slotRect(i);

            bool empty = slots[i] < 0;
            TechniqueId id = empty ? TechniqueId::TECHNIQUE_COUNT : (TechniqueId)slots[i];
            bool ready = !empty && techniqueUsable(p, id);

            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, PanelStyle::BG.r, PanelStyle::BG.g, PanelStyle::BG.b, 235);
            SDL_RenderFillRect(r, &rect);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

            SDL_Color border = !empty && !ready ? SDL_Color{110, 60, 50, 255}
                              : ready            ? PanelStyle::ACCENT
                                                  : PanelStyle::dim(PanelStyle::ACCENT, 2);
            SDL_SetRenderDrawColor(r, border.r, border.g, border.b, 255);
            SDL_RenderDrawRect(r, &rect);

            // Slot number, top-left corner.
            std::string numStr = std::to_string((i + 1) % 10);
            txt(r, f, numStr.c_str(), rect.x + 3, rect.y + 1, {150, 145, 130, 255});

            if (!empty) {
                const Technique& t = techniqueInfo(id);
                std::string label = shortLabel(t.name);
                SDL_Color labelCol = ready ? SDL_Color{225, 210, 150, 255}
                                           : SDL_Color{130, 110, 95, 255};
                txt(r, f, label.c_str(), rect.x + 3, rect.y + 16, labelCol);

                std::string cost = std::to_string((int)t.staminaCost);
                txt(r, f, cost.c_str(), rect.x + 3, rect.y + 29, {110, 160, 200, 255});
            }
        }
    }

private:
    // Keeps the slot label from overflowing a 42px box — first word only.
    static std::string shortLabel(const char* name) {
        std::string s(name);
        size_t sp = s.find(' ');
        return sp == std::string::npos ? s : s.substr(0, sp);
    }

    static void txt(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderText_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
};
