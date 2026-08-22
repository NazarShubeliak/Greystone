#pragma once
#include "terrain.h"
#include "astar.h"
#include "menu_hub.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <queue>
#include <string>
#include <vector>

const int OVERMAP_W = 100;
const int OVERMAP_H = 100;

// height/isWater/flowDX-DY/fertility — docs/world.md "Шар 1: Фізичний світ" +
// "Шар 2: Розселення". Temperature/humidity are NOT stored here — they only
// matter transiently while classifying biome in Overmap::generate() and are
// discarded afterward.
struct SectorInfo {
    BiomeType biome      = BiomeType::PLAINS;
    bool      explored   = false;
    bool      hasVillage = false;
    float     height     = 0.5f;  // 0=lowest .. 1=highest — drives rivers/fertility/village siting
    bool      isWater     = false; // river or lake cell
    int8_t    flowDX = 0, flowDY = 0; // isWater only: direction toward the next downstream sector (0,0 = lake/edge terminus)
    int8_t    riverFromDX = 0, riverFromDY = 0; // isWater only: direction back toward the upstream sector (0,0 = source, no entry edge)
    float     fertility   = 0.0f;  // 0..1, from height + distance to water — drives village siting
    bool      hasRoad = false;
    int8_t    roadDX = 0, roadDY = 0; // hasRoad only: direction toward the next sector along the road (0,0 = terminus/village here)
    int8_t    roadFromDX = 0, roadFromDY = 0; // hasRoad only: direction back toward the previous sector (0,0 = start of this road edge)
};

struct BiomeVisual {
    const char* name;
    const char* description;
    const char* symbol;
    SDL_Color   fg;
    SDL_Color   bg;
};

static const BiomeVisual biomeVisuals[] = {
    { "Plains",       "Open grasslands. Farms and villages are common here.",
      ".",  {100, 180,  60, 255}, { 10,  35,   5, 255} },
    { "Forest",       "Dense woodland. Wolves, bandits and worse lurk in the shadows.",
      "%",  { 20, 130,  20, 255}, {  3,  22,   3, 255} },
    { "Swamp",        "Murky wetlands thick with fog. The ground is treacherous.",
      "~",  { 70, 120,  40, 255}, { 10,  25,   8, 255} },
    { "Desert",       "Scorching sands under a merciless sun. Water is scarce.",
      ".",  {210, 175,  80, 255}, { 45,  35,   5, 255} },
    { "Tundra",       "Frozen wastelands swept by bitter winds. Few survive here.",
      ".",  {180, 205, 225, 255}, { 25,  35,  55, 255} },
    { "Cursed Lands", "Blighted by dark magic. The very ground writhes with evil.",
      "&",  {150,  40, 170, 255}, { 25,   5,  35, 255} },
};

struct Overmap {
    SectorInfo sectors[OVERMAP_H][OVERMAP_W];
    bool       visible = false;
    int        camX = 50, camY = 50;   // overmap camera (independent of player)

    SDL_Texture* biomeTex[6] = {};
    SDL_Texture* unknownTex  = nullptr;
    SDL_Texture* playerTex   = nullptr;
    SDL_Texture* villageTex  = nullptr;
    SDL_Texture* waterTex    = nullptr; // rivers/lakes — separate glyph+colour from Swamp's "~"
    SDL_Texture* roadTex     = nullptr; // inter-village roads (buildRoads())

    // ---------------------------------------------------------------- generate

