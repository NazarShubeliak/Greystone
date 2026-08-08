#pragma once
#include "actor.h"
#include "item.h"
#include "astar.h"
#include "panel_style.h"
#include "menu_hub.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <functional>

// ================================================================ Data

struct CraftIngredient {
    std::string name;
    int         count = 1;
};

struct Recipe {
    std::string                   name;
    std::string                   description;
    std::vector<CraftIngredient>  ingredients;
    int                           timeMins = 10;
    std::string                   resultDesc; // shown in UI
    std::function<Item()>         make;       // factory
};

// ================================================================ CraftPanel

struct CraftPanel {

    enum class State { HIDDEN, BROWSE, CONFIRM } panelState = State::HIDDEN;

    bool visible() const { return panelState != State::HIDDEN; }

    void open()  { panelState = State::BROWSE; }
    void close() { panelState = State::HIDDEN; }

    int selected = 0;  // selected recipe index
    int scroll   = 0;  // top visible recipe

    // ── Ingredient helpers ───────────────────────────────────────────────────

    static int countItems(const Player& p, const std::string& name) {
        int n = 0;
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT; s++) {
            if (!p.worn[s].has_value()) continue;
            for (const Item& it : p.worn[s]->contents)
                if (it.name == name) n += it.count;
        }
        return n;
    }

    static void consumeItems(Player& p, const std::string& name, int need) {
        for (int s = 0; s < (int)EquipSlot::SLOT_COUNT && need > 0; s++) {
            if (!p.worn[s].has_value()) continue;
            auto& c = p.worn[s]->contents;
            for (int i = (int)c.size() - 1; i >= 0 && need > 0; i--) {
                if (c[i].name != name) continue;
                int take = std::min(need, c[i].count);
                c[i].count -= take;
                need       -= take;
                if (c[i].count <= 0) c.erase(c.begin() + i);
            }
        }
    }

    bool canCraft(const Player& p) const {
        const Recipe& r = allRecipes()[selected];
        for (const auto& ing : r.ingredients)
            if (countItems(p, ing.name) < ing.count) return false;
        return true;
    }

    // Call when player confirms. Consumes materials, fills outItem/outMins.
    // Returns true on success.
    bool startCraft(Player& p, Item& outItem, int& outMins) {
        if (!canCraft(p)) return false;
        const Recipe& r = allRecipes()[selected];
        for (const auto& ing : r.ingredients)
            consumeItems(p, ing.name, ing.count);
        outItem  = r.make();
        outMins  = r.timeMins;
        panelState = State::HIDDEN;
        return true;
    }

    // ── Input ────────────────────────────────────────────────────────────────

    // Returns true when crafting should begin (fills outItem, outMins).
    bool handleKey(SDL_Keycode key, Player& p, Item& outItem, int& outMins) {
        if (panelState == State::HIDDEN) return false;

        if (panelState == State::CONFIRM) {
            if (key == SDLK_y || key == SDLK_RETURN)
                return startCraft(p, outItem, outMins);
            if (key == SDLK_n || key == SDLK_ESCAPE)
                panelState = State::BROWSE;
            return false;
        }

        // BROWSE state
        int n = (int)allRecipes().size();
        if (key == SDLK_UP   || key == SDLK_w) { selected = (selected - 1 + n) % n; clampScroll(); }
        if (key == SDLK_DOWN || key == SDLK_s) { selected = (selected + 1) % n;      clampScroll(); }
        if (key == SDLK_RETURN && canCraft(p))  panelState = State::CONFIRM;
        if (key == SDLK_ESCAPE)                 panelState = State::HIDDEN;
        return false;
    }

    // Returns true when crafting should begin.
    bool handleClick(int mx, int my, Player& p, Item& outItem, int& outMins) {
        if (panelState == State::HIDDEN) return false;

        dims();

        if (panelState == State::CONFIRM) {
            // Yes button
            if (mx >= yesX && mx <= yesX + btnW &&
                my >= btnY && my <= btnY + btnH)
                return startCraft(p, outItem, outMins);
            // No button
            if (mx >= noX && mx <= noX + btnW &&
                my >= btnY && my <= btnY + btnH)
                panelState = State::BROWSE;
            // Click outside → cancel
            if (mx < PX || mx > PX+PW || my < PY || my > PY+PH)
                panelState = State::BROWSE;
            return false;
        }

        // BROWSE: click recipe list
        for (int i = 0; i < LIST_ROWS; i++) {
            int ri = scroll + i;
            if (ri >= (int)allRecipes().size()) break;
            int ry = PY + HDR_H + i * ROW_H + 4;
            if (mx >= PX + 6 && mx <= PX + LIST_W - 6 &&
                my >= ry      && my <= ry + ROW_H - 2) {
                selected = ri;
                clampScroll();
                break;
            }
        }

        // BROWSE: click Craft button
        if (mx >= craftBtnX && mx <= craftBtnX + craftBtnW &&
            my >= craftBtnY && my <= craftBtnY + craftBtnH && canCraft(p))
            panelState = State::CONFIRM;

        // Click outside → close
        if (mx < PX || mx > PX+PW || my < PY || my > PY+PH)
            panelState = State::HIDDEN;

        return false;
    }

    // ── Render ───────────────────────────────────────────────────────────────

    void render(SDL_Renderer* r, TTF_Font* f, const Player& p) {
        if (panelState == State::HIDDEN) return;
        dims();

        // ── Title (borderless — the hub already dims the background) ─────────
        txt(r, f, "ESC close  \xe2\x86\x91\xe2\x86\x93 select  Enter craft",
            PX + PW - textW(f, "ESC close  \xe2\x86\x91\xe2\x86\x93 select  Enter craft"),
            PY + 6, {90,85,65,255});

        hline(r, PX, PY + HDR_H - 2, PW);

        // ── Recipe list (left column) ────────────────────────────────────────
        const auto& recs = allRecipes();
        int listTop = PY + HDR_H;
        for (int i = 0; i < LIST_ROWS; i++) {
            int ri = scroll + i;
            if (ri >= (int)recs.size()) break;
            int ry = listTop + i * ROW_H + 4;
            bool sel = (ri == selected);
            bool ok  = true;
            for (const auto& ing : recs[ri].ingredients)
                if (countItems(p, ing.name) < ing.count) { ok = false; break; }
            SDL_Color col = ok ? SDL_Color{200,195,165,255} : SDL_Color{130,120,95,255};
            if (sel) {
                SDL_SetRenderDrawColor(r, 40,36,20,255);
                SDL_Rect hl = {PX+4, ry-2, LIST_W-8, ROW_H};
                SDL_RenderFillRect(r, &hl);
                col = ok ? SDL_Color{240,230,160,255} : SDL_Color{160,150,110,255};
            }
            std::string prefix = sel ? "> " : "  ";
            txt(r, f, (prefix + recs[ri].name).c_str(), PX + 8, ry, col);
        }

        // divider between columns
        SDL_SetRenderDrawColor(r, 50,48,38,255);
        SDL_RenderDrawLine(r, PX + LIST_W, PY + HDR_H, PX + LIST_W, PY + PH - 1);

        // ── Detail panel (right column) ──────────────────────────────────────
        int dx = PX + LIST_W + 12;
        int dy = PY + HDR_H + 8;
        const Recipe& rec = recs[selected];

        txt(r, f, rec.name.c_str(), dx, dy, {220,210,160,255});
        dy += LH + 2;
        txt(r, f, rec.description.c_str(), dx, dy, {140,135,110,255});
        dy += LH + 8;
        hline(r, PX + LIST_W + 4, dy, PW - LIST_W - 8);
        dy += 8;

        txt(r, f, "Requires:", dx, dy, {160,155,120,255});
        dy += LH + 2;
        for (const auto& ing : rec.ingredients) {
            int have = countItems(p, ing.name);
            bool ok  = (have >= ing.count);
            std::string line = "  " + ing.name + " x" + std::to_string(ing.count);
            txt(r, f, line.c_str(), dx, dy, ok ? SDL_Color{120,200,100,255}
                                               : SDL_Color{200,80,60,255});
            std::string haveStr = "[have: " + std::to_string(have) + "]";
            txt(r, f, haveStr.c_str(), dx + 160, dy, {80,78,60,255});
            dy += LH;
        }
        dy += 6;

        hline(r, PX + LIST_W + 4, dy, PW - LIST_W - 8);
        dy += 8;

        std::string timeStr = "Time: " + std::to_string(rec.timeMins) + " minutes";
        txt(r, f, timeStr.c_str(), dx, dy, {160,155,120,255});
        dy += LH + 2;
        txt(r, f, rec.resultDesc.c_str(), dx, dy, {130,125,100,255});
        dy += LH + 14;

        // Craft button
        bool ok = canCraft(p);
        SDL_Color btnBg  = ok ? SDL_Color{50,90,40,255}   : SDL_Color{40,40,38,255};
        SDL_Color btnTxt = ok ? SDL_Color{160,230,120,255} : SDL_Color{80,78,60,255};
        SDL_Rect  btn    = {craftBtnX, craftBtnY, craftBtnW, craftBtnH};
        SDL_SetRenderDrawColor(r, btnBg.r, btnBg.g, btnBg.b, 255);
        SDL_RenderFillRect(r, &btn);
        SDL_SetRenderDrawColor(r, ok ? 80 : 55, ok ? 130 : 55, ok ? 60 : 48, 255);
        SDL_RenderDrawRect(r, &btn);
        int bw = textW(f, "[ CRAFT ]");
        txt(r, f, "[ CRAFT ]", craftBtnX + (craftBtnW - bw)/2, craftBtnY + 4, btnTxt);

        // ── Confirmation overlay ─────────────────────────────────────────────
        if (panelState == State::CONFIRM) {
            const int CW = 360, CH = 140;
            const int CX = (SCREEN_WIDTH - CW) / 2;
            const int CY = (MAP_VIEW_HEIGHT - CH) / 2;

            PanelStyle::frame(r, f, CX, CY, CW, CH, nullptr);

            int cy = CY + 12;
            std::string q = "Craft " + rec.name + "?";
            txt(r, f, q.c_str(), CX + 14, cy, {220,210,160,255});
            cy += LH + 4;

            std::string t1 = "This will take " + std::to_string(rec.timeMins) + " game minutes.";
            txt(r, f, t1.c_str(), CX + 14, cy, {160,155,120,255});
            cy += LH;
            txt(r, f, "You will be distracted while working.", CX + 14, cy, {160,100,60,255});
            cy += LH + 12;
            hline(r, CX, cy, CW);
            cy += 8;

            // Yes / No buttons
            SDL_Rect yBtn = {yesX, btnY, btnW, btnH};
            SDL_SetRenderDrawColor(r, 50,90,40,255);
            SDL_RenderFillRect(r, &yBtn);
            SDL_SetRenderDrawColor(r, 80,130,60,255);
            SDL_RenderDrawRect(r, &yBtn);
            int yw = textW(f, "[ Yes ]");
            txt(r, f, "[ Yes ]", yesX + (btnW - yw)/2, btnY + 4, {160,230,120,255});

            SDL_Rect nBtn = {noX, btnY, btnW, btnH};
            SDL_SetRenderDrawColor(r, 70,30,30,255);
            SDL_RenderFillRect(r, &nBtn);
            SDL_SetRenderDrawColor(r, 120,50,50,255);
            SDL_RenderDrawRect(r, &nBtn);
            int nw = textW(f, "[ No ]");
            txt(r, f, "[ No ]", noX + (btnW - nw)/2, btnY + 4, {220,100,80,255});

            txt(r, f, "Y / Enter  to confirm   N / ESC to cancel",
                CX + 14, CY + CH - 18, {55,52,38,255});
        }
    }

    // ── Recipe table ─────────────────────────────────────────────────────────

    static const std::vector<Recipe>& allRecipes() {
        static std::vector<Recipe> recs = {
            {
                "Wooden Club",
                "A heavy log shaped into a bludgeon. Simple to make.",
                {{"Log", 1}},
                20, "Weapon  dmg 18",
                Items::woodenClub
            },
            {
                "Stone Knife",
                "A sharp stone flake lashed to a short stick.",
                {{"Stone", 2}},
                15, "Weapon  dmg 14",
                Items::stoneKnife
            },
            {
                "Crude Axe",
                "A stone head bound to a branch. Cuts wood and enemies alike.",
                {{"Stone", 1}, {"Branch", 2}},
                35, "Weapon  dmg 22  can chop trees",
                Items::crudeAxe
            },
            {
                "Rope",
                "Thin branches twisted and braided into a sturdy cord.",
                {{"Branch", 4}},
                15, "Misc  useful for many things",
                Items::ropeItem
            },
            {
                "Herbal Poultice",
                "Crushed herbs packed in leaves. Soothes wounds and stops bleeding.",
                {{"Herb", 2}},
                15, "Medicine  heals and stops bleeding",
                Items::herbalPoultice
            },
            {
                "Mushroom Stew",
                "Mushrooms slow-cooked on hot stones. Rich and very filling.",
                {{"Mushroom", 2}},
                20, "Food  nutrition 24",
                Items::mushroomStew
            },
            {
                "Flatbread",
                "Grain ground and baked on a sun-heated stone.",
                {{"Grain", 3}},
                25, "Food  nutrition 20",
                Items::flatbread
            },
            {
                "Roasted Berries",
                "Berries dried over embers. Sweeter and more sustaining than raw.",
                {{"Berries", 4}},
                10, "Food  nutrition 10",
                Items::roastedBerries
            },
        };
        return recs;
    }

