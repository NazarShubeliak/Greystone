#include "astar.h"
#include "actor.h"
#include "ui.h"
#include "bottom_panel.h"
#include "body_panel.h"
#include "context_menu.h"
#include "examine_panel.h"
#include "overmap.h"
#include "cheat_console.h"
#include "inventory.h"
#include "item_examine_panel.h"
#include "enemy_examine_panel.h"
#include "pickup_panel.h"
#include "time_system.h"
#include "enemy_types.h"
#include "corpse.h"
#include "map.h"
#include "npc.h"
#include "villager_examine_panel.h"
#include "craft_panel.h"
#include "wait_panel.h"
#include "confirm_panel.h"
#include "effects_panel.h"
#include "trade_panel.h"
#include "render.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <ctime>
#include <algorithm>

SDL_Color white = {255, 255, 255, 255};
SDL_Color red   = {255,   0,   0, 255};

Tile map[MAP_HEIGHT][MAP_WIDTH];

SDL_Texture* texCursor     = nullptr;
SDL_Texture* playerTexture = nullptr;
SDL_Texture* enemyTexture  = nullptr;

Player player(20, 15);
std::vector<Enemy>    enemies;
std::vector<Corpse>   corpses;
std::vector<Villager> villagers;
int villageWellX = -1, villageWellY = -1; // walkable tile beside the well, set in spawnVillagers()

std::vector<SDL_Point> currentPath;
int pathIndex = 0;
Uint32 lastMoveTime = 0;

// Deferred action executed when the player finishes walking to a destination.
enum class PendingAct { NONE, PICKUP_ONE, PICKUP_PANEL, INTERACT, AUTO_INTERACT };
PendingAct pendingAct  = PendingAct::NONE;
int        pendingActX = -1, pendingActY = -1;

// When chasing an enemy, store a pointer so path can be recalculated each step.
Enemy* attackTarget = nullptr;

int hoverX = 0, hoverY = 0;
int lastHoverX = -1, lastHoverY = -1;
std::vector<SDL_Point> previewPath;

int cameraX = 0, cameraY = 0;
UI ui;
BottomPanel panel;
BodyPanel bodyPanel;
EffectsPanel effectsPanel;
TradePanel   tradePanel;
ContextMenu contextMenu;
ExaminePanel examinePanel;
Overmap overmap;
CheatConsole console;
InventoryPanel inventoryPanel;
ItemExaminePanel  itemExaminePanel;
EnemyExaminePanel   enemyExaminePanel;
VillagerExaminePanel villagerExaminePanel;
CraftPanel craftPanel;
PickupPanel pickupPanel;

WaitPanel    waitPanel;
ConfirmPanel needsConfirmPanel;

// ── Wait state ───────────────────────────────────────────────────────────────
bool isWaiting         = false;
int  waitTargetMinutes = 0;
bool waitPaused        = false; // true while needsConfirmPanel is up, mid-wait
int  waitPrevHungerLv  = 0;
int  waitPrevThirstLv  = 0;

// ── Crafting state ──────────────────────────────────────────────────────────
bool isCrafting       = false;
int  craftMinutesLeft = 0;
int  craftTotalMins   = 0;
Item craftPendingItem;
std::vector<GroundItem> groundItems;
WorldTime worldTime;
int playerSectorX = 50;
int playerSectorY = 50;

// ------------------------------------------------------------------ helpers

// Weighted random body part hit (CDDA-style distribution).
PartTarget randomHitPart() {
    int r = rand() % 100;
    if (r < 10) return PartTarget::HEAD;
    if (r < 55) return PartTarget::TORSO;
    if (r < 67) return PartTarget::ARM_L;
    if (r < 79) return PartTarget::ARM_R;
    if (r < 89) return PartTarget::LEG_L;
                return PartTarget::LEG_R;
}

const char* partName(PartTarget t) {
    switch (t) {
        case PartTarget::HEAD:  return "head";
        case PartTarget::TORSO: return "torso";
        case PartTarget::ARM_L: return "left arm";
        case PartTarget::ARM_R: return "right arm";
        case PartTarget::LEG_L: return "left leg";
        case PartTarget::LEG_R: return "right leg";
    }
    return "body";
}

Enemy* getEnemyAt(int x, int y) {
    for (Enemy& e : enemies)
        if (e.isAlive() && e.x == x && e.y == y) return &e;
    return nullptr;
}

// Returns the first ground item at (x, y), or nullptr if none.
GroundItem* getGroundItemAt(int x, int y) {
    for (GroundItem& gi : groundItems)
        if (gi.x == x && gi.y == y) return &gi;
    return nullptr;
}

// Returns copies of all items at (x, y) in groundItems order.
std::vector<Item> getGroundItemsAt(int x, int y) {
    std::vector<Item> result;
    for (const GroundItem& gi : groundItems)
        if (gi.x == x && gi.y == y) result.push_back(gi.item);
    return result;
}

// Pick up items at (gx, gy) according to selection mask (parallel to groundItems order at that tile).
void doPickup(int gx, int gy, const std::vector<bool>& sel) {
    int selIdx = 0;
    for (int i = (int)groundItems.size() - 1; i >= 0; i--) {
        if (groundItems[i].x != gx || groundItems[i].y != gy) continue;
        // sel is in forward order; we iterate backward, so map accordingly
        // Build forward list first to align indices
        (void)selIdx;
        // We'll do it in a forward pass below
    }
    // Forward pass to align with sel[]
    std::vector<int> atTile;
    for (int i = 0; i < (int)groundItems.size(); i++)
        if (groundItems[i].x == gx && groundItems[i].y == gy) atTile.push_back(i);

    // Erase selected in reverse order to preserve indices
    for (int j = (int)atTile.size() - 1; j >= 0; j--) {
        if (j >= (int)sel.size() || !sel[j]) continue;
        int idx = atTile[j];
        std::string n = groundItems[idx].item.name;
        if (player.addToContainer(groundItems[idx].item)) {
            panel.addMessage("You pick up the " + n + ".");
            groundItems.erase(groundItems.begin() + idx);
        } else {
            panel.addMessage("No room for the " + n + ".");
        }
    }
}

// Pick up all items at player's position вЂ" shows selection panel if multiple.
void pickUpAtPlayer() {
    std::vector<Item> here = getGroundItemsAt(player.x, player.y);
    if (here.empty()) { panel.addMessage("Nothing to pick up here."); return; }
    if (here.size() == 1) {
        std::vector<bool> sel = {true};
        doPickup(player.x, player.y, sel);
        return;
    }
    // Multiple items вЂ" show selection panel
    pickupPanel.show(player.x, player.y, std::move(here));
    pickupPanel.onConfirm = [px=player.x, py=player.y](const std::vector<bool>& sel) {
        doPickup(px, py, sel);
    };
}

bool isTileOccupied(int x, int y) {
    for (Enemy& e : enemies)
        if (e.alive && e.x == x && e.y == y) return true;
    return false;
}

void spawnEnemy(std::function<Enemy(int,int)> factory) {
    bool isVillageSector = overmap.sectors[playerSectorY][playerSectorX].hasVillage;
    const int VCX = MAP_WIDTH / 2, VCY = MAP_HEIGHT / 2;

    for (int attempt = 0; attempt < 50; attempt++) {
        int x = rand() % (MAP_WIDTH  - 4) + 2;
        int y = rand() % (MAP_HEIGHT - 4) + 2;
        if (!map[y][x].walkable()) continue;
        if (map[y][x].terrainId == T_FLOOR) continue; // never spawn inside a building
        if (isVillageSector) {
            // No village-raid AI yet — keep spawns outside the village proper for now.
            int vdx = x - VCX, vdy = y - VCY;
            if (vdx*vdx + vdy*vdy < 55*55) continue;
        }
        if (isTileOccupied(x, y))  continue;
        // Don't spawn too close to player.
        int dx = x - player.x, dy = y - player.y;
        if (dx*dx + dy*dy < 12*12) continue;
        enemies.push_back(factory(x, y));
        return;
    }
}

void initEnemy() {
    enemies.clear();
    BiomeType biome = overmap.sectors[playerSectorY][playerSectorX].biome;

    switch (biome) {
        case BiomeType::PLAINS:
            for (int i = 0; i < 3; i++) spawnEnemy(EnemyTypes::bandit);
            for (int i = 0; i < 4; i++) spawnEnemy(EnemyTypes::wolf);
            for (int i = 0; i < 2; i++) spawnEnemy(EnemyTypes::goblin);
            break;

        case BiomeType::FOREST:
            for (int i = 0; i < 5; i++) spawnEnemy(EnemyTypes::goblin);
            for (int i = 0; i < 3; i++) spawnEnemy(EnemyTypes::wolf);
            for (int i = 0; i < 1; i++) spawnEnemy(EnemyTypes::skeleton);
            break;

        case BiomeType::SWAMP:
            for (int i = 0; i < 4; i++) spawnEnemy(EnemyTypes::skeleton);
            for (int i = 0; i < 3; i++) spawnEnemy(EnemyTypes::goblin);
            break;

        case BiomeType::DESERT:
            for (int i = 0; i < 3; i++) spawnEnemy(EnemyTypes::bandit);
            for (int i = 0; i < 2; i++) spawnEnemy(EnemyTypes::orcWarrior);
            break;

        case BiomeType::TUNDRA:
            for (int i = 0; i < 5; i++) spawnEnemy(EnemyTypes::wolf);
            for (int i = 0; i < 2; i++) spawnEnemy(EnemyTypes::orcWarrior);
            break;

        case BiomeType::CURSED_LANDS:
            for (int i = 0; i < 5; i++) spawnEnemy(EnemyTypes::skeleton);
            for (int i = 0; i < 2; i++) spawnEnemy(EnemyTypes::orcWarrior);
            for (int i = 0; i < 2; i++) spawnEnemy(EnemyTypes::goblin);
            break;

        default:
            for (int i = 0; i < 5; i++) spawnEnemy(EnemyTypes::goblin);
            break;
    }
}

// Scatters a container's contents onto the ground as individual pickups, plus the
// (now empty) container itself — nothing a creature had vanishes on death, and
// nothing lives as an invisible inventory in the meantime.
void dropBag(const Actor& who, const std::optional<Item>& bag) {
    if (!bag) return;
    std::string list;
    for (const Item& item : bag->contents) {
        groundItems.push_back({who.x, who.y, item});
        if (!list.empty()) list += ", ";
        list += item.name;
    }
    if (!list.empty()) panel.addMessage(who.name + " drops: " + list + ".");
    Item emptyBag = *bag;
    emptyBag.contents.clear();
    groundItems.push_back({who.x, who.y, emptyBag});
}

// Drop everything an enemy had and leave a corpse.
void dropEnemyLoot(const Enemy& enemy) {
    dropBag(enemy, enemy.bag);
    corpses.push_back(makeCorpse(enemy, worldTime.minutes));
}

// ------------------------------------------------------------------ turn system
//
// One "world tick" = everyone gains speed energy; enemies spend theirs acting.
// The world only ticks when the player chooses to act (world freezes while idle).
// Fast actors (speed > 100) accumulate energy and act multiple times per cycle.
// Slow actors (speed < 100) act less often вЂ" multiple ticks pass per their action.

