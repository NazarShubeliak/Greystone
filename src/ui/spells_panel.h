#pragma once
#include "astar.h"
#include "actor.h"
#include "magic.h"
#include "panel_style.h"
#include "menu_hub.h"
#include "hotbar.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <algorithm>

// Left sidebar lists every school that has at least one spell defined
// (skillName(school)); clicking one filters the right-hand list down to just
// that school's spells, same left-list/right-content split as InventoryPanel.
// Locked spells show grayed out; an unlocked one binds to any of the 9 hotbar
// slots by clicking its numbered mini-box — click the slot it's already in
// to unbind. Pure UI, mirrors TechniquesPanel otherwise: the actual "cast a
// spell" logic lives in main.cpp (useSpellAtTile()), this panel only edits
// Hotbar::slots (and its own selectedSchool).
struct SpellsPanel {
    bool  visible        = false;
    Skill selectedSchool = Skill::FIRE;

    void toggle() { visible = !visible; }
    void hide()   { visible = false; }

    static constexpr int SIDEBAR_W    = 130;
    static constexpr int SCHOOL_ROW_H = 30;
    static constexpr int ROW_H        = 96;
    static constexpr int BOX          = 20;
    static constexpr int BOX_GAP      = 4;
    static constexpr int CONTENT_X    = SIDEBAR_W + 20;

    int rowTop(int row) const { return MenuHub::TAB_H + 20 + row * ROW_H; }

    SDL_Rect miniBoxRect(int row, int slot) const {
        int x = CONTENT_X + slot * (BOX + BOX_GAP);
        int y = rowTop(row) + 68;
        return { x, y, BOX, BOX };
    }

    SDL_Rect schoolTabRect(int idx) const {
        return { 0, MenuHub::TAB_H + idx * SCHOOL_ROW_H, SIDEBAR_W, SCHOOL_ROW_H };
    }

    // Every school with at least one spell defined, in first-appearance order
    // within SpellId — dynamic, so a new school's first spell just makes its
    // tab show up here with no list to maintain in this file.
    static std::vector<Skill> availableSchools() {
        std::vector<Skill> out;
        for (int i = 0; i < (int)SpellId::SPELL_COUNT; i++) {
            Skill sc = spellInfo((SpellId)i).school;
            if (std::find(out.begin(), out.end(), sc) == out.end()) out.push_back(sc);
        }
        return out;
    }

