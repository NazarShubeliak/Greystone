#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstring>
#include <deque>
#include <string>
#include "astar.h"
#include "actor.h"
#include "time_system.h"
#include "context_menu.h"

enum class PanelMode { LOG, DIALOGUE };

struct BottomPanel {
    PanelMode mode = PanelMode::LOG;

    // --- Log ---
    std::deque<std::string> log;
    static const int MAX_LOG = 40;

    void addMessage(const std::string& msg) {
        log.push_front(msg);
        if ((int)log.size() > MAX_LOG) log.pop_back();
    }

    // --- Dialogue --- (see startDialogue()/endDialogue() below)
    std::string npcName, npcLine;
    std::vector<MenuItem> dialogueOptions;
    int dialogueHover = -1;

    void startDialogue(const std::string& name, const std::string& line, std::vector<MenuItem> options) {
        mode            = PanelMode::DIALOGUE;
        npcName         = name;
        npcLine         = line;
        dialogueOptions = std::move(options);
        dialogueHover   = -1;
    }
    void endDialogue() {
        mode = PanelMode::LOG;
        dialogueOptions.clear();
    }

    // --- Render ---
    void render(SDL_Renderer* renderer, TTF_Font* font, const Player& player, const WorldTime& wt) {
        const int TOP = MAP_VIEW_HEIGHT;
        const int HUD_H = 48;
        const int LOG_H = PANEL_HEIGHT - HUD_H - 1;

        // Background
        SDL_Rect bg = {0, TOP, SCREEN_WIDTH, PANEL_HEIGHT};
        SDL_SetRenderDrawColor(renderer, 10, 10, 10, 255);
        SDL_RenderFillRect(renderer, &bg);

        // Top border
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderDrawLine(renderer, 0, TOP, SCREEN_WIDTH, TOP);

        // Divider above HUD
        SDL_RenderDrawLine(renderer, 0, SCREEN_HEIGHT - HUD_H, SCREEN_WIDTH, SCREEN_HEIGHT - HUD_H);

        switch (mode) {
            case PanelMode::LOG:      renderLog(renderer, font, TOP, LOG_H);      break;
            case PanelMode::DIALOGUE: renderDialogue(renderer, font, TOP, LOG_H); break;
        }

        renderHUD(renderer, font, player, wt);
    }

    // ---- dialogue input -------------------------------------------------------
    static constexpr int DLG_ROW_H = 20;

    // Row rects share this geometry with renderDialogue() so hit-testing can't drift.
    std::vector<SDL_Rect> dialogueRowRects(int top) const {
        int y = top + 8 + DLG_ROW_H + 6; // below the NPC's line
        std::vector<SDL_Rect> rects;
        for (size_t i = 0; i < dialogueOptions.size(); i++) {
            rects.push_back({8, y, SCREEN_WIDTH - 16, DLG_ROW_H});
            y += DLG_ROW_H;
        }
        return rects;
    }

    void handleMotion(int mx, int my) {
        if (mode != PanelMode::DIALOGUE) return;
        auto rects = dialogueRowRects(MAP_VIEW_HEIGHT);
        dialogueHover = -1;
        for (size_t i = 0; i < rects.size(); i++)
            if (mx >= rects[i].x && mx < rects[i].x + rects[i].w &&
                my >= rects[i].y && my < rects[i].y + rects[i].h)
                dialogueHover = (int)i;
    }

    // Returns true if the click was consumed (dialogue is modal while open).
    bool handleClick(int mx, int my) {
        if (mode != PanelMode::DIALOGUE) return false;
        auto rects = dialogueRowRects(MAP_VIEW_HEIGHT);
        for (size_t i = 0; i < rects.size(); i++) {
            if (mx >= rects[i].x && mx < rects[i].x + rects[i].w &&
                my >= rects[i].y && my < rects[i].y + rects[i].h) {
                // Copy the action out first — the callback may replace
                // dialogueOptions (e.g. "Ask about work" rebuilds the list).
                auto action = dialogueOptions[i].action;
                if (action) action();
                return true;
            }
        }
        return true; // still swallow — clicking the world shouldn't act while talking
    }

