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
#include "combat.h"
#include "magic.h"
#include "craft_panel.h"
#include "wait_panel.h"
#include "confirm_panel.h"
#include "effects_panel.h"
#include "skills_panel.h"
#include "hotbar.h"
#include "techniques_panel.h"
#include "spells_panel.h"
#include "trade_panel.h"
#include "panel_style.h"
#include "menu_hub.h"
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

// A tile currently on fire from a manualArea spell (Wall of Fire) — burns
// dmgPerTurn into whoever's standing there once per player action
// (tickFireHazards()) until turnsLeft counts down to zero. See
// confirmWallTargeting() for how these get created.
struct FireHazardTile {
    int x, y;
    int turnsLeft;
    int dmgPerTurn;
};
std::vector<FireHazardTile> fireHazards;

std::vector<SDL_Point> currentPath;
int pathIndex = 0;
Uint32 lastMoveTime = 0;

// Deferred action executed when the player finishes walking to a destination.
enum class PendingAct { NONE, PICKUP_ONE, PICKUP_PANEL, INTERACT, AUTO_INTERACT };
PendingAct pendingAct  = PendingAct::NONE;
int        pendingActX = -1, pendingActY = -1;

// When chasing an enemy (or hostile villager), store a pointer so path can be
// recalculated each step. Separate pointers because Enemy/Villager aren't
// related by a common polymorphic base pointer anywhere else in the codebase.
Enemy*    attackTarget         = nullptr;
Villager* attackTargetVillager = nullptr;

// Walking up to a villager to talk to them — same recalculated-path chase as
// attackTargetVillager, but starts a dialogue instead of a fight on arrival.
Villager* talkTargetVillager = nullptr;

// Set by useHotbarSlot() when a technique or spell needs a target picked
// manually: every tile within its range gets painted (renderHotbarTargeting(),
// near the main render loop) and input is swallowed until the player clicks a
// tile or cancels (see the hotbarTargeting block in handleInput(), and
// resolveHotbarTargeting()/cancelHotbarTargeting() near useHotbarSlot()).
bool hotbarTargeting  = false;
int  hotbarTargetSlot = -1;

// Separate targeting mode for manualArea spells (Wall of Fire): instead of
// one click resolving the cast immediately, the player toggles any number of
// tiles within range on/off (toggleWallTile(), in the wallTargeting block of
// handleInput()) and confirms with Enter (confirmWallTargeting()) or cancels
// with Esc/right-click. Mutually exclusive with hotbarTargeting — only one
// targeting mode is ever active at a time.
bool wallTargeting  = false;
int  wallTargetSlot = -1;
std::vector<SDL_Point> wallSelection;

// Two-phase targeting for throwsWall spells (Wall Throw): first click picks
// which existing O_WALL to hurl (within WALL_THROW_PICK_RADIUS — close by,
// "grab" range, not the spell's normal aim range), second click picks the
// target (within the spell's normal range — thrown farther than it's
// picked up). Doesn't fit hotbarTargeting (one click) or wallTargeting
// (open-ended multi-select) — needs exactly two clicks with different
// validity rules each. Mutually exclusive with both of those.
bool wallThrowTargeting     = false;
int  wallThrowSlot          = -1;
bool wallThrowPickingSource = true;
int  wallThrowSrcX = -1, wallThrowSrcY = -1;
constexpr int WALL_THROW_PICK_RADIUS = 3;

// Purely cosmetic: a spell's projectile flying from caster to target after a
// cast resolves (damage is already applied — see useSpellAtTile()).
// Advanced by wall-clock time (SDL_GetTicks()) each frame, independent of the
// turn system, and drawn by renderSpellProjectile() near the main render loop.
struct SpellProjectile {
    bool        active    = false;
    float       fromX = 0, fromY = 0, toX = 0, toY = 0;
    Uint32      startTime = 0;
    Uint32      duration  = 160;
    const char* symbol    = "*";
    SDL_Color   color     = {255, 255, 255, 255};
};
SpellProjectile spellProjectile;

// Purely cosmetic: a fading tile-radius flash marking a BURST spell's blast
// area (Fireball, Explosion) — so the player can actually see how big the
// splash was, not just read it in the tooltip. Timed to appear once the
// projectile above finishes its flight (delay), then fades out over
// duration. Drawn by renderSpellBurst() near the main render loop.
struct SpellBurst {
    bool        active    = false;
    int         cx = 0, cy = 0;   // impact tile, blast center
    int         radius    = 0;
    Uint32      startTime = 0;
    Uint32      delay     = 160;  // matches SpellProjectile::duration
    Uint32      duration  = 260;
    SDL_Color   color     = {255, 255, 255, 255};
};
SpellBurst spellBurst;

// Purely cosmetic: a knocked-back actor's own glyph sliding from its old
// tile to its new one, so a Gust/Vortex push actually reads as a shove
// instead of the target just silently teleporting one tile over. A vector,
// not a single slot like SpellBurst — Vortex can knock back several actors
// in one cast and each gets its own streak. Drawn by renderKnockbackFlashes()
// near the main render loop, pruned once its short flight is done.
struct KnockbackFlash {
    float       fromX, fromY, toX, toY;
    Uint32      startTime;
    const char* symbol;
    SDL_Color   color;
};
std::vector<KnockbackFlash> knockbackFlashes;
constexpr Uint32 KNOCKBACK_FLASH_DURATION = 150;

int hoverX = 0, hoverY = 0;
int lastHoverX = -1, lastHoverY = -1;
std::vector<SDL_Point> previewPath;

int cameraX = 0, cameraY = 0;

SDL_Window* window       = nullptr;
bool        isFullscreen = false; // set true once the window is actually created fullscreen
TTF_Font*   font         = nullptr; // set once in main(); handleInput() needs it for tab-bar hit-testing

UI ui;
BottomPanel panel;
BodyPanel bodyPanel;
EffectsPanel effectsPanel;
SkillsPanel  skillsPanel;
Hotbar          hotbar;
TechniquesPanel techniquesPanel;
SpellsPanel     spellsPanel;
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
MenuHub hub;

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
//
// randomHitPart()/partName() and the resolveAttack() formula now live in combat.h
// (shared by every attacker/defender pair — see docs/weapons.md).

// Prints the "Your X skill increased to N!" message when an attack result leveled
// up the attacker's skill. Only called from the player-attacks-* call sites (NPCs
// level up silently — not worth the log spam).
void reportSkillUp(const AttackResult& r) {
    if (r.leveledUp)
        panel.addMessage(std::string("Your ") + skillName(r.skillUsed)
                         + " skill increased to " + std::to_string(r.newSkillLevel) + "!");
}

Enemy* getEnemyAt(int x, int y) {
    for (Enemy& e : enemies)
        if (e.isAlive() && e.x == x && e.y == y) return &e;
    return nullptr;
}

Villager* getVillagerAt(int x, int y) {
    for (Villager& v : villagers)
        if (v.isAlive() && v.x == x && v.y == y) return &v;
    return nullptr;
}

// Returns the first ground item at (x, y) in the player's CURRENT sector, or
// nullptr if none — the same local (x,y) coordinate range is reused by every
// sector, so a sector check is required or items from other sectors bleed in.
GroundItem* getGroundItemAt(int x, int y) {
    for (GroundItem& gi : groundItems)
        if (gi.x == x && gi.y == y && gi.sectorX == playerSectorX && gi.sectorY == playerSectorY)
            return &gi;
    return nullptr;
}

// Returns copies of all items at (x, y) in groundItems order, current sector only.
std::vector<Item> getGroundItemsAt(int x, int y) {
    std::vector<Item> result;
    for (const GroundItem& gi : groundItems)
        if (gi.x == x && gi.y == y && gi.sectorX == playerSectorX && gi.sectorY == playerSectorY)
            result.push_back(gi.item);
    return result;
}

// Pick up items at (gx, gy) according to selection mask (parallel to groundItems order at that tile).
void doPickup(int gx, int gy, const std::vector<bool>& sel) {
    // Forward pass to align with sel[] — current sector only (see getGroundItemAt()).
    std::vector<int> atTile;
    for (int i = 0; i < (int)groundItems.size(); i++)
        if (groundItems[i].x == gx && groundItems[i].y == gy &&
            groundItems[i].sectorX == playerSectorX && groundItems[i].sectorY == playerSectorY)
            atTile.push_back(i);

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
        groundItems.push_back({who.x, who.y, item, playerSectorX, playerSectorY});
        if (!list.empty()) list += ", ";
        list += item.name;
    }
    if (!list.empty()) panel.addMessage(who.name + " drops: " + list + ".");
    Item emptyBag = *bag;
    emptyBag.contents.clear();
    groundItems.push_back({who.x, who.y, emptyBag, playerSectorX, playerSectorY});
}

// Drop everything an enemy had and leave a corpse.
void dropEnemyLoot(const Enemy& enemy) {
    dropBag(enemy, enemy.bag);
    corpses.push_back(makeCorpse(enemy, worldTime.minutes, playerSectorX, playerSectorY));
}

// Inheritance for the one piece of village state a death could otherwise
// orphan (docs/village.md "Спадщина": майно переходить дітям/родині; без
// спадкоємців — забирається). Only the granary owner ever has v.granary
// engaged (see npc.h's comment on granaryOwnerId) — everyone else in the
// household just points at the owner's index, so if the owner dies without
// this, the whole family loses access to it forever. Heir order: spouse,
// else the first still-living child; no heir left -> stock spills onto the
// ground where they died instead of vanishing with them.
void transferGranary(Villager& deceased) {
    if (!deceased.granary) return;

    int heirIdx = -1;
    if (deceased.spouseId >= 0 && deceased.spouseId < (int)villagers.size()
        && villagers[deceased.spouseId].alive) {
        heirIdx = deceased.spouseId;
    } else {
        for (int cid : deceased.childIds) {
            if (cid >= 0 && cid < (int)villagers.size() && villagers[cid].alive) {
                heirIdx = cid;
                break;
            }
        }
    }

    if (heirIdx >= 0) {
        Villager& heir = villagers[heirIdx];
        heir.granary  = std::move(deceased.granary);
        heir.granaryX = deceased.granaryX;
        heir.granaryY = deceased.granaryY;
        deceased.granary.reset();

        int deceasedIdx = (int)(&deceased - &villagers[0]);
        for (Villager& hh : villagers)
            if (hh.granaryOwnerId == deceasedIdx) hh.granaryOwnerId = heirIdx;

        panel.addMessage(heir.name + " inherits the family's granary.");
    } else {
        for (const Item& item : deceased.granary->contents)
            groundItems.push_back({deceased.x, deceased.y, item, playerSectorX, playerSectorY});
        deceased.granary.reset();
    }
}

