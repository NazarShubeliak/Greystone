#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

struct MenuItem {
    std::string label;
    std::function<void()> action;
};

struct ContextMenu {
    bool visible = false;
    int x = 0, y = 0;
    std::vector<MenuItem> items;

    const int ITEM_HEIGHT = 28;
    const int MIN_WIDTH   = 150;
    const int PADDING = 8;
    int menuWidth = MIN_WIDTH; // widened in show() to fit the longest label

    void show(int screenX, int screenY, std::vector<MenuItem> menuItems, TTF_Font* font = nullptr) {
        x = screenX;
        y = screenY;
        items = menuItems;
        visible = true;

        menuWidth = MIN_WIDTH;
        if (font) {
            for (const MenuItem& it : items) {
                int w = 0, h = 0;
                TTF_SizeText(font, it.label.c_str(), &w, &h);
                menuWidth = std::max(menuWidth, w + PADDING * 2);
            }
        }
    }

    void hide() {
        visible = false;
        items.clear();
    }

    bool handleClick(int mx, int my) {
        if (!visible) return false;

        // Клік поза меню — закрити
        SDL_Rect bounds = getBounds();
        if (mx < bounds.x || mx > bounds.x + bounds.w ||
            my < bounds.y || my > bounds.y + bounds.h) {
            hide();
            return true;
        }

        // Клік на пункт меню
        for (int i = 0; i < (int)items.size(); i++) {
            SDL_Rect itemRect = {bounds.x, bounds.y + i * ITEM_HEIGHT, menuWidth, ITEM_HEIGHT};
            if (mx >= itemRect.x && mx <= itemRect.x + itemRect.w &&
                my >= itemRect.y && my <= itemRect.y + itemRect.h) {
                items[i].action();
                hide();
                return true;
            }
        }
        return false;
    }

    void render(SDL_Renderer* renderer, TTF_Font* font) {
        if (!visible) return;

        SDL_Rect bounds = getBounds();

        // Фон
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 240);
        SDL_RenderFillRect(renderer, &bounds);

        // Рамка
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderDrawRect(renderer, &bounds);

        // Пункти
        for (int i = 0; i < (int)items.size(); i++) {
            SDL_Rect itemRect = {bounds.x, bounds.y + i * ITEM_HEIGHT, menuWidth, ITEM_HEIGHT};

            // Підсвітка при hover — пізніше додамо
            SDL_Color color = {255, 255, 255, 255};

            SDL_Surface* s = TTF_RenderText_Solid(font, items[i].label.c_str(), color);
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);

            int w, h;
            SDL_QueryTexture(t, NULL, NULL, &w, &h);
            SDL_Rect dest = {itemRect.x + PADDING, itemRect.y + (ITEM_HEIGHT - h) / 2, w, h};
            SDL_RenderCopy(renderer, t, NULL, &dest);
            SDL_DestroyTexture(t);

            // Лінія між пунктами
            if (i < (int)items.size() - 1) {
                SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
                SDL_RenderDrawLine(renderer, bounds.x, bounds.y + (i + 1) * ITEM_HEIGHT,
                                   bounds.x + menuWidth, bounds.y + (i + 1) * ITEM_HEIGHT);
            }
        }
    }

private:
    SDL_Rect getBounds() {
        return {x, y, menuWidth, (int)items.size() * ITEM_HEIGHT};
    }
};