    static std::vector<SpellId> spellsInSchool(Skill school) {
        std::vector<SpellId> out;
        for (int i = 0; i < (int)SpellId::SPELL_COUNT; i++)
            if (spellInfo((SpellId)i).school == school) out.push_back((SpellId)i);
        return out;
    }

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p, const Hotbar& hotbar) const {
        if (!visible) return;

        std::vector<Skill> schools = availableSchools();
        for (size_t si = 0; si < schools.size(); si++) {
            SDL_Rect rect   = schoolTabRect((int)si);
            bool     active = schools[si] == selectedSchool;

            SDL_SetRenderDrawColor(r, active ? 42 : 18, active ? 38 : 16, active ? 22 : 14, 255);
            SDL_RenderFillRect(r, &rect);
            if (active) {
                SDL_SetRenderDrawColor(r, PanelStyle::ACCENT.r, PanelStyle::ACCENT.g, PanelStyle::ACCENT.b, 255);
                SDL_Rect stripe = { rect.x, rect.y, 3, rect.h };
                SDL_RenderFillRect(r, &stripe);
            }
            txt(r, f, skillName(schools[si]), rect.x + 12, rect.y + 6,
                active ? PanelStyle::TITLE : SDL_Color{150, 145, 130, 255});
        }

        SDL_SetRenderDrawColor(r, PanelStyle::DIVIDER.r, PanelStyle::DIVIDER.g, PanelStyle::DIVIDER.b, 255);
        SDL_RenderDrawLine(r, SIDEBAR_W, MenuHub::TAB_H, SIDEBAR_W, SCREEN_HEIGHT);

        std::vector<SpellId> ids = spellsInSchool(selectedSchool);
        for (size_t row = 0; row < ids.size(); row++) {
            SpellId      id        = ids[row];
            const Spell& s         = spellInfo(id);
            bool         unlocked  = spellUnlocked(p, id);
            bool         usable    = spellUsable(p, id);
            int          y         = rowTop((int)row);

            SDL_Color nameCol = unlocked ? PanelStyle::TITLE : SDL_Color{110, 108, 100, 255};
            txt(r, f, s.name, CONTENT_X, y, nameCol);

            std::string req = std::string("  Requires ") + skillName(s.school) + " "
                             + std::to_string(s.minLevel)
                             + " (currently " + std::to_string(p.skill(s.school).level) + ")";
            SDL_Color reqCol = unlocked ? SDL_Color{110, 190, 110, 255} : SDL_Color{190, 110, 90, 255};
            txt(r, f, req.c_str(), CONTENT_X + textW(f, s.name), y, reqCol);

            txt(r, f, s.description, CONTENT_X, y + 18, {170, 165, 155, 255});

            int failPct = spellFailChance(p, s.school);
            std::string cost = "Stamina: " + std::to_string((int)s.staminaCost)
                              + ((s.manualArea || s.manualBuild) ? "/tile" : "")
                              + "   Extra time: +" + std::to_string(s.extraEnergy)
                              + (s.selfCast ? "   Self-cast, no target needed"
                                            : "   Range: " + std::to_string(s.range))
                              + "   Backfire risk: " + std::to_string(failPct) + "%"
                              + (s.manualArea ? "   Burns for " + std::to_string(s.hazardTurns) + " turns" : "")
                              + (unlocked && !usable ? "   (not enough stamina right now)" : "");
            txt(r, f, cost.c_str(), CONTENT_X, y + 36, {130, 150, 190, 255});

            txt(r, f, "Bind to slot:", CONTENT_X, y + 54, {130, 125, 115, 255});
            for (int sIdx = 0; sIdx < Hotbar::SLOT_COUNT; sIdx++) {
                SDL_Rect box = miniBoxRect((int)row, sIdx);
                bool bound = hotbar.slots[sIdx] == Hotbar::SPELL_SLOT_OFFSET + (int)id;

                SDL_SetRenderDrawColor(r, bound ? PanelStyle::ACCENT.r : 30,
                                       bound ? PanelStyle::ACCENT.g : 28,
                                       bound ? PanelStyle::ACCENT.b : 24, 255);
                SDL_RenderFillRect(r, &box);
                SDL_SetRenderDrawColor(r, unlocked ? 150 : 70, unlocked ? 145 : 68,
                                       unlocked ? 130 : 60, 255);
                SDL_RenderDrawRect(r, &box);

                std::string label = std::to_string((sIdx + 1) % 10);
                txt(r, f, label.c_str(), box.x + 6, box.y + 2,
                    unlocked ? SDL_Color{220, 210, 190, 255} : SDL_Color{90, 88, 80, 255});
            }
        }
    }

    bool handleClick(int mx, int my, const Player& p, Hotbar& hotbar) {
        if (!visible) return false;

        std::vector<Skill> schools = availableSchools();
        for (size_t si = 0; si < schools.size(); si++) {
            SDL_Rect rect = schoolTabRect((int)si);
            if (mx >= rect.x && mx < rect.x + rect.w && my >= rect.y && my < rect.y + rect.h) {
                selectedSchool = schools[si];
                return true;
            }
        }

        std::vector<SpellId> ids = spellsInSchool(selectedSchool);
        for (size_t row = 0; row < ids.size(); row++) {
            SpellId id = ids[row];
            if (!spellUnlocked(p, id)) continue;
            for (int sIdx = 0; sIdx < Hotbar::SLOT_COUNT; sIdx++) {
                SDL_Rect box = miniBoxRect((int)row, sIdx);
                if (mx >= box.x && mx < box.x + box.w && my >= box.y && my < box.y + box.h) {
                    hotbar.assign(sIdx, id);
                    return true;
                }
            }
        }
        return true; // swallow anyway — this tab has no other clickable content
    }

private:
    static void txt(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderUTF8_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }

    static int textW(TTF_Font* f, const char* text) {
        int w = 0, h = 0; TTF_SizeText(f, text, &w, &h); return w;
    }
};