// Same as dropEnemyLoot(), for a villager killed by the player (or anything
// else) — also drops what they were wearing, not just what they carried, so
// nothing survives them as an invisible "worn" item.
void dropVillagerLoot(Villager& v) {
    transferGranary(v);
    dropBag(v, v.bag);
    if (v.outfit) {
        groundItems.push_back({v.x, v.y, *v.outfit, playerSectorX, playerSectorY});
        panel.addMessage(v.name + "'s " + v.outfit->name + " falls to the ground.");
        v.outfit.reset();
    }
    corpses.push_back(makeCorpse(v, worldTime.minutes, playerSectorX, playerSectorY));
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
        const Item* weapon = enemy.weaponItem();
        AttackResult r = resolveAttack(enemy, player, enemy.strength, enemy.dexterity,
                                        player.effectiveDex(), player.totalDefense(), weapon);
        if (!r.hit) {
            panel.addMessage(enemy.name + " swings and misses you.");
        } else {
            std::string withWeapon = weapon ? (" with " + weapon->name) : "";
            panel.addMessage(enemy.name + " hits your " + partName(r.part)
                             + withWeapon + " for " + std::to_string(r.damage) + " damage.");
            if (!player.isAlive())
                panel.addMessage("You have been slain by " + enemy.name + ".");
            if (r.reflectedDamage > 0) {
                panel.addMessage("Your Fire Shield burns " + enemy.name + " for "
                                 + std::to_string(r.reflectedDamage) + " damage!");
                if (!enemy.isAlive()) {
                    panel.addMessage(enemy.name + " is consumed by the flames.");
                    dropEnemyLoot(enemy);
                }
            }
        }
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

// Rolls fight-or-flee for one villager and commits them to it — occupation
// sets the base courage, a real weapon in their bag (e.g. Blacksmith stock)
// adds to it. No side effects beyond this villager; callers decide whether
// to also alert anyone else (see villagerReactToAttack() below).
// victimName empty = this villager is the one being hit; otherwise it's a
// witness reacting to seeing victimName attacked (changes the message only).
void rollCombatReaction(Villager& v, const std::string& victimName) {
    int fightChance;
    switch (v.occupation) {
        case Occupation::BLACKSMITH: fightChance = 70; break;
        case Occupation::WOODCUTTER: fightChance = 50; break;
        case Occupation::ELDER:      fightChance = 35; break;
        default:                     fightChance = 15; break;
    }
    if (v.weaponDmg() > 0) fightChance += 20;

    bool willFight = (rand() % 100) < fightChance;
    v.state       = willFight ? Villager::State::FIGHT : Villager::State::FLEE;
    v.disposition = -80;
    v.homePath.clear(); // interrupts whatever path they were following (home/field/well)

    if (victimName.empty()) {
        panel.addMessage(willFight ? (v.name + " draws a weapon and turns to fight!")
                                    : (v.name + " flees in terror!"));
    } else {
        panel.addMessage(willFight
            ? (v.name + " sees " + victimName + " under attack and rushes to help!")
            : (v.name + " sees " + victimName + " under attack and flees in terror!"));
    }
}

// Called once, the moment a villager first takes damage from the player.
// Rolls their own fight/flee, then alerts any other villager close enough to
// have witnessed it (visible tile, within range) — each witness rolls their
// own reaction independently via rollCombatReaction(), NOT this function, so
// the alert can't chain further out and swallow the whole village at once.
void villagerReactToAttack(Villager& v) {
    if (v.state == Villager::State::FIGHT || v.state == Villager::State::FLEE) return;
    rollCombatReaction(v, "");

    const int WITNESS_RANGE = 12;
    for (Villager& other : villagers) {
        if (&other == &v || !other.alive) continue;
        if (other.state == Villager::State::FIGHT || other.state == Villager::State::FLEE) continue;
        int dx = other.x - v.x, dy = other.y - v.y;
        if (dx * dx + dy * dy > WITNESS_RANGE * WITNESS_RANGE) continue;
        if (!map[other.y][other.x].visible) continue; // must actually be able to see it happen
        rollCombatReaction(other, v.name);
    }
}

// Per-tick behavior once a villager is FIGHT/FLEE — mirrors enemyAct()'s
// aggro/flee pattern so the two hostile-AI loops stay recognizably the same shape.
void villagerCombatAct(Villager& v) {
    int dx = player.x - v.x;
    int dy = player.y - v.y;

    // A fighting villager who takes enough damage breaks and runs, same
    // threshold spirit as Enemy::wantsToFlee().
    if (v.state == Villager::State::FIGHT && v.maxHp > 0 && (float)v.hp / v.maxHp < 0.30f)
        v.state = Villager::State::FLEE;

    if (v.state == Villager::State::FLEE) {
        int dist2 = dx * dx + dy * dy;
        if (dist2 > 20 * 20) { v.state = Villager::State::WANDER; return; } // safe now
        int fx = v.x - (dx != 0 ? (dx > 0 ? 1 : -1) : 0);
        int fy = v.y - (dy != 0 ? (dy > 0 ? 1 : -1) : 0);
        fx = std::max(1, std::min(MAP_WIDTH  - 2, fx));
        fy = std::max(1, std::min(MAP_HEIGHT - 2, fy));
        if (map[fy][fx].walkable() && !isTileOccupied(fx, fy) &&
            !(fx == player.x && fy == player.y)) {
            v.x = fx; v.y = fy;
        }
        return;
    }

    // FIGHT
    std::vector<SDL_Point> path = findPath(v.x, v.y, player.x, player.y);
    if ((int)path.size() < 2) return;

    SDL_Point next = path[1];
    if (next.x == player.x && next.y == player.y) {
        const Item* weapon = v.weaponItem();
        AttackResult r = resolveAttack(v, player, v.strength, v.dexterity,
                                        player.effectiveDex(), player.totalDefense(), weapon);
        if (!r.hit) {
            panel.addMessage(v.name + " swings and misses you.");
        } else {
            std::string withWeapon = weapon ? (" with " + weapon->name) : "";
            panel.addMessage(v.name + " hits your " + partName(r.part)
                             + withWeapon + " for " + std::to_string(r.damage) + " damage.");
            if (!player.isAlive())
                panel.addMessage("You have been slain by " + v.name + ".");
            if (r.reflectedDamage > 0) {
                panel.addMessage("Your Fire Shield burns " + v.name + " for "
                                 + std::to_string(r.reflectedDamage) + " damage!");
                if (!v.isAlive()) {
                    panel.addMessage(v.name + " is consumed by the flames.");
                    dropVillagerLoot(v);
                }
            }
        }
    } else if (!isTileOccupied(next.x, next.y)) {
        v.x = next.x;
        v.y = next.y;
    }
}

void updateVillagers();    // forward declaration — defined after checkSectorTransition
void tickVillagerNeeds();  // forward declaration — defined after updateVillagers
void tickEnemyNeeds();     // forward declaration — defined below, called once per player action
void tickFireHazards();    // forward declaration — defined near Wall of Fire casting, called once per player action
void interruptCrafting(bool playerHit); // forward declaration — defined in villager section
void simulateVillageYear(std::vector<Villager>& vs); // forward declaration — defined next to spawnVillagers()

// One world tick: give everyone energy, then let enemies spend theirs.
// NOTE: this can run several times per single player action (see onPlayerAct()), with the
// iteration count depending on player speed — so anything tied to real elapsed game time
// (hunger/thirst/etc.) must NOT live in here. See tickEnemyNeeds().
void tickWorld() {
    player.energy += player.effectiveSpeed();

    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        e.energy += e.effectiveSpeed();
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
    if (attackTargetVillager && !attackTargetVillager->alive) attackTargetVillager = nullptr;
    if (talkTargetVillager && !talkTargetVillager->alive) talkTargetVillager = nullptr;
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

// Ages every actor by one year and kills anyone who reaches their rolled
// naturalDeathAge (docs/village.md "Смерть від старості"). Body of the old
// tickAging(), unchanged — just extracted so tickYearlyEvents() below can
// call it once per elapsed year instead of just once per detected boundary.
void tickAgingOneYear() {
    player.age++;
    if (player.naturalDeathAge > 0 && player.age >= player.naturalDeathAge && player.isAlive()) {
        player.body.torso.hp = 0;
        player.sync();
        panel.addMessage("Your body finally gives out. You have died of old age.");
    }

    for (Villager& v : villagers) {
        if (!v.alive) continue;
        v.age++;
        if (v.naturalDeathAge > 0 && v.age >= v.naturalDeathAge) {
            v.body.torso.hp = 0;
            v.sync();
            if (!v.alive) {
                panel.addMessage(v.name + " has died of old age.");
                dropVillagerLoot(v);
            }
        }
    }

    for (Enemy& e : enemies) {
        if (!e.alive) continue;
        e.age++;
        if (e.naturalDeathAge > 0 && e.age >= e.naturalDeathAge) {
            e.body.torso.hp = 0;
            e.sync();
            if (!e.alive) {
                panel.addMessage(e.name + " dies of old age.");
                dropEnemyLoot(e);
            }
        }
    }
}

// Runs tickAgingOneYear() + simulateVillageYear() once for every in-game year
// crossed since the last call — not just once per call — so a jump of several
// years in one go (skipyears cheat, or a future instant travel/time-skip
// feature) doesn't silently drop years the way a plain boundary check would.
// A year is 518400 actions (WorldTime::MINUTES_PER_YEAR) — this will almost
// never fire from ordinary movement, by design (no artificial age
// acceleration); it fires correctly during long time-skips since it's called
// every simulated minute, same as tickEnemyNeeds()/tickVillagerNeeds().
// Called once per player action from onPlayerAct() and once per simulated
// minute from the Wait/Rest loop, not from tickWorld() — same "must track
// real elapsed time, not speed-dependent ticks" reasoning tickAging() always used.
void tickYearlyEvents() {
    static long long lastYear = worldTime.minutes / WorldTime::MINUTES_PER_YEAR;
    long long curYear = worldTime.minutes / WorldTime::MINUTES_PER_YEAR;
    while (lastYear < curYear) {
        lastYear++;
        tickAgingOneYear();
        simulateVillageYear(villagers);
    }
}

// Called after every player action.
// Spends 100 energy (+extraEnergy, for techniques that cost more time than a
// plain attack), then ticks the world until the player can act again.
// Result: player.energy >= 100 when this returns.
void onPlayerAct(int extraEnergy = 0) {
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
    player.energy -= (100 + extraEnergy);
    while (player.energy < 100)
        tickWorld();

    // Needs advance and starvation damage once per player action (not per world tick)
    player.tickNeeds();
    tickVillagerNeeds();
    tickEnemyNeeds();
    tickFireHazards();
    tickYearlyEvents();
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
                groundItems.push_back({player.x, player.y, craftPendingItem, playerSectorX, playerSectorY});
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
void toggleFullscreen();
void openMenuTab(MenuTab t);
void closeMenuHub();
void toggleMenuTab(MenuTab t);
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

// Picks the right greeting line for a villager — Elder speaks differently,
// and both flavors differ between asleep and awake. Shared by the context
// menu's "Talk to X" and the walk-up-then-talk arrival in updatePlayer().
std::string greetingFor(const Villager& v) {
    bool sleeping = (v.state == Villager::State::SLEEP);
    bool isElder  = (v.occupation == Occupation::ELDER);
    return sleeping
        ? (isElder ? ELDER_GREETINGS_NIGHT[v.greetIdx % countStrings(ELDER_GREETINGS_NIGHT)]
                   : GREETINGS_NIGHT      [v.greetIdx % countStrings(GREETINGS_NIGHT)])
        : (isElder ? ELDER_GREETINGS_DAY  [v.greetIdx % countStrings(ELDER_GREETINGS_DAY)]
                   : GREETINGS_DAY        [v.greetIdx % countStrings(GREETINGS_DAY)]);
}

// Linear scan — enemies is small. Needed because vector indices shift whenever
// enemies.erase(remove_if(!alive)) runs (tickWorld()), but a villager's
// goalTargetEnemyId must keep pointing at the same Enemy across many ticks.
Enemy* findEnemyById(int id) {
    for (Enemy& e : enemies) if (e.id == id) return &e;
    return nullptr;
}

// Rough compass hint from the player to (tx,ty) — a quick, temporary stand-in
// for a real quest waypoint/marker (which doesn't exist yet), so "go kill the
// goblin the game mentioned" isn't a blind search of the whole sector.
std::string compassHint(int tx, int ty) {
    int dx = tx - player.x, dy = ty - player.y;
    int dist = std::max(std::abs(dx), std::abs(dy));
    std::string ns = dy < -3 ? "north" : dy > 3 ? "south" : "";
    std::string ew = dx < -3 ? "west"  : dx > 3 ? "east"  : "";
    std::string dir = ns + ew;
    return std::to_string(dist) + " tiles " + (dir.empty() ? "away, right around here" : "to the " + dir);
}

// Response options for talking to villagers[vi] — Trade (if they sell
// anything), Ask about their work (shows another line, rebuilds this same
// list so the player can keep going), a goal-dependent option if this
// villager has one (docs/village.md "Цілі NPC → квести"), Farewell. Not a
// real branching tree — one level of depth, reused every time "Ask" is picked.
std::vector<MenuItem> buildDialogueOptions(int vi) {
    std::vector<MenuItem> opts;
    if (vi < 0 || vi >= (int)villagers.size()) return opts;
    Villager& v = villagers[vi];

    bool canTrade = v.occupation != Occupation::NONE && v.occupation != Occupation::ELDER;
    if (canTrade) {
        opts.push_back({"Trade with " + v.name + " (" + occupationName(v.occupation) + ")",
            [vi]() { panel.endDialogue(); tradePanel.show(vi); }});
    }
    if (v.occupation != Occupation::NONE) {
        opts.push_back({"Ask about their work", [vi]() {
            if (vi < 0 || vi >= (int)villagers.size()) return;
            Villager& tv = villagers[vi];
            panel.startDialogue(tv.name, occupationFlavor(tv.occupation), buildDialogueOptions(vi));
        }});
    }

    // Goal state is derived fresh every call, never cached — same "pull live
    // state" approach effects_panel.h uses for buffs. A missing target inside
    // one continuous sector visit can only mean it died (by any cause — a
    // sector change would have wiped this Villager along with it), matching
    // village.md's "піде на нього сам рано чи пізно — з гравцем чи без".
    if (v.goalTargetEnemyId >= 0) {
        Enemy* target = findEnemyById(v.goalTargetEnemyId);
        if (target && target->alive) {
            if (!v.goalAccepted) {
                opts.push_back({"Ask about their trouble", [vi]() {
                    if (vi < 0 || vi >= (int)villagers.size()) return;
                    Villager& tv = villagers[vi];
                    Enemy* t = findEnemyById(tv.goalTargetEnemyId);
                    if (!t) { panel.endDialogue(); return; } // resolved between menu open and click
                    std::string line = "There's a " + t->name + " that's been trouble lately — "
                                        "last I saw, it was about " + compassHint(t->x, t->y) + ". "
                                        "I'd feel a lot safer if someone dealt with it.";
                    std::vector<MenuItem> subOpts;
                    subOpts.push_back({"I'll deal with it", [vi]() {
                        if (vi < 0 || vi >= (int)villagers.size()) return;
                        villagers[vi].goalAccepted = true;
                        panel.addMessage(villagers[vi].name + ": \"Thank you, truly.\"");
                        panel.endDialogue();
                    }});
                    subOpts.push_back({"Not my problem", [vi]() {
                        if (vi < 0 || vi >= (int)villagers.size()) return;
                        Villager& tv2 = villagers[vi];
                        panel.startDialogue(tv2.name, greetingFor(tv2), buildDialogueOptions(vi));
                    }});
                    panel.startDialogue(tv.name, line, subOpts);
                }});
            } else {
                opts.push_back({"Ask about the " + target->name, [vi]() {
                    if (vi < 0 || vi >= (int)villagers.size()) return;
                    Enemy* t = findEnemyById(villagers[vi].goalTargetEnemyId);
                    std::string hint = t ? (" Last I heard, it was about " + compassHint(t->x, t->y) + ".") : "";
                    panel.addMessage(villagers[vi].name + ": \"Any luck? Please be careful." + hint + "\"");
                }});
            }
        } else if (v.goalAccepted) {
            opts.push_back({"Return about their trouble", [vi]() {
                if (vi < 0 || vi >= (int)villagers.size()) return;
                Villager& tv = villagers[vi];
                Item reward = Items::goldCoin();
                reward.count = 15;
                player.addToContainer(reward);
                panel.addMessage(tv.name + ": \"It's gone? Thank you — please, take this.\"");
                tv.goalTargetEnemyId = -1;
                tv.goalAccepted = false;
                panel.endDialogue();
            }});
        }
    }

    opts.push_back({"Farewell", []() { panel.endDialogue(); }});
    return opts;
}

// Whether aIdx/bIdx are parent-child or share a parent, for the marriage
// eligibility check below. Deliberately checks all four motherId/fatherId
// cross-combinations rather than assuming a.motherId lines up with
// b.motherId specifically: spawnVillagers()'s initial household batch and
// simulateVillageYear()'s ongoing births pass pass motherIdx/fatherIdx to
// spawnChild() in opposite index order (spouse/primary vs. lower/higher
// index), so two siblings — one from each source — can end up with their
// shared parents recorded in swapped slots. A same-slot-only check missed
// exactly that case and let siblings marry; this doesn't care which slot
// a shared parent landed in.
bool areRelated(const Villager& a, int aIdx, const Villager& b, int bIdx) {
    if (a.motherId == bIdx || a.fatherId == bIdx) return true;
    if (b.motherId == aIdx || b.fatherId == aIdx) return true;
    auto shared = [](int x, int y) { return x >= 0 && x == y; };
    return shared(a.motherId, b.motherId) || shared(a.motherId, b.fatherId)
        || shared(a.fatherId, b.motherId) || shared(a.fatherId, b.fatherId);
}

// Adds an occupation's goods into a villager's bag (real container, no floating
// items). Shared by spawnVillagers() (initial household assignment) and
// simulateVillageYear()'s occupation-succession pass.
void giveOccupation(Villager& v, Occupation occ) {
    v.occupation = occ;
    if (!v.bag) return;
    for (Item& g : goodsFor(occ)) addToContainer(*v.bag, std::move(g));
}

// Creates one child of motherIdx/fatherIdx and appends it to vs. ageOverride=0
// means a brand-new birth (simulateVillageYear()'s yearly roll); spawnVillagers()
// passes a random worldgen-style age instead, since the village it builds is
// meant to already have some lived-in history. motherId/fatherId are just
// consistent slot names — the game has no gender mechanic.
// Simplification vs. the original inline version this was extracted from:
// doesn't dedupe first names against siblings (that bookkeeping only existed
// in spawnVillagers()'s local scope) — a repeated first name in a big family
// is a cosmetic risk, not worth threading extra state through both callers for.
int spawnChild(std::vector<Villager>& vs, int motherIdx, int fatherIdx, int ageOverride = 0) {
    Villager& mother = vs[motherIdx];
    Villager& father = vs[fatherIdx];

    Villager child;
    child.isChild = true;
    child.age     = ageOverride;
    child.bedX    = father.bedX;
    child.bedY    = father.bedY;

    // Distinct nearby walkable tile so kids don't all path onto the exact same
    // spot as a parent or an existing sibling — search a small ring around the
    // household's bed for a free floor tile.
    child.sleepX = father.sleepX;
    child.sleepY = father.sleepY;
    std::vector<SDL_Point> takenHomeTiles = {
        {father.sleepX, father.sleepY}, {mother.sleepX, mother.sleepY}
    };
    for (int cid : father.childIds)
        if (cid >= 0 && cid < (int)vs.size())
            takenHomeTiles.push_back({vs[cid].sleepX, vs[cid].sleepY});
    bool foundTile = false;
    for (int dy = -2; dy <= 2 && !foundTile; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int tx = child.bedX + dx, ty = child.bedY + dy;
            if (tx < 1 || tx >= MAP_WIDTH-1 || ty < 1 || ty >= MAP_HEIGHT-1) continue;
            if (!map[ty][tx].walkable()) continue;
            bool taken = false;
            for (const SDL_Point& p : takenHomeTiles)
                if (p.x == tx && p.y == ty) { taken = true; break; }
            if (taken) continue;
            child.sleepX = tx; child.sleepY = ty;
            foundTile = true;
            break;
        }
    }
    child.x = child.sleepX; child.y = child.sleepY;

    int nFirstNames = countStrings(NPC_FIRST_NAMES);
    int fnIdx = rand() % nFirstNames;
    std::string surname = father.name.substr(father.name.find(' ') + 1);
    child.name     = std::string(NPC_FIRST_NAMES[fnIdx]) + " " + surname;
    child.color    = VILLAGER_COLORS[(int)vs.size() % VILLAGER_COLOR_COUNT];
    child.greetIdx = (int)vs.size() % countStrings(GREETINGS_DAY);
    child.state    = Villager::State::WANDER;

    child.bag = Items::backpack();
    {
        Item loaves = Items::bread();
        loaves.count = 3;
        addToContainer(*child.bag, std::move(loaves));
    }
    child.outfit = Items::commonClothes();

    child.motherId       = motherIdx;
    child.fatherId       = fatherIdx;
    child.granaryOwnerId = father.granaryOwnerId;

    int childId = (int)vs.size();
    vs.push_back(child);
    vs[fatherIdx].childIds.push_back(childId);
    vs[motherIdx].childIds.push_back(childId);
    return childId;
}

// docs/village.md "Демографія" / docs/world.md "Шар 3" — one year of life for
// the currently-loaded village: children come of age, a dead worker's job
// passes to an heir, eligible adults marry, and married couples might have a
// child. Runs once per elapsed in-game year via tickYearlyEvents(). Operates
// only on `vs` — the live `villagers` vector for whichever village the player
// is currently standing in. Distant, unvisited villages don't tick yet (no
// persistent per-sector village store exists — see CLAUDE.md roadmap).
static constexpr int MARRIAGE_CHANCE_PERCENT   = 20;
static constexpr int BIRTH_CHANCE_PERCENT      = 15;
static constexpr int APPRENTICE_CHANCE_PERCENT = 25;

void simulateVillageYear(std::vector<Villager>& vs) {
    if (vs.empty()) return;

    // ---- Growing up: child -> adult at the race's age of adulthood ----------
    for (Villager& v : vs) {
        if (!v.alive || !v.isChild) continue;
        if (v.age >= raceTraits[(int)v.race].minAge) {
            v.isChild = false;
            panel.addMessage(v.name + " has grown into an adult.");
        }
    }

    // ---- Occupation succession: a dead worker's job passes to an heir -------
    // Heir order: spouse first, else the first alive child (same as
    // transferGranary()). If neither exists, docs/village.md's other stated
    // path applies — "йде в учні до майстра без спадкоємця": any unrelated
    // adult with no trade of their own can pick up the vacant one, rolled per
    // year rather than instantly — this is what stops a profession from
    // staying vacant forever once its founding line has no children left.
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& deceased = vs[i];
        if (deceased.alive || deceased.occupation == Occupation::NONE) continue;

        int heirIdx = -1;
        bool viaFamily = true;
        if (deceased.spouseId >= 0 && deceased.spouseId < (int)vs.size()
            && vs[deceased.spouseId].alive && !vs[deceased.spouseId].isChild) {
            heirIdx = deceased.spouseId;
        } else {
            for (int cid : deceased.childIds) {
                if (cid >= 0 && cid < (int)vs.size() && vs[cid].alive && !vs[cid].isChild) {
                    heirIdx = cid;
                    break;
                }
            }
        }

        if (heirIdx < 0) {
            viaFamily = false;
            std::vector<int> apprentices;
            for (int k = 0; k < (int)vs.size(); k++)
                if (vs[k].alive && !vs[k].isChild && vs[k].occupation == Occupation::NONE)
                    apprentices.push_back(k);
            if (!apprentices.empty() && (rand() % 100) < APPRENTICE_CHANCE_PERCENT)
                heirIdx = apprentices[rand() % apprentices.size()];
        }

        if (heirIdx >= 0 && vs[heirIdx].occupation == Occupation::NONE) {
            Occupation occ = deceased.occupation;
            giveOccupation(vs[heirIdx], occ);
            deceased.occupation = Occupation::NONE;
            panel.addMessage(viaFamily
                ? vs[heirIdx].name + " takes up the family trade as " + occupationName(occ) + "."
                : vs[heirIdx].name + " apprentices as the village's new " + occupationName(occ) + ", the trade having no heir.");
        }
    }

    // ---- Marriage: pair up eligible unmarried/widowed adults -----------------
    std::vector<int> eligible;
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& v = vs[i];
        if (!v.alive || v.isChild) continue;
        if (v.age < raceTraits[(int)v.race].minAge) continue;
        bool free = v.spouseId < 0 || v.spouseId >= (int)vs.size() || !vs[v.spouseId].alive;
        if (free) eligible.push_back(i);
    }
    std::vector<bool> matchedThisYear(vs.size(), false);
    for (int i : eligible) {
        if (matchedThisYear[i]) continue;
        if ((rand() % 100) >= MARRIAGE_CHANCE_PERCENT) continue;

        std::vector<int> candidates;
        for (int j : eligible) {
            if (j == i || matchedThisYear[j]) continue;
            if (areRelated(vs[i], i, vs[j], j)) continue;
            candidates.push_back(j);
        }
        if (candidates.empty()) continue;

        int j = candidates[rand() % candidates.size()];
        vs[i].spouseId = j;
        vs[j].spouseId = i;
        matchedThisYear[i] = matchedThisYear[j] = true;
        panel.addMessage(vs[i].name + " and " + vs[j].name + " are married.");
    }

    // ---- Births: married couples at full adulthood might have a child --------
    for (int i = 0; i < (int)vs.size(); i++) {
        Villager& a = vs[i];
        if (!a.alive || a.isChild || a.spouseId < 0) continue;
        int j = a.spouseId;
        if (j <= i || j >= (int)vs.size()) continue; // handle each pair once
        Villager& b = vs[j];
        if (!b.alive) continue;

        if (lifeStageFor(a.age, a.race) != LifeStage::ADULT) continue;
        if (lifeStageFor(b.age, b.race) != LifeStage::ADULT) continue;

        if ((rand() % 100) < BIRTH_CHANCE_PERCENT) {
            int childId = spawnChild(vs, i, j);
            panel.addMessage(vs[childId].name + " was born to " + vs[i].name + " and " + vs[j].name + ".");
        }
    }
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
        v.outfit = Items::commonClothes(); // never "naked with a backpack"

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

    // Nearest O_BARREL to a bed — every farmstead room already has exactly
    // one (map.cpp's placeFarmstead()), so this reliably finds that
    // household's own barrel rather than the Smithy's/Woodcutter's (those
    // sit in a completely different building, far outside this radius).
    auto findNearbyBarrel = [](int bx, int by) -> SDL_Point {
        SDL_Point best{-1, -1};
        int bestD = -1;
        for (int y = std::max(0, by - 12); y <= std::min(MAP_HEIGHT - 1, by + 12); y++)
            for (int x = std::max(0, bx - 12); x <= std::min(MAP_WIDTH - 1, bx + 12); x++) {
                if (map[y][x].objectId != O_BARREL) continue;
                int d = (x - bx) * (x - bx) + (y - by) * (y - by);
                if (bestD < 0 || d < bestD) { bestD = d; best = {x, y}; }
            }
        return best;
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

        bool isFarmHousehold = (role == BuildingRole::FARM || role == BuildingRole::HERBALIST_FARM);

        // Family granary — shared food reserve tied to the barrel map.cpp
        // already furnishes every farmstead room with (repurposed from pure
        // decoration). The primary worker holds the real Item;
        // granaryOwnerId (set on the primary itself, then propagated to
        // spouse/children below) is how everyone else reaches it.
        if (isFarmHousehold) {
            SDL_Point barrel = findNearbyBarrel(villagers[primary].bedX, villagers[primary].bedY);
            if (barrel.x >= 0) {
                Item stock   = Items::grainBarrel();
                Item starter = (occ == Occupation::HERBALIST) ? Items::mushroomStew() : Items::flatbread();
                starter.count = 8;
                addToContainer(stock, starter);
                villagers[primary].granary        = stock;
                villagers[primary].granaryX       = barrel.x;
                villagers[primary].granaryY       = barrel.y;
                villagers[primary].granaryOwnerId = primary;
            }
        }

        // Real spouse link for any two-bed household (farm/herbalist/elder) —
        // not just a shared surname. Both spouses already got an independent
        // adult age from Villager()'s default ctor; re-roll the second one
        // close to the first's instead of leaving two unrelated ages.
        if (kv.second.size() > 1) {
            int spouseIdx = kv.second[1];
            villagers[primary].spouseId   = spouseIdx;
            villagers[spouseIdx].spouseId = primary;
            villagers[spouseIdx].granaryOwnerId = villagers[primary].granaryOwnerId;
            int lo = std::max(raceTraits[(int)Race::HUMAN].minAge, villagers[primary].age - 10);
            int hi = std::min(raceTraits[(int)Race::HUMAN].maxAge, villagers[primary].age + 10);
            villagers[spouseIdx].age = Names::generateAge(Race::HUMAN, lo, hi);

            // Rare chance: the spouse in a non-farm household becomes a
            // Seamstress instead of a plain helper.
            if (!isFarmHousehold && (rand() % 100) < 20)
                giveOccupation(villagers[spouseIdx], Occupation::SEAMSTRESS);
        }

        // Children — farm/herbalist households (married couples) only. No bed
        // exists for them (map.cpp never furnishes more than 2 beds per
        // building), so they share a parent's home tile instead of pathing to
        // one of their own; the family granary (above) plus a small personal
        // stock covers their food.
        if (isFarmHousehold && kv.second.size() > 1) {
            int fatherIdx = primary, motherIdx = kv.second[1];
            int youngerParentAge = std::min(villagers[fatherIdx].age, villagers[motherIdx].age);
            int maxChildAge = youngerParentAge - 16; // parent was at least 16 at birth

            if (maxChildAge >= 1 && (rand() % 100) < 60) {
                int nKids = 1 + rand() % 3;
                for (int k = 0; k < nKids; k++)
                    spawnChild(villagers, motherIdx, fatherIdx, Names::generateAge(Race::HUMAN, 1, std::min(17, maxChildAge)));
            }
        }
    }

    // One real goal per sector visit (docs/village.md "Цілі NPC → квести") — pick a
    // random non-child villager to be worried about an actual threat already spawned
    // in this sector. initEnemy() already ran before spawnVillagers() was called (see
    // the sector-transition site), so `enemies` is populated by this point.
    if (!enemies.empty()) {
        std::vector<int> eligible;
        for (int i = 0; i < (int)villagers.size(); i++)
            if (!villagers[i].isChild) eligible.push_back(i);
        if (!eligible.empty()) {
            int chosen = eligible[rand() % eligible.size()];
            villagers[chosen].goalTargetEnemyId = enemies[rand() % enemies.size()].id;
        }
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
        groundItems.push_back({player.x, player.y, partial, playerSectorX, playerSectorY});
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
                    // Eats the first nutrition>0 stack out of a container.
                    auto eatFrom = [](Item& container) {
                        auto& c = container.contents;
                        for (int i = 0; i < (int)c.size(); i++) {
                            if (c[i].nutrition > 0) {
                                if (c[i].count > 1) c[i].count--;
                                else                 c.erase(c.begin() + i);
                                return true;
                            }
                        }
                        return false;
                    };

                    if (v.bag) ate = eatFrom(*v.bag);

                    // Family granary — shared reserve, tried before anyone
                    // goes hungry. Every household member (owner included)
                    // points at it via granaryOwnerId, so this is the same
                    // lookup for everyone, not a special case for the farmer.
                    if (!ate && v.granaryOwnerId >= 0 && v.granaryOwnerId < (int)villagers.size()) {
                        Villager& owner = villagers[v.granaryOwnerId];
                        if (owner.granary) ate = eatFrom(*owner.granary);
                    }

                    if (!ate && (v.occupation == Occupation::FARMER || v.occupation == Occupation::HERBALIST) && v.granary) {
                        // Granary's empty too — harvest a mature crop from
                        // their own field straight into it (a real Item, not
                        // an invisible hunger reset), then eat one portion.
                        int wantId = (v.occupation == Occupation::FARMER) ? O_WHEAT : O_HERB;
                        for (int dy = -6; dy <= 6 && !ate; dy++)
                            for (int dx = -6; dx <= 6 && !ate; dx++) {
                                int fx = v.bedX + dx, fy = v.bedY + dy;
                                if (fx < 0 || fx >= MAP_WIDTH || fy < 0 || fy >= MAP_HEIGHT) continue;
                                Tile& t = map[fy][fx];
                                if (t.objectId == wantId && t.plantAge >= 170) {
                                    t.plantAge = 0; // harvested — regrows over the season
                                    Item food = (v.occupation == Occupation::FARMER)
                                              ? Items::flatbread() : Items::mushroomStew();
                                    food.count = 3;
                                    addToContainer(*v.granary, food);
                                    ate = eatFrom(*v.granary);
                                }
                            }
                    }
                    // If nothing worked, hunger stays high — a real risk, not just flavor.
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
            case Villager::State::FLEE:
            case Villager::State::FIGHT:
                villagerCombatAct(v);
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
        // Can kill via bleed-out on its own (tickNeeds() drains torso HP from any
        // open wound every call, not just on the hour) — so the alive-check below
        // must NOT be gated behind hourCrossed, or a villager who takes a bleeding
        // wound and flees instead of dying outright silently vanishes with no
        // corpse/loot the next time this runs before the hour ticks over.
        v.tickNeeds(); // inherited from Actor — same rate constants as the player

        if (hourCrossed) {
            if (v.hunger >= 1.0f) v.takeDamage(1, PartTarget::TORSO);
            if (v.thirst >= 1.0f) v.takeDamage(2, PartTarget::TORSO);
        }

        if (!v.alive) {
            std::string cause = v.thirst >= 1.0f ? "has died of dehydration."
                               : v.hunger >= 1.0f ? "has died of starvation."
                                                    : "has bled to death.";
            panel.addMessage(v.name + " " + cause);
            // Everything they had (and wore) drops — same rule as enemy loot, nothing vanishes.
            dropVillagerLoot(v);
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

    // Enemies/villagers don't persist across sectors yet (no save system) — they
    // respawn fresh each visit. Corpses/groundItems DO persist (tagged by sector,
    // filtered at render/interaction time), so no clearing needed for those here.
    attackTarget         = nullptr; // enemies.clear() below would otherwise dangle it
    attackTargetVillager = nullptr; // spawnVillagers() below clears villagers, same reason
    talkTargetVillager    = nullptr;
    enemies.clear();
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

// Clears the object at (tx,ty) and scatters the resources destroying it
// would yield — shared by interactWithObject() (player chopping/mining) and
// applySpellObjectDamage() (any spell breaking scenery) so both paths give
// the same loot for the same object type.
void destroyWorldObject(int tx, int ty, int oid) {
    Tile& tile = map[ty][tx];
    tile.objectId = -1;
    tile.objectHp = 0;

    auto drop = [&](Item item) {
        groundItems.push_back({tx, ty, std::move(item), playerSectorX, playerSectorY});
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
        case O_WALL:
            for (int k = 0; k < rand() % 2 + 2; k++) drop(Items::stonePiece());
            panel.addMessage("The wall crumbles into rubble!");
            break;
        default:
            panel.addMessage("The " + std::string(objectDefs[oid].name) + " breaks apart.");
            break;
    }

    currentPath.clear(); pathIndex = 0; previewPath.clear();
    updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
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
                groundItems.push_back({tx, ty, item, playerSectorX, playerSectorY});
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
        destroyWorldObject(tx, ty, oid);
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
    // Corpses/groundItems persist across sectors (tagged + filtered by sector);
    // only enemies/villagers respawn fresh, same as checkSectorTransition().
    attackTarget         = nullptr; // enemies.clear() below would otherwise dangle it
    attackTargetVillager = nullptr; // spawnVillagers() below clears villagers, same reason
    talkTargetVillager    = nullptr;
    enemies.clear();
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

// ------------------------------------------------------------------ techniques
//
// Right-click-menu-triggered technique attacks. Unlike the plain "Attack" option
// these require the player to already be adjacent — no auto-walk-then-technique
// chase yet, that's a follow-up once techniques prove out.

// Per-technique modifiers fed into resolveAttack(); shared by both target types below.
// unaware is an input here (caller already knows — only a sleeping Villager counts
// right now) since Backstab's damage bonus only applies when it's actually true.
static void techniqueModifiers(TechniqueId id, bool unaware, int& hitBonus,
                               float& dmgMult, bool& deepWound) {
    hitBonus  = (id == TechniqueId::LUNGE)                    ? 25   : 0;
    dmgMult   = (id == TechniqueId::BACKSTAB && unaware)      ? 2.0f : 1.0f;
    deepWound = (id == TechniqueId::BRUTAL_STRIKE);
}

void useTechniqueOnEnemy(Enemy* enemy, int ex, int ey, TechniqueId id) {
    if (!enemy || !enemy->alive) return;
    const Technique& t = techniqueInfo(id);
    int dist = std::max(std::abs(ex - player.x), std::abs(ey - player.y));
    if (dist > t.range) {
        panel.addMessage("You need to be right next to them to use " + std::string(t.name) + ".");
        return;
    }
    if (!player.hasStamina(t.staminaCost)) {
        panel.addMessage("Not enough stamina for " + std::string(t.name) + "!");
        return;
    }
    player.spendStamina(t.staminaCost);

    int hitBonus; float dmgMult; bool deepWound;
    techniqueModifiers(id, false, hitBonus, dmgMult, deepWound); // enemies: no "unaware" tracking yet

    const Item*  weapon = player.weaponItem();
    AttackResult r = resolveAttack(player, *enemy, player.effectiveStr(), player.effectiveDex(),
                                    enemy->dexterity, 0, weapon, false, hitBonus, dmgMult, deepWound);
    if (!r.hit) {
        panel.addMessage(std::string(t.name) + "! You miss " + enemy->name + ".");
    } else {
        panel.addMessage(std::string(t.name) + "! You hit " + enemy->name + "'s " + partName(r.part)
                         + " for " + std::to_string(r.damage) + " damage.");
        reportSkillUp(r);
        if (!enemy->isAlive()) {
            panel.addMessage(enemy->name + " dies.");
            dropEnemyLoot(*enemy);
        }
    }
    onPlayerAct(t.extraEnergy);
}

void useTechniqueOnVillager(int vi, TechniqueId id) {
    if (vi < 0 || vi >= (int)villagers.size()) return;
    Villager& v = villagers[vi];
    if (!v.alive) return;
    const Technique& t = techniqueInfo(id);
    int dist = std::max(std::abs(v.x - player.x), std::abs(v.y - player.y));
    if (dist > t.range) {
        panel.addMessage("You need to be right next to them to use " + std::string(t.name) + ".");
        return;
    }
    if (!player.hasStamina(t.staminaCost)) {
        panel.addMessage("Not enough stamina for " + std::string(t.name) + "!");
        return;
    }
    player.spendStamina(t.staminaCost);

    bool unaware = (v.state == Villager::State::SLEEP);
    int hitBonus; float dmgMult; bool deepWound;
    techniqueModifiers(id, unaware, hitBonus, dmgMult, deepWound);

    const Item*  weapon = player.weaponItem();
    AttackResult r = resolveAttack(player, v, player.effectiveStr(), player.effectiveDex(),
                                    v.dexterity, 0, weapon, unaware, hitBonus, dmgMult, deepWound);
    if (id == TechniqueId::BACKSTAB && !unaware)
        panel.addMessage(v.name + " notices you coming — the strike lands clean, but without the element of surprise.");
    if (!r.hit) {
        panel.addMessage(std::string(t.name) + "! You miss " + v.name + ".");
    } else {
        panel.addMessage(std::string(t.name) + "! You hit " + v.name + "'s " + partName(r.part)
                         + " for " + std::to_string(r.damage) + " damage.");
        reportSkillUp(r);
        if (!v.isAlive()) {
            panel.addMessage(v.name + " dies.");
            dropVillagerLoot(v);
        } else {
            villagerReactToAttack(v);
        }
    }
    onPlayerAct(t.extraEnergy);
}

// Purely cosmetic flight from caster to target — see SpellProjectile's comment
// near its declaration for why this doesn't gate damage resolution.
void spawnSpellProjectile(int fromX, int fromY, int toX, int toY, const Spell& s) {
    spellProjectile.active    = true;
    spellProjectile.fromX     = (float)fromX;
    spellProjectile.fromY     = (float)fromY;
    spellProjectile.toX       = (float)toX;
    spellProjectile.toY       = (float)toY;
    spellProjectile.startTime = SDL_GetTicks();
    spellProjectile.symbol    = s.symbol;
    spellProjectile.color     = s.color;
}

// See SpellBurst's comment near its declaration. delay defaults to matching
// a normal projectile's flight time; ground-magic callers (Stone Wall/
// Architect) that never spawn a projectile pass 0 for an instant flash.
void spawnSpellBurst(int cx, int cy, int radius, SDL_Color color, Uint32 delay = 160) {
    spellBurst.active    = true;
    spellBurst.cx         = cx;
    spellBurst.cy         = cy;
    spellBurst.radius     = radius;
    spellBurst.startTime  = SDL_GetTicks();
    spellBurst.delay      = delay;
    spellBurst.color      = color;
}

// See KnockbackFlash's comment near its declaration.
void spawnKnockbackFlash(const Actor& who, int fromX, int fromY, int toX, int toY) {
    knockbackFlashes.push_back({(float)fromX, (float)fromY, (float)toX, (float)toY,
                                 SDL_GetTicks(), who.symbol, who.color});
}

// A fire spell landing on empty, flammable ground gets a chance to scorch it
// instead of just fizzling — docs/magic.md: "Іскра... підпалює траву". Purely
// a cosmetic ground-cover swap (no spreading/DoT fire hazard yet, no tile
// system exists for that). Returns true if this tile actually caught.
bool tryIgniteGround(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) return false;
    Tile& t = map[y][x];
    bool flammable = t.groundId == G_GRASS || t.groundId == G_TALL_GRASS
                   || t.groundId == G_MOSS  || t.groundId == G_LEAVES;
    if (!flammable) return false;
    if (rand() % 100 >= 30) return false;
    t.groundId = G_SCORCHED;
    return true;
}

// How much of a spell's raw damage actually gets through to a struck
// object's material — docs/magic.md doesn't spec numbers for this, these are
// our own calibration: fire chews through wood fast, barely scratches stone,
// and does something in-between to worked metal (heats it, doesn't consume
// it). Other schools don't model a material interaction yet — full damage
// until one does, same "no gap left unhandled" default as elsewhere.
inline float materialResistance(Skill school, Material mat) {
    if (school == Skill::FIRE) {
        switch (mat) {
            case Material::WOOD:  return 1.6f;
            case Material::STONE: return 0.15f;
            case Material::METAL: return 0.4f;
            default:               return 1.0f;
        }
    }
    return 1.0f;
}

// Any object caught in a spell's footprint takes real damage, scaled by how
// well its material resists that school (materialResistance() above) — a
// Fireball chews through a wooden door fast but barely dents a stone wall.
// Deterministic, unlike tryIgniteGround()'s cosmetic chance-roll — this is a
// real hit, same as chopping/mining, just from magic instead of a tool.
// Objects with durability 0 (the well) are the indestructible sentinel and
// just absorb the shot, matching interactWithObject()'s special-cased
// protection for them. Reuses destroyWorldObject() on a kill, so a
// magic-felled tree/log drops the same loot an axe would. Returns true if
// there was an object here at all (whether or not it broke), so the caller
// knows not to also roll a ground-scorch on the same tile.
bool applySpellObjectDamage(int x, int y, const Spell& s) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) return false;
    Tile& t = map[y][x];
    if (t.objectId < 0) return false;
    int oid = t.objectId;
    const ObjectDef& od = objectDefs[oid];
    if (od.durability <= 0) return true; // indestructible — shot is absorbed, nothing happens

    float resist = materialResistance(s.school, od.material);
    float spread = 0.85f + (rand() % 31) / 100.0f; // ±15%
    int   dmg    = std::max(1, (int)(s.baseDamage * resist * spread));
    t.objectHp  -= dmg;

    if (t.objectHp <= 0) {
        // destroyWorldObject() prints its own "it breaks/falls/shatters" message —
        // no separate "destroyed!" announcement here, same as interactWithObject().
        destroyWorldObject(x, y, oid);
    } else {
        panel.addMessage(std::string(s.name) + " hits the " + od.name + " for "
                         + std::to_string(dmg) + " damage.");
    }
    return true;
}

// A Water spell touching an active fire-wall tile snuffs it out early — the
// natural counter to Wall of Fire/Fireball scorch marks. Returns true if
// there was fire there to put out.
bool tryExtinguishFire(int x, int y) {
    for (auto it = fireHazards.begin(); it != fireHazards.end(); ++it) {
        if (it->x == x && it->y == y) { fireHazards.erase(it); return true; }
    }
    return false;
}

// Pushes target one tile directly away from (fromX,fromY) — Air's signature
// control effect (docs/magic.md: "Порив — Відштовхнути ворога на 1 тайл").
// Generic over Actor so it works for enemy/villager/player alike, matching
// the world-symmetry rule. No-ops silently (returns false, no message of its
// own — the caller decides what, if anything, to say) if the destination
// tile is off-map, unwalkable, or already occupied by someone else.
bool tryKnockback(Actor& target, int fromX, int fromY) {
    int dx = target.x - fromX, dy = target.y - fromY;
    int sx = (dx > 0) - (dx < 0);
    int sy = (dy > 0) - (dy < 0);
    if (sx == 0 && sy == 0) return false; // caster and target share a tile — no direction to push
    int nx = target.x + sx, ny = target.y + sy;
    if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) return false;
    if (!map[ny][nx].walkable()) return false;
    if (getEnemyAt(nx, ny) || getVillagerAt(nx, ny)) return false;
    if (player.x == nx && player.y == ny) return false;
    spawnKnockbackFlash(target, target.x, target.y, nx, ny);
    target.x = nx;
    target.y = ny;
    return true;
}

