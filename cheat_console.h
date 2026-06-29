#pragma once
#include "overmap.h"
#include "astar.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <string>

struct CheatConsole {
    bool        visible = false;
    std::string input;
    std::string result;
    bool        resultOk = true;   // true = green, false = red

    // Pending teleport — set by execute(), consumed by main.cpp.
    bool pendingTeleport = false;
    int  tpX = 0, tpY = 0;

    void open() {
        visible = true;
        input.clear();
        result.clear();
        SDL_StartTextInput();
    }

    void close() {
        visible = false;
        SDL_StopTextInput();
    }

    // Returns true if the event was consumed.
    bool handleEvent(SDL_Event& e, Overmap& overmap) {
        if (!visible) return false;

        if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE:
                    close();
                    return true;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    execute(overmap);
                    return true;
                case SDLK_BACKSPACE:
                    if (!input.empty()) input.pop_back();
                    result.clear();
                    return true;
                default:
                    return true;  // consume all other keys
            }
        }

        if (e.type == SDL_TEXTINPUT) {
            input += e.text.text;
            result.clear();
            return true;
        }

        return false;
    }

    void execute(Overmap& overmap) {
        std::string cmd = input;
        input.clear();

        if (cmd.empty()) return;

        // ── Commands ─────────────────────────────────────────────────────
        if (cmd == "reveal_map" || cmd == "rm") {
            for (int y = 0; y < OVERMAP_H; y++)
                for (int x = 0; x < OVERMAP_W; x++)
                    overmap.sectors[y][x].explored = true;
            result  = "All sectors revealed.";
            resultOk = true;

        } else if (cmd == "hide_map" || cmd == "hm") {
            for (int y = 0; y < OVERMAP_H; y++)
                for (int x = 0; x < OVERMAP_W; x++)
                    overmap.sectors[y][x].explored = false;
            result  = "Map hidden.";
            resultOk = true;

        } else if (cmd == "help") {
            result  = "Commands: reveal_map (rm)  hide_map (hm)  tp X Y  help";
            resultOk = true;

        } else {
            // tp X Y — teleport to overmap sector
            int x, y;
            if (sscanf(cmd.c_str(), "tp %d %d", &x, &y) == 2) {
                if (x >= 0 && x < OVERMAP_W && y >= 0 && y < OVERMAP_H) {
                    pendingTeleport = true;
                    tpX = x; tpY = y;
                    result   = "Teleporting to [" + std::to_string(x) + ", " + std::to_string(y) + "]...";
                    resultOk = true;
                } else {
                    result   = "Out of range. Valid: 0-" + std::to_string(OVERMAP_W-1) + " / 0-" + std::to_string(OVERMAP_H-1);
                    resultOk = false;
                }
                return;
            }
            result  = "Unknown: " + cmd;
            resultOk = false;
        }
    }

    void render(SDL_Renderer* r, TTF_Font* f) {
        if (!visible) return;

        const int H  = 28;
        const int Y  = MAP_VIEW_HEIGHT - H;

        // Background strip
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 8, 10, 14, 225);
        SDL_Rect bg = {0, Y, SCREEN_WIDTH, H};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // Top border line
        SDL_SetRenderDrawColor(r, 100, 90, 45, 255);
        SDL_RenderDrawLine(r, 0, Y, SCREEN_WIDTH, Y);

        // Prompt + typed text + blinking cursor
        std::string display = "> " + input + "_";
        SDL_Surface* s = TTF_RenderText_Solid(f, display.c_str(), {210, 195, 105, 255});
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            int w, h;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_Rect td = {8, Y + (H - h) / 2, w, h};
            SDL_RenderCopy(r, t, nullptr, &td);
            SDL_DestroyTexture(t);
        }

        // Hint (right side)
        SDL_Surface* hs = TTF_RenderText_Solid(f, "Enter: run   Esc: close   type 'help'", {55, 55, 50, 255});
        if (hs) {
            SDL_Texture* ht = SDL_CreateTextureFromSurface(r, hs);
            SDL_FreeSurface(hs);
            int hw, hh;
            SDL_QueryTexture(ht, nullptr, nullptr, &hw, &hh);
            SDL_Rect hd = {SCREEN_WIDTH - hw - 8, Y + (H - hh) / 2, hw, hh};
            SDL_RenderCopy(r, ht, nullptr, &hd);
            SDL_DestroyTexture(ht);
        }

        // Result line (above the input bar)
        if (!result.empty()) {
            SDL_Color rc = resultOk ? SDL_Color{90, 200, 90, 255} : SDL_Color{220, 80, 80, 255};
            SDL_Surface* rs = TTF_RenderText_Solid(f, result.c_str(), rc);
            if (rs) {
                SDL_Texture* rt = SDL_CreateTextureFromSurface(r, rs);
                SDL_FreeSurface(rs);
                int rw, rh;
                SDL_QueryTexture(rt, nullptr, nullptr, &rw, &rh);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
                SDL_Rect rbg = {0, Y - rh - 6, SCREEN_WIDTH, rh + 6};
                SDL_RenderFillRect(r, &rbg);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                SDL_Rect rd = {8, Y - rh - 3, rw, rh};
                SDL_RenderCopy(r, rt, nullptr, &rd);
                SDL_DestroyTexture(rt);
            }
        }
    }
};