private:
    // ── Layout constants (computed once per frame via dims()) ────────────────
    static constexpr int LIST_W   = 195;
    static constexpr int HDR_H    = 28;
    static constexpr int ROW_H    = 22;
    static constexpr int LH       = 20;

    // PW/PH/LIST_ROWS fill the hub's available space — recomputed by dims()
    // (called every render()/handleClick()) so the panel grows with the window.
    int PW = 0, PH = 0, LIST_ROWS = 1;
    int PX = 0, PY = 0;
    int craftBtnX = 0, craftBtnY = 0, craftBtnW = 110, craftBtnH = 26;
    int yesX = 0, noX = 0, btnY = 0, btnW = 100, btnH = 26;

    void dims() {
        const int PAD = 40;
        PX = PAD;
        PY = MenuHub::TAB_H + 16;
        PW = SCREEN_WIDTH - PAD * 2;
        PH = SCREEN_HEIGHT - PY - 20;
        LIST_ROWS = (PH - HDR_H - 10) / ROW_H;
        if (LIST_ROWS < 1) LIST_ROWS = 1;

        craftBtnX = PX + LIST_W + 12;
        craftBtnY = PY + PH - 44;
        const int CW = 360, CH = 140;
        const int CX = (SCREEN_WIDTH - CW) / 2;
        const int CY = (MAP_VIEW_HEIGHT - CH) / 2;
        btnY = CY + CH - 42;
        yesX = CX + 40;
        noX  = CX + CW - 140;
    }

    void clampScroll() {
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + LIST_ROWS) scroll = selected - LIST_ROWS + 1;
    }

    static void txt(SDL_Renderer* r, TTF_Font* f,
                    const char* text, int x, int y, SDL_Color col) {
        SDL_Surface* s = TTF_RenderUTF8_Solid(f, text, col);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
        SDL_FreeSurface(s);
        int w, h; SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        SDL_Rect dst{x, y, w, h};
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
};