// Casts any spell (POINT/BURST) at a clicked tile — no actor has to be
// standing there, and that includes the player: if the blast reaches the
// caster's own tile (a BURST cast at your own feet, or a large enough
// radius that circles back), it hits them too, same formula as any other
// target — no free pass for self-inflicted splash. POINT only affects the
// exact impact tile; BURST covers every tile within aoeRadius of it. Any
// touched tile with nobody on it still deals real damage to whatever object
// sits there (applySpellObjectDamage(), scaled by material) and, failing
// that, gets a shot at scorching the ground cover (tryIgniteGround()) if the
// spell's school is Fire — a shot into empty ground is never wasted.
// rainCall/buildsWall/reclaimsWall spells short-circuit all of the above —
// see their own branches near the top of the function body. manualArea/
// manualBuild spells (Wall of Fire, Architect) don't go through here at all
// — see confirmWallTargeting(); throwsWall (Wall Throw) doesn't either — see
// castWallThrow()/resolveWallThrowClick().
void useSpellAtTile(int tx, int ty, SpellId id) {
    const Spell& s = spellInfo(id);
    if (!player.hasStamina(s.staminaCost)) {
        panel.addMessage("Not enough stamina to cast " + std::string(s.name) + "!");
        return;
    }
    player.spendStamina(s.staminaCost);

    SpellCastRoll roll = rollSpellCast(player, id);
    if (roll.backfired) {
        panel.addMessage(std::string(s.name) + " backfires, hurting your "
                         + partName(PartTarget::ARM_R) + " for "
                         + std::to_string(roll.selfDamage) + " damage!");
        if (!player.isAlive()) panel.addMessage("The backfire kills you.");
        onPlayerAct(s.extraEnergy);
        return;
    }

    // Ground magic (buildsWall/reclaimsWall) never flies through the air —
    // no projectile for those. throwsWall (Wall Throw) doesn't come through
    // here at all anymore — see castWallThrow()/resolveWallThrowClick().
    bool groundMagic = s.buildsWall || s.reclaimsWall;
    if (!groundMagic) {
        spawnSpellProjectile(player.x, player.y, tx, ty, s);
        if (s.shape == SpellShape::BURST) spawnSpellBurst(tx, ty, s.aoeRadius, s.color);
    }

    // rainCall (Rain Call) and buildsWall (Stone Wall) don't hit actors at
    // all — pure map/hazard effects, resolved and returned here before the
    // normal footprint/damage logic ever runs.
    if (s.rainCall) {
        int extinguished = 0;
        for (int y = ty - s.aoeRadius; y <= ty + s.aoeRadius; y++) {
            for (int x = tx - s.aoeRadius; x <= tx + s.aoeRadius; x++) {
                if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) continue;
                if (std::max(std::abs(x - tx), std::abs(y - ty)) > s.aoeRadius) continue;
                if (tryExtinguishFire(x, y)) extinguished++;
                if (map[y][x].groundId == G_SCORCHED) map[y][x].groundId = G_GRASS;
            }
        }
        panel.addMessage(extinguished > 0 ? "Rain pours down, dousing the flames!"
                                           : "Rain pours down over the area.");
        if (roll.leveledUp)
            panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                             + std::to_string(roll.newSkillLevel) + "!");
        onPlayerAct(s.extraEnergy);
        return;
    }
    if (s.buildsWall) {
        bool blocked = !map[ty][tx].walkable() || map[ty][tx].objectId >= 0
                     || getEnemyAt(tx, ty) || getVillagerAt(tx, ty)
                     || (tx == player.x && ty == player.y);
        if (blocked) {
            panel.addMessage(std::string(s.name) + " fizzles — the ground there isn't clear.");
        } else {
            map[ty][tx].objectId = O_WALL;
            map[ty][tx].objectHp = objectDefs[O_WALL].durability;
            spawnSpellBurst(tx, ty, 0, s.color, 0); // ground shudders right where it rises — no thrown dart
            panel.addMessage("A slab of stone rises from the earth!");
            updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
        }
        if (roll.leveledUp)
            panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                             + std::to_string(roll.newSkillLevel) + "!");
        onPlayerAct(s.extraEnergy);
        return;
    }
    if (s.reclaimsWall) {
        if (map[ty][tx].objectId != O_WALL) {
            panel.addMessage(std::string(s.name) + " fizzles — there's no stone wall there.");
        } else {
            map[ty][tx].objectId = -1;
            map[ty][tx].objectHp = 0;
            spawnSpellBurst(tx, ty, 0, s.color, 0);
            panel.addMessage("The wall sinks back into the earth.");
            updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
        }
        if (roll.leveledUp)
            panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                             + std::to_string(roll.newSkillLevel) + "!");
        onPlayerAct(s.extraEnergy);
        return;
    }
    std::vector<SDL_Point> footprint;
    if (s.shape == SpellShape::BURST) {
        for (int y = ty - s.aoeRadius; y <= ty + s.aoeRadius; y++)
            for (int x = tx - s.aoeRadius; x <= tx + s.aoeRadius; x++)
                if (x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT &&
                    std::max(std::abs(x - tx), std::abs(y - ty)) <= s.aoeRadius)
                    footprint.push_back({x, y});
    } else { // POINT
        footprint.push_back({tx, ty});
    }

    std::vector<Enemy*>    hitEnemies;
    std::vector<Villager*> hitVillagers;
    bool hitPlayer      = false;
    bool hitObject      = false;
    bool ignitedAny     = false;
    bool extinguishedAny = false;
    for (const SDL_Point& t : footprint) {
        if (s.school == Skill::WATER && tryExtinguishFire(t.x, t.y)) extinguishedAny = true;
        if (t.x == player.x && t.y == player.y) { hitPlayer = true; continue; }
        if (Enemy* e = getEnemyAt(t.x, t.y)) { hitEnemies.push_back(e); continue; }
        if (Villager* v = getVillagerAt(t.x, t.y)) { hitVillagers.push_back(v); continue; }
        if (applySpellObjectDamage(t.x, t.y, s)) { hitObject = true; continue; }
        if (s.school == Skill::FIRE && tryIgniteGround(t.x, t.y)) ignitedAny = true;
    }

    if (extinguishedAny) panel.addMessage("Steam hisses as the flames go out.");

    if (!hitPlayer && !hitObject && hitEnemies.empty() && hitVillagers.empty()) {
        panel.addMessage("You cast " + std::string(s.name)
                         + (ignitedAny ? " — flames catch on whatever's there."
                                       : " — it lands on empty ground."));
    }

    if (hitPlayer) {
        SpellResult r = resolveSpellHit(player, player, id);
        panel.addMessage("Your own " + std::string(s.name) + " engulfs your "
                         + partName(r.part) + " for " + std::to_string(r.damage) + " damage!");
        if (!player.isAlive()) panel.addMessage("You are consumed by your own spell.");
    }

    for (Enemy* e : hitEnemies) {
        SpellResult r = resolveSpellHit(player, *e, id);
        panel.addMessage(std::string(s.name) + " hits " + e->name + "'s "
                         + partName(r.part) + " for " + std::to_string(r.damage) + " damage.");
        if (!e->isAlive()) {
            panel.addMessage(e->name + " dies" + (s.school == Skill::FIRE ? ", charred." : "."));
            dropEnemyLoot(*e);
        } else {
            if (s.knockback && tryKnockback(*e, player.x, player.y))
                panel.addMessage(e->name + " is thrown back by the wind!");
            if (s.slows) {
                e->slowedTicks = s.buffTurns;
                panel.addMessage(e->name + " is slowed by the water!");
            }
        }
    }
    for (Villager* v : hitVillagers) {
        SpellResult r = resolveSpellHit(player, *v, id);
        panel.addMessage(std::string(s.name) + " hits " + v->name + "'s "
                         + partName(r.part) + " for " + std::to_string(r.damage) + " damage.");
        if (!v->isAlive()) {
            panel.addMessage(v->name + " dies" + (s.school == Skill::FIRE ? ", charred." : "."));
            dropVillagerLoot(*v);
        } else {
            villagerReactToAttack(*v);
            if (s.knockback && tryKnockback(*v, player.x, player.y))
                panel.addMessage(v->name + " is thrown back by the wind!");
            if (s.slows) {
                v->slowedTicks = s.buffTurns;
                panel.addMessage(v->name + " is slowed by the water!");
            }
        }
    }

    if (roll.leveledUp)
        panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                         + std::to_string(roll.newSkillLevel) + "!");

    onPlayerAct(s.extraEnergy);
}

