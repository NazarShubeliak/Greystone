#pragma once
#include "actor.h"
#include "astar.h"
#include "panel_style.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

struct EnemyExaminePanel {
    bool  visible = false;
    Enemy snapshot = Enemy(0, 0, "?", {255,255,255,255}, 100, Race::HUMAN, 10);

    void show(const Enemy& e) { snapshot = e; visible = true; }
    void hide()               { visible = false; }

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible) return;

        const int W   = 440;
        const int PAD = 14;
        const int LH  = 20;

        // Count carried items for height
        int extraLines = snapshot.bag ? (int)snapshot.bag->contents.size() : 0;
        const int H = 290 + extraLines * LH;
        const int X = (SCREEN_WIDTH  - W) / 2;
        const int Y = (MAP_VIEW_HEIGHT - H) / 2;

        SDL_Color ec = snapshot.color; // enemy color
        PanelStyle::frame(r, f, X, Y, W, H, nullptr, ec);

        int ty = Y + 10;

        // ── Header: symbol + name ─────────────────────────────────────────
        std::string header = std::string("[") + snapshot.symbol + "] " + snapshot.name;
        txt(r, f, header.c_str(), X + PAD, ty, ec);

        static const char* raceNames[] = {
            "Human","Elf","Dwarf","Orc","Halfling","Vampire","Skeleton","Ghost"
        };
        const char* raceName = raceNames[(int)snapshot.race];
        int rnw = textW(f, raceName);
        txt(r, f, raceName, X + W - rnw - PAD, ty, {ec.r, ec.g, ec.b, 180});

        ty += LH + 2;
        hline(r, X, ty, W);
        ty += 10;

        // ── Overall HP bar ────────────────────────────────────────────────
        txt(r, f, "HP:", X + PAD, ty, {100,100,90,255});
        std::string hpNum = std::to_string(snapshot.hp) + " / " + std::to_string(snapshot.maxHp);
        txt(r, f, hpNum.c_str(), X + PAD + 30, ty, {210,210,200,255});

        // Condition label
        float totalRatio = snapshot.maxHp > 0 ? (float)snapshot.hp / snapshot.maxHp : 0;
        const char* cond;
        SDL_Color   condCol;
        if      (totalRatio > 0.75f) { cond = "Healthy";       condCol = {100,210, 80,255}; }
        else if (totalRatio > 0.50f) { cond = "Wounded";       condCol = {220,180, 60,255}; }
        else if (totalRatio > 0.25f) { cond = "Badly wounded"; condCol = {230,120, 40,255}; }
        else                         { cond = "Near death";    condCol = {220, 60, 60,255}; }
        int cw = textW(f, cond);
        txt(r, f, cond, X + W - cw - PAD, ty, condCol);

        ty += LH + 2;
        // Full-width HP bar
        const int BW = W - PAD*2;
        const int BH = 10;
        SDL_Rect barBg   = {X + PAD, ty, BW, BH};
        SDL_Rect barFill = {X + PAD, ty, (int)(BW * totalRatio), BH};
        SDL_Color fc = totalRatio > 0.5f ? SDL_Color{0,200,0,255} :
                       totalRatio > 0.25f ? SDL_Color{255,165,0,255} :
                                            SDL_Color{220,0,0,255};
        SDL_SetRenderDrawColor(r, 40,0,0,255);
        SDL_RenderFillRect(r, &barBg);
        SDL_SetRenderDrawColor(r, fc.r, fc.g, fc.b, 255);
        SDL_RenderFillRect(r, &barFill);
        SDL_SetRenderDrawColor(r, 90,90,90,255);
        SDL_RenderDrawRect(r, &barBg);

        ty += BH + 10;
        hline(r, X, ty, W);
        ty += 8;

        // ── Body parts grid (2 columns) ───────────────────────────────────
        struct PartRow { const char* label; const BodyPart& part; };
        PartRow rows[] = {
            {"Head  ", snapshot.body.head },
            {"Torso ", snapshot.body.torso},
            {"Arm L ", snapshot.body.armL },
            {"Arm R ", snapshot.body.armR },
            {"Leg L ", snapshot.body.legL },
            {"Leg R ", snapshot.body.legR },
        };

        const int COL  = W / 2 - PAD;
        const int MBW  = COL - 60; // mini bar width
        const int MBH  = 8;

        for (int i = 0; i < 6; i++) {
            int cx = (i % 2 == 0) ? X + PAD : X + W/2 + 4;
            int cy = ty + (i / 2) * (LH + 2);

            const BodyPart& bp = rows[i].part;
            SDL_Color lc = {100,100,90,255};
            txt(r, f, rows[i].label, cx, cy, lc);

            int labelOff = textW(f, rows[i].label) + 4;
            float pr = bp.severed ? 0.0f : bp.ratio();
            SDL_Color pfc = pr > 0.5f ? SDL_Color{0,180,0,255} :
                            pr > 0.2f ? SDL_Color{220,140,0,255} :
                                        SDL_Color{200,40,40,255};
            if (bp.severed) pfc = {80,80,80,255};

            SDL_Rect mbBg   = {cx + labelOff, cy + (LH - MBH)/2, MBW, MBH};
            SDL_Rect mbFill = {cx + labelOff, cy + (LH - MBH)/2, (int)(MBW * pr), MBH};
            SDL_SetRenderDrawColor(r, 35,0,0,255);
            SDL_RenderFillRect(r, &mbBg);
            SDL_SetRenderDrawColor(r, pfc.r, pfc.g, pfc.b, 255);
            SDL_RenderFillRect(r, &mbFill);
            SDL_SetRenderDrawColor(r, 70,70,70,255);
            SDL_RenderDrawRect(r, &mbBg);

            // Severed / broken label
            if (bp.severed) {
                txt(r, f, "SEVERED", cx + labelOff + MBW + 4, cy, {180,60,60,255});
            } else if (bp.broken) {
                txt(r, f, "broken", cx + labelOff + MBW + 4, cy, {200,140,60,255});
            } else if (bp.bleed > 0.5f) {
                txt(r, f, "bleeding", cx + labelOff + MBW + 4, cy, {200,60,60,255});
            }
        }

        ty += 3 * (LH + 2) + 6;
        hline(r, X, ty, W);
        ty += 8;

        // ── Stats ─────────────────────────────────────────────────────────
        SDL_Color sl = {100,100,90,255};
        SDL_Color sv = {210,210,200,255};
        stat(r, f, "STR:", std::to_string(snapshot.strength),  X + PAD,          ty, sl, sv);
        stat(r, f, "DEX:", std::to_string(snapshot.dexterity), X + PAD + 90,      ty, sl, sv);
        stat(r, f, "Speed:", std::to_string(snapshot.baseSpeed),X + PAD + 180,    ty, sl, sv);
        stat(r, f, "Aggro:", std::to_string(snapshot.aggroRange),X + PAD + 290,   ty, sl, sv);
        ty += LH;

        if (snapshot.flees) {
            txt(r, f, "Will flee when badly wounded.", X + PAD, ty, {200,160,60,255});
            ty += LH;
        }

        // ── Carried items ─────────────────────────────────────────────────
        if (snapshot.bag && !snapshot.bag->contents.empty()) {
            hline(r, X, ty, W);
            ty += 8;
            txt(r, f, "Carries:", X + PAD, ty, {100,100,90,255});
            ty += LH;
            for (const Item& item : snapshot.bag->contents) {
                std::string itemLine = "  " + item.name;
                if (item.type == ItemType::WEAPON) {
                    itemLine += "  (dmg " + std::to_string(item.damage) + ", "
                              + skillName(item.weaponSkill) + " "
                              + std::to_string(snapshot.skills[(int)item.weaponSkill].level) + ")";
                }
                else if (item.type == ItemType::ARMOR)
                    itemLine += "  (def " + std::to_string(item.defense) + ")";
                txt(r, f, itemLine.c_str(), X + PAD, ty, {160,155,135,255});
                ty += LH;
            }
        }

        // ── Footer ────────────────────────────────────────────────────────
        hline(r, X, Y + H - 24, W);
        txt(r, f, "Click anywhere to close", X + PAD, Y + H - 18, {55,55,50,255});
    }

private:
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

    static void stat(SDL_Renderer* r, TTF_Font* f,
                     const char* label, const std::string& val,
                     int x, int y, SDL_Color lc, SDL_Color vc) {
        txt(r, f, label, x, y, lc);
        txt(r, f, val.c_str(), x + textW(f, label) + 4, y, vc);
    }

    static void hline(SDL_Renderer* r, int x, int y, int w) {
        SDL_SetRenderDrawColor(r, 50,48,38,255);
        SDL_RenderDrawLine(r, x + 8, y, x + w - 8, y);
    }
};
