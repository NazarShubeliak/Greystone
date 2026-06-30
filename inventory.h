#pragma once
#include "item.h"
#include "actor.h"
#include "context_menu.h"
#include "item_examine_panel.h"
#include "astar.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

struct InventoryPanel {
    bool visible = false;

    // Set from main.cpp after init so Use/Eat can print messages and advance time.
    std::function<void(const std::string&)> onMessage;
    std::function<void()>                   onAct;

    void toggle() { visible = !visible; }
    void close()  { visible = false; }

    // ---------------------------------------------------------------- click

    // Returns true if the click was consumed by the panel.
    bool handleClick(int mx, int my, Player& p,
                     ContextMenu& ctx, std::vector<GroundItem>& ground,
                     ItemExaminePanel* examPanel = nullptr) {
        if (!visible) return false;

        const int LEFT_W  = 340;
        const int TITLE_H = 26;
        const int ROW_H   = 22;

        // ── Left panel: equipped slot clicked ──────────────────────
        if (mx < LEFT_W) {
            int s = (my - TITLE_H) / ROW_H;
            if (s < 0 || s >= (int)EquipSlot::SLOT_COUNT) return true;
            if (!p.worn[s].has_value()) return true;

            if (p.worn[s]->isContainer()) {
                p.activeContainerSlot = s;
                return true;
            }

            // Show options for this equipped item
            std::vector<MenuItem> eqOpts;
            eqOpts.push_back({"Unequip", [&p, s, &ground]() {
                if (!p.worn[s]) return;
                Item it = std::move(*p.worn[s]);
                p.worn[s].reset();
                if (!p.addToContainer(it))
                    ground.push_back({p.x, p.y, std::move(it)});
            }});
            eqOpts.push_back({"Drop", [&p, s, &ground]() {
                if (!p.worn[s]) return;
                ground.push_back({p.x, p.y, std::move(*p.worn[s])});
                p.worn[s].reset();
            }});
            if (examPanel) {
                eqOpts.push_back({"Examine", [ep=examPanel, &p, s]() {
                    if (p.worn[s].has_value()) ep->show(*p.worn[s]);
                }});
            }
            ctx.show(mx, my, eqOpts);
            return true;
        }

        // ── Right panel: item inside active container clicked ───────
        int cs = p.activeContainerSlot;
        if (cs < 0 || cs >= (int)EquipSlot::SLOT_COUNT) return true;
        if (!p.worn[cs].has_value()) return true;

        int idx = (my - TITLE_H) / ROW_H;
        if (idx < 0 || idx >= (int)p.worn[cs]->contents.size()) return true;

        // Build context menu for this inventory item
        std::vector<MenuItem> opts;
        Item& it = p.worn[cs]->contents[idx];

        // Equip — only if item has a slot
        if (it.isEquippable()) {
            opts.push_back({"Equip", [&p, cs, idx]() {
                if (!p.worn[cs]) return;
                auto& cont = p.worn[cs]->contents;
                if (idx >= (int)cont.size()) return;
                Item item = cont[idx];
                int  s    = (int)item.slot;
                if (s < 0 || s >= (int)EquipSlot::SLOT_COUNT) return;
                cont.erase(cont.begin() + idx);
                // Swap old item back into container if slot was occupied
                if (p.worn[s].has_value()) {
                    Item old = std::move(*p.worn[s]);
                    p.worn[s].reset();
                    p.addToContainer(std::move(old));
                }
                p.worn[s] = std::move(item);
            }});
        }

        // Use — food / drink
        if (it.type == ItemType::FOOD || it.type == ItemType::DRINK) {
            opts.push_back({"Use", [this, &p, cs, idx]() {
                if (!p.worn[cs]) return;
                auto& cont = p.worn[cs]->contents;
                if (idx >= (int)cont.size()) return;
                Item item = cont[idx];
                float oldH = p.hunger, oldT = p.thirst;
                p.hunger = std::max(0.0f, p.hunger - item.nutrition / 100.0f);
                p.thirst = std::max(0.0f, p.thirst - item.hydration / 100.0f);
                cont.erase(cont.begin() + idx);
                if (onMessage) {
                    std::string verb = (item.type == ItemType::FOOD) ? "eat" : "drink";
                    std::string msg  = "You " + verb + " the " + item.name + ".";
                    if (oldH - p.hunger > 0.01f) {
                        int pct = (int)((oldH - p.hunger) * 100);
                        msg += " [hunger -" + std::to_string(pct) + "%]";
                    }
                    if (oldT - p.thirst > 0.01f) {
                        int pct = (int)((oldT - p.thirst) * 100);
                        msg += " [thirst -" + std::to_string(pct) + "%]";
                    }
                    onMessage(msg);
                }
                if (onAct) onAct();
            }});
        }

        opts.push_back({"Drop", [&p, cs, idx, &ground]() {
            if (!p.worn[cs]) return;
            auto& cont = p.worn[cs]->contents;
            if (idx >= (int)cont.size()) return;
            ground.push_back({p.x, p.y, cont[idx]});
            cont.erase(cont.begin() + idx);
        }});
        if (examPanel) {
            opts.push_back({"Examine", [ep=examPanel, &p, cs, idx]() {
                if (!p.worn[cs]) return;
                auto& cont = p.worn[cs]->contents;
                if (idx >= (int)cont.size()) return;
                ep->show(cont[idx]);
            }});
        }

        ctx.show(mx, my, opts);
        return true;
    }