    // Coarse-grid -> bilinear-upsample -> one box-blur pass "value noise"
    // (docs/world.md: "свій, ~50 рядків") — used independently for both
    // terrain height and humidity below. Not mathematically pure Perlin
    // noise, just coherent enough that terrain reads as hills/lowlands
    // instead of a blocky random grid — same spirit as the rest of this
    // game's procedural generation.
    static void noiseField(float (&out)[OVERMAP_H][OVERMAP_W]) {
        const int COARSE = 9; // 10x10 control points
        float coarse[COARSE + 1][COARSE + 1];
        for (int y = 0; y <= COARSE; y++)
            for (int x = 0; x <= COARSE; x++)
                coarse[y][x] = (rand() % 1000) / 1000.0f;

        for (int y = 0; y < OVERMAP_H; y++) {
            for (int x = 0; x < OVERMAP_W; x++) {
                float fx = (float)x / OVERMAP_W * COARSE;
                float fy = (float)y / OVERMAP_H * COARSE;
                int x0 = (int)fx, y0 = (int)fy;
                int x1 = std::min(x0 + 1, COARSE), y1 = std::min(y0 + 1, COARSE);
                float tx = fx - x0, ty = fy - y0;
                float top = coarse[y0][x0] * (1 - tx) + coarse[y0][x1] * tx;
                float bot = coarse[y1][x0] * (1 - tx) + coarse[y1][x1] * tx;
                out[y][x] = top * (1 - ty) + bot * ty;
            }
        }

        static float tmp[OVERMAP_H][OVERMAP_W]; // static: too big (40KB) to be happy on the stack
        for (int y = 0; y < OVERMAP_H; y++) {
            for (int x = 0; x < OVERMAP_W; x++) {
                float sum = 0; int n = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= OVERMAP_W || ny < 0 || ny >= OVERMAP_H) continue;
                        sum += out[ny][nx]; n++;
                    }
                }
                tmp[y][x] = sum / n;
            }
        }
        for (int y = 0; y < OVERMAP_H; y++)
            for (int x = 0; x < OVERMAP_W; x++)
                out[y][x] = tmp[y][x];
    }

    void generateHeightfield() {
        static float h[OVERMAP_H][OVERMAP_W];
        noiseField(h);
        for (int y = 0; y < OVERMAP_H; y++)
            for (int x = 0; x < OVERMAP_W; x++)
                sectors[y][x].height = h[y][x];
    }

    // Temperature from latitude (warmest at the vertical center, coldest at
    // the top/bottom edges, blended with a touch of this cell's own humidity
    // noise so the band boundary isn't a perfectly straight line) +
    // independent humidity noise -> the existing 6 BiomeType values via a
    // simple hot/cold x wet/dry table (docs/world.md step 2). Cursed Lands
    // stays a rare magical-blight overlay scattered on top afterward — a
    // corruption, not a climate zone, so it doesn't compete for a
    // temperature/humidity band of its own.
    void classifyBiomes() {
        static float humidity[OVERMAP_H][OVERMAP_W];
        noiseField(humidity);

        for (int y = 0; y < OVERMAP_H; y++) {
            for (int x = 0; x < OVERMAP_W; x++) {
                float latitude = std::abs(y - OVERMAP_H / 2.0f) / (OVERMAP_H / 2.0f); // 0=equator .. 1=pole
                float temp = std::max(0.0f, std::min(1.0f, (1.0f - latitude) * 0.85f + humidity[y][x] * 0.15f));
                float hum  = humidity[y][x];

                BiomeType b;
                if (temp < 0.35f)      b = (hum > 0.45f) ? BiomeType::FOREST : BiomeType::TUNDRA;
                else if (temp < 0.7f)  b = (hum > 0.6f)  ? BiomeType::FOREST : BiomeType::PLAINS;
                else                   b = (hum > 0.55f) ? BiomeType::SWAMP  : (hum < 0.3f ? BiomeType::DESERT : BiomeType::PLAINS);

                sectors[y][x].biome = b;
            }
        }

        int nBlights = 3 + rand() % 3; // 3-5
        for (int i = 0; i < nBlights; i++) {
            int cx = rand() % OVERMAP_W, cy = rand() % OVERMAP_H;
            int r  = 3 + rand() % 4;
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                    if (dx * dx + dy * dy > r * r) continue;
                    int nx = cx + dx, ny = cy + dy;
                    if (nx >= 0 && nx < OVERMAP_W && ny >= 0 && ny < OVERMAP_H)
                        sectors[ny][nx].biome = BiomeType::CURSED_LANDS;
                }
        }
    }

    // A handful of sources at the highest sectors, each traced downhill by
    // steepest descent (docs/world.md: "течуть з гір вниз за градієнтом
    // висоти") until it runs off the map edge, merges into an already-traced
    // river, or hits a local minimum with nowhere lower to go — that becomes
    // a lake ("озера — у западинах"). Best-effort: doesn't guarantee every
    // river reaches the edge or that lakes never form mid-slope; a stalled
    // trace just stops, same graceful-degradation spirit as the rest of
    // worldgen (e.g. placeRoleBuilding() giving up after 30 tries).
    void traceRivers() {
        std::vector<SDL_Point> peaks;
        for (int y = 0; y < OVERMAP_H; y++)
            for (int x = 0; x < OVERMAP_W; x++)
                if (sectors[y][x].height > 0.82f) peaks.push_back({x, y});

        int nRivers = std::min((int)peaks.size(), 6 + rand() % 5); // 6-10, capped by how many peaks exist
        for (int i = 0; i < nRivers && !peaks.empty(); i++) {
            int pick = rand() % (int)peaks.size();
            int cx = peaks[pick].x, cy = peaks[pick].y;
            peaks.erase(peaks.begin() + pick);

            for (int step = 0; step < OVERMAP_W + OVERMAP_H; step++) { // path can't outlive this many steps
                SectorInfo& cur = sectors[cy][cx];
                if (cur.isWater) break; // merged into an existing river/lake

                int bestDX = 0, bestDY = 0; float bestH = cur.height; bool found = false;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || nx >= OVERMAP_W || ny < 0 || ny >= OVERMAP_H) continue;
                        if (sectors[ny][nx].height < bestH) {
                            bestH = sectors[ny][nx].height; bestDX = dx; bestDY = dy; found = true;
                        }
                    }
                }

                if (!found) { cur.isWater = true; cur.flowDX = 0; cur.flowDY = 0; break; }

                cur.isWater = true; cur.flowDX = (int8_t)bestDX; cur.flowDY = (int8_t)bestDY;
                cx += bestDX; cy += bestDY;
                // The sector we just stepped into knows it was entered from
                // the opposite direction — carveRiver() (map.cpp) uses this
                // plus flowDX/DY, hashed together with the actual neighbour
                // sector's own coordinates, so both sides of a boundary
                // agree on exactly where the river crosses it instead of
                // each picking an independent point (used to make the same
                // river look crooked/discontinuous right at the seam).
                sectors[cy][cx].riverFromDX = (int8_t)(-bestDX);
                sectors[cy][cx].riverFromDY = (int8_t)(-bestDY);
                if (cx <= 0 || cx >= OVERMAP_W - 1 || cy <= 0 || cy >= OVERMAP_H - 1) {
                    sectors[cy][cx].isWater = true; // flows off the map edge
                    break;
                }
            }
        }
    }

    // docs/world.md step 4: "низини біля води — родючі". Nearest water
    // sector within a small search radius (Chebyshev distance) combined with
    // how low-lying this sector itself is.
    void computeFertility() {
        const int RADIUS = 6;
        for (int y = 0; y < OVERMAP_H; y++) {
            for (int x = 0; x < OVERMAP_W; x++) {
                SectorInfo& sec = sectors[y][x];
                if (sec.isWater) { sec.fertility = 0.0f; continue; }

                int bestDist = RADIUS + 1;
                for (int dy = -RADIUS; dy <= RADIUS; dy++) {
                    for (int dx = -RADIUS; dx <= RADIUS; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= OVERMAP_W || ny < 0 || ny >= OVERMAP_H) continue;
                        if (!sectors[ny][nx].isWater) continue;
                        int dist = std::max(std::abs(dx), std::abs(dy));
                        if (dist < bestDist) bestDist = dist;
                    }
                }

                float waterFactor   = (bestDist > RADIUS) ? 0.1f : std::max(0.1f, 1.0f - (float)bestDist / RADIUS);
                float lowlandFactor = std::max(0.0f, 1.0f - sec.height * 0.9f);
                sec.fertility = std::max(0.0f, std::min(1.0f, lowlandFactor * waterFactor));
            }
        }
    }

    // Replaces the old random-scatter-in-any-PLAINS-sector placement:
    // villages now go where the land is actually good ("Село — біля води +
    // родюча земля", docs/world.md step 2), highest fertility first, same
    // 5-sector spacing guard as before. Fewer than 15 placed (not enough
    // sectors clear the fertility/height bar with enough spacing) is an
    // accepted outcome, not a bug — same tolerance as placeRoleBuilding().
    void placeVillages() {
        struct Candidate { int x, y; float score; };
        std::vector<Candidate> candidates;
        for (int y = 0; y < OVERMAP_H; y++) {
            for (int x = 0; x < OVERMAP_W; x++) {
                const SectorInfo& sec = sectors[y][x];
                if (sec.isWater) continue;
                if (sec.height > 0.75f) continue;   // too mountainous to found a village on
                if (sec.fertility < 0.25f) continue;
                candidates.push_back({x, y, sec.fertility});
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        int placed = 0;
        for (const Candidate& c : candidates) {
            if (placed >= 15) break;
            bool tooClose = false;
            for (int dy = -5; dy <= 5 && !tooClose; dy++)
                for (int dx = -5; dx <= 5 && !tooClose; dx++) {
                    int nx = c.x + dx, ny = c.y + dy;
                    if (nx >= 0 && nx < OVERMAP_W && ny >= 0 && ny < OVERMAP_H)
                        if (sectors[ny][nx].hasVillage) tooClose = true;
                }
            if (!tooClose) { sectors[c.y][c.x].hasVillage = true; placed++; }
        }
    }

    // Overmap-scale A* — same octile-heuristic shape as src/core/astar.cpp's
    // findPath(), but a separate implementation: that one is hard-wired to
    // the 200x200 in-sector Tile grid (Tile::walkable()/moveCost()), this
    // one runs over the 100x100 SectorInfo grid with a terrain-height cost
    // function instead (docs/world.md: "вартість за рельєфом: рівнина
    // дешева, гори дорогі"). Water is costly, not forbidden — fording, no
    // bridges exist.
    struct PathNode { int x, y, g, h, f; PathNode* parent; };

    std::vector<SDL_Point> findOvermapPath(int startX, int startY, int endX, int endY) {
        static bool      closedSet[OVERMAP_H][OVERMAP_W];
        static PathNode* openMap  [OVERMAP_H][OVERMAP_W];
        memset(closedSet, 0, sizeof(closedSet));
        memset(openMap,   0, sizeof(openMap));
        std::vector<PathNode*> allNodes;

        auto heuristic = [](int x1, int y1, int x2, int y2) {
            int dx = std::abs(x1 - x2), dy = std::abs(y1 - y2);
            return 100 * (dx + dy) - 60 * std::min(dx, dy);
        };
        auto stepCost = [&](int x, int y) {
            const SectorInfo& s = sectors[y][x];
            return s.isWater ? 800 : (int)(100 + s.height * 300);
        };

        auto cmp = [](const PathNode* a, const PathNode* b) { return a->f > b->f; };
        std::priority_queue<PathNode*, std::vector<PathNode*>, decltype(cmp)> openQueue(cmp);

        PathNode* start = new PathNode{startX, startY, 0, heuristic(startX, startY, endX, endY), 0, nullptr};
        start->f = start->h;
        allNodes.push_back(start);
        openQueue.push(start);
        openMap[startY][startX] = start;

        const int dx8[] = { 0,  0,  1, -1,  1,  1, -1, -1 };
        const int dy8[] = { 1, -1,  0,  0,  1, -1,  1, -1 };

        std::vector<SDL_Point> result;
        while (!openQueue.empty()) {
            PathNode* current = openQueue.top();
            openQueue.pop();
            int cx = current->x, cy = current->y;
            if (closedSet[cy][cx]) continue;
            if (openMap[cy][cx] != current) continue;
            closedSet[cy][cx] = true;

            if (cx == endX && cy == endY) {
                PathNode* n = current;
                while (n) { result.push_back({n->x, n->y}); n = n->parent; }
                std::reverse(result.begin(), result.end());
                break;
            }

            for (int i = 0; i < 8; i++) {
                int nx = cx + dx8[i], ny = cy + dy8[i];
                if (nx < 0 || nx >= OVERMAP_W || ny < 0 || ny >= OVERMAP_H) continue;
                if (closedSet[ny][nx]) continue;

                int tileCost = stepCost(nx, ny);
                int moveCost = (i < 4) ? tileCost : tileCost * 14 / 10;
                int newG     = current->g + moveCost;

                if (openMap[ny][nx] == nullptr || newG < openMap[ny][nx]->g) {
                    int h = heuristic(nx, ny, endX, endY);
                    PathNode* neighbor = new PathNode{nx, ny, newG, h, newG + h, current};
                    allNodes.push_back(neighbor);
                    openMap[ny][nx] = neighbor;
                    openQueue.push(neighbor);
                }
            }
        }

        for (PathNode* n : allNodes) delete n;
        return result;
    }

    // Connects every village into one road network (docs/world.md: "Дороги
    // з'єднують поселення"). MST (Prim's, O(n^2) — trivial for ~15 nodes)
    // over Euclidean distance picks which ~14 village pairs actually get a
    // road — a cheap proxy so findOvermapPath() (real terrain-cost A*) only
    // has to run for the edges that matter, not all C(15,2) pairs. Every
    // sector each resulting path crosses gets hasRoad + the direction to the
    // next step; a village sector touched by more than one edge just keeps
    // whichever was processed last — same best-effort tolerance already
    // accepted for river tracing.
    void buildRoads() {
        std::vector<SDL_Point> villages;
        for (int y = 0; y < OVERMAP_H; y++)
            for (int x = 0; x < OVERMAP_W; x++)
                if (sectors[y][x].hasVillage) villages.push_back({x, y});
        if (villages.size() < 2) return;

        int n = (int)villages.size();
        std::vector<bool>  inTree(n, false);
        std::vector<float> minDist(n, 1e18f);
        std::vector<int>   parent(n, -1);
        minDist[0] = 0;

        for (int iter = 0; iter < n; iter++) {
            int u = -1;
            for (int i = 0; i < n; i++)
                if (!inTree[i] && (u == -1 || minDist[i] < minDist[u])) u = i;
            if (u == -1) break;
            inTree[u] = true;

            if (parent[u] >= 0) {
                std::vector<SDL_Point> path = findOvermapPath(
                    villages[parent[u]].x, villages[parent[u]].y, villages[u].x, villages[u].y);
                // roadDX/DY = direction to the next step (0,0 = terminus —
                // this village is the end of this edge); roadFromDX/DY =
                // direction back to the previous step (0,0 = start of this
                // edge — left alone, not overwritten, so a village that's
                // already the *destination* of an earlier-processed edge
                // keeps that arrival direction instead of losing it here).
                // carveRoad() (map.cpp) hashes both directions together
                // with the actual neighbour sector's coordinates so every
                // crossing matches on both sides of the boundary.
                for (size_t i = 0; i < path.size(); i++) {
                    SectorInfo& s = sectors[path[i].y][path[i].x];
                    s.hasRoad = true;
                    if (i + 1 < path.size()) {
                        s.roadDX = (int8_t)(path[i + 1].x - path[i].x);
                        s.roadDY = (int8_t)(path[i + 1].y - path[i].y);
                    }
                    if (i > 0) {
                        s.roadFromDX = (int8_t)(path[i - 1].x - path[i].x);
                        s.roadFromDY = (int8_t)(path[i - 1].y - path[i].y);
                    }
                }
            }

            for (int v = 0; v < n; v++) {
                if (inTree[v]) continue;
                float dx = (float)(villages[u].x - villages[v].x);
                float dy = (float)(villages[u].y - villages[v].y);
                float d  = dx * dx + dy * dy;
                if (d < minDist[v]) { minDist[v] = d; parent[v] = u; }
            }
        }
    }

    void generate() {
        for (int y = 0; y < OVERMAP_H; y++)
            for (int x = 0; x < OVERMAP_W; x++)
                sectors[y][x] = SectorInfo{};

        generateHeightfield();
        classifyBiomes();
        traceRivers();
        computeFertility();
        placeVillages();
        buildRoads();
    }

    // Reveal the 3x3 area around a sector when the player enters it.
    void reveal(int sx, int sy) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = sx+dx, ny = sy+dy;
                if (nx >= 0 && nx < OVERMAP_W && ny >= 0 && ny < OVERMAP_H)
                    sectors[ny][nx].explored = true;
            }
    }

    // Open overmap and snap camera to player position.
    void open(int playerSX, int playerSY) {
        visible = true;
        camX = playerSX;
        camY = playerSY;
    }
    void close() { visible = false; }

    void moveCam(int dx, int dy) {
        camX = std::max(0, std::min(OVERMAP_W - 1, camX + dx));
        camY = std::max(0, std::min(OVERMAP_H - 1, camY + dy));
    }

    // Jumps the camera directly to a sector (same clamp as moveCam()) — used
    // by mouse click/drag panning (main.cpp), as an alternative to the arrow
    // keys' relative moveCam().
    void setCam(int sx, int sy) {
        camX = std::max(0, std::min(OVERMAP_W - 1, sx));
        camY = std::max(0, std::min(OVERMAP_H - 1, sy));
    }

    // Converts a screen pixel to the overmap sector it falls on, or {-1,-1}
    // if outside the grid (title bar/legend strip) — mirrors render()'s exact
    // viewport geometry below so a click always lands on the cell the player
    // actually sees. const int/int-division here must stay identical to
    // render()'s CS/TOP/TITH/GRIDY/vW/vH/offX/offY — kept in sync by hand
    // since duplicating a handful of ints isn't worth a third helper both
    // this and render() call into.
    SDL_Point sectorAtScreen(int mx, int my) const {
        const int CS    = TILE_SIZE;
        const int TOP   = MenuHub::TAB_H;
        const int TITH  = 26;
        const int GRIDY = TOP + TITH;
        const int vW    = SCREEN_WIDTH             / CS;
        const int vH    = (MAP_VIEW_HEIGHT - GRIDY) / CS;
        const int offX  = camX - vW / 2;
        const int offY  = camY - vH / 2;

        if (my < GRIDY || my >= GRIDY + vH * CS) return {-1, -1};
        if (mx < 0 || mx >= vW * CS) return {-1, -1};

        int vx = mx / CS;
        int vy = (my - GRIDY) / CS;
        return SDL_Point{ offX + vx, offY + vy };
    }

    // ---------------------------------------------------------------- textures

    void initTextures(SDL_Renderer* r, TTF_Font* f) {
        auto make = [&](const char* sym, SDL_Color col) -> SDL_Texture* {
            SDL_Surface* s = TTF_RenderUTF8_Solid(f, sym, col);
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            SDL_FreeSurface(s);
            return t;
        };
        for (int i = 0; i < 6; i++)
            biomeTex[i] = make(biomeVisuals[i].symbol, biomeVisuals[i].fg);
        unknownTex  = make("?",  {38,  38,  35, 255});
        playerTex   = make("@",  {255, 255, 180, 255});
        villageTex  = make("#",  {255, 210,  60, 255});
        waterTex    = make("=",  {110, 170, 230, 255});
        roadTex     = make("+",  {170, 140,  90, 255});
    }

    void destroyTextures() {
        for (auto& t : biomeTex) { SDL_DestroyTexture(t); t = nullptr; }
        SDL_DestroyTexture(unknownTex);  unknownTex  = nullptr;
        SDL_DestroyTexture(playerTex);   playerTex   = nullptr;
        SDL_DestroyTexture(villageTex);  villageTex  = nullptr;
        SDL_DestroyTexture(waterTex);    waterTex    = nullptr;
        SDL_DestroyTexture(roadTex);     roadTex     = nullptr;
    }

    // ---------------------------------------------------------------- render

    void render(SDL_Renderer* r, TTF_Font* f, int playerSX, int playerSY) {
        if (!visible) return;

        const int CS    = TILE_SIZE;
        const int TOP   = MenuHub::TAB_H; // hub's tab strip
        const int TITH  = 26;             // this panel's own info bar, right below it
        const int GRIDY = TOP + TITH;
        const int vW   = SCREEN_WIDTH               / CS;
        const int vH   = (MAP_VIEW_HEIGHT - GRIDY)   / CS;
        const int offX = camX - vW / 2;    // viewport centered on camera, not player
        const int offY = camY - vH / 2;

        // Clear map view area
        SDL_SetRenderDrawColor(r, 5, 5, 8, 255);
        SDL_Rect mapArea = {0, GRIDY, SCREEN_WIDTH, MAP_VIEW_HEIGHT - GRIDY};
        SDL_RenderFillRect(r, &mapArea);

        for (int vy = 0; vy < vH; vy++) {
            for (int vx = 0; vx < vW; vx++) {
                int sx = offX + vx;
                int sy = offY + vy;
                SDL_Rect cell = {vx * CS, GRIDY + vy * CS, CS, CS};

                if (sx < 0 || sx >= OVERMAP_W || sy < 0 || sy >= OVERMAP_H)
                    continue; // void outside world bounds

                const SectorInfo& sec = sectors[sy][sx];

                if (!sec.explored) {
                    SDL_SetRenderDrawColor(r, 8, 8, 10, 255);
                    SDL_RenderFillRect(r, &cell);
                    SDL_RenderCopy(r, unknownTex, nullptr, &cell);
                    continue;
                }

                if (sec.isWater) {
                    SDL_SetRenderDrawColor(r, 10, 25, 55, 255);
                    SDL_RenderFillRect(r, &cell);
                    SDL_RenderCopy(r, waterTex, nullptr, &cell);
                    continue;
                }

                int bi = (int)sec.biome;
                SDL_Color bgc = biomeVisuals[bi].bg;
                SDL_SetRenderDrawColor(r, bgc.r, bgc.g, bgc.b, 255);
                SDL_RenderFillRect(r, &cell);
                // Road overlay skipped on village sectors — the village
                // marker already dominates and every village sits at a road
                // endpoint anyway (buildRoads()).
                SDL_Texture* glyphTex = sec.hasVillage ? villageTex
                                      : sec.hasRoad     ? roadTex
                                                         : biomeTex[bi];
                SDL_RenderCopy(r, glyphTex, nullptr, &cell);
            }
        }

        // Camera crosshair — gold rectangle around the center cell.
        SDL_SetRenderDrawColor(r, 180, 150, 55, 255);
        SDL_Rect crosshair = {(vW / 2) * CS, GRIDY + (vH / 2) * CS, CS, CS};
        SDL_RenderDrawRect(r, &crosshair);

        // Player marker (@) at actual player sector position.
        int pvx = playerSX - offX;
        int pvy = playerSY - offY;
        if (pvx >= 0 && pvx < vW && pvy >= 0 && pvy < vH) {
            SDL_Rect pcell = {pvx * CS, GRIDY + pvy * CS, CS, CS};
            SDL_RenderCopy(r, playerTex, nullptr, &pcell);
        }

        // ── Sector info box (bottom-left, above legend) ─────────────────
        {
            const int BW = 320;
            const int BH = 74;
            const int BX = 8;
            const int BY = MAP_VIEW_HEIGHT - 22 - BH - 6;
            const SectorInfo& sec = sectors[camY][camX];
            int bi2 = (int)sec.biome;

            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 10, 12, 16, 218);
            SDL_Rect box = {BX, BY, BW, BH};
            SDL_RenderFillRect(r, &box);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

            // Border tinted with biome colour (dimmed) — water sectors get a
            // blue-ish border instead, to match waterTex's colour.
            SDL_Color bc = !sec.explored ? SDL_Color{55,55,50,255}
                         : sec.isWater   ? SDL_Color{110,170,230,255}
                                         : biomeVisuals[bi2].fg;
            SDL_SetRenderDrawColor(r, bc.r / 2, bc.g / 2, bc.b / 2, 255);
            SDL_RenderDrawRect(r, &box);

            int ty = BY + 8;
            auto line = [&](const char* text, SDL_Color col) {
                SDL_Surface* s = TTF_RenderUTF8_Solid(f, text, col);
                if (!s) return;
                SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
                SDL_FreeSurface(s);
                int w, h;
                SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
                SDL_Rect d = {BX + 10, ty, w, h};
                SDL_RenderCopy(r, t, nullptr, &d);
                SDL_DestroyTexture(t);
                ty += h + 4;
            };

            if (!sec.explored) {
                line("Unknown Region",              {85, 85, 80, 255});
                line("This area has not been explored yet.", {60, 60, 55, 255});
                line("Enter the region to reveal it.",       {50, 50, 45, 255});
            } else if (sec.isWater) {
                bool isLake = (sec.flowDX == 0 && sec.flowDY == 0);
                line(isLake ? "Lake" : "River", {110, 170, 230, 255});
                line(isLake ? "Still water pooled in a basin." : "Flowing water, carved down from the highlands.",
                     {150, 145, 130, 255});
                std::string hline = "Height: " + std::to_string((int)(sec.height * 100)) + "%";
                line(hline.c_str(), {110, 110, 100, 255});
            } else {
                const BiomeVisual& bv = biomeVisuals[bi2];
                line(bv.name,        bv.fg);
                line(bv.description, {150, 145, 130, 255});
                std::string fline = "Height: " + std::to_string((int)(sec.height * 100))
                                  + "%   Fertility: " + std::to_string((int)(sec.fertility * 100)) + "%";
                line(fline.c_str(), {110, 110, 100, 255});
            }
        }

        // Info bar (semi-transparent strip), right below the hub's tab strip.
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 165);
        SDL_Rect titleBar = {0, TOP, SCREEN_WIDTH, TITH};
        SDL_RenderFillRect(r, &titleBar);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        // Show camera coords + biome (or River/Lake) at camera + player coords.
        const SectorInfo& camSec = sectors[camY][camX];
        int bi = (int)camSec.biome;
        std::string camBiomeName = camSec.isWater
            ? ((camSec.flowDX == 0 && camSec.flowDY == 0) ? "Lake" : "River")
            : biomeVisuals[bi].name;
        std::string camInfo = std::string("[") + std::to_string(camX) + "," + std::to_string(camY) + "] "
                            + camBiomeName;
        std::string playerInfo = std::string("@:[") + std::to_string(playerSX) + ","
                               + std::to_string(playerSY) + "]";
        std::string title = camInfo + "   " + playerInfo + "   |   Arrows/Click/Drag: pan   M: close";
        SDL_Surface* ts = TTF_RenderUTF8_Solid(f, title.c_str(), {175, 150, 65, 255});
        SDL_Texture* tt = SDL_CreateTextureFromSurface(r, ts);
        SDL_FreeSurface(ts);
        int tw, th;
        SDL_QueryTexture(tt, nullptr, nullptr, &tw, &th);
        SDL_Rect tdst = {8, TOP + (TITH - th) / 2, tw, th};
        SDL_RenderCopy(r, tt, nullptr, &tdst);
        SDL_DestroyTexture(tt);

        // Legend strip at bottom
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
        SDL_Rect legBar = {0, MAP_VIEW_HEIGHT - 22, SCREEN_WIDTH, 22};
        SDL_RenderFillRect(r, &legBar);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        const char* legend = ".: Plains   %: Forest   ~: Swamp   .: Desert   .: Tundra   &: Cursed Lands   =: River/Lake   +: Road   ?: Unexplored";
        SDL_Surface* ls = TTF_RenderUTF8_Solid(f, legend, {75, 75, 70, 255});
        SDL_Texture* lt = SDL_CreateTextureFromSurface(r, ls);
        SDL_FreeSurface(ls);
        int lw, lh;
        SDL_QueryTexture(lt, nullptr, nullptr, &lw, &lh);
        SDL_Rect ldst = {(SCREEN_WIDTH - lw) / 2, MAP_VIEW_HEIGHT - 11 - lh / 2, lw, lh};
        SDL_RenderCopy(r, lt, nullptr, &ldst);
        SDL_DestroyTexture(lt);
    }
};