    // Returns true if consumed. ESC and 1-9 shortcuts.
    bool handleKey(SDL_Keycode key) {
        if (mode != PanelMode::DIALOGUE) return false;
        if (key == SDLK_ESCAPE) { endDialogue(); return true; }
        if (key >= SDLK_1 && key <= SDLK_9) {
            int idx = (int)(key - SDLK_1);
            if (idx < (int)dialogueOptions.size()) {
                auto action = dialogueOptions[idx].action;
                if (action) action();
            }
        }
        return true;
    }

private:
    // ---- dialogue -------------------------------------------------------------
    void renderDialogue(SDL_Renderer* r, TTF_Font* f, int top, int logH) {
        int y = top + 8;
        std::string line = npcName + ": " + npcLine;
        renderText(r, f, line.c_str(), 8, y, {210, 195, 140, 255});

        auto rects = dialogueRowRects(top);
        for (size_t i = 0; i < dialogueOptions.size(); i++) {
            bool hov = ((int)i == dialogueHover);
            if (hov) {
                SDL_SetRenderDrawColor(r, 45, 40, 25, 255);
                SDL_RenderFillRect(r, &rects[i]);
            }
            SDL_Color col = hov ? SDL_Color{230, 210, 120, 255} : SDL_Color{180, 175, 155, 255};
            std::string label = std::to_string(i + 1) + ". " + dialogueOptions[i].label;
            renderText(r, f, label.c_str(), 8, rects[i].y + 1, col);
        }
    }


    // ---- log ----------------------------------------------------------------
    void renderLog(SDL_Renderer* r, TTF_Font* f, int top, int logH) {
        const int LINE_H  = 18;
        const int PADDING = 6;
        int maxLines = (logH - PADDING) / LINE_H;
        int y = top + PADDING;

        for (int i = 0; i < (int)log.size() && i < maxLines; i++) {
            // Newest message is brightest; older messages fade.
            int brightness = 255 - i * (160 / maxLines);
            SDL_Color col = {(Uint8)brightness, (Uint8)brightness, (Uint8)brightness, 255};
            renderText(r, f, log[i].c_str(), 8, y, col);
            y += LINE_H;
        }
    }