void enemyAct(Enemy& enemy) {
    int dx = player.x - enemy.x;
    int dy = player.y - enemy.y;
    int dist2 = dx * dx + dy * dy;

    // Flee: move away from player when wounded.
    if (enemy.wantsToFlee()) {
        int fx = enemy.x - (dx != 0 ? (dx > 0 ? 1 : -1) : 0);
        int fy = enemy.y - (dy != 0 ? (dy > 0 ? 1 : -1) : 0);
        fx = std::max(1, std::min(MAP_WIDTH  - 2, fx));
        fy = std::max(1, std::min(MAP_HEIGHT - 2, fy));
        if (map[fy][fx].walkable() && !isTileOccupied(fx, fy)) {
            enemy.x = fx; enemy.y = fy;
        }
        return;
    }

    if (dist2 > enemy.aggroRange * enemy.aggroRange) return;

    std::vector<SDL_Point> path = findPath(enemy.x, enemy.y, player.x, player.y);
    if ((int)path.size() < 2) return;

    SDL_Point next = path[1];
    if (next.x == player.x && next.y == player.y) {
        PartTarget part   = randomHitPart();
        int        rawDmg = 3 + (enemy.strength - 10) / 2 + enemy.weaponDmg();
        if (part == PartTarget::HEAD) rawDmg = (int)(rawDmg * 1.5f);
        int damage = std::max(1, rawDmg - player.totalDefense());
        player.takeDamage(damage, part);
        std::string withWeapon;
        if (enemy.bag)
            for (const Item& item : enemy.bag->contents)
                if (item.type == ItemType::WEAPON) { withWeapon = " with " + item.name; break; }
        panel.addMessage(enemy.name + " hits your " + partName(part)
                         + withWeapon + " for " + std::to_string(damage) + " damage.");
        if (!player.isAlive())
            panel.addMessage("You have been slain by " + enemy.name + ".");
    } else if (!isTileOccupied(next.x, next.y)) {
        enemy.x = next.x;
        enemy.y = next.y;
    } else {
        // Planned step is blocked by another enemy — try adjacent tiles that
        // bring us closer to the player so enemies don't pile up and freeze.
        static const int dirs[8][2] = {
            {0,-1},{0,1},{-1,0},{1,0},{-1,-1},{1,-1},{-1,1},{1,1}
        };
        int bestDist = dx*dx + dy*dy;
        int bestX = -1, bestY = -1;
        int start = rand() % 8;
        for (int d = 0; d < 8; d++) {
            int nd = (start + d) % 8;
            int nx = enemy.x + dirs[nd][0];
            int ny = enemy.y + dirs[nd][1];
            if (nx <= 0 || nx >= MAP_WIDTH-1 || ny <= 0 || ny >= MAP_HEIGHT-1) continue;
            if (!map[ny][nx].walkable()) continue;
            if (isTileOccupied(nx, ny)) continue;
            int ndx = player.x - nx, ndy = player.y - ny;
            int newDist = ndx*ndx + ndy*ndy;
            if (newDist < bestDist) { bestDist = newDist; bestX = nx; bestY = ny; }
        }
        if (bestX >= 0) { enemy.x = bestX; enemy.y = bestY; }
    }
}

void updateVillagers();    // forward declaration — defined after checkSectorTransition
void tickVillagerNeeds();  // forward declaration — defined after updateVillagers
void tickEnemyNeeds();     // forward declaration — defined below, called once per player action
void interruptCrafting(bool playerHit); // forward declaration — defined in villager section

// One world tick: give everyone energy, then let enemies spend theirs.
// NOTE: this can run several times per single player action (see onPlayerAct()), with the
// iteration count depending on player speed — so anything tied to real elapsed game time
// (hunger/thirst/etc.) must NOT live in here. See tickEnemyNeeds().
void tickWorld() {
    int effSpeed = std::max(1, player.speed - player.needsSpeedPenalty());
    player.energy += effSpeed;

    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        e.energy += e.speed;
        while (e.energy >= 100) {
            enemyAct(e);
            e.energy -= 100;
        }
    }

    updateVillagers();

    // Corpse decay
    corpses.erase(
        std::remove_if(corpses.begin(), corpses.end(),
                       [](const Corpse& c){ return c.decayed(worldTime.minutes); }),
        corpses.end());

    // Drop attack target if it's dead, then remove all dead enemies.
    if (attackTarget && !attackTarget->alive) attackTarget = nullptr;
    enemies.erase(
        std::remove_if(enemies.begin(), enemies.end(),
                       [](const Enemy& e){ return !e.alive; }),
        enemies.end());
}

// Hunger/thirst/bleed-out tick for enemies, once per player action — mirrors
// tickVillagerNeeds(). Must NOT live inside tickWorld(): that function can run several
// times per single player action depending on player speed, which would make enemy
// needs advance faster the slower the player is instead of tracking real game time.
void tickEnemyNeeds() {
    static int lastEnemyNeedsHour = -1;
    bool hourCrossed = (lastEnemyNeedsHour != worldTime.hour());
    lastEnemyNeedsHour = worldTime.hour();

    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        e.tickNeeds();
        if (hourCrossed) {
            if (e.hunger >= 1.0f) e.takeDamage(1, PartTarget::TORSO);
            if (e.thirst >= 1.0f) e.takeDamage(2, PartTarget::TORSO);
        }
        if (!e.alive) {
            std::string cause = e.thirst >= 1.0f ? "dies of dehydration."
                               : e.hunger >= 1.0f ? "dies of starvation."
                                                   : "bleeds to death.";
            panel.addMessage(e.name + " " + cause);
            dropEnemyLoot(e);
        }
    }
}

// Called after every player action.
// Spends 100 energy, then ticks the world until the player can act again.
// Result: player.energy >= 100 when this returns.
void onPlayerAct() {
    int prevMinutes = worldTime.minutes;
    int prevSeason  = worldTime.season();
    worldTime.advance();

    // Seasonal sunrise/sunset events
    int rise    = (int)worldTime.sunriseHour();
    int set_    = (int)worldTime.sunsetHour();
    int dawnBeg = rise - 1;
    if (worldTime.crossedHour(dawnBeg, prevMinutes))
        panel.addMessage("The sky begins to lighten in the east.");
    if (worldTime.crossedHour(rise, prevMinutes))
        panel.addMessage("The sun rises over the horizon.");
    if (worldTime.crossedHour(12, prevMinutes))
        panel.addMessage("The sun stands high overhead.");
    if (worldTime.crossedHour(set_, prevMinutes))
        panel.addMessage("The sun sinks toward the horizon.");
    if (worldTime.crossedHour(set_ + 1, prevMinutes)) {
        panel.addMessage("Darkness falls across the land.");
        if (player.totalLightRadius() == 0)
            panel.addMessage("It is pitch dark. You need a light source!");
    }

    if (worldTime.season() != prevSeason)
        panel.addMessage(std::string("The season changes: ") + worldTime.seasonName() + " begins.");

    int prevHungerLv = player.hungerLevel();
    int prevThirstLv = player.thirstLevel();

    int hpBefore = player.hp;
    player.energy -= 100;
    while (player.energy < 100)
        tickWorld();

    // Needs advance and starvation damage once per player action (not per world tick)
    player.tickNeeds();
    tickVillagerNeeds();
    tickEnemyNeeds();
    if (player.hunger >= 1.0f) {
        player.body.torso.hp = std::max(0, player.body.torso.hp - 1);
        player.sync();
        if (worldTime.minutes % 10 == 0)
            panel.addMessage("You are starving and taking damage!");
    }
    if (player.thirst >= 1.0f) {
        player.body.torso.hp = std::max(0, player.body.torso.hp - 2);
        player.sync();
        if (worldTime.minutes % 10 == 0)
            panel.addMessage("You are dehydrated and taking damage!");
    }

    // Interrupt crafting if player was hit this tick
    if (isCrafting && player.hp < hpBefore) {
        interruptCrafting(true);
        return;
    }

    // Crafting tick — one game-minute per player action
    if (isCrafting) {
        craftMinutesLeft--;
        updateVisibility(6, 0); // narrow focus while crafting
        if (craftMinutesLeft <= 0) {
            isCrafting = false;
            if (player.addToContainer(craftPendingItem))
                panel.addMessage("You finish crafting " + craftPendingItem.name + ".");
            else {
                // No room — drop at feet
                groundItems.push_back({player.x, player.y, craftPendingItem});
                panel.addMessage("You finish crafting " + craftPendingItem.name
                                 + " but have no room — it drops at your feet.");
            }
            updateVisibility(worldTime.viewRadius(player.totalLightRadius()),
                             (int)(worldTime.darkness() * 7.0f));
        }
    }

    // Notify player when crossing hunger/thirst thresholds
    int newHungerLv = player.hungerLevel();
    int newThirstLv = player.thirstLevel();

    if (newHungerLv > prevHungerLv) {
        const char* msgs[] = {
            "", "You feel hungry.",
            "You are very hungry. Your strength wanes.",
            "You are starving! Your body is failing.",
            "You are dying of starvation!"
        };
        if (newHungerLv <= 4) panel.addMessage(msgs[newHungerLv]);
    }
    if (newThirstLv > prevThirstLv) {
        const char* msgs[] = {
            "", "You feel thirsty.",
            "You are very thirsty. Your body weakens.",
            "You are dying of thirst! Your vision blurs.",
            "You are dying of dehydration!"
        };
        if (newThirstLv <= 4) panel.addMessage(msgs[newThirstLv]);
    }

    if (!player.isAlive())
        panel.addMessage("You have died.");
}

void updateCamera();
void doTeleport(int newSX, int newSY);
void interactWithObject(int tx, int ty);
void spawnVillagers(bool isVillage);
void updateVillagers();
void tickVillagerNeeds();
void interruptCrafting(bool playerHit);

// ------------------------------------------------------------------ villagers

static const char* GREETINGS_DAY[] = {
    "Fine weather today, traveler.",
    "Welcome to our village.",
    "Have you come far?",
    "Mind the road at night — it's not safe.",
    "Good day to you.",
    nullptr
};
static const char* GREETINGS_NIGHT[] = {
    "Mmph... let me sleep.",
    "Zz...",
    "Come back in the morning.",
    nullptr
};

// The Elder speaks differently from ordinary villagers — with authority, not small talk.
static const char* ELDER_GREETINGS_DAY[] = {
    "Welcome, traveler. This village is under my care — mind you keep the peace.",
    "I've led this village through harder years than this one. What brings you here?",
    "Every soul in this village answers to me, one way or another.",
    "Speak your business plainly. I've no patience for riddles.",
    "Mine is the final word in this village. Remember that.",
    nullptr
};
static const char* ELDER_GREETINGS_NIGHT[] = {
    "Even an elder needs rest. Come back with the sun.",
    "The village sleeps under my watch. So should you.",
    nullptr
};

static int countStrings(const char** arr) {
    int n = 0; while (arr[n]) n++; return n;
}