// Resolves a selfCast spell (Create Water, or a buffTurns spell — Fire
// Shield/Skin Hardening/Lightness) immediately — no targeting mode, no
// click to wait on, no travel. Still spends stamina and rolls the usual
// single backfire per docs/magic.md's "невдалі касти на низькій навичці",
// same as any other spell; only the "there's an impact point" part is
// skipped. Each selfCast spell gets its own branch by SpellId rather than a
// generic "utility effect" field — there are only a handful of these.
void useSelfSpell(SpellId id) {
    const Spell& s = spellInfo(id);
    if (!player.hasStamina(s.staminaCost)) {
        panel.addMessage("Not enough stamina to cast " + std::string(s.name) + "!");
        return;
    }
    player.spendStamina(s.staminaCost);

    SpellCastRoll roll = rollSpellCast(player, id);
    if (roll.backfired) {
        panel.addMessage(std::string(s.name) + " backfires, hurting your "
                         + partName(PartTarget::ARM_R) + " for "
                         + std::to_string(roll.selfDamage) + " damage!");
        if (!player.isAlive()) panel.addMessage("The backfire kills you.");
        onPlayerAct(s.extraEnergy);
        return;
    }

    if (id == SpellId::CREATE_WATER) {
        bool wasThirsty = player.thirst > 0.0f;
        player.thirst = std::max(0.0f, player.thirst - 0.5f);
        panel.addMessage(wasThirsty
            ? "You conjure a stream of water from the air and drink deeply."
            : "You conjure water from the air, but you're not thirsty.");
    } else if (id == SpellId::FIRE_SHIELD) {
        player.fireShieldTicks = s.buffTurns;
        panel.addMessage("Shimmering heat wraps around you.");
    } else if (id == SpellId::SKIN_HARDENING) {
        player.skinHardenTicks = s.buffTurns;
        panel.addMessage("Your skin hardens like stone.");
    } else if (id == SpellId::LIGHTNESS) {
        player.lightnessTicks = s.buffTurns;
        panel.addMessage("Your steps grow lighter and faster.");
    } else if (id == SpellId::ACCELERATION) {
        player.accelTicks = s.buffTurns;
        panel.addMessage("A surge of wind carries you forward!");
    } else if (id == SpellId::MINOR_HEAL) {
        int healAmt = 15;
        int before  = player.body.torso.hp;
        player.body.torso.hp = std::min(player.body.torso.maxHp, player.body.torso.hp + healAmt);
        player.sync();
        int healed = player.body.torso.hp - before;
        panel.addMessage(healed > 0
            ? "Warm light knits your wounds — you recover " + std::to_string(healed) + " HP."
            : "Warm light washes over you, but you're already at full health.");
    }

    if (roll.leveledUp)
        panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                         + std::to_string(roll.newSkillLevel) + "!");

    onPlayerAct(s.extraEnergy);
}

