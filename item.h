#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>

// ================================================================ Enums

enum class ItemType {
    WEAPON, ARMOR, FOOD, DRINK, CONTAINER, JEWELRY, BOOK, TOOL, MISC
};

enum class EquipSlot {
    HEAD = 0, FACE, NECK,
    TORSO, OVER_TORSO,
    HAND_R, HAND_L, HANDS,
    RING_R, RING_L,
    BRACELET_R, BRACELET_L,
    WAIST, LEGS, FEET, BACK,
    SLOT_COUNT,
    NONE = -1
};

static const char* slotNames[(int)EquipSlot::SLOT_COUNT] = {
    "Head", "Face", "Neck",
    "Torso", "Over Torso",
    "Hand R", "Hand L", "Hands",
    "Ring R", "Ring L",
    "Bracelet R", "Bracelet L",
    "Waist", "Legs", "Feet", "Back"
};

// Ground symbol + color per item type (player sees category but not exact item)
struct TypeVisual { const char* symbol; SDL_Color color; };

static const TypeVisual typeVisuals[] = {
    { "/",  {220, 200,  50, 255} }, // WEAPON
    { "[",  {170, 170, 170, 255} }, // ARMOR
    { "%",  {100, 200,  80, 255} }, // FOOD
    { "~",  { 80, 130, 220, 255} }, // DRINK
    { "\\", {160, 110,  60, 255} }, // CONTAINER
    { "\"", {220, 180,  50, 255} }, // JEWELRY
    { "?",  {210, 210, 210, 255} }, // BOOK
    { "!",  {180, 180, 160, 255} }, // TOOL
    { ".",  {150, 150, 150, 255} }, // MISC
};

// ================================================================ Item

struct Item {
    std::string name;
    std::string description;
    ItemType    type   = ItemType::MISC;
    int         volume = 500;    // ml
    int         weight = 500;    // grams

    // Weapon
    int  damage    = 0;
    bool twoHanded = false;

    // Armor / Jewelry — which slot this item goes into
    EquipSlot slot    = EquipSlot::NONE;
    int       defense = 0;

    // Container
    int maxVolume = 0;   // 0 = not a container
    int maxWeight = 0;
    std::vector<Item> contents;

    // Food / Drink
    int nutrition = 0;   // reduces hunger (0..100 scale)
    int hydration = 0;   // reduces thirst

    // Jewelry passive bonuses
    int strBonus = 0;
    int dexBonus = 0;
    int conBonus = 0;

    // Economy
    int value = 0;   // approximate worth in gold coins

    // Light source (equipped item illuminates surroundings at night)
    int lightRadius = 0;

    // Tool capabilities
    bool canChop = false;  // can chop trees / fallen logs
    bool canMine = false;  // can mine rocks / boulders

    // Partial craft state (item interrupted mid-crafting)
    bool        isPartial       = false;
    int         craftProgress   = 0;   // minutes already spent
    int         craftTotalMins  = 0;   // total minutes the recipe requires
    std::string craftRecipeName;       // recipe name, used to resume

    // ---- Helpers ----
    const char* groundSymbol() const { return typeVisuals[(int)type].symbol; }
    SDL_Color   groundColor()  const { return typeVisuals[(int)type].color;  }
    bool        isContainer()  const { return maxVolume > 0; }
    bool        isEquippable() const { return slot != EquipSlot::NONE; }

    int usedVolume() const {
        int v = 0; for (const auto& i : contents) v += i.volume; return v;
    }
    int usedWeight() const {
        int w = 0; for (const auto& i : contents) w += i.weight; return w;
    }
    bool canFit(const Item& item) const {
        if (!isContainer()) return false;
        return (usedVolume() + item.volume <= maxVolume) &&
               (usedWeight() + item.weight <= maxWeight);
    }
};

// ================================================================ Ground item

struct GroundItem {
    int  x, y;
    Item item;
};

// ================================================================ Item factories