    // ---- HUD ----------------------------------------------------------------
    void renderHUD(SDL_Renderer* r, TTF_Font* f, const Player& p, const WorldTime& wt) {
        const int Y      = SCREEN_HEIGHT - 44;
        const int Y2     = Y + 20;
        const int BAR_W  = 130;
        const int BAR_H  = 13;
        const int GAP    = 14;

        // Left side is a running cursor — each element starts after the previous
        // one's *actual* rendered width, so long values (e.g. 3-digit HP) never
        // get overdrawn by whatever comes next.
        int x = 8;

        // HP label
        std::string hpStr = "HP: " + std::to_string(p.hp) + "/" + std::to_string(p.maxHp);
        renderText(r, f, hpStr.c_str(), x, Y, {255, 80, 80, 255});
        x += textWidth(f, hpStr.c_str()) + GAP;

        // HP bar
        SDL_Rect barBg = {x, Y + 1, BAR_W, BAR_H};
        SDL_SetRenderDrawColor(r, 60, 0, 0, 255);
        SDL_RenderFillRect(r, &barBg);
        float ratio = (float)p.hp / p.maxHp;
        SDL_Color fillCol = ratio > 0.5f ? SDL_Color{0, 200, 0, 255} :
                            ratio > 0.25f ? SDL_Color{255, 165, 0, 255} :
                                            SDL_Color{220, 0, 0, 255};
        SDL_Rect barFill = {x, Y + 1, (int)(BAR_W * ratio), BAR_H};
        SDL_SetRenderDrawColor(r, fillCol.r, fillCol.g, fillCol.b, 255);
        SDL_RenderFillRect(r, &barFill);
        SDL_SetRenderDrawColor(r, 120, 120, 120, 255);
        SDL_RenderDrawRect(r, &barBg);
        x += BAR_W + GAP;

        // Speed / Energy — speed shown after hunger/thirst penalty (matches tickWorld's effSpeed)
        int effSpeed = std::max(1, p.speed - p.needsSpeedPenalty());
        SDL_Color spdCol = p.needsSpeedPenalty() > 0 ? SDL_Color{220, 140, 90, 255} : SDL_Color{180, 180, 180, 255};
        std::string spdStr = "SPD: " + std::to_string(effSpeed);
        renderText(r, f, spdStr.c_str(), x, Y, spdCol);
        x += textWidth(f, spdStr.c_str()) + GAP;

        std::string nrgStr = "NRG: " + std::to_string(p.energy);
        renderText(r, f, nrgStr.c_str(), x, Y, {80, 180, 255, 255});
        x += textWidth(f, nrgStr.c_str()) + GAP;

        // Stamina — dims when low so a spent-out bar reads at a glance, once
        // techniques/spells actually start spending it.
        float staRatio = p.maxStamina > 0 ? p.stamina / p.maxStamina : 0.0f;
        SDL_Color staCol = staRatio > 0.5f ? SDL_Color{130, 210, 140, 255} :
                            staRatio > 0.25f ? SDL_Color{200, 190, 90, 255} :
                                               SDL_Color{210, 120, 80, 255};
        std::string staStr = "STA: " + std::to_string((int)p.stamina);
        renderText(r, f, staStr.c_str(), x, Y, staCol);

        // Time + date
        int h = wt.hour();
        SDL_Color timeCol =
            (h >= 7 && h < 19)  ? SDL_Color{220, 200,  70, 255} :
            (h >= 5 && h < 7)   ? SDL_Color{210, 150,  60, 255} :
            (h >= 19 && h < 21) ? SDL_Color{200, 110,  50, 255} :
                                   SDL_Color{ 80, 110, 200, 255};
        renderText(r, f, wt.timeStr().c_str(), SCREEN_WIDTH - 340, Y, timeCol);
        renderText(r, f, wt.dateStr().c_str(), SCREEN_WIDTH - 290, Y, {150, 145, 130, 255});

        // Light indicator — only show when dark enough to matter
        float dark = wt.darkness();
        if (dark > 0.1f) {
            int light = p.totalLightRadius();
            std::string lightStr;
            SDL_Color   lightCol;
            if (light == 0) {
                lightStr = "[Dark]";
                lightCol = {180, 80, 80, 255};
            } else {
                lightStr = "[Light " + std::to_string(light) + "]";
                lightCol = {220, 200, 100, 255};
            }
            renderText(r, f, lightStr.c_str(), SCREEN_WIDTH - 90, Y, lightCol);
        }

        // Status effects row — hunger/thirst debuffs, colored by severity.
        int statusX = 8;
        int hl = p.hungerLevel();
        int tl = p.thirstLevel();
        if (hl > 0) {
            SDL_Color col = levelColor(hl);
            renderText(r, f, p.hungerLabel(), statusX, Y2, col);
            statusX += (int)strlen(p.hungerLabel()) * 9 + 14;
        }
        if (tl > 0) {
            SDL_Color col = levelColor(tl);
            renderText(r, f, p.thirstLabel(), statusX, Y2, col);
        }
    }

    // Severity 1-4 -> yellow/orange/orange-red/red, matching body_panel's palette.
    static SDL_Color levelColor(int lvl) {
        switch (lvl) {
            case 1:  return {210, 185,  60, 255};
            case 2:  return {225, 130,  45, 255};
            case 3:  return {235,  90,  40, 255};
            default: return {220,  50,  50, 255};
        }
    }

    // ---- util ---------------------------------------------------------------
    int textWidth(TTF_Font* f, const char* text) {
        int w, h;
        TTF_SizeText(f, text, &w, &h);
        return w;
    }

    void renderText(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
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
};