// Ends hotbar-targeting mode (technique or spell). msg empty = a target was
// found and the action already fired (or the mode is being torn down
// silently); non-empty = shown as a log message (cancelled / out of range /
// nothing there).
void cancelHotbarTargeting(const std::string& msg) {
    hotbarTargeting  = false;
    hotbarTargetSlot = -1;
    if (!msg.empty()) panel.addMessage(msg);
}

// Called with the tile the player clicked while targeting mode is active (see
// the hotbarTargeting block in handleInput()). Must be within the bound
// action's range. Techniques (melee) still need an enemy or villager on that
// exact tile; spells never require one — they always land, see
// useSpellAtTile(). Anything invalid cancels rather than silently doing
// nothing, so a stray click doesn't leave the player stuck in targeting mode.
void resolveHotbarTargeting(int mx, int my) {
    int raw = hotbar.slots[hotbarTargetSlot];
    bool isSpell = Hotbar::isSpellSlot(raw);
    int  range   = isSpell ? spellInfo(Hotbar::spellOf(raw)).range
                            : techniqueInfo(Hotbar::techniqueOf(raw)).range;

    if (mx < 0 || mx >= MAP_WIDTH || my < 0 || my >= MAP_HEIGHT) {
        cancelHotbarTargeting("Out of range.");
        return;
    }
    int dist = std::max(std::abs(mx - player.x), std::abs(my - player.y));
    if (dist > range || !map[my][mx].visible) {
        cancelHotbarTargeting("Out of range.");
        return;
    }

    // Spells always land wherever the player clicks — no actor has to be
    // standing on that exact tile (see useSpellAtTile()). Techniques are
    // melee, so they still need a real target under the cursor.
    if (isSpell) {
        cancelHotbarTargeting("");
        useSpellAtTile(mx, my, Hotbar::spellOf(raw));
        return;
    }

    if (Enemy* enemy = getEnemyAt(mx, my)) {
        cancelHotbarTargeting("");
        useTechniqueOnEnemy(enemy, mx, my, Hotbar::techniqueOf(raw));
        return;
    }
    if (Villager* v = getVillagerAt(mx, my)) {
        int vi = (int)(v - &villagers[0]);
        cancelHotbarTargeting("");
        useTechniqueOnVillager(vi, Hotbar::techniqueOf(raw));
        return;
    }
    cancelHotbarTargeting("No target there.");
}

// Triggered by the hotbar (number key or mouse click on a slot). Doesn't pick
// a target itself — it enters targeting mode so the player clicks who to hit
// among whatever's actually in range (renderHotbarTargeting() paints the
// range, resolveHotbarTargeting() above resolves the click).
void useHotbarSlot(int slot) {
    if (slot < 0 || slot >= Hotbar::SLOT_COUNT) return;
    int raw = hotbar.slots[slot];
    if (raw < 0) { panel.addMessage("That hotbar slot is empty."); return; }

    if (Hotbar::isSpellSlot(raw)) {
        SpellId id = Hotbar::spellOf(raw);
        const Spell& s = spellInfo(id);
        if (!spellUnlocked(player, id)) {
            panel.addMessage("You haven't learned " + std::string(s.name) + " yet.");
            return;
        }
        if (!player.hasStamina(s.staminaCost)) {
            panel.addMessage("Not enough stamina to cast " + std::string(s.name) + "!");
            return;
        }

        if (s.manualArea || s.manualBuild) {
            wallTargeting  = true;
            wallTargetSlot = slot;
            wallSelection.clear();
            panel.addMessage("Click tiles to mark " + std::string(s.name)
                             + " (Enter to cast, right-click/Esc to cancel).");
            return;
        }

        if (s.selfCast) {
            useSelfSpell(id);
            return;
        }

        if (s.throwsWall) {
            wallThrowTargeting     = true;
            wallThrowSlot          = slot;
            wallThrowPickingSource = true;
            wallThrowSrcX = wallThrowSrcY = -1;
            panel.addMessage("Click a stone wall to hurl (right-click/Esc to cancel).");
            return;
        }

        hotbarTargeting  = true;
        hotbarTargetSlot = slot;
        panel.addMessage("Select a target for " + std::string(s.name)
                         + " (right-click or Esc to cancel).");
        return;
    }

    TechniqueId id = Hotbar::techniqueOf(raw);
    const Technique& t = techniqueInfo(id);
    if (!techniqueUnlocked(player, id)) {
        panel.addMessage("You haven't learned " + std::string(t.name) + " yet.");
        return;
    }
    if (!player.hasStamina(t.staminaCost)) {
        panel.addMessage("Not enough stamina for " + std::string(t.name) + "!");
        return;
    }

    hotbarTargeting  = true;
    hotbarTargetSlot = slot;
    panel.addMessage("Select a target for " + std::string(t.name)
                     + " (right-click or Esc to cancel).");
}

// ------------------------------------------------------------------ Wall of Fire (manualArea)
//
// Separate multi-tile targeting flow for manualArea spells — see wallTargeting's
// comment near its declaration. useHotbarSlot() enters this mode instead of the
// normal single-click hotbarTargeting when the bound spell has manualArea set.

// Ends wallTargeting. msg empty = the cast already resolved (or the mode is
// being torn down silently before resolving); non-empty = shown as a log
// message (cancelled).
void cancelWallTargeting(const std::string& msg) {
    wallTargeting  = false;
    wallTargetSlot = -1;
    wallSelection.clear();
    if (!msg.empty()) panel.addMessage(msg);
}

// Adds or removes one tile from the in-progress selection — clicking an
// already-marked tile un-marks it, mirroring Hotbar::assign()'s toggle
// affordance. Reports the running tile count and stamina cost so the player
// can see exactly what a bigger wall will cost before confirming.
void toggleWallTile(int mx, int my) {
    if (wallTargetSlot < 0) return;
    int raw = hotbar.slots[wallTargetSlot];
    if (raw < 0 || !Hotbar::isSpellSlot(raw)) { cancelWallTargeting(""); return; }
    const Spell& s = spellInfo(Hotbar::spellOf(raw));

    if (mx < 0 || mx >= MAP_WIDTH || my < 0 || my >= MAP_HEIGHT) return;
    int dist = std::max(std::abs(mx - player.x), std::abs(my - player.y));
    if (dist > s.range || !map[my][mx].visible) {
        panel.addMessage("Out of range.");
        return;
    }

    auto it = std::find_if(wallSelection.begin(), wallSelection.end(),
                            [&](const SDL_Point& p) { return p.x == mx && p.y == my; });
    if (it != wallSelection.end()) wallSelection.erase(it);
    else                           wallSelection.push_back({mx, my});

    float cost = s.staminaCost * wallSelection.size();
    panel.addMessage(std::to_string(wallSelection.size()) + " tile"
                     + (wallSelection.size() == 1 ? "" : "s") + " marked — "
                     + std::to_string((int)cost) + " stamina. (Enter to cast)");
}

// Adds/refreshes one burning tile — an already-burning tile just has its
// timer and damage reset rather than stacking a second entry, so re-marking
// the same tile in a later cast doesn't create duplicate hazards on it.
void igniteFireHazardTile(int x, int y, int turns, int dmgPerTurn) {
    for (FireHazardTile& f : fireHazards) {
        if (f.x == x && f.y == y) { f.turnsLeft = turns; f.dmgPerTurn = dmgPerTurn; return; }
    }
    fireHazards.push_back({x, y, turns, dmgPerTurn});
}

// Confirms the current wallSelection and casts — spends staminaCost per
// marked tile, rolls the usual single backfire for the whole cast, then
// either (manualBuild, Architect) raises a wall on every clear marked tile,
// or (manualArea, Wall of Fire) ignites every marked tile — immediate hit if
// someone's standing there, including the caster if they marked their own
// tile; applySpellObjectDamage/tryIgniteGround if not — and lights a
// lingering FireHazardTile on each so tickFireHazards() keeps burning it for
// hazardTurns more player actions.
void confirmWallTargeting() {
    int slot = wallTargetSlot;
    std::vector<SDL_Point> tiles = wallSelection;
    cancelWallTargeting("");

    if (slot < 0) return;
    if (tiles.empty()) { panel.addMessage("You need to mark at least one tile."); return; }

    int raw = hotbar.slots[slot];
    if (raw < 0 || !Hotbar::isSpellSlot(raw)) return;
    SpellId id = Hotbar::spellOf(raw);
    const Spell& s = spellInfo(id);

    float totalCost = s.staminaCost * tiles.size();
    if (!player.hasStamina(totalCost)) {
        panel.addMessage("Not enough stamina to hold " + std::to_string(tiles.size())
                         + " tiles of " + std::string(s.name) + " (needs "
                         + std::to_string((int)totalCost) + ").");
        return;
    }
    player.spendStamina(totalCost);

    SpellCastRoll roll = rollSpellCast(player, id);
    if (roll.backfired) {
        panel.addMessage(std::string(s.name) + " backfires, hurting your "
                         + partName(PartTarget::ARM_R) + " for "
                         + std::to_string(roll.selfDamage) + " damage!");
        if (!player.isAlive()) panel.addMessage("The backfire kills you.");
        onPlayerAct(s.extraEnergy);
        return;
    }

    // manualBuild (Architect) raises a permanent wall on every clear marked
    // tile instead of igniting a FireHazardTile — construction, not
    // destruction, so it skips the rest of this function entirely.
    if (s.manualBuild) {
        int built = 0;
        for (const SDL_Point& t : tiles) {
            bool blocked = !map[t.y][t.x].walkable() || map[t.y][t.x].objectId >= 0
                         || getEnemyAt(t.x, t.y) || getVillagerAt(t.x, t.y)
                         || (t.x == player.x && t.y == player.y);
            if (blocked) continue;
            map[t.y][t.x].objectId = O_WALL;
            map[t.y][t.x].objectHp = objectDefs[O_WALL].durability;
            spawnSpellBurst(t.x, t.y, 0, s.color, 0);
            built++;
        }
        panel.addMessage(built > 0
            ? "You raise " + std::to_string(built) + " slab" + (built == 1 ? "" : "s")
              + " of stone from the earth!"
            : "The earth doesn't stir — every marked tile was already occupied.");
        updateVisibility(worldTime.viewRadius(player.totalLightRadius()), (int)(worldTime.darkness() * 7.0f));
        if (roll.leveledUp)
            panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                             + std::to_string(roll.newSkillLevel) + "!");
        onPlayerAct(s.extraEnergy);
        return;
    }

    int  hitCount   = 0;
    bool ignitedAny = false;
    for (const SDL_Point& t : tiles) {
        igniteFireHazardTile(t.x, t.y, s.hazardTurns, s.baseDamage);

        if (t.x == player.x && t.y == player.y) {
            SpellResult r = resolveSpellHit(player, player, id);
            panel.addMessage("Your own " + std::string(s.name) + " engulfs your "
                             + partName(r.part) + " for " + std::to_string(r.damage) + " damage!");
            hitCount++;
            if (!player.isAlive()) panel.addMessage("You are consumed by your own spell.");
            continue;
        }
        if (Enemy* e = getEnemyAt(t.x, t.y)) {
            SpellResult r = resolveSpellHit(player, *e, id);
            panel.addMessage(std::string(s.name) + " engulfs " + e->name + "'s "
                             + partName(r.part) + " for " + std::to_string(r.damage) + " damage.");
            hitCount++;
            if (!e->isAlive()) { panel.addMessage(e->name + " dies, charred."); dropEnemyLoot(*e); }
            continue;
        }
        if (Villager* v = getVillagerAt(t.x, t.y)) {
            SpellResult r = resolveSpellHit(player, *v, id);
            panel.addMessage(std::string(s.name) + " engulfs " + v->name + "'s "
                             + partName(r.part) + " for " + std::to_string(r.damage) + " damage.");
            hitCount++;
            if (!v->isAlive()) { panel.addMessage(v->name + " dies, charred."); dropVillagerLoot(*v); }
            else villagerReactToAttack(*v);
            continue;
        }
        if (applySpellObjectDamage(t.x, t.y, s)) { hitCount++; continue; }
        if (s.school == Skill::FIRE && tryIgniteGround(t.x, t.y)) ignitedAny = true;
    }

    panel.addMessage("A wall of fire roars up across " + std::to_string(tiles.size()) + " tile"
                     + (tiles.size() == 1 ? "" : "s") + "."
                     + (hitCount == 0 && ignitedAny ? " Flames catch on whatever's there." : ""));

    if (roll.leveledUp)
        panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                         + std::to_string(roll.newSkillLevel) + "!");

    onPlayerAct(s.extraEnergy);
}

// Burns whoever's standing in an active fire-wall tile, once per player
// action (same cadence as tickEnemyNeeds()/tickVillagerNeeds()), then ages
// the tile down and clears it once its turns run out.
void tickFireHazards() {
    for (auto it = fireHazards.begin(); it != fireHazards.end(); ) {
        FireHazardTile& f = *it;

        if (player.x == f.x && player.y == f.y && player.isAlive()) {
            player.takeDamage(f.dmgPerTurn, randomHitPart());
            panel.addMessage("The flames burn you for " + std::to_string(f.dmgPerTurn) + " damage!");
            if (!player.isAlive()) panel.addMessage("The fire consumes you.");
        }
        if (Enemy* e = getEnemyAt(f.x, f.y)) {
            bool wasAlive = e->isAlive();
            e->takeDamage(f.dmgPerTurn, randomHitPart());
            if (wasAlive && !e->isAlive()) {
                panel.addMessage(e->name + " burns to death in the flames.");
                dropEnemyLoot(*e);
            }
        }
        if (Villager* v = getVillagerAt(f.x, f.y)) {
            bool wasAlive = v->isAlive();
            v->takeDamage(f.dmgPerTurn, randomHitPart());
            if (wasAlive && !v->isAlive()) {
                panel.addMessage(v->name + " burns to death in the flames.");
                dropVillagerLoot(*v);
            } else {
                villagerReactToAttack(*v);
            }
        }

        f.turnsLeft--;
        if (f.turnsLeft <= 0) it = fireHazards.erase(it);
        else                  ++it;
    }
}

// ------------------------------------------------------------------ Wall Throw (two-phase)
//
// Separate two-click targeting flow for throwsWall spells (Wall Throw) —
// see wallThrowTargeting's comment near its declaration. useHotbarSlot()
// enters this mode instead of hotbarTargeting/wallTargeting when the bound
// spell has throwsWall set.