void spawnVillagers(bool isVillage) {
    villagers.clear();
    villageWellX = villageWellY = -1;
    if (!isVillage) return;

    // Walkable tile beside the well — every village has exactly one, at map center.
    {
        int wx = MAP_WIDTH / 2, wy = MAP_HEIGHT / 2;
        const int DIRS[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
        for (auto& d : DIRS) {
            int nx = wx + d[0], ny = wy + d[1];
            if (nx < 1 || nx >= MAP_WIDTH-1 || ny < 1 || ny >= MAP_HEIGHT-1) continue;
            if (map[ny][nx].walkable()) { villageWellX = nx; villageWellY = ny; break; }
        }
    }

    // Collect all bed positions
    struct BedPos { int x, y; };
    std::vector<BedPos> beds;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (map[y][x].objectId == O_BED)
                beds.push_back({x, y});

    if (beds.empty()) return;

    // Pair nearby beds → same surname (same household, ≤ 8 tiles apart)
    int nSurnames = countStrings(NPC_SURNAMES);
    std::vector<int> surnameIdx(beds.size(), -1);
    int nextSurnameSlot = 0;
    for (int i = 0; i < (int)beds.size(); i++) {
        if (surnameIdx[i] >= 0) continue;
        surnameIdx[i] = nextSurnameSlot;
        for (int j = i + 1; j < (int)beds.size(); j++) {
            if (surnameIdx[j] >= 0) continue;
            int dist = std::max(std::abs(beds[i].x - beds[j].x),
                                std::abs(beds[i].y - beds[j].y));
            if (dist <= 8) { surnameIdx[j] = nextSurnameSlot; break; }
        }
        nextSurnameSlot++;
    }

    int nFirstNames = countStrings(NPC_FIRST_NAMES);
    // Track which first names were used per surname group
    std::map<int,std::vector<int>> usedFirst;

    for (int i = 0; i < (int)beds.size(); i++) {
        Villager v;
        v.bedX = beds[i].x;
        v.bedY = beds[i].y;

        // Find a walkable tile adjacent to the bed (bed itself blocksMove=true).
        v.sleepX = beds[i].x;
        v.sleepY = beds[i].y;
        {
            const int DIRS[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
            for (auto& d : DIRS) {
                int nx = beds[i].x + d[0], ny = beds[i].y + d[1];
                if (nx < 1 || nx >= MAP_WIDTH-1 || ny < 1 || ny >= MAP_HEIGHT-1) continue;
                Tile& t = map[ny][nx];
                // Count closed doors as passable (NPC will open them)
                bool passable = t.walkable() || t.objectId == O_DOOR_CLOSED;
                if (passable) { v.sleepX = nx; v.sleepY = ny; break; }
            }
        }

        v.x = v.sleepX;
        v.y = v.sleepY;

        // Pick unique first name per household
        int fnIdx;
        do { fnIdx = rand() % nFirstNames; }
        while (std::count(usedFirst[surnameIdx[i]].begin(),
                          usedFirst[surnameIdx[i]].end(), fnIdx) > 0
               && (int)usedFirst[surnameIdx[i]].size() < nFirstNames);
        usedFirst[surnameIdx[i]].push_back(fnIdx);

        int snIdx = surnameIdx[i] % nSurnames;
        v.name      = std::string(NPC_FIRST_NAMES[fnIdx]) + " " + NPC_SURNAMES[snIdx];
        v.color     = VILLAGER_COLORS[i % VILLAGER_COLOR_COUNT];
        v.greetIdx  = i % countStrings(GREETINGS_DAY);

        // Everything they carry lives in one real container — no floating inventory.
        // Starting food reserve: Farmer/Herbalist can top this up later by harvesting
        // their own field; everyone else only ever depletes it.
        v.bag = Items::backpack();
        {
            Item loaves = Items::bread();
            loaves.count = 6;
            addToContainer(*v.bag, std::move(loaves));
        }

        // Start sleeping if it's night, else place them near their bed
        if (worldTime.darkness() > 0.5f) {
            v.state = Villager::State::SLEEP;
        } else {
            v.state = Villager::State::WANDER;
            // Start a few tiles away so they look like they're already about their day
            for (int tries = 0; tries < 20; tries++) {
                int ox = beds[i].x + (rand() % 7) - 3;
                int oy = beds[i].y + (rand() % 7) - 3;
                if (ox >= 1 && ox < MAP_WIDTH-1 && oy >= 1 && oy < MAP_HEIGHT-1
                    && map[oy][ox].walkable()) {
                    v.x = ox; v.y = oy; break;
                }
            }
        }

        villagers.push_back(v);
    }

    // ---- Assign occupations, one primary worker per household ----------------
    // map.cpp's placeVillage() already decided what each building is (villageBuildings);
    // just match each household's bed to the nearest recorded building anchor.
    std::map<int, std::vector<int>> households; // surnameIdx -> villager indices
    for (int i = 0; i < (int)villagers.size(); i++)
        households[surnameIdx[i]].push_back(i);

    // Adds an occupation's goods into a villager's bag (real container, no floating items).
    auto giveOccupation = [](Villager& v, Occupation occ) {
        v.occupation = occ;
        if (!v.bag) return;
        for (Item& g : goodsFor(occ)) addToContainer(*v.bag, std::move(g));
    };

    for (auto& kv : households) {
        int primary = kv.second[0];

        BuildingRole role = BuildingRole::FARM;
        int bestDist = -1;
        for (const VillageBuildingInfo& info : villageBuildings) {
            int dx = beds[primary].x - info.bedX, dy = beds[primary].y - info.bedY;
            int d  = dx*dx + dy*dy;
            if (bestDist < 0 || d < bestDist) { bestDist = d; role = info.role; }
        }

        Occupation occ;
        switch (role) {
            case BuildingRole::FARM:           occ = Occupation::FARMER;     break;
            case BuildingRole::HERBALIST_FARM:  occ = Occupation::HERBALIST;  break;
            case BuildingRole::SMITHY:          occ = Occupation::BLACKSMITH; break;
            case BuildingRole::ELDER:           occ = Occupation::ELDER;      break;
            default:                            occ = Occupation::WOODCUTTER; break;
        }
        giveOccupation(villagers[primary], occ);

        // Rare chance: the spouse in a non-farm household becomes a Seamstress
        // instead of a plain helper.
        bool isFarmHousehold = (role == BuildingRole::FARM || role == BuildingRole::HERBALIST_FARM);
        if (!isFarmHousehold && kv.second.size() > 1 && (rand() % 100) < 20)
            giveOccupation(villagers[kv.second[1]], Occupation::SEAMSTRESS);
    }
}

// Build a home path using A*, treating closed doors as passable.
// Interrupt crafting, save partial item to inventory.
void interruptCrafting(bool playerHit) {
    if (!isCrafting) return;

    int minsDone = craftTotalMins - craftMinutesLeft;
    int pct      = craftTotalMins > 0 ? minsDone * 100 / craftTotalMins : 0;

    Item partial            = craftPendingItem;
    partial.isPartial       = true;
    partial.craftProgress   = minsDone;
    partial.craftTotalMins  = craftTotalMins;
    partial.craftRecipeName = craftPendingItem.name;
    partial.name            = craftPendingItem.name + " (" + std::to_string(pct) + "%)";

    isCrafting = false;
    updateVisibility(worldTime.viewRadius(player.totalLightRadius()),
                     (int)(worldTime.darkness() * 7.0f));

    std::string msg = playerHit ? "You were hit and lose focus! " : "You stop crafting. ";
    if (player.addToContainer(partial))
        panel.addMessage(msg + partial.name + " saved to inventory.");
    else {
        groundItems.push_back({player.x, player.y, partial});
        panel.addMessage(msg + partial.name + " dropped at your feet.");
    }
}

// Targets any walkable tile (sleepX/sleepY for home, the well tile for water) — never
// a blocksMove=true tile like the bed itself.
static void buildPathTo(Villager& v, int tx, int ty) {
    // Temporarily open all closed doors so A* can route through them.
    std::vector<SDL_Point> closedDoors;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (map[y][x].objectId == O_DOOR_CLOSED) {
                closedDoors.push_back({x, y});
                map[y][x].objectId = O_DOOR;
            }

    v.homePath    = findPath(v.x, v.y, tx, ty);
    v.homePathIdx = 1;

    // Restore closed doors.
    for (const SDL_Point& p : closedDoors)
        map[p.y][p.x].objectId = O_DOOR_CLOSED;
}

// Advances one step of v.homePath toward (tx,ty), (re)building the path as needed and
// opening/closing doors along the way. Returns true once the villager has arrived.
static bool followPath(Villager& v, int tx, int ty) {
    if (v.x == tx && v.y == ty) return true;

    // Cooldown prevents rebuilding a failed path every tick.
    if (v.pathRetryCool > 0) { v.pathRetryCool--; return false; }

    // Build path if we don't have one or exhausted it without arriving.
    if (v.homePath.empty() || v.homePathIdx >= (int)v.homePath.size()) {
        buildPathTo(v, tx, ty);
        if (v.homePath.empty()) {
            v.pathRetryCool = 30; // wait 30 ticks before retrying
            return false;
        }
    }

    if (v.homePathIdx < (int)v.homePath.size()) {
        SDL_Point next = v.homePath[v.homePathIdx];
        Tile& t = map[next.y][next.x];

        // Close any previously opened door once we've moved away from it
        if (v.lastDoorX >= 0 && (v.x != v.lastDoorX || v.y != v.lastDoorY)) {
            map[v.lastDoorY][v.lastDoorX].objectId = O_DOOR_CLOSED;
            v.lastDoorX = v.lastDoorY = -1;
        }

        // Open a closed door and remember it to close later
        if (t.objectId == O_DOOR_CLOSED) {
            t.objectId  = O_DOOR;
            v.lastDoorX = next.x;
            v.lastDoorY = next.y;
        }

        if (t.walkable()) {
            v.x = next.x;
            v.y = next.y;
            v.homePathIdx++;
        } else {
            // Tile became impassable — recompute next tick.
            v.homePath.clear();
        }
    }
    return false;
}

void updateVillagers() {
    float dark     = worldTime.darkness();
    float sunriseH = worldTime.sunriseHour();
    float curH     = (float)worldTime.hour() + worldTime.minute() / 60.0f;

    for (Villager& v : villagers) {
        if (!v.alive) continue;
        v.gainEnergy();
        if (!v.canAct()) continue;
        v.spendEnergy();

        // State transitions
        if (v.state == Villager::State::WANDER && dark > 0.5f) {
            v.state = Villager::State::WALK_HOME;
            v.homePath.clear(); // force path rebuild
        } else if (v.state == Villager::State::WANDER && v.thirst >= 0.6f) {
            v.state = Villager::State::DRINK; // thirst is the more urgent need
            v.homePath.clear();
        } else if (v.state == Villager::State::WANDER && v.hunger >= 0.6f) {
            v.state = Villager::State::EAT;
            v.homePath.clear();
        }
        if (v.state == Villager::State::SLEEP && curH >= sunriseH && dark <= 0.0f) {
            v.state = Villager::State::WANDER;
            v.x = v.sleepX; v.y = v.sleepY; // step off the bed tile
        }

        switch (v.state) {
            case Villager::State::WANDER: {
                // 40% chance to stay put (makes movement look natural)
                if (rand() % 10 < 4) break;
                int dx = (rand() % 3) - 1;
                int dy = (rand() % 3) - 1;
                if (dx == 0 && dy == 0) break;
                int nx = v.x + dx, ny = v.y + dy;
                // Stay within 18 tiles of home bed
                int homeDist = std::max(std::abs(nx - v.bedX), std::abs(ny - v.bedY));
                if (homeDist > 18) break;
                if (nx < 1 || nx >= MAP_WIDTH-1 || ny < 1 || ny >= MAP_HEIGHT-1) break;
                // Don't walk into player
                if (nx == player.x && ny == player.y) break;
                // Open doors the villager wanders into during the day
                if (map[ny][nx].objectId == O_DOOR_CLOSED)
                    map[ny][nx].objectId = O_DOOR;
                if (map[ny][nx].walkable()) { v.x = nx; v.y = ny; }
                break;
            }
            case Villager::State::WALK_HOME:
                if (followPath(v, v.sleepX, v.sleepY)) {
                    // Close any door we opened on the way in
                    if (v.lastDoorX >= 0) {
                        map[v.lastDoorY][v.lastDoorX].objectId = O_DOOR_CLOSED;
                        v.lastDoorX = v.lastDoorY = -1;
                    }
                    v.x = v.bedX; v.y = v.bedY; // lie down on the bed
                    v.state = Villager::State::SLEEP;
                }
                break;
            case Villager::State::EAT:
                if (followPath(v, v.sleepX, v.sleepY)) {
                    bool ate = false;
                    if (v.bag) {
                        auto& c = v.bag->contents;
                        for (int i = 0; i < (int)c.size(); i++) {
                            if (c[i].nutrition > 0) {
                                if (c[i].count > 1) c[i].count--;
                                else                 c.erase(c.begin() + i);
                                ate = true;
                                break;
                            }
                        }
                    }
                    if (!ate && (v.occupation == Occupation::FARMER || v.occupation == Occupation::HERBALIST)) {
                        // No stock left — try to harvest a mature crop from their own field.
                        int wantId = (v.occupation == Occupation::FARMER) ? O_WHEAT : O_HERB;
                        for (int dy = -6; dy <= 6 && !ate; dy++)
                            for (int dx = -6; dx <= 6 && !ate; dx++) {
                                int fx = v.bedX + dx, fy = v.bedY + dy;
                                if (fx < 0 || fx >= MAP_WIDTH || fy < 0 || fy >= MAP_HEIGHT) continue;
                                Tile& t = map[fy][fx];
                                if (t.objectId == wantId && t.plantAge >= 170) {
                                    t.plantAge = 0; // harvested — regrows over the season
                                    ate = true;
                                }
                            }
                    }
                    // If neither worked, hunger stays high — a real risk, not just flavor.
                    if (ate) v.hunger = 0.0f;
                    v.state = Villager::State::WANDER;
                }
                break;
            case Villager::State::DRINK:
                if (villageWellX < 0) { v.state = Villager::State::WANDER; break; }
                if (followPath(v, villageWellX, villageWellY)) {
                    v.thirst = 0.0f;
                    v.state  = Villager::State::WANDER;
                }
                break;
            case Villager::State::SLEEP:
                v.x = v.bedX; v.y = v.bedY; // stay on the bed
                break;
        }
    }
}

// Advances villager hunger/thirst by exactly one game-minute's worth, and applies
// starvation/dehydration damage once per crossed game-hour. Called once per game-minute
// from onPlayerAct()/the wait loop — NOT from updateVillagers(), whose energy-based
// per-actor loop doesn't fire at a fixed rate relative to real game time (same reason
// player.tickNeeds() lives outside tickWorld()).
void tickVillagerNeeds() {
    static int lastNeedsHour = -1;
    int curHour = worldTime.hour();
    bool hourCrossed = (lastNeedsHour != curHour);
    lastNeedsHour = curHour;

    for (Villager& v : villagers) {
        if (!v.alive) continue;
        v.tickNeeds(); // inherited from Actor — same rate constants as the player

        if (!hourCrossed) continue;
        bool wasThirstDeath = v.thirst >= 1.0f; // check before takeDamage() may push it further
        if (v.hunger >= 1.0f) v.takeDamage(1, PartTarget::TORSO);
        if (v.thirst >= 1.0f) v.takeDamage(2, PartTarget::TORSO);
        if (!v.alive) {
            panel.addMessage(v.name + " has died of " +
                             (wasThirstDeath ? "dehydration." : "starvation."));
            // Everything they had drops — same rule as enemy loot, nothing vanishes.
            dropBag(v, v.bag);
            corpses.push_back(makeCorpse(v, worldTime.minutes));
        }
    }
}

// ------------------------------------------------------------------ sector transition

void checkSectorTransition() {
    if (player.x > 0 && player.x < MAP_WIDTH - 1 &&
        player.y > 0 && player.y < MAP_HEIGHT - 1) return;

    int newSX = playerSectorX, newSY = playerSectorY;
    int newPX = player.x,     newPY = player.y;

    // Horizontal transitions take priority over vertical.
    if      (player.x == 0)             { newSX--; newPX = MAP_WIDTH  - 2; }
    else if (player.x == MAP_WIDTH - 1) { newSX++; newPX = 1;              }
    else if (player.y == 0)             { newSY--; newPY = MAP_HEIGHT - 2; }
    else if (player.y == MAP_HEIGHT -1) { newSY++; newPY = 1;              }

    // Clamp at world boundary вЂ" push player back inside.
    if (newSX < 0 || newSX >= OVERMAP_W || newSY < 0 || newSY >= OVERMAP_H) {
        player.x = std::max(1, std::min(MAP_WIDTH  - 2, player.x));
        player.y = std::max(1, std::min(MAP_HEIGHT - 2, player.y));
        return;
    }

    playerSectorX = newSX;
    playerSectorY = newSY;
    player.x = newPX;
    player.y = newPY;

    generateSector(overmap.sectors[playerSectorY][playerSectorX].biome,
                   playerSectorX, playerSectorY,
                   overmap.sectors[playerSectorY][playerSectorX].hasVillage);
    overmap.reveal(playerSectorX, playerSectorY);

    attackTarget = nullptr; // enemies.clear() below would otherwise dangle it
    enemies.clear();
    corpses.clear();
    initEnemy();
    spawnVillagers(overmap.sectors[playerSectorY][playerSectorX].hasVillage);
    currentPath.clear();
    pathIndex = 0;
    previewPath.clear();
    examinePanel.hide();
    tradePanel.hide();
    updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
    updateCamera();

    int bi = (int)overmap.sectors[playerSectorY][playerSectorX].biome;
    panel.addMessage(std::string("You enter: ") + biomeVisuals[bi].name + ".");
}

// ------------------------------------------------------------------ world interaction

// Walk to an adjacent walkable tile next to (tx,ty), then fire INTERACT pending action.
void walkAdjacentTo(int tx, int ty) {
    static const int dirs[8][2] = {
        {0,-1},{0,1},{-1,0},{1,0},{-1,-1},{1,-1},{-1,1},{1,1}
    };
    int bestDist = INT_MAX, bestX = -1, bestY = -1;
    for (auto& d : dirs) {
        int nx = tx + d[0], ny = ty + d[1];
        if (nx < 1 || nx >= MAP_WIDTH-1 || ny < 1 || ny >= MAP_HEIGHT-1) continue;
        if (!map[ny][nx].walkable()) continue;
        int dist = (nx - player.x)*(nx - player.x) + (ny - player.y)*(ny - player.y);
        if (dist < bestDist) { bestDist = dist; bestX = nx; bestY = ny; }
    }
    if (bestX < 0) return;
    pendingAct  = PendingAct::INTERACT;
    pendingActX = tx; pendingActY = ty;
    currentPath = findPath(player.x, player.y, bestX, bestY);
    pathIndex   = 1;
}

void interactWithObject(int tx, int ty) {
    Tile& tile = map[ty][tx];
    if (tile.objectId < 0) return;

    int oid = tile.objectId;

    // Door toggle
    if (oid == O_DOOR || oid == O_DOOR_CLOSED) {
        bool opening = (oid == O_DOOR_CLOSED);
        tile.objectId = opening ? O_DOOR : O_DOOR_CLOSED;
        panel.addMessage(opening ? "You open the door." : "You close the door.");
        updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
        onPlayerAct();
        return;
    }

    // Well — drink
    if (oid == O_WELL) {
        float before = player.thirst;
        player.thirst = std::max(0.0f, player.thirst - 0.7f);
        if (before > 0.0f)
            panel.addMessage("You drink deeply from the well. Refreshing!");
        else
            panel.addMessage("You are not thirsty.");
        onPlayerAct();
        return;
    }

    // Plants — instant harvest if mature, otherwise reject
    if (objectDefs[oid].isPlant) {
        if (tile.plantAge < 170) {
            panel.addMessage("The " + std::string(objectDefs[oid].name) + " is still growing.");
            return;
        }
        tile.objectId = -1;
        tile.objectHp = 0;
        tile.plantAge = 0;

        // Harvested plants go directly to inventory; overflow falls to the ground.
        auto gather = [&](Item item) {
            if (!player.addToContainer(item))
                groundItems.push_back({tx, ty, item});
        };

        switch (oid) {
            case O_BUSH:
                gather(Items::berries());
                for (int k = 0; k < rand() % 2 + 1; k++) gather(Items::branch());
                panel.addMessage("You strip the bush clean.");
                break;
            case O_WHEAT:
                gather(Items::grain());
                panel.addMessage("You harvest the wheat.");
                break;
            case O_HERB:
                gather(Items::herb());
                panel.addMessage("You gather the herbs.");
                break;
            case O_MUSHROOM:
                gather(Items::mushroom());
                panel.addMessage("You pick the mushrooms.");
                break;
        }

        currentPath.clear(); pathIndex = 0; previewPath.clear();
        updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
        onPlayerAct();
        return;
    }

    // Tool requirement checks
    bool needsAxe  = (oid == O_TREE || oid == O_DEAD_TREE || oid == O_FALLEN_LOG ||
                      oid == O_LAMP || oid == O_TABLE || oid == O_BED ||
                      oid == O_BARREL || oid == O_WINDOW);
    bool needsPick = (oid == O_ROCK || oid == O_BOULDER ||
                      oid == O_WALL || oid == O_FIREPLACE);

    if (needsAxe && !player.hasChopTool()) {
        panel.addMessage("You need an axe to chop this.");
        return;
    }
    if (needsPick && !player.hasMineTool()) {
        panel.addMessage("You need a pickaxe to mine this.");
        return;
    }

    const ObjectDef& od = objectDefs[tile.objectId];
    int hitDmg = 20 + std::max(0, (player.effectiveStr() - 10) * 2);
    tile.objectHp -= hitDmg;

    if (tile.objectHp <= 0) {
        tile.objectId = -1;
        tile.objectHp = 0;

        auto drop = [&](Item item) {
            groundItems.push_back({tx, ty, std::move(item)});
        };

        switch (oid) {
            case O_TREE:
                for (int k = 0; k < rand() % 2 + 2; k++) drop(Items::woodLog());
                for (int k = 0; k < rand() % 2 + 1; k++) drop(Items::branch());
                panel.addMessage("The tree falls with a crash!");
                break;
            case O_DEAD_TREE:
                for (int k = 0; k < rand() % 2 + 1; k++) drop(Items::woodLog());
                drop(Items::branch());
                panel.addMessage("The dead tree splinters and collapses.");
                break;
            case O_ROCK:
                for (int k = 0; k < rand() % 2 + 2; k++) drop(Items::stonePiece());
                panel.addMessage("The rock breaks apart.");
                break;
            case O_BOULDER:
                for (int k = 0; k < rand() % 3 + 4; k++) drop(Items::stonePiece());
                panel.addMessage("The boulder finally shatters!");
                break;
            case O_FALLEN_LOG:
                for (int k = 0; k < rand() % 2 + 1; k++) drop(Items::woodLog());
                panel.addMessage("You chop the log into sections.");
                break;
            default:
                panel.addMessage("The " + std::string(objectDefs[oid].name) + " breaks apart.");
                break;
        }

        currentPath.clear(); pathIndex = 0; previewPath.clear();
        updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
    } else {
        panel.addMessage("You strike the " + std::string(od.name)
                       + ". (" + std::to_string(tile.objectHp)
                       + "/" + std::to_string(od.durability) + " HP)");
    }

    onPlayerAct();
}

void doTeleport(int newSX, int newSY) {
    playerSectorX = newSX;
    playerSectorY = newSY;
    player.x = MAP_WIDTH  / 2;
    player.y = MAP_HEIGHT / 2;

    generateSector(overmap.sectors[playerSectorY][playerSectorX].biome,
                   playerSectorX, playerSectorY,
                   overmap.sectors[playerSectorY][playerSectorX].hasVillage);
    overmap.reveal(playerSectorX, playerSectorY);
    attackTarget = nullptr; // enemies.clear() below would otherwise dangle it
    enemies.clear();
    corpses.clear();
    initEnemy();
    spawnVillagers(overmap.sectors[playerSectorY][playerSectorX].hasVillage);
    currentPath.clear();
    pathIndex = 0;
    previewPath.clear();
    examinePanel.hide();
    tradePanel.hide();
    updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
    updateCamera();

    int bi = (int)overmap.sectors[playerSectorY][playerSectorX].biome;
    panel.addMessage("Teleported to [" + std::to_string(newSX) + ", "
                     + std::to_string(newSY) + "] - " + biomeVisuals[bi].name + ".");
}

// ------------------------------------------------------------------ input

void handleInput(SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) running = false;

    // Backtick (~) opens/closes the cheat console from anywhere.
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKQUOTE) {
        if (console.visible) console.close();
        else                 console.open();
        // The same keypress also queues an SDL_TEXTINPUT("`") right behind this
        // event once text input is active; discard it so it doesn't land in input.
        SDL_FlushEvent(SDL_TEXTINPUT);
        return;
    }

    // Console intercepts all input while open (except the backtick above).
    if (console.handleEvent(event, overmap, worldTime)) return;

    // Trade panel intercepts input while visible.
    if (tradePanel.visible) {
        if (event.type == SDL_MOUSEMOTION)
            tradePanel.handleMotion(event.motion.x, event.motion.y, player, villagers);
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            std::string msg = tradePanel.handleClick(event.button.x, event.button.y, player, villagers);
            if (!msg.empty()) panel.addMessage(msg);
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            tradePanel.hide();
            panel.addMessage("You leave the trade.");
        }
        return;
    }

    // Pickup panel intercepts keyboard while visible.
    if (pickupPanel.visible) {
        if (event.type == SDL_KEYDOWN)
            pickupPanel.handleKey(event.key.keysym.sym);
        return;
    }

    // Needs-warning confirm popup intercepts input while visible (mid-wait pause).
    if (needsConfirmPanel.visible) {
        int r = -2;
        if (event.type == SDL_KEYDOWN)
            r = needsConfirmPanel.handleKey(event.key.keysym.sym);
        if (event.type == SDL_MOUSEMOTION)
            needsConfirmPanel.handleMotion(event.motion.x, event.motion.y);
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
            r = needsConfirmPanel.handleClick(event.button.x, event.button.y);
        if (r == 1) {
            waitPaused = false; // resume fast-forwarding
        } else if (r == 0) {
            isWaiting  = false;
            waitPaused = false;
            panel.addMessage("You stop waiting.");
        }
        return;
    }

    // Wait panel intercepts input while visible.
    if (waitPanel.visible) {
        if (event.type == SDL_KEYDOWN) {
            int t = waitPanel.handleKey(event.key.keysym.sym);
            if (t >= 0) {
                isWaiting         = true;
                waitTargetMinutes = t;
                waitPaused        = false;
                waitPrevHungerLv  = player.hungerLevel();
                waitPrevThirstLv  = player.thirstLevel();
            }
        }
        if (event.type == SDL_MOUSEMOTION)
            waitPanel.handleMotion(event.motion.x, event.motion.y);
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            int t = waitPanel.handleClick(event.button.x, event.button.y);
            if (t >= 0) {
                isWaiting         = true;
                waitTargetMinutes = t;
                waitPaused        = false;
                waitPrevHungerLv  = player.hungerLevel();
                waitPrevThirstLv  = player.thirstLevel();
            }
        }
        return;
    }

    // Craft panel intercepts input while visible.
    if (craftPanel.visible()) {
        if (event.type == SDL_KEYDOWN) {
            Item outItem; int outMins = 0;
            if (craftPanel.handleKey(event.key.keysym.sym, player, outItem, outMins)) {
                isCrafting       = true;
                craftMinutesLeft = outMins;
                craftTotalMins   = outMins;
                craftPendingItem = outItem;
                panel.addMessage("You begin crafting " + outItem.name
                                 + "... (" + std::to_string(outMins) + " min)");
            }
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            Item outItem; int outMins = 0;
            if (craftPanel.handleClick(event.button.x, event.button.y, player, outItem, outMins)) {
                isCrafting       = true;
                craftMinutesLeft = outMins;
                craftTotalMins   = outMins;
                craftPendingItem = outItem;
                panel.addMessage("You begin crafting " + outItem.name
                                 + "... (" + std::to_string(outMins) + " min)");
            }
        }
        return;
    }

    // Overmap handles arrow keys and M while open.
    if (overmap.visible) {
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_m:     overmap.close(); break;
                case SDLK_UP:    overmap.moveCam( 0, -1); break;
                case SDLK_DOWN:  overmap.moveCam( 0,  1); break;
                case SDLK_LEFT:  overmap.moveCam(-1,  0); break;
                case SDLK_RIGHT: overmap.moveCam( 1,  0); break;
                default: break;
            }
        }
        return;
    }

    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_o)      ui.toggle();
        if (event.key.keysym.sym == SDLK_b)      bodyPanel.toggle();
        if (event.key.keysym.sym == SDLK_s)      effectsPanel.toggle();
        if (event.key.keysym.sym == SDLK_e)      examinePanel.hide();
        if (event.key.keysym.sym == SDLK_m)      overmap.open(playerSectorX, playerSectorY);
        if (event.key.keysym.sym == SDLK_i)      inventoryPanel.toggle();
        if (event.key.keysym.sym == SDLK_c)      craftPanel.open();
        if (event.key.keysym.sym == SDLK_t && !isCrafting && !isWaiting)
            waitPanel.show(worldTime);
        if (event.key.keysym.sym == SDLK_g && !isCrafting) pickUpAtPlayer();
        // Block most actions while crafting — only allow waiting (advances crafting)
        if (isCrafting) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                interruptCrafting(false);
            } else if (event.key.keysym.sym == SDLK_SPACE) {
                onPlayerAct(); // manually advance one minute
            }
            return;
        }
        if (event.key.keysym.sym == SDLK_SPACE &&
            !pickupPanel.visible && !contextMenu.visible)
            onPlayerAct();  // wait one turn
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            if (inventoryPanel.visible) inventoryPanel.close();
            else                        running = false;
        }
    }

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        int mouseX = event.button.x / TILE_SIZE + cameraX;
        int mouseY = event.button.y / TILE_SIZE + cameraY;

        // Any click closes examine panels.
        if (effectsPanel.visible) {
            effectsPanel.hide();
            return;
        }
        if (itemExaminePanel.visible) {
            itemExaminePanel.handleClick(event.button.x, event.button.y);
            return;
        }
        if (enemyExaminePanel.visible) {
            enemyExaminePanel.hide();
            return;
        }
        if (villagerExaminePanel.visible) {
            villagerExaminePanel.hide();
            return;
        }

        // Pickup panel handles its own clicks (item toggle, button, outside-close).
        if (pickupPanel.visible) {
            pickupPanel.handleMouseClick(event.button.x, event.button.y);
            return;
        }

        // Any click closes the tile examine panel.
        if (examinePanel.visible) {
            examinePanel.hide();
            return;
        }

        // Context menu has priority вЂ" handles menu items from inventory too.
        if (contextMenu.visible) {
            contextMenu.handleClick(event.button.x, event.button.y);
            return;
        }

        // Inventory panel вЂ" consumes all clicks while open.
        if (inventoryPanel.visible) {
            inventoryPanel.handleClick(event.button.x, event.button.y,
                                       player, contextMenu, groundItems,
                                       &itemExaminePanel);
            return;
        }

        if (event.button.button == SDL_BUTTON_LEFT) {
            if (mouseX >= 0 && mouseX < MAP_WIDTH && mouseY >= 0 && mouseY < MAP_HEIGHT) {
                Enemy* enemy = getEnemyAt(mouseX, mouseY);
                int attackDist = std::max(std::abs(mouseX - player.x),
                                          std::abs(mouseY - player.y));
                if (enemy && map[mouseY][mouseX].visible) {
                    if (attackDist <= 1) {
                        PartTarget part   = randomHitPart();
                        int        damage = 5 + (player.effectiveStr() - 10) / 2 + player.weaponDamage() - 1;
                        if (part == PartTarget::HEAD) damage = (int)(damage * 1.5f);
                        enemy->takeDamage(damage, part);
                        panel.addMessage("You hit " + enemy->name + "'s " + partName(part)
                                         + " for " + std::to_string(damage) + " damage.");
                        if (!enemy->isAlive()) {
                            panel.addMessage(enemy->name + " dies.");
                            dropEnemyLoot(*enemy);
                        }
                        onPlayerAct();
                    } else {
                        // Walk toward enemy — path recalculates each step via attackTarget
                        attackTarget = enemy;
                        pendingAct   = PendingAct::NONE;
                        currentPath  = findPath(player.x, player.y, mouseX, mouseY);
                        pathIndex    = 1;
                    }
                } else if (map[mouseY][mouseX].walkable()) {
                    currentPath = findPath(player.x, player.y, mouseX, mouseY);
                    pathIndex = 1;
                }
            }
        }

        if (event.button.button == SDL_BUTTON_RIGHT) {
            if (mouseX >= 0 && mouseX < MAP_WIDTH && mouseY >= 0 && mouseY < MAP_HEIGHT) {
                Enemy* enemy = getEnemyAt(mouseX, mouseY);
                if (enemy && map[mouseY][mouseX].visible) {
                    int ex = mouseX, ey = mouseY;
                    contextMenu.show(event.button.x, event.button.y, {
                        {"Attack", [enemy, ex, ey]() {
                            int dist = std::max(std::abs(ex - player.x),
                                                std::abs(ey - player.y));
                            if (dist <= 1) {
                                PartTarget part   = randomHitPart();
                                int        damage = 5 + (player.effectiveStr() - 10) / 2 + player.weaponDamage() - 1;
                                if (part == PartTarget::HEAD) damage = (int)(damage * 1.5f);
                                enemy->takeDamage(damage, part);
                                panel.addMessage("You hit " + enemy->name + "'s " + partName(part)
                                                 + " for " + std::to_string(damage) + " damage.");
                                if (!enemy->isAlive()) {
                                    panel.addMessage(enemy->name + " dies.");
                                    dropEnemyLoot(*enemy);
                                }
                                onPlayerAct();
                            } else {
                                attackTarget = enemy;
                                pendingAct   = PendingAct::NONE;
                                currentPath  = findPath(player.x, player.y, ex, ey);
                                pathIndex    = 1;
                            }
                        }},
                        {"Examine", [enemy]() {
                            enemyExaminePanel.show(*enemy);
                        }}
                    });
                } else {
                    // Non-enemy tile: show context menu if explored
                    if (map[mouseY][mouseX].explored) {
                        int mx = mouseX, my = mouseY;
                        std::vector<MenuItem> items;

                        // Villager interaction
                        for (Villager& v : villagers) {
                            if (!v.alive || v.x != mx || v.y != my) continue;
                            if (!map[my][mx].visible) break;
                            bool sleeping = (v.state == Villager::State::SLEEP);
                            std::string talkLabel = "Talk to " + v.name;
                            bool isElder = (v.occupation == Occupation::ELDER);
                            std::string greeting  = sleeping
                                ? (isElder ? ELDER_GREETINGS_NIGHT[v.greetIdx % countStrings(ELDER_GREETINGS_NIGHT)]
                                           : GREETINGS_NIGHT      [v.greetIdx % countStrings(GREETINGS_NIGHT)])
                                : (isElder ? ELDER_GREETINGS_DAY  [v.greetIdx % countStrings(ELDER_GREETINGS_DAY)]
                                           : GREETINGS_DAY        [v.greetIdx % countStrings(GREETINGS_DAY)]);
                            items.push_back({talkLabel,
                                [greeting, vname = v.name, sleeping]() {
                                    if (sleeping)
                                        panel.addMessage(vname + ": " + greeting);
                                    else
                                        panel.addMessage(vname + " says: \"" + greeting + "\"");
                                }});
                            items.push_back({"Examine " + v.name,
                                [vsnap = v]() {
                                    villagerExaminePanel.show(vsnap);
                                }});
                            bool canTrade = v.occupation != Occupation::NONE
                                          && v.occupation != Occupation::ELDER;
                            if (canTrade && !sleeping) {
                                int vi = (int)(&v - &villagers[0]);
                                items.push_back({"Trade with " + v.name
                                                 + " (" + occupationName(v.occupation) + ")",
                                    [vi]() { tradePanel.show(vi); }});
                            }
                            break; // one NPC per tile is enough
                        }

                        // Gather corpse info for Examine (captured by value — safe if vector reallocates)
                        std::string corpseNameHere;
                        bool        corpseFreshHere = false;
                        bool        hasCorpseHere   = false;
                        for (const Corpse& c : corpses) {
                            if (c.x == mx && c.y == my && map[my][mx].visible) {
                                corpseNameHere  = c.name;
                                corpseFreshHere = c.isFresh(worldTime.minutes);
                                hasCorpseHere   = true;
                                break;
                            }
                        }

                        // Ground item(s): pick up option (walk to item if needed)
                        GroundItem* gi = getGroundItemAt(mx, my);
                        if (gi && map[my][mx].visible) {
                            std::vector<Item> here = getGroundItemsAt(mx, my);
                            int dist = std::max(std::abs(mx - player.x),
                                                std::abs(my - player.y));
                            if (here.size() == 1) {
                                items.push_back({"Pick up " + gi->item.name,
                                    [gx=mx, gy=my, dist]() {
                                        if (dist <= 1) {
                                            std::vector<bool> sel = {true};
                                            doPickup(gx, gy, sel);
                                        } else {
                                            pendingAct  = PendingAct::PICKUP_ONE;
                                            pendingActX = gx; pendingActY = gy;
                                            currentPath = findPath(player.x, player.y, gx, gy);
                                            pathIndex   = 1;
                                        }
                                    }
                                });
                            } else {
                                std::string label = "Pick up items... (" + std::to_string(here.size()) + ")";
                                items.push_back({label,
                                    [gx=mx, gy=my, dist]() {
                                        if (dist <= 1) {
                                            auto h = getGroundItemsAt(gx, gy);
                                            pickupPanel.show(gx, gy, std::move(h));
                                            pickupPanel.onConfirm = [gx, gy](const std::vector<bool>& sel) {
                                                doPickup(gx, gy, sel);
                                            };
                                        } else {
                                            pendingAct  = PendingAct::PICKUP_PANEL;
                                            pendingActX = gx; pendingActY = gy;
                                            currentPath = findPath(player.x, player.y, gx, gy);
                                            pathIndex   = 1;
                                        }
                                    }
                                });
                            }
                        }

                        if (map[my][mx].walkable()) {
                            items.push_back({"Move here", [mx, my]() {
                                pendingAct   = PendingAct::NONE;
                                attackTarget = nullptr;
                                currentPath  = findPath(player.x, player.y, mx, my);
                                pathIndex    = 1;
                            }});
                        }

                        // World interaction — walk to object first if needed
                        {
                            int oid = map[my][mx].objectId;
                            if (oid >= 0 && map[my][mx].visible) {
                                if (oid == O_DOOR || oid == O_DOOR_CLOSED) {
                                    // Door: open or close
                                    bool isOpen = (oid == O_DOOR);
                                    items.push_back({isOpen ? "Close Door" : "Open Door",
                                        [mx, my]() {
                                            int dist = std::max(std::abs(mx - player.x),
                                                                std::abs(my - player.y));
                                            if (dist <= 1) interactWithObject(mx, my);
                                            else           walkAdjacentTo(mx, my);
                                        }});
                                } else if (oid == O_WELL) {
                                    items.push_back({"Drink from well", [mx, my]() {
                                        int dist = std::max(std::abs(mx - player.x),
                                                            std::abs(my - player.y));
                                        if (dist <= 1) interactWithObject(mx, my);
                                        else           walkAdjacentTo(mx, my);
                                    }});
                                } else if (objectDefs[oid].isPlant) {
                                    bool mature = (map[my][mx].plantAge >= 170);
                                    std::string label = mature
                                        ? ("Harvest " + std::string(objectDefs[oid].name))
                                        : (std::string(objectDefs[oid].name) + " (growing...)");
                                    items.push_back({label, [mx, my, mature]() {
                                        if (!mature) {
                                            panel.addMessage("This plant is not yet ready to harvest.");
                                            return;
                                        }
                                        int dist = std::max(std::abs(mx - player.x),
                                                            std::abs(my - player.y));
                                        if (dist <= 1) {
                                            pendingAct  = PendingAct::AUTO_INTERACT;
                                            pendingActX = mx; pendingActY = my;
                                        } else {
                                            walkAdjacentTo(mx, my);
                                            pendingAct = PendingAct::AUTO_INTERACT;
                                        }
                                    }});
                                } else if (objectDefs[oid].durability > 0) {
                                    // Destructible objects: chop/mine/break — auto-repeats until done
                                    const char* verb =
                                        (oid == O_ROCK || oid == O_BOULDER || oid == O_WALL || oid == O_FIREPLACE) ? "Mine" :
                                        (oid == O_TREE || oid == O_DEAD_TREE || oid == O_FALLEN_LOG)               ? "Chop" :
                                        "Break";
                                    std::string label = std::string(verb)
                                                      + " " + objectDefs[oid].name
                                                      + " (" + std::to_string(map[my][mx].objectHp)
                                                      + "/" + std::to_string(objectDefs[oid].durability)
                                                      + ")";
                                    items.push_back({label, [mx, my]() {
                                        int dist = std::max(std::abs(mx - player.x),
                                                            std::abs(my - player.y));
                                        if (dist <= 1) {
                                            pendingAct  = PendingAct::AUTO_INTERACT;
                                            pendingActX = mx; pendingActY = my;
                                        } else {
                                            walkAdjacentTo(mx, my);
                                            pendingAct = PendingAct::AUTO_INTERACT;
                                        }
                                    }});
                                }
                            }
                        }

                        // Examine — tile info + items + corpse if present
                        {
                            bool hasCN = hasCorpseHere;
                            std::string cn = corpseNameHere;
                            bool cf = corpseFreshHere;
                            bool hasGI = (gi && map[my][mx].visible);
                            MenuItem examItem;
                            static const char examLbl[] = {'E','x','a','m','i','n','e','\0'};
                            examItem.label = examLbl;
                            examItem.action = [mx, my, hasCN, cn, cf, hasGI]() {
                                if (hasGI)
                                    examinePanel.show(mx, my, getGroundItemsAt(mx, my));
                                else
                                    examinePanel.show(mx, my);
                                examinePanel.hasCorpse   = hasCN;
                                examinePanel.corpseName  = cn;
                                examinePanel.corpseFresh = cf;
                            };
                            items.push_back(std::move(examItem));
                        }
                        contextMenu.show(event.button.x, event.button.y, items);
                    } else {
                        currentPath.clear();
                        pathIndex = 0;
                        previewPath.clear();
                        lastHoverX = -1;
                        lastHoverY = -1;
                    }
                }
            }
        }
    }

    if (event.type == SDL_MOUSEMOTION) {
        hoverX = event.motion.x / TILE_SIZE + cameraX;
        hoverY = event.motion.y / TILE_SIZE + cameraY;
        if (pickupPanel.visible)
            pickupPanel.handleMouseMotion(event.motion.x, event.motion.y);
    }
}

