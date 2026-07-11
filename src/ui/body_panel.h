#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include "actor.h"
#include "astar.h"

struct BodyPanel {
    bool visible = false;
    void toggle() { visible = !visible; }

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p) {
        if (!visible) return;

        const int W  = 380, H = 390;
        const int X  = (SCREEN_WIDTH  - W) / 2;
        const int Y  = (MAP_VIEW_HEIGHT - H) / 2;
        const int LH = 18;

        // Background + border
        SDL_Rect bg = {X, Y, W, H};
        SDL_SetRenderDrawColor(r, 12, 12, 12, 248);
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawColor(r, 110, 110, 110, 255);
        SDL_RenderDrawRect(r, &bg);

        // Title
        txt(r, f, "BODY CONDITION", X + 115, Y + 7, {255, 215, 0, 255});
        hline(r, X, Y + 26, W);

        // ---- Left: ASCII body art (decorative, gray) ----
        const int AX = X + 55;   // art left edge
        int ay = Y + 34;
        const SDL_Color art = {70, 70, 70, 255};

        txt(r, f, "    ___    ", AX, ay, art); ay += LH;
        txt(r, f, "   (o o)   ", AX, ay, art); ay += LH;
        txt(r, f, "    \\_/    ", AX, ay, art); ay += LH;
        txt(r, f, "     |     ", AX, ay, art); ay += LH;
        txt(r, f, "  .-----.  ", AX, ay, art); ay += LH;
        txt(r, f, "  |     |  ", AX, ay, art); ay += LH;
        txt(r, f, "  .-----.  ", AX, ay, art); ay += LH;
        txt(r, f, " /|     |\\ ", AX, ay, art); ay += LH;
        txt(r, f, "( |     | )", AX, ay, art); ay += LH;
        txt(r, f, " \\|     |/ ", AX, ay, art); ay += LH;
        txt(r, f, "   |   |   ", AX, ay, art); ay += LH;
        txt(r, f, "  _|   |_  ", AX, ay, art); ay += LH;
        txt(r, f, " |_|   |_| ", AX, ay, art); ay += LH;

        // ---- Right: status rows ----
        const int SX = X + 210;  // status column X
        int sy = Y + 34;

        // Divider between art and status
        SDL_SetRenderDrawColor(r, 50, 50, 50, 255);
        SDL_RenderDrawLine(r, X + 195, Y + 27, X + 195, Y + H - 30);

        // HEAD  (rows 1-2 of art)
        sy += LH * 1;   // align with "(o o)" line
        partRow(r, f, "HEAD ", p.body.head, p.medicineSkill, SX, sy); sy += LH;

        // TORSO (rows 4-6)
        sy += LH * 3;
        partRow(r, f, "TORSO", p.body.torso, p.medicineSkill, SX, sy); sy += LH;

        // ARM_L / ARM_R (rows 7-9)
        sy += LH * 1;
        partRow(r, f, "ARM L", p.body.armL, p.medicineSkill, SX, sy); sy += LH;
        partRow(r, f, "ARM R", p.body.armR, p.medicineSkill, SX, sy); sy += LH;

        // LEG_L / LEG_R (rows 11-13)
        sy += LH * 1;
        partRow(r, f, "LEG L", p.body.legL, p.medicineSkill, SX, sy); sy += LH;
        partRow(r, f, "LEG R", p.body.legR, p.medicineSkill, SX, sy);

        // ---- Needs section ----
        hline(r, X, Y + H - 100, W);
        int ny = Y + H - 94;
        txt(r, f, "NEEDS", X + 163, ny, {200, 200, 200, 255});
        ny += LH + 2;

        needsBar(r, f, "Hunger", p.hunger,
                 {210, 80,  30, 255},  // full→empty: orange-red
                 {70,  150, 50, 255},  // full color (low hunger = good = green)
                 X + 20, ny, W - 40);
        ny += 22;
        needsBar(r, f, "Thirst", p.thirst,
                 {200, 60,  60, 255},  // full→empty: red
                 {60,  120, 220, 255}, // full color (low thirst = good = blue)
                 X + 20, ny, W - 40);
        ny += 22;

        // Footer: medicine skill notice
        hline(r, X, Y + H - 28, W);
        std::string footer = p.medicineSkill == 0
            ? "No medical training - estimates only"
            : "Medicine " + std::to_string(p.medicineSkill) + " - assessment accurate";
        txt(r, f, footer.c_str(), X + 60, Y + H - 22, {80, 80, 80, 255});
    }