// Ends wallThrowTargeting. msg empty = the cast already resolved (or the
// mode is being torn down silently before resolving); non-empty = shown as
// a log message (cancelled / invalid pick).
void cancelWallThrowTargeting(const std::string& msg) {
    wallThrowTargeting     = false;
    wallThrowSlot          = -1;
    wallThrowPickingSource = true;
    wallThrowSrcX = wallThrowSrcY = -1;
    if (!msg.empty()) panel.addMessage(msg);
}

// Actually resolves a Wall Throw once both the source wall and the target
// tile are known — spends stamina, rolls the usual single backfire, then
// un-makes the wall and hurls it. Same resolution useSpellAtTile() used to
// do inline for this spell before picking the wall became a player choice.
void castWallThrow(int wallX, int wallY, int tx, int ty, SpellId id) {
    const Spell& s = spellInfo(id);
    if (!player.hasStamina(s.staminaCost)) {
        panel.addMessage("Not enough stamina to cast " + std::string(s.name) + "!");
        return;
    }
    player.spendStamina(s.staminaCost);

    SpellCastRoll roll = rollSpellCast(player, id);
    if (roll.backfired) {
        panel.addMessage(std::string(s.name) + " backfires, hurting your "
                         + partName(PartTarget::ARM_R) + " for "
                         + std::to_string(roll.selfDamage) + " damage!");
        if (!player.isAlive()) panel.addMessage("The backfire kills you.");
        onPlayerAct(s.extraEnergy);
        return;
    }

    map[wallY][wallX].objectId = -1;
    map[wallY][wallX].objectHp = 0;
    spawnSpellProjectile(wallX, wallY, tx, ty, s); // flies from the wall, not the caster

    if (tx == player.x && ty == player.y) {
        SpellResult r = resolveSpellHit(player, player, id);
        panel.addMessage(std::string("The flying wall slams into your own ") + partName(r.part)
                         + " for " + std::to_string(r.damage) + " damage!");
        if (!player.isAlive()) panel.addMessage("You are crushed by your own spell.");
    } else if (Enemy* e = getEnemyAt(tx, ty)) {
        SpellResult r = resolveSpellHit(player, *e, id);
        panel.addMessage("The flying wall slams into " + e->name + "'s " + partName(r.part)
                         + " for " + std::to_string(r.damage) + " damage!");
        if (!e->isAlive()) { panel.addMessage(e->name + " is crushed."); dropEnemyLoot(*e); }
        else if (s.knockback && tryKnockback(*e, wallX, wallY))
            panel.addMessage(e->name + " is thrown back by the impact!");
    } else if (Villager* v = getVillagerAt(tx, ty)) {
        SpellResult r = resolveSpellHit(player, *v, id);
        panel.addMessage("The flying wall slams into " + v->name + "'s " + partName(r.part)
                         + " for " + std::to_string(r.damage) + " damage!");
        if (!v->isAlive()) { panel.addMessage(v->name + " is crushed."); dropVillagerLoot(*v); }
        else {
            villagerReactToAttack(*v);
            if (s.knockback && tryKnockback(*v, wallX, wallY))
                panel.addMessage(v->name + " is thrown back by the impact!");
        }
    } else {
        applySpellObjectDamage(tx, ty, s);
        panel.addMessage("The wall crashes into the ground.");
    }

    if (roll.leveledUp)
        panel.addMessage(std::string(skillName(roll.skillUsed)) + " skill increased to "
                         + std::to_string(roll.newSkillLevel) + "!");
    onPlayerAct(s.extraEnergy);
}

// Handles one click while wallThrowTargeting is active. Phase 1 (picking the
// source) accepts any O_WALL within WALL_THROW_PICK_RADIUS of the player —
// a much more generous radius than the spell's own range, since that range
// is for aiming at the target, not for how far you can reach for ammunition.
// Phase 2 (picking the target) uses the spell's normal range and, once
// clicked, actually casts via castWallThrow(). An invalid pick at either
// phase cancels the whole thing rather than re-prompting.
void resolveWallThrowClick(int mx, int my) {
    if (wallThrowSlot < 0) { cancelWallThrowTargeting(""); return; }
    int raw = hotbar.slots[wallThrowSlot];
    if (raw < 0 || !Hotbar::isSpellSlot(raw)) { cancelWallThrowTargeting(""); return; }
    SpellId id = Hotbar::spellOf(raw);
    const Spell& s = spellInfo(id);

    if (mx < 0 || mx >= MAP_WIDTH || my < 0 || my >= MAP_HEIGHT || !map[my][mx].visible) {
        cancelWallThrowTargeting("Out of range.");
        return;
    }

    if (wallThrowPickingSource) {
        int dist = std::max(std::abs(mx - player.x), std::abs(my - player.y));
        if (dist > WALL_THROW_PICK_RADIUS || map[my][mx].objectId != O_WALL) {
            cancelWallThrowTargeting("That's not a stone wall within reach.");
            return;
        }
        wallThrowSrcX = mx;
        wallThrowSrcY = my;
        wallThrowPickingSource = false;
        panel.addMessage("Now click a target to hurl the wall at (right-click/Esc to cancel).");
        return;
    }

    int dist = std::max(std::abs(mx - player.x), std::abs(my - player.y));
    if (dist > s.range) {
        cancelWallThrowTargeting("Out of range.");
        return;
    }

    int wallX = wallThrowSrcX, wallY = wallThrowSrcY;
    cancelWallThrowTargeting("");
    castWallThrow(wallX, wallY, mx, my, id);
}

// ------------------------------------------------------------------ input