namespace Items {

inline Item ironSword() {
    Item i;
    i.name        = "Iron Sword";
    i.description = "A reliable iron sword. Heavy, durable.";
    i.type        = ItemType::WEAPON;
    i.volume      = 1500; i.weight = 1200;
    i.damage      = 8;    i.slot   = EquipSlot::HAND_R;
    i.value       = 25;
    return i;
}

inline Item woodenClub() {
    Item i;
    i.name        = "Wooden Club";
    i.description = "A heavy length of wood shaped into a bludgeon.";
    i.type        = ItemType::WEAPON;
    i.volume      = 2000; i.weight = 1800;
    i.damage      = 18;   i.slot   = EquipSlot::HAND_R;
    i.value       = 4;
    return i;
}

inline Item ironHelmet() {
    Item i;
    i.name        = "Iron Helmet";
    i.description = "Basic iron helmet. Protects the head.";
    i.type        = ItemType::ARMOR;
    i.volume      = 1500; i.weight = 1000;
    i.defense     = 3;    i.slot   = EquipSlot::HEAD;
    i.value       = 20;
    return i;
}

inline Item leatherVest() {
    Item i;
    i.name        = "Leather Vest";
    i.description = "Supple leather vest. Light protection.";
    i.type        = ItemType::ARMOR;
    i.volume      = 2000; i.weight = 800;
    i.defense     = 2;    i.slot   = EquipSlot::TORSO;
    i.value       = 12;
    return i;
}

inline Item leatherBoots() {
    Item i;
    i.name        = "Leather Boots";
    i.description = "Sturdy leather boots.";
    i.type        = ItemType::ARMOR;
    i.volume      = 800;  i.weight = 500;
    i.defense     = 1;    i.slot   = EquipSlot::FEET;
    i.value       = 8;
    return i;
}

inline Item backpack() {
    Item i;
    i.name        = "Backpack";
    i.description = "A canvas backpack with multiple pockets.";
    i.type        = ItemType::CONTAINER;
    i.volume      = 500;   i.weight    = 400;
    i.slot        = EquipSlot::BACK;
    i.maxVolume   = 15000; i.maxWeight = 12000;
    i.value       = 15;
    return i;
}

inline Item beltPouch() {
    Item i;
    i.name        = "Belt Pouch";
    i.description = "A small leather pouch for the belt.";
    i.type        = ItemType::CONTAINER;
    i.volume      = 150;  i.weight    = 80;
    i.slot        = EquipSlot::WAIST;
    i.maxVolume   = 2000; i.maxWeight = 2000;
    i.value       = 5;
    return i;
}

inline Item bread() {
    Item i;
    i.name        = "Bread";
    i.description = "A loaf of bread. Filling.";
    i.type        = ItemType::FOOD;
    i.volume      = 500; i.weight  = 300;
    i.nutrition   = 35;  i.value   = 1;
    return i;
}

inline Item waterFlask() {
    Item i;
    i.name        = "Water Flask";
    i.description = "A flask of clean water.";
    i.type        = ItemType::DRINK;
    i.volume      = 400; i.weight  = 500;
    i.hydration   = 40;  i.value   = 2;
    return i;
}

inline Item goldRing() {
    Item i;
    i.name        = "Gold Ring";
    i.description = "A simple gold ring. Carries a faint enchantment.";
    i.type        = ItemType::JEWELRY;
    i.volume      = 10;   i.weight   = 15;
    i.slot        = EquipSlot::RING_R;
    i.strBonus    = 1;    i.value    = 50;
    return i;
}

inline Item silverAmulet() {
    Item i;
    i.name        = "Silver Amulet";
    i.description = "A silver amulet on a thin chain.";
    i.type        = ItemType::JEWELRY;
    i.volume      = 20;   i.weight   = 30;
    i.slot        = EquipSlot::NECK;
    i.conBonus    = 1;    i.value    = 35;
    return i;
}

inline Item woodLog() {
    Item i;
    i.name        = "Log";
    i.description = "A solid section of tree trunk. Useful for building or fuel.";
    i.type        = ItemType::MISC;
    i.volume      = 8000; i.weight = 5000; i.value = 3;
    return i;
}

inline Item branch() {
    Item i;
    i.name        = "Branch";
    i.description = "A sturdy wooden branch. Could serve as a crude weapon.";
    i.type        = ItemType::MISC;
    i.volume      = 1500; i.weight = 600; i.value = 1;
    return i;
}

inline Item stonePiece() {
    Item i;
    i.name        = "Stone";
    i.description = "A chunk of rough stone. Heavy and hard.";
    i.type        = ItemType::MISC;
    i.volume      = 600; i.weight = 1200; i.value = 1;
    return i;
}

inline Item torch() {
    Item i;
    i.name        = "Torch";
    i.description = "A wooden stick wrapped in oil-soaked cloth. Burns for hours.";
    i.type        = ItemType::TOOL;
    i.volume      = 500;  i.weight      = 200;
    i.slot        = EquipSlot::HAND_L;
    i.lightRadius = 10;
    i.value       = 4;
    return i;
}

inline Item lantern() {
    Item i;
    i.name        = "Lantern";
    i.description = "An oil lantern with a glass shield. Bright and wind-resistant.";
    i.type        = ItemType::TOOL;
    i.volume      = 800;  i.weight      = 600;
    i.slot        = EquipSlot::HAND_L;
    i.lightRadius = 14;
    i.value       = 30;
    return i;
}

inline Item crudeDagger() {
    Item i;
    i.name        = "Crude Dagger";
    i.description = "A rough-edged blade, crudely made from scrap metal.";
    i.type        = ItemType::WEAPON;
    i.slot        = EquipSlot::HAND_R;
    i.damage      = 3; i.volume = 150; i.weight = 180; i.value = 2;
    return i;
}

inline Item warAxe() {
    Item i;
    i.name        = "War Axe";
    i.description = "A heavy axe favoured by orcish warriors. Brutal.";
    i.type        = ItemType::WEAPON;
    i.slot        = EquipSlot::HAND_R;
    i.damage      = 12; i.volume = 2000; i.weight = 2100; i.value = 40;
    i.canChop     = true;
    return i;
}

inline Item hatchet() {
    Item i;
    i.name        = "Hatchet";
    i.description = "A small axe for chopping wood. Decent in a pinch.";
    i.type        = ItemType::TOOL;
    i.slot        = EquipSlot::HAND_R;
    i.damage      = 5; i.volume = 600; i.weight = 700; i.value = 12;
    i.canChop     = true;
    return i;
}

inline Item pickaxe() {
    Item i;
    i.name        = "Pickaxe";
    i.description = "A sturdy iron pickaxe for breaking stone.";
    i.type        = ItemType::TOOL;
    i.slot        = EquipSlot::HAND_R;
    i.damage      = 6; i.volume = 1400; i.weight = 1800; i.value = 18;
    i.canMine     = true;
    return i;
}

inline Item boneClub() {
    Item i;
    i.name        = "Bone Club";
    i.description = "A large bone repurposed as a bludgeoning weapon.";
    i.type        = ItemType::WEAPON;
    i.slot        = EquipSlot::HAND_R;
    i.damage      = 4; i.volume = 500; i.weight = 450; i.value = 1;
    return i;
}

inline Item berries() {
    Item i;
    i.name        = "Berries";
    i.description = "A handful of wild berries. Tart but nourishing.";
    i.type        = ItemType::FOOD;
    i.volume      = 200; i.weight = 100; i.nutrition = 10; i.value = 1;
    return i;
}

inline Item grain() {
    Item i;
    i.name        = "Grain";
    i.description = "A bundle of wheat grain. Can be ground into flour.";
    i.type        = ItemType::MISC;
    i.volume      = 300; i.weight = 250; i.value = 2;
    return i;
}

inline Item herb() {
    Item i;
    i.name        = "Herb";
    i.description = "Dried medicinal herbs with a sharp scent. Useful in potions.";
    i.type        = ItemType::MISC;
    i.volume      = 150; i.weight = 50; i.value = 3;
    return i;
}

inline Item mushroom() {
    Item i;
    i.name        = "Mushroom";
    i.description = "A cluster of autumn mushrooms. Earthy and somewhat nourishing.";
    i.type        = ItemType::FOOD;
    i.volume      = 100; i.weight = 80; i.nutrition = 8; i.value = 2;
    return i;
}

// ── Crafted items ────────────────────────────────────────────────────────────

inline Item stoneKnife() {
    Item i;
    i.name        = "Stone Knife";
    i.description = "A sharp flake of stone lashed to a short handle.";
    i.type        = ItemType::WEAPON;
    i.slot        = EquipSlot::HAND_R;
    i.volume      = 400; i.weight = 320; i.damage = 14; i.value = 5;
    return i;
}

inline Item crudeAxe() {
    Item i;
    i.name        = "Crude Axe";
    i.description = "A stone head tied to a branch handle. Chops wood and cracks skulls.";
    i.type        = ItemType::WEAPON;
    i.slot        = EquipSlot::HAND_R;
    i.volume      = 1800; i.weight = 1500; i.damage = 22; i.value = 8;
    i.canChop     = true;
    return i;
}

inline Item ropeItem() {
    Item i;
    i.name        = "Rope";
    i.description = "Braided from thin branches. Useful for binding or climbing.";
    i.type        = ItemType::MISC;
    i.volume      = 600; i.weight = 400; i.value = 6;
    return i;
}

inline Item herbalPoultice() {
    Item i;
    i.name        = "Herbal Poultice";
    i.description = "Crushed herbs wrapped in bark. Helps wounds stop bleeding and begin healing.";
    i.type        = ItemType::MISC;
    i.volume      = 150; i.weight = 80; i.value = 8;
    return i;
}

inline Item mushroomStew() {
    Item i;
    i.name        = "Mushroom Stew";
    i.description = "Slow-cooked mushrooms in their own juices. Hearty and filling.";
    i.type        = ItemType::FOOD;
    i.volume      = 400; i.weight = 350; i.nutrition = 24; i.value = 5;
    return i;
}

inline Item flatbread() {
    Item i;
    i.name        = "Flatbread";
    i.description = "Grain pressed and sun-baked on a hot stone. Simple and filling.";
    i.type        = ItemType::FOOD;
    i.volume      = 300; i.weight = 200; i.nutrition = 20; i.value = 4;
    return i;
}

inline Item roastedBerries() {
    Item i;
    i.name        = "Roasted Berries";
    i.description = "Berries dried and caramelised over embers. Sweet and sustaining.";
    i.type        = ItemType::FOOD;
    i.volume      = 150; i.weight = 100; i.nutrition = 10; i.value = 3;
    return i;
}

} // namespace Items
