#pragma once
#include "map.h"
#include "item.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

struct ExaminePanel {
    bool visible = false;
    int  tileX = 0, tileY = 0;
    std::vector<Item> items;

    bool        hasCorpse   = false;
    std::string corpseName;
    bool        corpseFresh = false;

    void show(int x, int y) {
        tileX = x; tileY = y;
        items.clear();
        hasCorpse = false;
        visible = true;
    }

    void show(int x, int y, std::vector<Item> groundItems) {
        tileX = x; tileY = y;
        items = std::move(groundItems);
        hasCorpse = false;
        visible = true;
    }

    void hide() { visible = false; }

    void render(SDL_Renderer* r, TTF_Font* f, const Tile& tile) {
        if (!visible) return;

        const int W  = 480;
        const int H  = 300 + (int)items.size() * 100 + (hasCorpse ? 50 : 0);
        const int px = (SCREEN_WIDTH - W) / 2;
        const int py = (MAP_VIEW_HEIGHT - H) / 2;
        const int LX = px + 14;
        int cy = py + 12;

        // ── Helpers ──────────────────────────────────────────────────────
        auto txt = [&](const std::string& text, SDL_Color col) {
            SDL_Surface* s = TTF_RenderText_Solid(f, text.c_str(), col);
            if (!s) return;
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            int w, h;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_Rect dst = {LX, cy, w, h};
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            cy += h + 4;
        };

        auto hline = [&]() {
            SDL_SetRenderDrawColor(r, 65, 65, 60, 255);
            SDL_RenderDrawLine(r, px + 6, cy + 2, px + W - 6, cy + 2);
            cy += 10;
        };

        // ── Background + border ──────────────────────────────────────────
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 12, 14, 18, 235);
        SDL_Rect bg = {px, py, W, H};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 120, 100, 50, 255);
        SDL_RenderDrawRect(r, &bg);

        SDL_Color gold   = {200, 170,  80, 255};
        SDL_Color white  = {215, 215, 205, 255};
        SDL_Color dim    = {145, 145, 130, 255};
        SDL_Color objCol = {180, 200,  70, 255};
        SDL_Color gndCol = { 70, 180,  90, 255};
        SDL_Color terCol = { 70, 145, 185, 255};
        SDL_Color itmCol = {220, 195,  60, 255};

        // Title
        txt("EXAMINE  [" + std::to_string(tileX) + ", " + std::to_string(tileY) + "]", gold);
        hline();

        // ── Items on ground ───────────────────────────────────────────────
        for (const Item& it : items) {
            SDL_Color typeCol = typeVisuals[(int)it.type].color;

            txt(std::string("[Item]    ") + it.groundSymbol() + " " + it.name, itmCol);
            txt(std::string("  ") + it.description, dim);

            std::string line1 = "  Vol: " + std::to_string(it.volume) + " ml"
                              + "   Wt: " + std::to_string(it.weight) + " g";
            if (it.value > 0) line1 += "   Value: " + std::to_string(it.value) + " gold";
            txt(line1, white);

            std::string line2;
            switch (it.type) {
                case ItemType::WEAPON:
                    line2 = "  Damage: " + std::to_string(it.damage);
                    if (it.twoHanded) line2 += "   Two-handed";
                    if (it.slot != EquipSlot::NONE)
                        line2 += "   Slot: " + std::string(slotNames[(int)it.slot]);
                    break;
                case ItemType::ARMOR:
                    line2 = "  Defense: " + std::to_string(it.defense);
                    if (it.slot != EquipSlot::NONE)
                        line2 += "   Slot: " + std::string(slotNames[(int)it.slot]);
                    break;
                case ItemType::CONTAINER:
                    line2 = "  Capacity: " + std::to_string(it.maxVolume) + " ml"
                          + " / " + std::to_string(it.maxWeight) + " g";
                    break;
                case ItemType::FOOD:
                    line2 = "  Nutrition: " + std::to_string(it.nutrition) + "%";
                    break;
                case ItemType::DRINK:
                    line2 = "  Hydration: " + std::to_string(it.hydration) + "%";
                    break;
                case ItemType::JEWELRY: {
                    std::string bonuses;
                    if (it.strBonus) bonuses += "STR+" + std::to_string(it.strBonus) + " ";
                    if (it.dexBonus) bonuses += "DEX+" + std::to_string(it.dexBonus) + " ";
                    if (it.conBonus) bonuses += "CON+" + std::to_string(it.conBonus) + " ";
                    if (!bonuses.empty()) line2 = "  Bonuses: " + bonuses;
                    if (it.slot != EquipSlot::NONE)
                        line2 += "   Slot: " + std::string(slotNames[(int)it.slot]);
                    break;
                }
                default: break;
            }
            if (!line2.empty()) txt(line2, typeCol);
            hline();
        }

        // ── Corpse ────────────────────────────────────────────────────────
        if (hasCorpse) {
            SDL_Color cCol = {160, 90, 90, 255};
            txt(std::string("[Corpse]  ") + corpseName, cCol);
            std::string state = corpseFresh
                ? "  Fresh remains. A necromancer could animate this."
                : "  Decomposing remains.";
            txt(state, dim);
            hline();
        }

        // ── Object ───────────────────────────────────────────────────────
        if (tile.objectId >= 0) {
            const ObjectDef& od = objectDefs[tile.objectId];
            txt(std::string("[Object]  ") + od.name, objCol);
            txt(std::string("  ") + od.description, dim);

            if (od.durability > 0) {
                txt("  Durability: " + std::to_string(tile.objectHp)
                    + " / " + std::to_string(od.durability), white);
            }

            std::string pass = std::string("  Passable: ") + (od.blocksMove ? "No" : "Yes");
            if (!od.blocksMove && od.moveCostMod > 0)
                pass += "   Penalty: +" + std::to_string(od.moveCostMod);
            pass += std::string("   Blocks sight: ") + (od.blocksVision ? "Yes" : "No");
            txt(pass, dim);
            hline();
        }

        // ── Ground cover ──────────────────────────────────────────────────
        if (tile.groundId >= 0) {
            const GroundDef& gd = groundDefs[tile.groundId];
            txt(std::string("[Ground]  ") + gd.name, gndCol);
            txt(std::string("  ") + gd.description, dim);
            txt("  Move penalty: +" + std::to_string(gd.moveCostMod), white);
            hline();
        }

        // ── Terrain ───────────────────────────────────────────────────────
        const TerrainDef& td = terrainDefs[tile.terrainId];
        txt(std::string("[Terrain] ") + td.name, terCol);
        txt(std::string("  ") + td.description, dim);
        std::string terrStr = (td.moveCost == 0)
            ? "  Passable: No"
            : "  Base move cost: " + std::to_string(td.moveCost);
        txt(terrStr, white);
        hline();

        // ── Summary ───────────────────────────────────────────────────────
        std::string total = tile.walkable()
            ? "Total move cost: " + std::to_string(tile.moveCost())
            : "Total: Impassable";
        txt(total, {220, 220, 200, 255});

        // Close hint
        SDL_Surface* hs = TTF_RenderText_Solid(f, "Click or E to close", {65, 65, 60, 255});
        if (hs) {
            SDL_Texture* ht = SDL_CreateTextureFromSurface(r, hs);
            SDL_FreeSurface(hs);
            int hw, hh;
            SDL_QueryTexture(ht, nullptr, nullptr, &hw, &hh);
            SDL_Rect hdst = {px + W - hw - 10, py + H - hh - 6, hw, hh};
            SDL_RenderCopy(r, ht, nullptr, &hdst);
            SDL_DestroyTexture(ht);
        }
    }
};
