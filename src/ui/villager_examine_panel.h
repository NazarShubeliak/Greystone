#pragma once
#include "npc.h"
#include "astar.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

struct VillagerExaminePanel {
    bool     visible = false;
    Villager snapshot;

    // Resolved at show()-time from the live villagers vector (snapshot is a
    // by-value copy, so it can't chase spouseId/motherId/etc. itself) —
    // empty string/vector = none.
    std::string spouseName, motherName, fatherName;
    std::vector<std::string> childrenNames;

    void show(const Villager& v, const std::string& spouse = "",
              const std::string& mother = "", const std::string& father = "",
              const std::vector<std::string>& children = {}) {
        snapshot       = v;
        spouseName     = spouse;
        motherName     = mother;
        fatherName     = father;
        childrenNames  = children;
        visible        = true;
    }
    void hide() { visible = false; }

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible) return;

        const int W   = 380;
        const int H   = 242 + extraLines() * 20;
        const int PAD = 14;
        const int LH  = 20;
        const int X   = (SCREEN_WIDTH  - W) / 2;
        const int Y   = (MAP_VIEW_HEIGHT - H) / 2;

        SDL_Color vc = snapshot.color;
        PanelStyle::frame(r, f, X, Y, W, H, nullptr, vc);

        int ty = Y + 10;

        // ── Header: symbol + name ─────────────────────────────────────────
        std::string header = "[@] " + snapshot.name;
        txt(r, f, header.c_str(), X + PAD, ty, vc);

        std::string occTag = snapshot.occupation == Occupation::NONE
                            ? "Villager" : occupationName(snapshot.occupation);
        txt(r, f, occTag.c_str(), X + W - textW(f, occTag.c_str()) - PAD, ty, {vc.r, vc.g, vc.b, 160});
        ty += LH + 2;
        hline(r, X, ty, W);
        ty += 10;

        // ── State ─────────────────────────────────────────────────────────
        const char* stateStr;
        SDL_Color   stateCol;
        switch (snapshot.state) {
            case Villager::State::SLEEP:
                stateStr = "Sleeping";     stateCol = {120,140,200,255}; break;
            case Villager::State::WALK_HOME:
                stateStr = "Going home";   stateCol = {200,180,100,255}; break;
            case Villager::State::EAT:
                stateStr = "Eating";       stateCol = {200,170,100,255}; break;
            case Villager::State::DRINK:
                stateStr = "Fetching water"; stateCol = {100,170,200,255}; break;
            default:
                stateStr = "Wandering";    stateCol = {120,200,120,255}; break;
        }
        txt(r, f, "State:", X + PAD, ty, {100,100,90,255});
        txt(r, f, stateStr,  X + PAD + 55, ty, stateCol);
        ty += LH + 4;

        // ── Bed position / age ───────────────────────────────────────────
        std::string bedStr = (snapshot.isChild ? "Child, age " : "Age ")
                           + std::to_string(snapshot.age)
                           + " — home: (" + std::to_string(snapshot.bedX)
                           + ", "         + std::to_string(snapshot.bedY) + ")";
        txt(r, f, bedStr.c_str(), X + PAD, ty, {80,80,75,255});
        ty += LH + 4;

        // ── Needs ─────────────────────────────────────────────────────────
        txt(r, f, needsStr("Hunger", snapshot.hunger).c_str(), X + PAD, ty, needsCol(snapshot.hunger));
        txt(r, f, needsStr("Thirst", snapshot.thirst).c_str(), X + PAD + 170, ty, needsCol(snapshot.thirst));
        ty += LH + 4;

        // ── Family — real relationships, not just a shared surname ────────
        if (!spouseName.empty()) {
            txt(r, f, ("Spouse: " + spouseName).c_str(), X + PAD, ty, {200,150,180,255});
            ty += LH;
        }
        if (!motherName.empty() || !fatherName.empty()) {
            std::string parents = "Parents: ";
            if (!fatherName.empty()) parents += fatherName;
            if (!fatherName.empty() && !motherName.empty()) parents += ", ";
            if (!motherName.empty()) parents += motherName;
            txt(r, f, parents.c_str(), X + PAD, ty, {150,170,200,255});
            ty += LH;
        }
        if (!childrenNames.empty()) {
            std::string kids = "Children: ";
            for (size_t i = 0; i < childrenNames.size(); i++) {
                if (i > 0) kids += ", ";
                kids += childrenNames[i];
            }
            txt(r, f, kids.c_str(), X + PAD, ty, {170,200,150,255});
            ty += LH;
        }

        hline(r, X, ty, W);
        ty += 10;

        // ── Flavour ───────────────────────────────────────────────────────
        txt(r, f, occupationFlavor(snapshot.occupation), X + PAD, ty, {150,145,125,255});
        ty += LH;

        if (snapshot.state == Villager::State::SLEEP)
            txt(r, f, "They are sound asleep.", X + PAD, ty, {90,100,140,255});
        else
            txt(r, f, "They go about their daily business.", X + PAD, ty, {100,120,90,255});

        // ── Footer ────────────────────────────────────────────────────────
        hline(r, X, Y + H - 24, W);
        txt(r, f, "Click anywhere to close", X + PAD, Y + H - 18, {55,55,50,255});
    }

    bool handleClick() {
        if (!visible) return false;
        visible = false;
        return true;
    }

private:
    // Panel height grows with however many family lines this villager has.
    int extraLines() const {
        int n = 0;
        if (!spouseName.empty()) n++;
        if (!motherName.empty() || !fatherName.empty()) n++;
        if (!childrenNames.empty()) n++;
        return n;
    }

    static void txt(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderUTF8_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }

    static int textW(TTF_Font* f, const char* text) {
        int w = 0, h = 0; TTF_SizeText(f, text, &w, &h); return w;
    }

    static void hline(SDL_Renderer* r, int x, int y, int w) {
        SDL_SetRenderDrawColor(r, 50,48,38,255);
        SDL_RenderDrawLine(r, x + 8, y, x + w - 8, y);
    }

    // Same tiered labeling as the player's needs bar (body_panel.h).
    static std::string needsStr(const char* label, float v) {
        const char* state =
            v < 0.10f ? "Fine" : v < 0.30f ? "Peckish" :
            v < 0.55f ? "Hungry" : v < 0.80f ? "Very hungry" : "Starving!";
        return std::string(label) + ": " + state;
    }

    static SDL_Color needsCol(float v) {
        return v < 0.30f ? SDL_Color{90, 175, 65, 255}
             : v < 0.55f ? SDL_Color{210,185,60, 255}
             : v < 0.80f ? SDL_Color{225,130,45, 255}
                         : SDL_Color{220, 50,50, 255};
    }
};
