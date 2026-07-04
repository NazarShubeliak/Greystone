#pragma once
#include "astar.h"
#include "actor.h"
#include "npc.h"
#include "craft_panel.h" // reuse CraftPanel::countItems/consumeItems for Gold Coin bookkeeping
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <algorithm>

// Simple two-column buy/sell UI with a village merchant. Currency is the
// physical "Gold Coin" item — buying/selling moves coins and goods between
// the player's worn containers and the merchant's shopItems list.
struct TradePanel {
    bool visible     = false;
    int  villagerIdx = -1;

    void show(int idx) { villagerIdx = idx; visible = true; }
    void hide()         { visible = false; villagerIdx = -1; }

    static constexpr int W       = 560;
    static constexpr int ROW_H   = 22;
    static constexpr int HEADER  = 76;
    static constexpr int COL_GAP = 16;
    static constexpr int COL_W   = (W - COL_GAP - 24) / 2;

    // Reference to one item inside a worn container (slot + index within it).
    struct Ref { int slot, idx; };

    std::vector<Ref> sellableRefs(const Player& p) const {
        std::vector<Ref> v;
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++) {
            if (!p.worn[s].has_value()) continue;
            for (int i = 0; i < (int)p.worn[s]->contents.size(); i++)
                if (p.worn[s]->contents[i].name != "Gold Coin")
                    v.push_back({s, i});
        }
        return v;
    }

    // ── Layout ────────────────────────────────────────────────────────────
    int rows(const Villager& merch, const Player& p) const {
        return std::max({(int)merch.shopItems.size(), (int)sellableRefs(p).size(), 1});
    }
    int panelH(const Villager& merch, const Player& p) const {
        return HEADER + rows(merch, p) * ROW_H + 16;
    }
    int panelX() const { return (SCREEN_WIDTH - W) / 2; }
    int panelY(const Villager& merch, const Player& p) const {
        return std::max(10, (MAP_VIEW_HEIGHT - panelH(merch, p)) / 2);
    }

    // side: 0 = left "For Sale" column, 1 = right "Your Items" column. -1 if none.
    bool rowAt(int mx, int my, const Villager& merch, const Player& p,
               int& side, int& row) const {
        int X = panelX(), Y = panelY(merch, p);
        int listY = Y + HEADER;
        if (my < listY) return false;
        row = (my - listY) / ROW_H;
        int colLx = X + 12, colRx = X + 12 + COL_W + COL_GAP;
        if (mx >= colLx && mx < colLx + COL_W) { side = 0; return row < (int)merch.shopItems.size(); }
        if (mx >= colRx && mx < colRx + COL_W) { side = 1; return row < (int)sellableRefs(p).size(); }
        return false;
    }

    // ── Input ─────────────────────────────────────────────────────────────
    int hoverSide = -1, hoverRow = -1;

    void handleMotion(int mx, int my, const Player& p, std::vector<Villager>& villagers) {
        if (!visible || villagerIdx < 0 || villagerIdx >= (int)villagers.size()) return;
        int side, row;
        if (rowAt(mx, my, villagers[villagerIdx], p, side, row)) { hoverSide = side; hoverRow = row; }
        else { hoverSide = -1; hoverRow = -1; }
    }

    // Returns a log message describing what happened, or "" for no-op.
    std::string handleClick(int mx, int my, Player& p, std::vector<Villager>& villagers) {
        if (!visible) return "";
        if (villagerIdx < 0 || villagerIdx >= (int)villagers.size()) { hide(); return ""; }
        Villager& merch = villagers[villagerIdx];

        int X = panelX(), Y = panelY(merch, p), H = panelH(merch, p);
        bool inside = (mx >= X && mx < X + W && my >= Y && my < Y + H);
        if (!inside) { hide(); return "You leave the trade."; }

        int side, row;
        if (!rowAt(mx, my, merch, p, side, row)) return "";

        if (side == 0) return buy(merch, p, row);
        return sell(merch, p, sellableRefs(p)[row]);
    }

    std::string buy(Villager& merch, Player& p, int idx) {
        if (idx < 0 || idx >= (int)merch.shopItems.size()) return "";
        Item& stock = merch.shopItems[idx];
        int price = std::max(1, stock.value);
        if (CraftPanel::countItems(p, "Gold Coin") < price)
            return "Not enough gold to buy " + stock.name + ".";
        Item copy = stock;
        if (!p.addToContainer(copy))
            return "Not enough room in your inventory.";
        CraftPanel::consumeItems(p, "Gold Coin", price);
        std::string name = stock.name;
        merch.shopItems.erase(merch.shopItems.begin() + idx);
        return "You buy " + name + " for " + std::to_string(price) + " gold.";
    }

    std::string sell(Villager& merch, Player& p, Ref ref) {
        if (!p.worn[ref.slot].has_value()) return "";
        auto& c = p.worn[ref.slot]->contents;
        if (ref.idx < 0 || ref.idx >= (int)c.size()) return "";
        Item it = c[ref.idx];
        int price = std::max(1, it.value);
        c.erase(c.begin() + ref.idx);
        for (int k = 0; k < price; k++) p.addToContainer(Items::goldCoin());
        std::string name = it.name;
        merch.shopItems.push_back(std::move(it));
        return "You sell " + name + " for " + std::to_string(price) + " gold.";
    }

    // ── Render ────────────────────────────────────────────────────────────
    void render(SDL_Renderer* r, TTF_Font* f, Player& p, std::vector<Villager>& villagers) {
        if (!visible) return;
        if (villagerIdx < 0 || villagerIdx >= (int)villagers.size()) { hide(); return; }
        Villager& merch = villagers[villagerIdx];
        auto sellRefs = sellableRefs(p);

        int X = panelX(), Y = panelY(merch, p), H = panelH(merch, p);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 8, 8, 12, 248);
        SDL_Rect bg = {X, Y, W, H};
        SDL_RenderFillRect(r, &bg);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 130, 110, 50, 255);
        SDL_RenderDrawRect(r, &bg);

        int ty = Y + 10;
        txt(r, f, ("Trading with " + merch.name).c_str(), X + 12, ty, {200, 170, 60, 255});
        int gold = CraftPanel::countItems(p, "Gold Coin");
        std::string goldStr = "Gold: " + std::to_string(gold);
        int gw = textW(f, goldStr.c_str());
        txt(r, f, goldStr.c_str(), X + W - gw - 12, ty, {220, 200, 100, 255});
        ty += 22;

        hline(r, X, ty, W);
        ty += 8;

        int colLx = X + 12, colRx = X + 12 + COL_W + COL_GAP;
        txt(r, f, "For Sale", colLx, ty, {170, 165, 145, 255});
        txt(r, f, "Your Items", colRx, ty, {170, 165, 145, 255});
        ty += 20;

        int listY = ty;
        if (merch.shopItems.empty())
            txt(r, f, "(sold out)", colLx + 4, listY, {110, 110, 110, 255});
        for (int i = 0; i < (int)merch.shopItems.size(); i++) {
            bool hov = (hoverSide == 0 && hoverRow == i);
            drawRow(r, f, colLx, listY + i * ROW_H, COL_W, merch.shopItems[i].name,
                    merch.shopItems[i].value, hov);
        }

        if (sellRefs.empty())
            txt(r, f, "(nothing to sell)", colRx + 4, listY, {110, 110, 110, 255});
        for (int i = 0; i < (int)sellRefs.size(); i++) {
            const Item& it = p.worn[sellRefs[i].slot]->contents[sellRefs[i].idx];
            bool hov = (hoverSide == 1 && hoverRow == i);
            drawRow(r, f, colRx, listY + i * ROW_H, COL_W, it.name, it.value, hov);
        }
    }

private:
    void drawRow(SDL_Renderer* r, TTF_Font* f, int x, int y, int w,
                 const std::string& name, int price, bool hover) {
        if (hover) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 50, 45, 20, 200);
            SDL_Rect row = {x - 4, y - 1, w + 4, ROW_H};
            SDL_RenderFillRect(r, &row);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
        SDL_Color nameCol = hover ? SDL_Color{230, 210, 120, 255} : SDL_Color{190, 185, 170, 255};
        txt(r, f, name.c_str(), x, y + 2, nameCol);
        std::string priceStr = std::to_string(price) + "g";
        int pw = textW(f, priceStr.c_str());
        txt(r, f, priceStr.c_str(), x + w - pw, y + 2, {200, 175, 70, 255});
    }

    static void hline(SDL_Renderer* r, int x, int y, int w) {
        SDL_SetRenderDrawColor(r, 60, 58, 40, 255);
        SDL_RenderDrawLine(r, x + 8, y, x + w - 8, y);
    }

    static int textW(TTF_Font* f, const char* text) {
        int w = 0, h = 0; TTF_SizeText(f, text, &w, &h); return w;
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