// ------------------------------------------------------------------ update

// Advances the player one step along currentPath (with visual pacing).
// If the player has extra energy (speed > 100), the next call will act again
// before the world gets another tick вЂ" producing CDDA-style multi-actions.
bool updatePlayer() {
    // AUTO_INTERACT in-place: player is already adjacent, no path needed — keep firing each frame.
    if (pendingAct == PendingAct::AUTO_INTERACT && currentPath.empty()) {
        Uint32 now = SDL_GetTicks();
        if (now - lastMoveTime < 100) return false;
        lastMoveTime = now;

        while (player.energy < 100) tickWorld();

        int dist = std::max(std::abs(player.x - pendingActX),
                            std::abs(player.y - pendingActY));
        if (dist <= 1) {
            int prevObj = map[pendingActY][pendingActX].objectId;
            int prevHp  = map[pendingActY][pendingActX].objectHp;
            interactWithObject(pendingActX, pendingActY);
            int newObj = map[pendingActY][pendingActX].objectId;
            int newHp  = map[pendingActY][pendingActX].objectHp;
            if (!(newObj == prevObj && newHp < prevHp && newHp > 0)) {
                pendingAct  = PendingAct::NONE;
                pendingActX = pendingActY = -1;
            }
        } else {
            pendingAct  = PendingAct::NONE;
            pendingActX = pendingActY = -1;
        }
        return true;
    }

    if (pathIndex >= (int)currentPath.size()) return false;

    // Visual pacing: one rendered step per 100 ms regardless of game speed.
    Uint32 now = SDL_GetTicks();
    if (now - lastMoveTime < 100) return false;
    lastMoveTime = now;

    // World ticks until player has enough energy to act (handles speed < 100).
    while (player.energy < 100) tickWorld();

    SDL_Point next = currentPath[pathIndex];

    // Attack enemy blocking the path instead of moving.
    Enemy* blocker = getEnemyAt(next.x, next.y);
    if (blocker && blocker->alive) {
        PartTarget part   = randomHitPart();
        int        damage = 5 + (player.effectiveStr() - 10) / 2 + player.weaponDamage() - 1;
        if (part == PartTarget::HEAD) damage = (int)(damage * 1.5f);
        blocker->takeDamage(damage, part);
        panel.addMessage("You hit " + blocker->name + "'s " + partName(part)
                         + " for " + std::to_string(damage) + " damage.");
        if (!blocker->isAlive()) {
            panel.addMessage(blocker->name + " dies.");
            dropEnemyLoot(*blocker);
        }
        currentPath.clear();
        pathIndex = 0;
        pendingAct   = PendingAct::NONE;
        attackTarget = nullptr;
        onPlayerAct();
        return true;
    }

    // Tile became unwalkable (wall spawned, etc.).
    if (!map[next.y][next.x].walkable()) {
        currentPath.clear();
        pathIndex = 0;
        return false;
    }

    player.x = next.x;
    player.y = next.y;
    pathIndex++;
    onPlayerAct();

    // If chasing an enemy, recalculate path to their current position each step.
    if (attackTarget) {
        if (!attackTarget->alive) {
            attackTarget = nullptr;
        } else {
            currentPath = findPath(player.x, player.y,
                                   attackTarget->x, attackTarget->y);
            pathIndex = (currentPath.size() >= 2) ? 1 : (int)currentPath.size();
        }
    }

    if (pathIndex >= (int)currentPath.size()) {
        currentPath.clear();
        pathIndex = 0;
        lastHoverX = -1;
        lastHoverY = -1;
        previewPath.clear();

        attackTarget = nullptr;
        if (pendingAct != PendingAct::NONE) {
            int dist = std::max(std::abs(player.x - pendingActX),
                                std::abs(player.y - pendingActY));
            if (dist <= 1) {
                bool keepPending = false;
                switch (pendingAct) {
                    case PendingAct::PICKUP_ONE: {
                        std::vector<bool> sel = {true};
                        doPickup(pendingActX, pendingActY, sel);
                        break;
                    }
                    case PendingAct::PICKUP_PANEL: {
                        auto here = getGroundItemsAt(pendingActX, pendingActY);
                        if (!here.empty()) {
                            int px = pendingActX, py = pendingActY;
                            pickupPanel.show(px, py, std::move(here));
                            pickupPanel.onConfirm = [px, py](const std::vector<bool>& sel) {
                                doPickup(px, py, sel);
                            };
                        }
                        break;
                    }
                    case PendingAct::INTERACT:
                        interactWithObject(pendingActX, pendingActY);
                        break;
                    case PendingAct::AUTO_INTERACT: {
                        int prevObj = map[pendingActY][pendingActX].objectId;
                        int prevHp  = map[pendingActY][pendingActX].objectHp;
                        interactWithObject(pendingActX, pendingActY);
                        int newObj = map[pendingActY][pendingActX].objectId;
                        int newHp  = map[pendingActY][pendingActX].objectHp;
                        // Keep repeating if same object and HP was reduced
                        if (newObj == prevObj && newHp < prevHp && newHp > 0)
                            keepPending = true;
                        break;
                    }
                    default: break;
                }
                if (!keepPending) {
                    pendingAct  = PendingAct::NONE;
                    pendingActX = pendingActY = -1;
                }
            }
        }
    }

    return true;
}