void handleInput(SDL_Event& event, bool& running) {
    if (event.type == SDL_QUIT) running = false;

    // Alt+Enter toggles fullscreen/windowed from anywhere.
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN &&
        (event.key.keysym.mod & KMOD_ALT)) {
        toggleFullscreen();
        return;
    }

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

    // Dead player: nothing below this point should still run (movement,
    // combat, dialogue, targeting modes, etc.) — renderDeathScreen() already
    // draws the death overlay, but nothing was actually blocking further
    // input, so the game kept playing right through it.
    if (!player.isAlive()) return;

    // Hotbar targeting intercepts input while active — the highlighted-range
    // picker started by useHotbarSlot() for a technique or spell. Left-click on
    // a painted tile resolves it; right-click or Esc cancels without spending
    // the action.
    if (hotbarTargeting) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            cancelHotbarTargeting("Cancelled.");
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                cancelHotbarTargeting("Cancelled.");
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                int mx = event.button.x / TILE_SIZE + cameraX;
                int my = event.button.y / TILE_SIZE + cameraY;
                resolveHotbarTargeting(mx, my);
            }
        }
        return;
    }

    // Wall-of-Fire multi-tile targeting intercepts input while active — left-click
    // toggles a tile in/out of the selection, Enter confirms and casts, right-click
    // or Esc cancels the whole thing without spending anything.
    if (wallTargeting) {
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                cancelWallTargeting("Cancelled.");
            } else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER) {
                confirmWallTargeting();
            }
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                cancelWallTargeting("Cancelled.");
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                int mx = event.button.x / TILE_SIZE + cameraX;
                int my = event.button.y / TILE_SIZE + cameraY;
                toggleWallTile(mx, my);
            }
        }
        return;
    }

    // Wall Throw's two-click targeting (pick the wall, then pick the
    // target) intercepts input the same way — see wallThrowTargeting's
    // comment near its declaration.
    if (wallThrowTargeting) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            cancelWallThrowTargeting("Cancelled.");
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                cancelWallThrowTargeting("Cancelled.");
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                int mx = event.button.x / TILE_SIZE + cameraX;
                int my = event.button.y / TILE_SIZE + cameraY;
                resolveWallThrowClick(mx, my);
            }
        }
        return;
    }

    // Hub tab strip takes priority over whichever tab's own input handling
    // below, so clicking a tab always switches — even out of Craft/Map,
    // which otherwise intercept all input themselves further down.
    if (hub.visible() && event.type == SDL_MOUSEBUTTONDOWN &&
        event.button.button == SDL_BUTTON_LEFT) {
        if (hub.closeButtonAt(event.button.x, event.button.y)) { closeMenuHub(); return; }
        MenuTab clicked = hub.tabAt(font, event.button.x, event.button.y);
        if (clicked != MenuTab::NONE) { openMenuTab(clicked); return; }
    }

    // Dialogue intercepts input while active — a click on the world shouldn't
    // act while the player is mid-conversation.
    if (panel.mode == PanelMode::DIALOGUE) {
        if (event.type == SDL_MOUSEMOTION)
            panel.handleMotion(event.motion.x, event.motion.y);
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
            panel.handleClick(event.button.x, event.button.y);
        if (event.type == SDL_KEYDOWN)
            panel.handleKey(event.key.keysym.sym);
        return;
    }

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
            SDL_Keycode k = event.key.keysym.sym;
            // C closes it, mirroring O/B/I/M which all toggle open/closed on the
            // same key. These also switch straight to another tab, same as
            // clicking the tab bar — 's' stays reserved for recipe-list
            // navigation (down), so Effects isn't reachable by hotkey here.
            if (k == SDLK_c) { closeMenuHub();                       return; }
            if (k == SDLK_o) { openMenuTab(MenuTab::CHARACTER);      return; }
            if (k == SDLK_b) { openMenuTab(MenuTab::BODY);           return; }
            if (k == SDLK_k) { openMenuTab(MenuTab::SKILLS);         return; }
            if (k == SDLK_y) { openMenuTab(MenuTab::TECHNIQUES);     return; }
            if (k == SDLK_p) { openMenuTab(MenuTab::MAGIC);          return; }
            if (k == SDLK_i) { openMenuTab(MenuTab::INVENTORY);      return; }
            if (k == SDLK_m) { openMenuTab(MenuTab::MAP);            return; }

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
                case SDLK_m:     closeMenuHub(); break;
                // Same tab-switch hotkeys as everywhere else — Map has no WASD
                // navigation of its own, so none of these conflict.
                case SDLK_o:     openMenuTab(MenuTab::CHARACTER); break;
                case SDLK_b:     openMenuTab(MenuTab::BODY);      break;
                case SDLK_s:     openMenuTab(MenuTab::EFFECTS);   break;
                case SDLK_k:     openMenuTab(MenuTab::SKILLS);    break;
                case SDLK_y:     openMenuTab(MenuTab::TECHNIQUES); break;
                case SDLK_p:     openMenuTab(MenuTab::MAGIC);      break;
                case SDLK_i:     openMenuTab(MenuTab::INVENTORY); break;
                case SDLK_c:     openMenuTab(MenuTab::CRAFT);     break;
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
        if (event.key.keysym.sym == SDLK_o)      toggleMenuTab(MenuTab::CHARACTER);
        if (event.key.keysym.sym == SDLK_b)      toggleMenuTab(MenuTab::BODY);
        if (event.key.keysym.sym == SDLK_s)      toggleMenuTab(MenuTab::EFFECTS);
        if (event.key.keysym.sym == SDLK_k)      toggleMenuTab(MenuTab::SKILLS);
        if (event.key.keysym.sym == SDLK_y)      toggleMenuTab(MenuTab::TECHNIQUES);
        if (event.key.keysym.sym == SDLK_p)      toggleMenuTab(MenuTab::MAGIC);
        if (event.key.keysym.sym == SDLK_e)      examinePanel.hide();
        if (event.key.keysym.sym == SDLK_m)      toggleMenuTab(MenuTab::MAP);
        if (event.key.keysym.sym == SDLK_i)      toggleMenuTab(MenuTab::INVENTORY);
        if (event.key.keysym.sym == SDLK_c)      toggleMenuTab(MenuTab::CRAFT);
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
        // Hub is modal while any of its tabs is open — swallow the rest of
        // gameplay's keys (wait/pickup) here, but ESC always closes it.
        if (hub.visible()) {
            if (event.key.keysym.sym == SDLK_ESCAPE) closeMenuHub();
            return;
        }
        // Hotbar: 1-9 use whatever technique is bound to that slot. Not while
        // mid-wait or a modal popup (context menu/pickup) is up front of the map.
        if (!isWaiting && !pickupPanel.visible && !contextMenu.visible) {
            SDL_Keycode k = event.key.keysym.sym;
            if (k >= SDLK_1 && k <= SDLK_9) useHotbarSlot((int)(k - SDLK_1));
        }
        if (event.key.keysym.sym == SDLK_SPACE &&
            !pickupPanel.visible && !contextMenu.visible)
            onPlayerAct();  // wait one turn
        if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN) {
        int mouseX = event.button.x / TILE_SIZE + cameraX;
        int mouseY = event.button.y / TILE_SIZE + cameraY;

        // Character/Body/Effects/Skills have no click handling of their own —
        // swallow clicks so they don't fall through to map movement/attacks.
        // Inventory/Techniques handle their own clicks further below; Craft/Map
        // already returned earlier in this function.
        if (hub.activeTab == MenuTab::CHARACTER || hub.activeTab == MenuTab::BODY ||
            hub.activeTab == MenuTab::EFFECTS   || hub.activeTab == MenuTab::SKILLS)
            return;
        if (techniquesPanel.visible) {
            techniquesPanel.handleClick(event.button.x, event.button.y, player, hotbar);
            return;
        }
        if (spellsPanel.visible) {
            spellsPanel.handleClick(event.button.x, event.button.y, player, hotbar);
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
            // Hotbar overlays the bottom of the map view — a click there uses
            // that slot instead of being read as a move/attack on the tile behind it.
            int hbSlot = hotbar.slotAt(event.button.x, event.button.y);
            if (hbSlot >= 0) {
                useHotbarSlot(hbSlot);
                return;
            }
            if (mouseX >= 0 && mouseX < MAP_WIDTH && mouseY >= 0 && mouseY < MAP_HEIGHT) {
                Enemy* enemy = getEnemyAt(mouseX, mouseY);
                int attackDist = std::max(std::abs(mouseX - player.x),
                                          std::abs(mouseY - player.y));
                if (enemy && map[mouseY][mouseX].visible) {
                    if (attackDist <= 1) {
                        const Item*  weapon = player.weaponItem();
                        AttackResult r = resolveAttack(player, *enemy, player.effectiveStr(),
                                                        player.effectiveDex(), enemy->dexterity, 0, weapon);
                        if (!r.hit) {
                            panel.addMessage("You swing and miss " + enemy->name + ".");
                        } else {
                            panel.addMessage("You hit " + enemy->name + "'s " + partName(r.part)
                                             + " for " + std::to_string(r.damage) + " damage.");
                            reportSkillUp(r);
                            if (!enemy->isAlive()) {
                                panel.addMessage(enemy->name + " dies.");
                                dropEnemyLoot(*enemy);
                            }
                        }
                        onPlayerAct();
                    } else {
                        // Walk toward enemy — path recalculates each step via attackTarget
                        attackTarget         = enemy;
                        attackTargetVillager = nullptr;
                        talkTargetVillager   = nullptr;
                        pendingAct   = PendingAct::NONE;
                        currentPath  = findPath(player.x, player.y, mouseX, mouseY);
                        pathIndex    = 1;
                    }
                } else if (Villager* v = getVillagerAt(mouseX, mouseY);
                           v && map[mouseY][mouseX].visible && v->isHostile()) {
                    // Left click only auto-attacks once they're already hostile
                    // (mid-fight or fleeing from an earlier hit) — a neutral
                    // villager can only be attacked deliberately, via the
                    // right-click "Attack" menu, so a stray click never kills
                    // an innocent by accident.
                    if (attackDist <= 1) {
                        const Item*  weapon  = player.weaponItem();
                        bool         unaware = v->state == Villager::State::SLEEP;
                        AttackResult r = resolveAttack(player, *v, player.effectiveStr(),
                                                        player.effectiveDex(), v->dexterity, 0,
                                                        weapon, unaware);
                        if (!r.hit) {
                            panel.addMessage("You swing and miss " + v->name + ".");
                        } else {
                            panel.addMessage("You hit " + v->name + "'s " + partName(r.part)
                                             + " for " + std::to_string(r.damage) + " damage.");
                            reportSkillUp(r);
                            if (!v->isAlive()) {
                                panel.addMessage(v->name + " dies.");
                                dropVillagerLoot(*v);
                            } else {
                                villagerReactToAttack(*v);
                            }
                        }
                        onPlayerAct();
                    } else {
                        // Walk toward them — path recalculates each step via attackTargetVillager.
                        attackTargetVillager = v;
                        attackTarget         = nullptr;
                        talkTargetVillager   = nullptr;
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
                    std::vector<MenuItem> enemyOpts;
                    enemyOpts.push_back({"Attack", [enemy, ex, ey]() {
                            int dist = std::max(std::abs(ex - player.x),
                                                std::abs(ey - player.y));
                            if (dist <= 1) {
                                const Item*  weapon = player.weaponItem();
                                AttackResult r = resolveAttack(player, *enemy, player.effectiveStr(),
                                                                player.effectiveDex(), enemy->dexterity, 0, weapon);
                                if (!r.hit) {
                                    panel.addMessage("You swing and miss " + enemy->name + ".");
                                } else {
                                    panel.addMessage("You hit " + enemy->name + "'s " + partName(r.part)
                                                     + " for " + std::to_string(r.damage) + " damage.");
                                    reportSkillUp(r);
                                    if (!enemy->isAlive()) {
                                        panel.addMessage(enemy->name + " dies.");
                                        dropEnemyLoot(*enemy);
                                    }
                                }
                                onPlayerAct();
                            } else {
                                attackTarget         = enemy;
                                attackTargetVillager = nullptr;
                                pendingAct   = PendingAct::NONE;
                                currentPath  = findPath(player.x, player.y, ex, ey);
                                pathIndex    = 1;
                            }
                    }});
                    // Techniques only show up once the equipped weapon's skill is high
                    // enough — same "unlocked by use" spirit as everything else in Skill.
                    const Item* pw = player.weaponItem();
                    if (pw) {
                        for (int i = 0; i < (int)TechniqueId::TECHNIQUE_COUNT; i++) {
                            TechniqueId id = (TechniqueId)i;
                            const Technique& t = techniqueInfo(id);
                            if (id == TechniqueId::BACKSTAB) continue; // needs an unaware target — villager-only for now
                            if (pw->weaponSkill != t.skill || !techniqueUnlocked(player, id)) continue;
                            enemyOpts.push_back({t.name, [enemy, ex, ey, id]() {
                                useTechniqueOnEnemy(enemy, ex, ey, id);
                            }});
                        }
                    }
                    enemyOpts.push_back({"Examine", [enemy]() {
                        enemyExaminePanel.show(*enemy);
                    }});
                    contextMenu.show(event.button.x, event.button.y, enemyOpts, font);
                } else {
                    // Non-enemy tile: show context menu if explored
                    if (map[mouseY][mouseX].explored) {
                        int mx = mouseX, my = mouseY;
                        std::vector<MenuItem> items;

                        // Villager interaction
                        bool hasVillagerHere = false;
                        for (Villager& v : villagers) {
                            if (!v.alive || v.x != mx || v.y != my) continue;
                            if (!map[my][mx].visible) break;
                            hasVillagerHere = true;
                            std::string talkLabel = "Talk to " + v.name;
                            {
                                int viTalk = (int)(&v - &villagers[0]);
                                items.push_back({talkLabel, [viTalk]() {
                                    if (viTalk < 0 || viTalk >= (int)villagers.size()) return;
                                    Villager& tv = villagers[viTalk];
                                    if (!tv.alive) return;
                                    int dist = std::max(std::abs(tv.x - player.x),
                                                        std::abs(tv.y - player.y));
                                    if (dist <= 1) {
                                        if (tv.state == Villager::State::SLEEP)
                                            panel.addMessage(tv.name + ": " + greetingFor(tv));
                                        else
                                            panel.startDialogue(tv.name, greetingFor(tv), buildDialogueOptions(viTalk));
                                    } else {
                                        // Walk up to them first — arrival triggers the
                                        // greeting in updatePlayer(), same idea as chasing
                                        // an enemy/hostile villager to attack.
                                        talkTargetVillager    = &tv;
                                        attackTarget          = nullptr;
                                        attackTargetVillager  = nullptr;
                                        pendingAct   = PendingAct::NONE;
                                        currentPath  = findPath(player.x, player.y, tv.x, tv.y);
                                        pathIndex    = 1;
                                    }
                                }});
                            }
                            {
                                std::string spouseName = (v.spouseId >= 0 && v.spouseId < (int)villagers.size())
                                                        ? villagers[v.spouseId].name : "";
                                std::string motherName = (v.motherId >= 0 && v.motherId < (int)villagers.size())
                                                        ? villagers[v.motherId].name : "";
                                std::string fatherName = (v.fatherId >= 0 && v.fatherId < (int)villagers.size())
                                                        ? villagers[v.fatherId].name : "";
                                std::vector<std::string> childrenNames;
                                for (int cid : v.childIds)
                                    if (cid >= 0 && cid < (int)villagers.size())
                                        childrenNames.push_back(villagers[cid].name);
                                items.push_back({"Examine",
                                    [vsnap = v, spouseName, motherName, fatherName, childrenNames]() {
                                        villagerExaminePanel.show(vsnap, spouseName, motherName, fatherName, childrenNames);
                                    }});
                            }
                            int vi = (int)(&v - &villagers[0]);
                            // Trading now happens through the Talk dialogue (buildDialogueOptions),
                            // not as its own menu item — one less redundant entry here.
                            items.push_back({"Attack", [vi]() {
                                if (vi < 0 || vi >= (int)villagers.size()) return;
                                Villager& tv = villagers[vi];
                                if (!tv.alive) return;
                                int dist = std::max(std::abs(tv.x - player.x),
                                                    std::abs(tv.y - player.y));
                                if (dist <= 1) {
                                    const Item*  weapon  = player.weaponItem();
                                    bool         unaware = tv.state == Villager::State::SLEEP;
                                    AttackResult r = resolveAttack(player, tv, player.effectiveStr(),
                                                                    player.effectiveDex(), tv.dexterity, 0,
                                                                    weapon, unaware);
                                    if (!r.hit) {
                                        panel.addMessage("You swing and miss " + tv.name + ".");
                                    } else {
                                        panel.addMessage("You hit " + tv.name + "'s " + partName(r.part)
                                                         + " for " + std::to_string(r.damage) + " damage.");
                                        reportSkillUp(r);
                                        if (!tv.isAlive()) {
                                            panel.addMessage(tv.name + " dies.");
                                            dropVillagerLoot(tv);
                                        } else {
                                            villagerReactToAttack(tv);
                                        }
                                    }
                                    onPlayerAct();
                                } else {
                                    // Walk toward them first — path recalculates each
                                    // step via attackTargetVillager, same as chasing an enemy.
                                    attackTargetVillager = &tv;
                                    attackTarget          = nullptr;
                                    talkTargetVillager     = nullptr;
                                    pendingAct   = PendingAct::NONE;
                                    currentPath  = findPath(player.x, player.y, tv.x, tv.y);
                                    pathIndex    = 1;
                                }
                            }});
                            // Techniques — same weapon-skill gate as the enemy menu, plus
                            // Backstab (dagger-only) shows up while this villager sleeps.
                            {
                                const Item* pw = player.weaponItem();
                                if (pw) {
                                    for (int ti = 0; ti < (int)TechniqueId::TECHNIQUE_COUNT; ti++) {
                                        TechniqueId id = (TechniqueId)ti;
                                        const Technique& t = techniqueInfo(id);
                                        if (pw->weaponSkill != t.skill || !techniqueUnlocked(player, id)) continue;
                                        if (id == TechniqueId::BACKSTAB && v.state != Villager::State::SLEEP) continue;
                                        items.push_back({t.name, [vi, id]() {
                                            useTechniqueOnVillager(vi, id);
                                        }});
                                    }
                                }
                            }
                            break; // one NPC per tile is enough
                        }

                        // Gather corpse info for Examine (captured by value — safe if vector reallocates)
                        std::string corpseNameHere;
                        bool        corpseFreshHere = false;
                        bool        hasCorpseHere   = false;
                        for (const Corpse& c : corpses) {
                            if (c.sectorX != playerSectorX || c.sectorY != playerSectorY) continue;
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
                                pendingAct           = PendingAct::NONE;
                                attackTarget         = nullptr;
                                attackTargetVillager = nullptr;
                                talkTargetVillager    = nullptr;
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

                        // Examine — tile info + items + corpse if present. Skipped when a
                        // villager is here — their own "Examine <name>" already covers it.
                        if (!hasVillagerHere) {
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
                        contextMenu.show(event.button.x, event.button.y, items, font);
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
        const Item*  weapon = player.weaponItem();
        AttackResult r = resolveAttack(player, *blocker, player.effectiveStr(),
                                        player.effectiveDex(), blocker->dexterity, 0, weapon);
        if (!r.hit) {
            panel.addMessage("You swing and miss " + blocker->name + ".");
        } else {
            panel.addMessage("You hit " + blocker->name + "'s " + partName(r.part)
                             + " for " + std::to_string(r.damage) + " damage.");
            reportSkillUp(r);
            if (!blocker->isAlive()) {
                panel.addMessage(blocker->name + " dies.");
                dropEnemyLoot(*blocker);
            }
        }
        currentPath.clear();
        pathIndex = 0;
        pendingAct           = PendingAct::NONE;
        attackTarget         = nullptr;
        attackTargetVillager = nullptr;
        talkTargetVillager   = nullptr;
        onPlayerAct();
        return true;
    }

    // Same, for a hostile villager blocking the path (neutral ones don't
    // block/auto-attack — chasing one down never happens without a deliberate
    // Attack first).
    Villager* vBlocker = getVillagerAt(next.x, next.y);
    if (vBlocker && vBlocker->alive && vBlocker->isHostile()) {
        const Item*  weapon = player.weaponItem();
        AttackResult r = resolveAttack(player, *vBlocker, player.effectiveStr(),
                                        player.effectiveDex(), vBlocker->dexterity, 0, weapon);
        if (!r.hit) {
            panel.addMessage("You swing and miss " + vBlocker->name + ".");
        } else {
            panel.addMessage("You hit " + vBlocker->name + "'s " + partName(r.part)
                             + " for " + std::to_string(r.damage) + " damage.");
            reportSkillUp(r);
            if (!vBlocker->isAlive()) {
                panel.addMessage(vBlocker->name + " dies.");
                dropVillagerLoot(*vBlocker);
            } else {
                villagerReactToAttack(*vBlocker);
            }
        }
        currentPath.clear();
        pathIndex = 0;
        pendingAct           = PendingAct::NONE;
        attackTarget         = nullptr;
        attackTargetVillager = nullptr;
        talkTargetVillager   = nullptr;
        onPlayerAct();
        return true;
    }

    // Reached the villager we were walking up to talk to — start the
    // conversation instead of trying to step onto (through) them. Doesn't
    // fire if they turned hostile en route (the block above catches that
    // first and attacks instead).
    if (talkTargetVillager && talkTargetVillager->alive &&
        next.x == talkTargetVillager->x && next.y == talkTargetVillager->y) {
        Villager& tv = *talkTargetVillager;
        if (tv.state == Villager::State::SLEEP)
            panel.addMessage(tv.name + ": " + greetingFor(tv));
        else
            panel.startDialogue(tv.name, greetingFor(tv),
                                buildDialogueOptions((int)(&tv - &villagers[0])));
        currentPath.clear();
        pathIndex = 0;
        pendingAct         = PendingAct::NONE;
        talkTargetVillager = nullptr;
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

    // If chasing an enemy (or hostile villager), recalculate path to their
    // current position each step.
    if (attackTarget) {
        if (!attackTarget->alive) {
            attackTarget = nullptr;
        } else {
            currentPath = findPath(player.x, player.y,
                                   attackTarget->x, attackTarget->y);
            pathIndex = (currentPath.size() >= 2) ? 1 : (int)currentPath.size();
        }
    }
    if (attackTargetVillager) {
        if (!attackTargetVillager->alive) {
            attackTargetVillager = nullptr;
        } else {
            currentPath = findPath(player.x, player.y,
                                   attackTargetVillager->x, attackTargetVillager->y);
            pathIndex = (currentPath.size() >= 2) ? 1 : (int)currentPath.size();
        }
    }
    // Same, for walking up to talk — keeps aiming at them if they wander off mid-approach.
    if (talkTargetVillager) {
        if (!talkTargetVillager->alive) {
            talkTargetVillager = nullptr;
        } else {
            currentPath = findPath(player.x, player.y,
                                   talkTargetVillager->x, talkTargetVillager->y);
            pathIndex = (currentPath.size() >= 2) ? 1 : (int)currentPath.size();
        }
    }

    if (pathIndex >= (int)currentPath.size()) {
        currentPath.clear();
        pathIndex = 0;
        lastHoverX = -1;
        lastHoverY = -1;
        previewPath.clear();

        attackTarget         = nullptr;
        attackTargetVillager = nullptr;
        talkTargetVillager   = nullptr;
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
            SDL_Surface* s = TTF_RenderUTF8_Solid(bigFont, text, col);
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
    SDL_Surface* s = TTF_RenderUTF8_Solid(font, "Press Escape to quit", {150, 150, 150, 255});
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

// Alt+Enter handler. Borderless fullscreen desktop <-> a fixed windowed size.
// After switching, re-reads the real window size from SDL (rather than assuming),
// then updates the SCREEN_WIDTH/HEIGHT globals that everything else lays out from.
void toggleFullscreen() {
    isFullscreen = !isFullscreen;
    if (isFullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(window, 0);
        SDL_SetWindowSize(window, WINDOWED_WIDTH, WINDOWED_HEIGHT);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    applyScreenSize(w, h);
    updateCamera();
}

// ------------------------------------------------------------------ menu hub
//
// Character/Body/Effects/Inventory/Craft/Map share one full-screen hub with
// a tab strip on top. Exactly one of the underlying panels' own visibility
// flags is ever true at a time — these three functions are the only place
// that's allowed to flip them, so the hub's activeTab can't drift out of
// sync with what's actually on screen.

void openMenuTab(MenuTab t) {
    ui.showStats = bodyPanel.visible = effectsPanel.visible = skillsPanel.visible
                 = techniquesPanel.visible = spellsPanel.visible = inventoryPanel.visible = false;
    if (craftPanel.visible()) craftPanel.close();
    if (overmap.visible)      overmap.close();

    hub.activeTab = t;
    switch (t) {
        case MenuTab::CHARACTER:  ui.showStats          = true; break;
        case MenuTab::BODY:       bodyPanel.visible      = true; break;
        case MenuTab::EFFECTS:    effectsPanel.visible   = true; break;
        case MenuTab::SKILLS:     skillsPanel.visible    = true; break;
        case MenuTab::TECHNIQUES: techniquesPanel.visible = true; break;
        case MenuTab::MAGIC:      spellsPanel.visible    = true; break;
        case MenuTab::INVENTORY:  inventoryPanel.visible = true; break;
        case MenuTab::CRAFT:      craftPanel.open();             break;
        case MenuTab::MAP:        overmap.open(playerSectorX, playerSectorY); break;
        default: break;
    }
}

void closeMenuHub() {
    ui.showStats = bodyPanel.visible = effectsPanel.visible = skillsPanel.visible
                 = techniquesPanel.visible = spellsPanel.visible = inventoryPanel.visible = false;
    if (craftPanel.visible()) craftPanel.close();
    if (overmap.visible)      overmap.close();
    hub.activeTab = MenuTab::NONE;
}

void toggleMenuTab(MenuTab t) {
    if (hub.activeTab == t) closeMenuHub();
    else                    openMenuTab(t);
}

// Paints every visible tile within the pending action's range while
// hotbarTargeting is active (set by useHotbarSlot()) — red tint over a tile
// that actually holds a valid target, dim yellow otherwise, so the player can
// see who's reachable before clicking (resolveHotbarTargeting() handles the
// click itself).
void renderHotbarTargeting(SDL_Renderer* r) {
    if (!hotbarTargeting) return;
    int raw   = hotbar.slots[hotbarTargetSlot];
    int range = Hotbar::isSpellSlot(raw) ? spellInfo(Hotbar::spellOf(raw)).range
                                          : techniqueInfo(Hotbar::techniqueOf(raw)).range;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int y = player.y - range; y <= player.y + range; y++) {
        for (int x = player.x - range; x <= player.x + range; x++) {
            if (x == player.x && y == player.y) continue;
            if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) continue;
            if (!map[y][x].visible) continue;

            int sx = (x - cameraX) * TILE_SIZE;
            int sy = (y - cameraY) * TILE_SIZE;
            if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;

            bool hasTarget = getEnemyAt(x, y) || getVillagerAt(x, y);
            SDL_Color col = hasTarget ? SDL_Color{200, 60, 50, 255} : SDL_Color{200, 170, 60, 255};
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, hasTarget ? 90 : 45);

            SDL_Rect rect = {sx, sy, TILE_SIZE, TILE_SIZE};
            SDL_RenderFillRect(r, &rect);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// While wallTargeting is active: dim tint over every in-range candidate tile
// (mirrors renderHotbarTargeting()'s range paint), bright fill in the spell's
// own color over tiles already marked in wallSelection, so the player can see
// the shape they're building before confirming with Enter.
void renderWallTargeting(SDL_Renderer* r) {
    if (!wallTargeting) return;
    int raw = hotbar.slots[wallTargetSlot];
    if (raw < 0 || !Hotbar::isSpellSlot(raw)) return;
    const Spell& s = spellInfo(Hotbar::spellOf(raw));

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int y = player.y - s.range; y <= player.y + s.range; y++) {
        for (int x = player.x - s.range; x <= player.x + s.range; x++) {
            if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) continue;
            if (!map[y][x].visible) continue;
            if (std::max(std::abs(x - player.x), std::abs(y - player.y)) > s.range) continue;

            int sx = (x - cameraX) * TILE_SIZE;
            int sy = (y - cameraY) * TILE_SIZE;
            if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;

            bool marked = std::find_if(wallSelection.begin(), wallSelection.end(),
                              [&](const SDL_Point& p) { return p.x == x && p.y == y; })
                          != wallSelection.end();
            SDL_Color col = marked ? s.color : SDL_Color{200, 170, 60, 255};
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, marked ? 150 : 45);

            SDL_Rect rect = {sx, sy, TILE_SIZE, TILE_SIZE};
            SDL_RenderFillRect(r, &rect);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// While wallThrowTargeting is active: phase 1 paints every O_WALL within
// pick range in red (the only valid picks) and everything else dim, phase 2
// switches to the spell's normal range with actors highlighted, same visual
// language as renderHotbarTargeting().
void renderWallThrowTargeting(SDL_Renderer* r) {
    if (!wallThrowTargeting) return;
    int raw = hotbar.slots[wallThrowSlot];
    if (raw < 0 || !Hotbar::isSpellSlot(raw)) return;
    const Spell& s = spellInfo(Hotbar::spellOf(raw));

    int radius = wallThrowPickingSource ? WALL_THROW_PICK_RADIUS : s.range;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int y = player.y - radius; y <= player.y + radius; y++) {
        for (int x = player.x - radius; x <= player.x + radius; x++) {
            if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) continue;
            if (!map[y][x].visible) continue;
            if (std::max(std::abs(x - player.x), std::abs(y - player.y)) > radius) continue;

            int sx = (x - cameraX) * TILE_SIZE;
            int sy = (y - cameraY) * TILE_SIZE;
            if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;

            bool valid = wallThrowPickingSource
                ? (map[y][x].objectId == O_WALL)
                : (getEnemyAt(x, y) || getVillagerAt(x, y));
            SDL_Color col = valid ? SDL_Color{200, 60, 50, 255} : s.color;
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, valid ? 110 : 40);

            SDL_Rect rect = {sx, sy, TILE_SIZE, TILE_SIZE};
            SDL_RenderFillRect(r, &rect);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// Draws every active fire-wall tile (fireHazards) with a subtle flicker
// (alternating alpha via wall-clock time) so it visibly persists turn to
// turn, unlike the fading one-shot SpellBurst flash.
void renderFireHazards(SDL_Renderer* r) {
    if (fireHazards.empty()) return;
    bool  bright = (SDL_GetTicks() / 150) % 2 == 0;
    Uint8 alpha  = bright ? 130 : 90;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 230, 90, 20, alpha);
    for (const FireHazardTile& f : fireHazards) {
        if (!map[f.y][f.x].visible) continue;
        int sx = (f.x - cameraX) * TILE_SIZE;
        int sy = (f.y - cameraY) * TILE_SIZE;
        if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;
        SDL_Rect rect = {sx, sy, TILE_SIZE, TILE_SIZE};
        SDL_RenderFillRect(r, &rect);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// Draws the cosmetic flying-projectile overlay (see SpellProjectile's comment)
// and clears it once its short flight duration has elapsed.
void renderSpellProjectile(SDL_Renderer* r, TTF_Font* f) {
    if (!spellProjectile.active) return;
    Uint32 elapsed = SDL_GetTicks() - spellProjectile.startTime;
    if (elapsed >= spellProjectile.duration) { spellProjectile.active = false; return; }

    float t  = elapsed / (float)spellProjectile.duration;
    float fx = spellProjectile.fromX + (spellProjectile.toX - spellProjectile.fromX) * t;
    float fy = spellProjectile.fromY + (spellProjectile.toY - spellProjectile.fromY) * t;
    int sx = (int)((fx - cameraX) * TILE_SIZE);
    int sy = (int)((fy - cameraY) * TILE_SIZE);
    if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) return;

    SDL_Surface* s = TTF_RenderUTF8_Solid(f, spellProjectile.symbol, spellProjectile.color);
    if (!s) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    SDL_Rect dst = {sx, sy, TILE_SIZE, TILE_SIZE};
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

// Draws the fading blast-radius flash (see SpellBurst's comment) once the
// projectile's flight delay has passed, then clears it after its duration.
void renderSpellBurst(SDL_Renderer* r) {
    if (!spellBurst.active) return;
    Uint32 elapsed = SDL_GetTicks() - spellBurst.startTime;
    if (elapsed < spellBurst.delay) return; // still mid-flight, nothing to show yet
    Uint32 sinceImpact = elapsed - spellBurst.delay;
    if (sinceImpact >= spellBurst.duration) { spellBurst.active = false; return; }

    float t     = sinceImpact / (float)spellBurst.duration; // 0..1
    Uint8 alpha = (Uint8)(150.0f * (1.0f - t));

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, spellBurst.color.r, spellBurst.color.g, spellBurst.color.b, alpha);
    for (int y = spellBurst.cy - spellBurst.radius; y <= spellBurst.cy + spellBurst.radius; y++) {
        for (int x = spellBurst.cx - spellBurst.radius; x <= spellBurst.cx + spellBurst.radius; x++) {
            if (std::max(std::abs(x - spellBurst.cx), std::abs(y - spellBurst.cy)) > spellBurst.radius) continue;
            if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) continue;
            int sx = (x - cameraX) * TILE_SIZE;
            int sy = (y - cameraY) * TILE_SIZE;
            if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;
            SDL_Rect rect = {sx, sy, TILE_SIZE, TILE_SIZE};
            SDL_RenderFillRect(r, &rect);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// Draws every in-flight knockback streak (see KnockbackFlash's comment) —
// the pushed actor's own glyph sliding from its old tile to its new one —
// and prunes each one once its short flight is done.
void renderKnockbackFlashes(SDL_Renderer* r, TTF_Font* f) {
    Uint32 now = SDL_GetTicks();
    for (auto it = knockbackFlashes.begin(); it != knockbackFlashes.end(); ) {
        Uint32 elapsed = now - it->startTime;
        if (elapsed >= KNOCKBACK_FLASH_DURATION) { it = knockbackFlashes.erase(it); continue; }

        float t  = elapsed / (float)KNOCKBACK_FLASH_DURATION;
        float fx = it->fromX + (it->toX - it->fromX) * t;
        float fy = it->fromY + (it->toY - it->fromY) * t;
        int sx = (int)((fx - cameraX) * TILE_SIZE);
        int sy = (int)((fy - cameraY) * TILE_SIZE);
        if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < MAP_VIEW_HEIGHT) {
            SDL_Surface* s = TTF_RenderUTF8_Solid(f, it->symbol, it->color);
            if (s) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(r, s);
                SDL_FreeSurface(s);
                SDL_Rect dst = {sx, sy, TILE_SIZE, TILE_SIZE};
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
        }
        ++it;
    }
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
    groundItems.push_back({player.x + 2, player.y,     Items::ironSword(),   playerSectorX, playerSectorY});
    groundItems.push_back({player.x - 2, player.y,     Items::goldRing(),    playerSectorX, playerSectorY});
    groundItems.push_back({player.x,     player.y + 2, Items::ironHelmet(),  playerSectorX, playerSectorY});
    groundItems.push_back({player.x + 1, player.y + 1, Items::leatherVest(), playerSectorX, playerSectorY});
    groundItems.push_back({player.x - 1, player.y - 1, Items::leatherBoots(),playerSectorX, playerSectorY});
    groundItems.push_back({player.x + 1, player.y,     Items::torch(),       playerSectorX, playerSectorY});
    groundItems.push_back({player.x - 1, player.y,     Items::lantern(),     playerSectorX, playerSectorY});

    // Give everyone starting energy so they're ready to act immediately.
    player.energy = player.speed;
    for (Enemy& e : enemies) e.energy = e.speed;

    // Wire up inventory callbacks.
    inventoryPanel.onMessage = [](const std::string& msg) { panel.addMessage(msg); };
    inventoryPanel.onAct     = []() { onPlayerAct(); };

    // Start borderless-fullscreen at the desktop's current resolution.
    SDL_DisplayMode desktopMode;
    SDL_GetCurrentDisplayMode(0, &desktopMode);
    applyScreenSize(desktopMode.w, desktopMode.h);

    window = SDL_CreateWindow("Greystone",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    isFullscreen = true;

    // The window manager may not grant exactly desktopMode's size — re-read to be sure.
    int actualW, actualH;
    SDL_GetWindowSize(window, &actualW, &actualH);
    applyScreenSize(actualW, actualH);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    font = TTF_OpenFont("fonts/DejaVuSansMono.ttf", 16);

    SDL_Surface* playerSurface = TTF_RenderUTF8_Solid(font, player.symbol, player.color);
    playerTexture = SDL_CreateTextureFromSurface(renderer, playerSurface);
    SDL_FreeSurface(playerSurface);

    SDL_Surface* enemySurface = TTF_RenderUTF8_Solid(font, "E", red);
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

        // Craft/Map can close themselves internally (ESC in browse state,
        // starting a craft, M inside the overmap's own key handler already
        // routes through closeMenuHub() — but startCraft() flips CraftPanel's
        // state directly) — reconcile so the hub doesn't stay "open" on an
        // empty tab.
        if (hub.activeTab == MenuTab::CRAFT && !craftPanel.visible()) hub.activeTab = MenuTab::NONE;
        if (hub.activeTab == MenuTab::MAP   && !overmap.visible)      hub.activeTab = MenuTab::NONE;

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
                groundItems.push_back({player.x, player.y, gi, playerSectorX, playerSectorY});
            console.pendingGive.clear();
        }

        if (!console.pendingSetSkill.empty()) {
            for (int i = 0; CheatConsole::SKILL_TOKENS[i]; i++) {
                if (console.pendingSetSkill == CheatConsole::SKILL_TOKENS[i]) {
                    SkillLevel& sk = player.skill((Skill)i);
                    sk.level = console.pendingSetSkillLevel;
                    sk.exp   = 0;
                    break;
                }
            }
            console.pendingSetSkill.clear();
        }

        if (console.pendingSetAge >= 0) {
            player.age = console.pendingSetAge;
            console.pendingSetAge = -1;
        }

        if (console.pendingSkipYears > 0) {
            worldTime.minutes += console.pendingSkipYears * WorldTime::MINUTES_PER_YEAR;
            console.pendingSkipYears = 0;
            tickYearlyEvents(); // apply immediately, don't wait for the player's next action
        }

        if (!console.pendingUnlockTech.empty()) {
            for (int i = 0; CheatConsole::TECH_TOKENS[i]; i++) {
                if (console.pendingUnlockTech == CheatConsole::TECH_TOKENS[i]) {
                    const Technique& t = techniqueInfo((TechniqueId)i);
                    SkillLevel& sk = player.skill(t.skill);
                    if (sk.level < t.minLevel) { sk.level = t.minLevel; sk.exp = 0; }
                    break;
                }
            }
            console.pendingUnlockTech.clear();
        }

        if (!console.pendingUnlockSpell.empty()) {
            for (int i = 0; CheatConsole::SPELL_TOKENS[i]; i++) {
                if (console.pendingUnlockSpell == CheatConsole::SPELL_TOKENS[i]) {
                    const Spell& s = spellInfo((SpellId)i);
                    SkillLevel& sk = player.skill(s.school);
                    if (sk.level < s.minLevel) { sk.level = s.minLevel; sk.exp = 0; }
                    break;
                }
            }
            console.pendingUnlockSpell.clear();
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
                tickYearlyEvents();

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
        renderFireHazards(renderer);

        // Corpses render first — ground items and actors draw on top.
        for (const Corpse& c : corpses) {
            if (c.sectorX != playerSectorX || c.sectorY != playerSectorY) continue;
            if (!map[c.y][c.x].visible) continue;
            int sx = (c.x - cameraX) * TILE_SIZE;
            int sy = (c.y - cameraY) * TILE_SIZE;
            if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;
            static const char cSym[2] = {'%', 0};
            SDL_Surface* s = TTF_RenderUTF8_Solid(font, cSym, c.color);
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
                if (gi.sectorX == playerSectorX && gi.sectorY == playerSectorY && map[gi.y][gi.x].visible)
                    tileCount[{gi.x, gi.y}]++;

            std::set<std::pair<int,int>> rendered;
            for (const auto& gi : groundItems) {
                if (gi.sectorX != playerSectorX || gi.sectorY != playerSectorY) continue;
                if (!map[gi.y][gi.x].visible) continue;
                auto key = std::make_pair(gi.x, gi.y);
                if (!rendered.insert(key).second) continue; // already rendered this tile

                int sx = (gi.x - cameraX) * TILE_SIZE;
                int sy = (gi.y - cameraY) * TILE_SIZE;
                if (sx < 0 || sx >= SCREEN_WIDTH || sy < 0 || sy >= MAP_VIEW_HEIGHT) continue;

                int cnt = tileCount[key];
                const char* sym = (cnt > 1) ? "$" : gi.item.groundSymbol();
                SDL_Color col   = (cnt > 1) ? SDL_Color{220, 200, 60, 255} : gi.item.groundColor();

                SDL_Surface* gs = TTF_RenderUTF8_Solid(font, sym, col);
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
        renderSpellProjectile(renderer, font);
        renderSpellBurst(renderer);
        renderKnockbackFlashes(renderer, font);

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

        renderHotbarTargeting(renderer);
        renderWallTargeting(renderer);
        renderWallThrowTargeting(renderer);

        panel.render(renderer, font, player, worldTime);

        // Hotbar overlays the bottom of the map viewport during free-roam play —
        // hidden while the full-screen hub has taken over the view instead.
        if (!hub.visible()) hotbar.render(renderer, font, player);

        // Character/Body/Effects/Skills/Techniques/Inventory/Craft/Map share one
        // full-screen hub with a tab strip on top — exactly one of them is visible.
        if (hub.visible()) {
            hub.renderBackground(renderer);
            ui.renderStats(renderer, font);
            bodyPanel.render(renderer, font, player);
            effectsPanel.render(renderer, font, player);
            skillsPanel.render(renderer, font, player);
            techniquesPanel.render(renderer, font, player, hotbar);
            spellsPanel.render(renderer, font, player, hotbar);
            inventoryPanel.render(renderer, font, player);
            craftPanel.render(renderer, font, player);
            overmap.render(renderer, font, playerSectorX, playerSectorY);
            hub.renderTabBar(renderer, font);
        }

        // Remaining contextual popups (tied to a specific object/NPC, not
        // "reference" screens) keep their own independent dim + rendering.
        bool anyPopupOpen = tradePanel.visible || examinePanel.visible || itemExaminePanel.visible
            || enemyExaminePanel.visible || villagerExaminePanel.visible || pickupPanel.visible
            || waitPanel.visible || needsConfirmPanel.visible;
        if (anyPopupOpen) PanelStyle::dimBackdrop(renderer);

        tradePanel.render(renderer, font, player, villagers);
        if (examinePanel.visible)
            examinePanel.render(renderer, font, map[examinePanel.tileY][examinePanel.tileX]);
        itemExaminePanel.render(renderer, font);
        enemyExaminePanel.render(renderer, font);
        villagerExaminePanel.render(renderer, font);
        pickupPanel.render(renderer, font);
        waitPanel.render(renderer, font, worldTime);
        needsConfirmPanel.render(renderer, font);
        contextMenu.render(renderer, font);
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
            SDL_Surface* ls = TTF_RenderUTF8_Solid(font, label.c_str(), {200,185,100,255});
            SDL_Surface* ts = TTF_RenderUTF8_Solid(font, timeLeft.c_str(), {130,125,90,255});
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
            SDL_Surface* ws = TTF_RenderUTF8_Solid(font, wstr.c_str(), {140, 170, 200, 255});
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