private:
    // Color per limb state
    SDL_Color stateColor(const BodyPart& bp) const {
        if (bp.severed) return {90, 90, 90, 255};
        switch (bp.state()) {
            case LimbState::NORMAL:    return {0,   200,   0, 255};
            case LimbState::WOUNDED:   return {255, 220,   0, 255};
            case LimbState::SEVERE:    return {255, 130,   0, 255};
            case LimbState::DESTROYED: return {220,   0,   0, 255};
            default:                   return {255, 255, 255, 255};
        }
    }

    // Description based on medicine skill
    std::string stateText(const BodyPart& bp, int medicine) const {
        if (bp.severed)
            return medicine > 0 ? "SEVERED" : "Missing!";

        if (medicine == 0) {
            // Untrained: vague, subjective
            switch (bp.state()) {
                case LimbState::NORMAL:    return "Fine";
                case LimbState::WOUNDED:   return "Hurts";
                case LimbState::SEVERE:    return "Really hurts";
                case LimbState::DESTROYED: return "Agony";
                default: return "?";
            }
        }
        if (medicine < 4) {
            // Basic training: clinical terms
            std::string s;
            switch (bp.state()) {
                case LimbState::NORMAL:    s = "Healthy"; break;
                case LimbState::WOUNDED:   s = bp.broken ? "Wounded, broken" : "Wounded"; break;
                case LimbState::SEVERE:    s = "Severe wound"; break;
                case LimbState::DESTROYED: s = "Destroyed"; break;
                default: s = "?";
            }
            if (bp.bleed > 0.0f) s += ", bleeding";
            return s;
        }
        // medicine >= 4: precise numbers
        std::string s = std::to_string(bp.hp) + "/" + std::to_string(bp.maxHp);
        if (bp.broken)    s += " [broken]";
        if (bp.bleed > 0) s += " [bleed:" + std::to_string((int)bp.bleed) + "]";
        return s;
    }

    void partRow(SDL_Renderer* r, TTF_Font* f, const char* label,
                 const BodyPart& bp, int medicine, int x, int y) {
        // Label in gray
        std::string full = std::string(label) + " : ";
        txt(r, f, full.c_str(), x, y, {130, 130, 130, 255});
        // Status in state color
        int labelW = (int)full.size() * 9;
        txt(r, f, stateText(bp, medicine).c_str(), x + labelW, y, stateColor(bp));
    }

    void txt(SDL_Renderer* r, TTF_Font* f, const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderText_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }

    // Draws a labeled horizontal bar for hunger/thirst.
    // value: 0=satisfied → 1=critical
    // critCol: color at maximum (dangerous), goodCol: color at zero (fine)
    void needsBar(SDL_Renderer* r, TTF_Font* f,
                  const char* label, float value,
                  SDL_Color critCol, SDL_Color goodCol,
                  int x, int y, int barW) {
        const int BAR_H  = 12;
        const int LABEL_W = 58;

        // Label
        txt(r, f, label, x, y, {120, 120, 115, 255});

        int bx = x + LABEL_W;
        int bw = barW - LABEL_W;

        // Background
        SDL_SetRenderDrawColor(r, 35, 35, 35, 255);
        SDL_Rect bg = {bx, y + 1, bw, BAR_H};
        SDL_RenderFillRect(r, &bg);

        // Fill — lerp from goodCol (value=0) to critCol (value=1)
        float v = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        SDL_Color fc = {
            (Uint8)(goodCol.r + (int)((critCol.r - goodCol.r) * v)),
            (Uint8)(goodCol.g + (int)((critCol.g - goodCol.g) * v)),
            (Uint8)(goodCol.b + (int)((critCol.b - goodCol.b) * v)),
            255
        };
        int fillW = (int)(bw * v);
        if (fillW > 0) {
            SDL_Rect fill = {bx, y + 1, fillW, BAR_H};
            SDL_SetRenderDrawColor(r, fc.r, fc.g, fc.b, 255);
            SDL_RenderFillRect(r, &fill);
        }

        // Border
        SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
        SDL_RenderDrawRect(r, &bg);

        // Percentage text inside bar
        std::string pct = std::to_string((int)(v * 100)) + "%";
        SDL_Surface* ps = TTF_RenderText_Solid(f, pct.c_str(), {220, 220, 215, 200});
        if (ps) {
            SDL_Texture* pt = SDL_CreateTextureFromSurface(r, ps);
            SDL_FreeSurface(ps);
            int pw, ph;
            SDL_QueryTexture(pt, nullptr, nullptr, &pw, &ph);
            SDL_Rect pdst = {bx + bw - pw - 4, y + (BAR_H - ph) / 2 + 1, pw, ph};
            SDL_RenderCopy(r, pt, nullptr, &pdst);
            SDL_DestroyTexture(pt);
        }

        // State label
        const char* stateStr =
            v < 0.10f ? "Fine" :
            v < 0.30f ? "Peckish" :
            v < 0.55f ? "Hungry" :
            v < 0.80f ? "Very hungry" : "Starving!";
        SDL_Color sc = v < 0.30f ? SDL_Color{90,175,65,255}
                     : v < 0.55f ? SDL_Color{210,185,60,255}
                     : v < 0.80f ? SDL_Color{225,130,45,255}
                                 : SDL_Color{220, 50, 50,255};
        txt(r, f, stateStr, bx + 4, y + (BAR_H - 14) / 2 + 1, sc);
    }

    void hline(SDL_Renderer* r, int x, int y, int w) {
        SDL_SetRenderDrawColor(r, 70, 70, 70, 255);
        SDL_RenderDrawLine(r, x + 8, y, x + w - 8, y);
    }
};