void renderDeathScreen(SDL_Renderer* renderer, TTF_Font* font) {
    // Dark overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // "YOU DIED" text вЂ" rendered twice for a shadow effect
    TTF_Font* bigFont = TTF_OpenFont("fonts/DejaVuSansMono.ttf", 48);
    if (bigFont) {
        auto renderCentered = [&](const char* text, int y, SDL_Color col) {
            SDL_Surface* s = TTF_RenderText_Solid(bigFont, text, col);
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FreeSurface(s);
            int w, h;
            SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
            SDL_Rect dst = {(SCREEN_WIDTH - w) / 2, y, w, h};
            SDL_RenderCopy(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        };
        renderCentered("YOU DIED", MAP_VIEW_HEIGHT / 2 - 60, {80, 0, 0, 255});
        renderCentered("YOU DIED", MAP_VIEW_HEIGHT / 2 - 62, {200, 0, 0, 255});
        TTF_CloseFont(bigFont);
    }

    // Subtitle
    SDL_Surface* s = TTF_RenderText_Solid(font, "Press Escape to quit", {150, 150, 150, 255});
    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    int w, h;
    SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
    SDL_Rect dst = {(SCREEN_WIDTH - w) / 2, MAP_VIEW_HEIGHT / 2, w, h};
    SDL_RenderCopy(renderer, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

void updateCamera() {
    const int tilesX = SCREEN_WIDTH    / TILE_SIZE;
    const int tilesY = MAP_VIEW_HEIGHT / TILE_SIZE;
    cameraX = player.x - tilesX / 2;
    cameraY = player.y - tilesY / 2;
    if (cameraX < 0) cameraX = 0;
    if (cameraY < 0) cameraY = 0;
    if (cameraX > MAP_WIDTH  - tilesX) cameraX = MAP_WIDTH  - tilesX;
    if (cameraY > MAP_HEIGHT - tilesY) cameraY = MAP_HEIGHT - tilesY;
}

// ------------------------------------------------------------------ main

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO);
    TTF_Init();
    srand((unsigned int)time(nullptr));
    overmap.generate();
    overmap.reveal(playerSectorX, playerSectorY);
    generateSector(overmap.sectors[playerSectorY][playerSectorX].biome,
                   playerSectorX, playerSectorY,
                   overmap.sectors[playerSectorY][playerSectorX].hasVillage);
    initEnemy();
    spawnVillagers(overmap.sectors[playerSectorY][playerSectorX].hasVillage);

    // Starting equipment
    player.worn[(int)EquipSlot::BACK]  = Items::backpack();
    player.worn[(int)EquipSlot::WAIST] = Items::beltPouch();
    player.addToContainer(Items::bread());
    player.addToContainer(Items::waterFlask());
    {
        Item startingGold = Items::goldCoin();
        startingGold.count = 30;
        player.addToContainer(std::move(startingGold));
    }

    // Scatter a few items on the ground near the player for testing
    groundItems.push_back({player.x + 2, player.y,     Items::ironSword()});
    groundItems.push_back({player.x - 2, player.y,     Items::goldRing()});
    groundItems.push_back({player.x,     player.y + 2, Items::ironHelmet()});
    groundItems.push_back({player.x + 1, player.y + 1, Items::leatherVest()});
    groundItems.push_back({player.x - 1, player.y - 1, Items::leatherBoots()});
    groundItems.push_back({player.x + 1, player.y,     Items::torch()});
    groundItems.push_back({player.x - 1, player.y,     Items::lantern()});

    // Give everyone starting energy so they're ready to act immediately.
    player.energy = player.speed;
    for (Enemy& e : enemies) e.energy = e.speed;

    // Wire up inventory callbacks.
    inventoryPanel.onMessage = [](const std::string& msg) { panel.addMessage(msg); };
    inventoryPanel.onAct     = []() { onPlayerAct(); };

    SDL_Window* window = SDL_CreateWindow("Greystone",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    TTF_Font* font = TTF_OpenFont("fonts/DejaVuSansMono.ttf", 16);

    SDL_Surface* playerSurface = TTF_RenderText_Solid(font, player.symbol, player.color);
    playerTexture = SDL_CreateTextureFromSurface(renderer, playerSurface);
    SDL_FreeSurface(playerSurface);

    SDL_Surface* enemySurface = TTF_RenderText_Solid(font, "E", red);
    enemyTexture = SDL_CreateTextureFromSurface(renderer, enemySurface);
    SDL_FreeSurface(enemySurface);

    initTextures(renderer, font);
    overmap.initTextures(renderer, font);
    updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
    updateCamera();

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event))
            handleInput(event, running);

        // Resume crafting from a partial item (set by ItemExaminePanel)
        if (itemExaminePanel.pendingResumeCraft) {
            itemExaminePanel.pendingResumeCraft = false;
            const Item& partial = itemExaminePanel.item;
            if (partial.isPartial) {
                const auto& recs = CraftPanel::allRecipes();
                for (const auto& rec : recs) {
                    if (rec.name == partial.craftRecipeName) {
                        // Remove the partial item from inventory
                        CraftPanel::consumeItems(player, partial.name, 1);
                        craftTotalMins   = partial.craftTotalMins;
                        craftMinutesLeft = partial.craftTotalMins - partial.craftProgress;
                        craftPendingItem = rec.make();
                        isCrafting       = true;
                        panel.addMessage("You resume crafting " + craftPendingItem.name
                                         + "... (" + std::to_string(craftMinutesLeft) + " min left)");
                        break;
                    }
                }
            }
        }

        // Auto-advance crafting — one tick per frame while crafting is active
        if (isCrafting) {
            onPlayerAct();
            SDL_Delay(40); // ~25 fps during craft so the player can see progress
        }

        if (console.pendingTeleport) {
            console.pendingTeleport = false;
            doTeleport(console.tpX, console.tpY);
        }

        if (!console.pendingSpawn.empty()) {
            std::string type  = console.pendingSpawn;
            int         count = console.pendingSpawnCount;
            console.pendingSpawn.clear();

            std::function<Enemy(int,int)> factory;
            if      (type == "goblin")   factory = EnemyTypes::goblin;
            else if (type == "orc")      factory = EnemyTypes::orcWarrior;
            else if (type == "skeleton") factory = EnemyTypes::skeleton;
            else if (type == "wolf")     factory = EnemyTypes::wolf;
            else if (type == "bandit")   factory = EnemyTypes::bandit;

            if (factory) {
                int spawned = 0;
                for (int n = 0; n < count; n++) {
                    for (int attempt = 0; attempt < 60; attempt++) {
                        int x = player.x + (rand() % 11) - 5;
                        int y = player.y + (rand() % 11) - 5;
                        if (x <= 0 || x >= MAP_WIDTH-1 || y <= 0 || y >= MAP_HEIGHT-1) continue;
                        if (!map[y][x].walkable()) continue;
                        if (isTileOccupied(x, y)) continue;
                        if (x == player.x && y == player.y) continue;
                        enemies.push_back(factory(x, y));
                        spawned++;
                        break;
                    }
                }
                if (spawned < count)
                    panel.addMessage("Spawned " + std::to_string(spawned) + "/" + std::to_string(count) + " (no room for rest).");
            }
        }

        if (!console.pendingGive.empty()) {
            const std::string& gn = console.pendingGive;
            Item gi;
            bool gok = true;
            if      (gn == "sword")    gi = Items::ironSword();
            else if (gn == "dagger")   gi = Items::crudeDagger();
            else if (gn == "axe")      gi = Items::warAxe();
            else if (gn == "club")     gi = Items::woodenClub();
            else if (gn == "boneclub") gi = Items::boneClub();
            else if (gn == "helmet")   gi = Items::ironHelmet();
            else if (gn == "vest")     gi = Items::leatherVest();
            else if (gn == "boots")    gi = Items::leatherBoots();
            else if (gn == "backpack") gi = Items::backpack();
            else if (gn == "pouch")    gi = Items::beltPouch();
            else if (gn == "hatchet")  gi = Items::hatchet();
            else if (gn == "pickaxe")  gi = Items::pickaxe();
            else if (gn == "torch")    gi = Items::torch();
            else if (gn == "lantern")  gi = Items::lantern();
            else if (gn == "bread")    gi = Items::bread();
            else if (gn == "water")    gi = Items::waterFlask();
            else if (gn == "ring")     gi = Items::goldRing();
            else if (gn == "amulet")   gi = Items::silverAmulet();
            else if (gn == "log")      gi = Items::woodLog();
            else if (gn == "stone")    gi = Items::stonePiece();
            else if (gn == "branch")   gi = Items::branch();
            else gok = false;

            if (gok && !player.addToContainer(gi))
                groundItems.push_back({player.x, player.y, gi});
            console.pendingGive.clear();
        }

        if (console.pendingGrowPlants) {
            console.pendingGrowPlants = false;
            for (int my = 0; my < MAP_HEIGHT; my++)
                for (int mx = 0; mx < MAP_WIDTH; mx++)
                    if (map[my][mx].objectId >= 0 && objectDefs[map[my][mx].objectId].isPlant)
                        map[my][mx].plantAge = 255;
        }

        // ── Waiting: fast-forward time until target, stop if enemy nearby ──────
        if (isWaiting && !waitPaused) {
            const int TICKS_PER_FRAME = 60;
            bool disturbed  = false;
            bool needsPause = false;
            int lastNeedsHour = worldTime.hour();

            for (int i = 0; i < TICKS_PER_FRAME && worldTime.minutes < waitTargetMinutes; i++) {
                // Check BEFORE ticking — enemies haven't acted yet this step
                for (const Enemy& e : enemies) {
                    if (!e.alive) continue;
                    int cheb = std::max(std::abs(e.x - player.x), std::abs(e.y - player.y));
                    if (cheb <= 14) { disturbed = true; break; }
                }
                if (disturbed) break;

                worldTime.advance();
                tickWorld();
                player.energy = 0;

                // Needs advance every game-minute, same rate as normal play.
                player.tickNeeds();
                tickVillagerNeeds();

                // Starvation/dehydration damage + message throttled to once per game hour.
                if (worldTime.hour() != lastNeedsHour) {
                    lastNeedsHour = worldTime.hour();
                    if (player.hunger >= 1.0f) {
                        player.body.torso.hp = std::max(0, player.body.torso.hp - 1);
                        player.sync();
                        panel.addMessage("Resting while starving — you take damage!");
                    }
                    if (player.thirst >= 1.0f) {
                        player.body.torso.hp = std::max(0, player.body.torso.hp - 2);
                        player.sync();
                        panel.addMessage("Resting while dehydrated — you take damage!");
                    }
                }

                if (!player.isAlive()) { disturbed = true; break; }

                // A hunger/thirst debuff level just got worse — ask before continuing.
                int hl = player.hungerLevel();
                int tl = player.thirstLevel();
                if (hl > waitPrevHungerLv || tl > waitPrevThirstLv) {
                    std::string what = (hl > waitPrevHungerLv) ? player.hungerLabel() : player.thirstLabel();
                    waitPrevHungerLv = hl;
                    waitPrevThirstLv = tl;
                    needsConfirmPanel.show("You are becoming " + what + ". Continue waiting?");
                    needsPause = true;
                    break;
                }
            }

            if (needsPause) {
                waitPaused = true; // isWaiting stays true; resumes or cancels via popup response
            } else if (disturbed) {
                isWaiting = false;
                panel.addMessage("Your rest is disturbed by a nearby enemy!");
            } else if (worldTime.minutes >= waitTargetMinutes) {
                isWaiting = false;
                panel.addMessage("You finish waiting. It is now " + worldTime.timeStr() + ".");
            }
        }

        updatePlayer();
        checkSectorTransition();
        updatePreviewPath();
        if (isCrafting)
            updateVisibility(6, 0);
        else
            updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
        updateCamera();

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        renderMap(renderer);

        // Corpses render first — ground items and actors draw on top.
        for (const Corpse& c : corpses) {
            if (!map[c.y][c.x].visible) continue;
            int sx = (c.x - cameraX) * TILE_SIZE;
            int sy = (c.y - cameraY) * TILE_SIZE;
            if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;
            static const char cSym[2] = {'%', 0};
            SDL_Surface* s = TTF_RenderText_Solid(font, cSym, c.color);
            if (s) {
                SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FreeSurface(s);
                SDL_Rect dst = {sx, sy, TILE_SIZE, TILE_SIZE};
                SDL_RenderCopy(renderer, t, nullptr, &dst);
                SDL_DestroyTexture(t);
            }
        }

        // Ground items on top of map tiles вЂ" show $ for piles, item symbol for single
        {
            // Count items per tile
            std::map<std::pair<int,int>, int> tileCount;
            for (const auto& gi : groundItems)
                if (map[gi.y][gi.x].visible) tileCount[{gi.x, gi.y}]++;

            std::set<std::pair<int,int>> rendered;
            for (const auto& gi : groundItems) {
                if (!map[gi.y][gi.x].visible) continue;
                auto key = std::make_pair(gi.x, gi.y);
                if (!rendered.insert(key).second) continue; // already rendered this tile

                int sx = (gi.x - cameraX) * TILE_SIZE;
                int sy = (gi.y - cameraY) * TILE_SIZE;
                if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;

                int cnt = tileCount[key];
                const char* sym = (cnt > 1) ? "$" : gi.item.groundSymbol();
                SDL_Color col   = (cnt > 1) ? SDL_Color{220, 200, 60, 255} : gi.item.groundColor();

                SDL_Surface* gs = TTF_RenderText_Solid(font, sym, col);
                if (gs) {
                    SDL_Texture* gt = SDL_CreateTextureFromSurface(renderer, gs);
                    SDL_FreeSurface(gs);
                    SDL_Rect dst = {sx, sy, TILE_SIZE, TILE_SIZE};
                    SDL_RenderCopy(renderer, gt, nullptr, &dst);
                    SDL_DestroyTexture(gt);
                }
            }
        }

        renderPath(renderer);
        renderPlayer(renderer);
        renderVillagers(renderer, font);
        renderEnemies(renderer, font);

        // Night/dusk darkness overlay over the map view
        {
            float dark = worldTime.darkness();
            if (dark > 0.0f) {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 0, 0, 20, (Uint8)(dark * 150));
                SDL_Rect overlay = {0, 0, SCREEN_WIDTH, MAP_VIEW_HEIGHT};
                SDL_RenderFillRect(renderer, &overlay);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            }
        }

        ui.renderStats(renderer, font);
        panel.render(renderer, font, player, worldTime);
        bodyPanel.render(renderer, font, player);
        effectsPanel.render(renderer, font, player);
        tradePanel.render(renderer, font, player, villagers);
        if (examinePanel.visible)
            examinePanel.render(renderer, font, map[examinePanel.tileY][examinePanel.tileX]);
        inventoryPanel.render(renderer, font, player);
        itemExaminePanel.render(renderer, font);
        enemyExaminePanel.render(renderer, font);
        villagerExaminePanel.render(renderer, font);
        craftPanel.render(renderer, font, player);
        pickupPanel.render(renderer, font);
        waitPanel.render(renderer, font, worldTime);
        needsConfirmPanel.render(renderer, font);
        contextMenu.render(renderer, font);
        overmap.render(renderer, font, playerSectorX, playerSectorY);
        console.render(renderer, font);

        if (!player.isAlive()) {
            renderDeathScreen(renderer, font);
        }

        // Crafting progress bar overlay
        if (isCrafting) {
            const int BW = 260, BH = 36, BX = (SCREEN_WIDTH - BW) / 2, BY = 6;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 8, 8, 12, 230);
            SDL_Rect bgr = {BX - 4, BY - 2, BW + 8, BH + 4};
            SDL_RenderFillRect(renderer, &bgr);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 80, 70, 40, 255);
            SDL_RenderDrawRect(renderer, &bgr);

            // Progress bar
            int total = craftPendingItem.value > 0 ? craftPendingItem.value : 1; // reuse stored mins
            // We don't have totalMins handy here; just show minutes left text
            std::string label = "Crafting " + craftPendingItem.name + "...";
            std::string timeLeft = std::to_string(craftMinutesLeft) + " min left  [ESC cancel]";
            SDL_Surface* ls = TTF_RenderText_Solid(font, label.c_str(), {200,185,100,255});
            SDL_Surface* ts = TTF_RenderText_Solid(font, timeLeft.c_str(), {130,125,90,255});
            if (ls) {
                SDL_Texture* lt = SDL_CreateTextureFromSurface(renderer, ls);
                int w,h; SDL_QueryTexture(lt,nullptr,nullptr,&w,&h);
                SDL_Rect ld{BX, BY+4, w, h};
                SDL_RenderCopy(renderer, lt, nullptr, &ld);
                SDL_FreeSurface(ls); SDL_DestroyTexture(lt);
            }
            if (ts) {
                SDL_Texture* tt = SDL_CreateTextureFromSurface(renderer, ts);
                int w,h; SDL_QueryTexture(tt,nullptr,nullptr,&w,&h);
                SDL_Rect td{BX, BY+4+18, w, h};
                SDL_RenderCopy(renderer, tt, nullptr, &td);
                SDL_FreeSurface(ts); SDL_DestroyTexture(tt);
            }
        }

        // Waiting HUD
        if (isWaiting) {
            std::string wstr = "Waiting...  " + worldTime.timeStr();
            SDL_Surface* ws = TTF_RenderText_Solid(font, wstr.c_str(), {140, 170, 200, 255});
            if (ws) {
                SDL_Texture* wt = SDL_CreateTextureFromSurface(renderer, ws);
                int ww, wh; SDL_QueryTexture(wt, nullptr, nullptr, &ww, &wh);
                SDL_Rect wd = {(SCREEN_WIDTH - ww) / 2, 6, ww, wh};
                SDL_RenderCopy(renderer, wt, nullptr, &wd);
                SDL_FreeSurface(ws); SDL_DestroyTexture(wt);
            }
        }

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(playerTexture);
    SDL_DestroyTexture(texCursor);
    SDL_DestroyTexture(enemyTexture);
    for (int i = 0; i < T_COUNT; i++) SDL_DestroyTexture(terrainTex[i]);
    for (int i = 0; i < G_COUNT; i++) SDL_DestroyTexture(groundTex[i]);
    for (int i = 0; i < O_COUNT; i++) SDL_DestroyTexture(objectTex[i]);
    overmap.destroyTextures();
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