    // ---------------------------------------------------------------- render

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p) {
        if (!visible) return;

        // Full-screen dark overlay
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 8, 10, 14, 242);
        SDL_Rect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        SDL_RenderFillRect(r, &full);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        const int LEFT_W  = 340;
        const int TITLE_H = 26;
        const int ROW_H   = 22;
        const int PAD     = 10;
        const int FOOT_H  = 24;

        SDL_Color gold  = {195, 165, 65, 255};
        SDL_Color white = {210, 210, 200, 255};
        SDL_Color dim   = {105, 105,  95, 255};
        SDL_Color empty = { 45,  45,  40, 255};

        // ── Title bar ────────────────────────────────────────────────
        SDL_SetRenderDrawColor(r, 18, 16, 12, 255);
        SDL_Rect titleBar = {0, 0, SCREEN_WIDTH, TITLE_H};
        SDL_RenderFillRect(r, &titleBar);
        txt(r, f, PAD, 5, "EQUIPPED", gold);
        // Right panel title shows active container
        {
            int cs = p.activeContainerSlot;
            if (cs >= 0 && cs < (int)EquipSlot::SLOT_COUNT && p.worn[cs].has_value()) {
                const Item& cont = *p.worn[cs];
                std::string hdr = cont.name
                    + "  [" + std::to_string(cont.usedVolume())
                    + " / " + std::to_string(cont.maxVolume) + " ml  |  "
                    + std::to_string(cont.usedWeight())
                    + " / " + std::to_string(cont.maxWeight) + " g]";
                txt(r, f, LEFT_W + PAD, 5, hdr.c_str(), gold);
            } else {
                txt(r, f, LEFT_W + PAD, 5, "No container selected", dim);
            }
        }

        // ── Left panel: slots ─────────────────────────────────────────
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++) {
            int ry = TITLE_H + s * ROW_H;

            // Highlight active container row
            if (s == p.activeContainerSlot && p.worn[s].has_value() && p.worn[s]->isContainer()) {
                SDL_SetRenderDrawColor(r, 30, 26, 18, 255);
                SDL_Rect hi = {0, ry, LEFT_W, ROW_H};
                SDL_RenderFillRect(r, &hi);
            }

            // Slot label
            txt(r, f, PAD, ry + 4, slotNames[s], dim);

            // Item name (or ---)
            if (p.worn[s].has_value()) {
                const Item& item = *p.worn[s];
                SDL_Color col = typeVisuals[(int)item.type].color;
                std::string label = item.name;
                if (item.isContainer()) label += " \xbb"; // »
                txt(r, f, 110, ry + 4, label.c_str(), col);
            } else {
                txt(r, f, 110, ry + 4, "---", empty);
            }

            // Row divider
            SDL_SetRenderDrawColor(r, 28, 25, 18, 255);
            SDL_RenderDrawLine(r, 0, ry + ROW_H - 1, LEFT_W - 1, ry + ROW_H - 1);
        }

        // Vertical divider
        SDL_SetRenderDrawColor(r, 65, 58, 38, 255);
        SDL_RenderDrawLine(r, LEFT_W, 0, LEFT_W, SCREEN_HEIGHT - FOOT_H);

        // ── Right panel: active container contents ────────────────────
        int cs = p.activeContainerSlot;
        if (cs >= 0 && cs < (int)EquipSlot::SLOT_COUNT && p.worn[cs].has_value()) {
            const Item& cont = *p.worn[cs];
            if (cont.contents.empty()) {
                txt(r, f, LEFT_W + PAD, TITLE_H + 8, "(empty)", dim);
            }
            for (int i = 0; i < (int)cont.contents.size(); i++) {
                const Item& item = cont.contents[i];
                int ry = TITLE_H + i * ROW_H;
                SDL_Color col = typeVisuals[(int)item.type].color;
                txt(r, f, LEFT_W + PAD, ry + 4, item.name.c_str(), col);

                // Volume + weight right-aligned
                std::string stats = std::to_string(item.volume) + "ml  "
                                  + std::to_string(item.weight) + "g";
                int sw = textW(f, stats.c_str());
                txt(r, f, SCREEN_WIDTH - sw - PAD, ry + 4, stats.c_str(), dim);

                SDL_SetRenderDrawColor(r, 28, 25, 18, 255);
                SDL_RenderDrawLine(r, LEFT_W + 1, ry + ROW_H - 1,
                                   SCREEN_WIDTH - 1, ry + ROW_H - 1);
            }
        } else {
            txt(r, f, LEFT_W + PAD, TITLE_H + 8,
                "Equip a bag or pouch to store items.", dim);
        }

        // ── Footer bar ─────────────────────────────────────────────────
        SDL_SetRenderDrawColor(r, 18, 16, 12, 255);
        SDL_Rect foot = {0, SCREEN_HEIGHT - FOOT_H, SCREEN_WIDTH, FOOT_H};
        SDL_RenderFillRect(r, &foot);
        SDL_SetRenderDrawColor(r, 55, 50, 33, 255);
        SDL_RenderDrawLine(r, 0, SCREEN_HEIGHT - FOOT_H,
                           SCREEN_WIDTH, SCREEN_HEIGHT - FOOT_H);

        std::string carry = "Carry: " + std::to_string(p.usedCarryVolume())
                          + " / " + std::to_string(p.totalCarryVolume()) + " ml"
                          + "   Defense: " + std::to_string(p.totalDefense())
                          + "   Weapon dmg: " + std::to_string(p.weaponDamage())
                          + "   |   I: close   Click slot or item for options";
        txt(r, f, PAD, SCREEN_HEIGHT - FOOT_H + 5, carry.c_str(), dim);
    }

private:
    static void txt(SDL_Renderer* r, TTF_Font* f,
                    int x, int y, const char* text, SDL_Color col) {
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

    static int textW(TTF_Font* f, const char* text) {
        int w = 0, h = 0;
        TTF_SizeText(f, text, &w, &h);
        return w;
    }
};
