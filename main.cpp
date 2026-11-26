// Horizons - a 2D block-world RPG written in C++ with no external libraries.
// Software renderer into a 32-bit framebuffer sized to the window, presented with Win32 GDI.
//
// Controls:
//   WASD / arrows  move            Shift  run (uses stamina)
//   E              use tile in front (chop, mine, fish, till, plant, harvest, talk, cook)
//   Shift+E        pick up a campfire
//   Space          attack monsters in front of you with your best weapon
//   R              place selected block (Stone, Plank, Brick, Campfire)
//   Q              eat selected food or dish
//   C              open/close crafting        1-9, 0, - craft while open
//   In cooking (E on a campfire): 1-9, 0 add ingredient, ENTER cook, X clear, C close
//   1-0 select hotbar item   [ ] change hotbar page
//   BKSP throw (Ctrl+BKSP throws the whole stack)
//   F5 save   F9 load   F11 fullscreen   Esc save and quit
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

typedef uint32_t u32;

#define RGBX(r, g, b) (0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

static const int TILE = 64;
static const int MAP_W = 4096;
static const int MAP_H = 4096;
static const float PI = 3.14159265f;
static const float DAY_LENGTH = 240.0f;
static const float AUTOSAVE_EVERY = 60.0f;
static const int HB_SLOTS = 10;
static const int KEYBAR_H = 26;
static const int VILLAGE_X = MAP_W / 2 + 22, VILLAGE_Y = MAP_H / 2 - 12;
static const float SPAWN_X = MAP_W / 2 + 0.5f, SPAWN_Y = MAP_H / 2 + 0.5f;
static const int MAX_MON = 14;

enum Ground : uint8_t { G_WATER, G_SAND, G_GRASS, G_FOREST, G_DIRT, G_TILLED, G_STONE, G_SNOW, G_DESERT, G_SWAMP,
                        G_JUNGLE, G_MEADOW, G_BADLANDS, G_CAVE, G_COUNT };
enum Obj : uint8_t { O_NONE, O_TREE, O_ROCK, O_PLANK, O_BRICK, O_CAMPFIRE, O_BED, O_TORCH, O_DOOR_CLOSED, O_DOOR_OPEN,
                 O_ORE_COPPER, O_ORE_IRON, O_ORE_DIAMOND, O_ORE_OBSIDIAN, O_CHEST, O_FURNACE, O_CAVE_HOLE, O_LADDER };

static const int DXS[4] = { 0, 1, 0, -1 };
static const int DYS[4] = { -1, 0, 1, 0 };

enum Screen { SCR_TITLE, SCR_WORLDS, SCR_GAME };
static Screen gScreen = SCR_TITLE;
static bool gMusMenu = true;          // calm menu music on the title and world screens
static int gMenuChord = -1;
static std::string gWorldDir, gWorldName;
static std::vector<std::string> gWorldList;
static int gWorldSel = 0;
static bool gOpenDoors = false;       // true while the player, friends or pets are pathing
struct PendingDrop { float x, y; int item; int n; };
static std::vector<PendingDrop> gPendingDrops;
static bool gKeyEdge[256];            // set once per frame by readKeys()
static bool gNaming = false;
enum SfxType { SFX_MEOW, SFX_BARK, SFX_MOO, SFX_BAA, SFX_NEIGH, SFX_CAMEL, SFX_SQUEAK, SFX_PICK, SFX_HIT, SFX_JUMP, SFX_SPLASH };
static void playSfx(int type, float x, float y, float volume);
struct Beast { float x, y, dx, dy, wander, hp; int kind, face; int tame; float cool; char name[12]; };
static std::vector<Beast> gBeasts;
struct Animal { float x, y, dx, dy, wander, hp; int face; int tame; char name[12]; };
static std::vector<Animal> gAnimals;
struct TamedSave { int type, kind, face; float x, y, hp; char name[12]; };   // type 0 = beast, 1 = rabbit
static std::vector<TamedSave> gPendingTamed;                  // tamed creatures read from a save
static int gRide = -1;                       // index of the beast the player is riding, or -1
static bool gJumping = false, gHoleJump = false, gInCave = false;
static float gZ = 0.0f, gVz = 0.0f;
static float gJumpSX = 0.0f, gJumpSY = 0.0f;
static bool gInvOpen = false;
static bool gMinimap = true;          // the 40-slot inventory screen (I)
static int gClickX = 0, gClickY = 0;
static bool gClickPending = false;
static bool gReleasePending = false;
static bool gDragging = false;
static int gDragItem = -1;
static int gDragFrom = -1;              // hotbar slot the item came from, or -1 for the inventory
static int gMouseX = 0, gMouseY = 0;
static int gHotbar[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // item in each hotbar slot
static const int INV_SLOTS = 40;
static std::string gNameInput;
static float gHunger = 100.0f;
static bool gEmpireFounded = false;
static bool gEmpirePanel = false;
static int gCraftPage = 0;
struct ChestData { int cell; int item[10]; int n[10]; };
static std::vector<ChestData> gChests;
static bool gChestOpen = false;
static int gChestCell = -1;
static int gRaidIdx = -1;
static float gRaidT = 0.0f;           // seconds left before a raid is lost
static int gLastUpkeepDay = 0;
static bool gRaidActive = false;
struct Empire { int x, y, friends, pets, food, state; };   // state 0 active, 1 defeated
static std::vector<Empire> gEmpires;
static std::vector<int> gDoors;       // cell indexes of every door on the map

// Closed doors block monsters and animals; the player, friends and pets open them.
static bool objSolid(uint8_t obj, bool openers) {
    if (obj == O_NONE || obj == O_DOOR_OPEN || obj == O_LADDER) return false;
    if (obj == O_DOOR_CLOSED) return !openers;
    return true;
}

static void setScreen(Screen s) {
    gScreen = s;
    gMusMenu = (s != SCR_GAME);
}
static bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

struct Settings { int shader; int texture; int fpsCap; bool vsync; bool music; int musicVol; bool showFps; bool sfx; bool turbo; };
static Settings gSet = { 2, 1, 0, false, true, 50, false, true, false };
static const int FPS_VALUES[5] = { 0, 30, 60, 120, 144 };
static const char* SHADER_NAMES[4] = { "LOW", "MEDIUM", "HIGH", "ULTRA" };
static const char* TEX_NAMES[3] = { "LOW", "HIGH", "ULTRA" };
static float gFps = 0.0f, gFpsT = 0.0f;
static int gFpsCnt = 0;
static void genAllSprites();
static void saveSettings();
static void loadSettings();
static void updateMenu(float dt);
static void drawMenu();
static void enterWorld(const std::string& name);
static void drawSettings();
static bool gSettingsOpen = false;
static std::string gSettingsPath;

static void updateFps(float dt) {
    gFpsT += dt;
    gFpsCnt++;
    if (gFpsT >= 0.5f) { gFps = gFpsCnt / gFpsT; gFpsT = 0.0f; gFpsCnt = 0; }
}
static float daylight();

// ---------- window state (sized to the real client area) ----------
static int gW = 1280, gH = 720;
static std::vector<u32> gFrame(1280 * 720);
static bool gFullscreen = false;
static WINDOWPLACEMENT gPrevPlacement;
static LONG_PTR gPrevStyle = 0;
static HWND gHwnd = nullptr;

// ---------- crops ----------
// style: 0 tall grain, 1 root vegetable, 2 bush/vine, 3 sunflower
struct CropDef { const char* name; float grow; u32 ripe; u32 leaf; int style; };
static const int NC = 12;
static const CropDef CROPS[NC] = {
    { "Wheat",      40, RGBX(215, 185, 70),  RGBX(90, 160, 50),  0 },
    { "Corn",       55, RGBX(240, 200, 40),  RGBX(60, 140, 40),  0 },
    { "Rice",       45, RGBX(230, 220, 160), RGBX(80, 170, 90),  0 },
    { "Carrot",     35, RGBX(240, 120, 30),  RGBX(60, 140, 50),  1 },
    { "Potato",     45, RGBX(150, 110, 60),  RGBX(70, 130, 50),  1 },
    { "Beet",       40, RGBX(150, 30, 70),   RGBX(60, 120, 60),  1 },
    { "Radish",     25, RGBX(220, 60, 80),   RGBX(90, 160, 70),  1 },
    { "Tomato",     50, RGBX(230, 50, 40),   RGBX(60, 140, 50),  2 },
    { "Strawberry", 35, RGBX(220, 30, 60),   RGBX(70, 150, 60),  2 },
    { "Cabbage",    40, RGBX(180, 220, 140), RGBX(60, 150, 60),  2 },
    { "Pumpkin",    65, RGBX(245, 130, 20),  RGBX(70, 150, 50),  2 },
    { "Sunflower",  50, RGBX(250, 210, 30),  RGBX(80, 150, 50),  3 },
};

// ---------- items ----------
enum ItemId {
    I_WOOD = 0, I_STONE, I_FISH, I_PLANK, I_BRICK, I_AXE, I_PICKAXE, I_HOE, I_ROD, I_BREAD,
    I_GEL, I_WOOD_SWORD, I_STONE_SWORD, I_MACE, I_CAMPFIRE,
    I_FLINT, I_CLAY, I_HIDE, I_MEAT, I_SPEAR, I_DAGGER, I_VEST, I_BED, I_TORCH, I_MEDICINE, I_DOOR,
    I_CU_INGOT, I_FE_INGOT, I_DIA, I_OBS, I_CHEST, I_CU_ORE, I_FE_ORE, I_FURNACE,
    I_TIER, I_TIER_END = I_TIER + 27,    // 4 metals x 7 tool and armour pieces
    I_SEED = I_TIER_END + 1,             // + crop index -> seed
    I_PROD = I_SEED + NC,   // + crop index -> harvested produce
    I_COUNT = I_PROD + NC
};

// Cooked dishes live after the items. A dish key encodes up to 3 ingredient codes
// (0 = empty, 1..12 = crops, 13 = fish), sorted, so order never matters.
static const int DISH_N = 15 * 15 * 15;
static const int T_COUNT = I_COUNT + DISH_N;
static int gCount[T_COUNT];

static int codeItem(int code) { return code == 13 ? I_FISH : code == 14 ? I_MEAT : I_PROD + (code - 1); }
static std::string codeName(int code) {
    if (code == 13) return "Fish";
    if (code == 14) return "Meat";
    return CROPS[code - 1].name;
}
static u32 codeColor(int code) {
    if (code == 13) return RGBX(74, 154, 224);
    if (code == 14) return RGBX(210, 70, 70);
    return CROPS[code - 1].ripe;
}

static int dishKey(std::vector<int> codes) {
    std::sort(codes.begin(), codes.end());
    while (codes.size() < 3) codes.insert(codes.begin(), 0);
    return codes[0] * 225 + codes[1] * 15 + codes[2];
}
static void dishCodes(int key, int out[3]) { out[0] = key / 225; out[1] = (key / 15) % 15; out[2] = key % 15; }

static std::string dishName(int key) {
    int c[3]; dishCodes(key, c);
    std::vector<int> v;
    for (int k : c) if (k) v.push_back(k);
    if (v.empty()) return "Empty dish";
    if (v.size() == 1) return "Grilled " + codeName(v[0]);
    if (v.size() == 2) return codeName(v[0]) + "-" + codeName(v[1]) + " Stew";
    return codeName(v[0]) + "-" + codeName(v[1]) + "-" + codeName(v[2]) + " Hash";
}

static const char* METAL_NAME[4] = { "Copper", "Iron", "Diamond", "Obsidian" };
static const char* PIECE_NAME[7] = { "Pickaxe", "Axe", "Sword", "Helmet", "Chestplate", "Leggings", "Boots" };
static const u32 METAL_COL[4] = { RGBX(210, 130, 70), RGBX(205, 205, 215), RGBX(110, 230, 240), RGBX(70, 40, 90) };

static std::string itemName(int i) {
    if (i >= I_COUNT) return dishName(i - I_COUNT);
    static const char* N[] = { "Wood", "Stone", "Raw Fish", "Plank", "Stone Brick", "Axe", "Pickaxe", "Hoe",
                               "Fishing Rod", "Bread", "Slime Gel", "Wood Sword", "Stone Sword", "Brick Mace",
                               "Campfire", "Flint", "Clay", "Hide", "Raw Meat", "Spear", "Dagger", "Hide Vest",
                               "Bed", "Torch", "Medicine", "Door" };
    static const char* EXTRA[8] = { "Copper Ingot", "Iron Ingot", "Diamond", "Obsidian", "Chest", "Copper Ore", "Iron Ore", "Furnace" };
    if (i < I_CU_INGOT) return N[i];
    if (i < I_TIER) return EXTRA[i - I_CU_INGOT];
    if (i <= I_TIER_END) return std::string(METAL_NAME[(i - I_TIER) / 7]) + " " + PIECE_NAME[(i - I_TIER) % 7];
    if (i < I_PROD) return std::string(CROPS[i - I_SEED].name) + " Seed";
    return CROPS[i - I_PROD].name;
}

static u32 itemColor(int i) {
    if (i >= I_COUNT) {
        int c[3]; dishCodes(i - I_COUNT, c);
        return c[2] ? codeColor(c[2]) : RGBX(200, 170, 120);
    }
    static const u32 C[] = { RGBX(139, 90, 43), RGBX(154, 154, 166), RGBX(74, 154, 224), RGBX(200, 160, 100),
                             RGBX(170, 70, 50), RGBX(180, 190, 200), RGBX(160, 170, 180), RGBX(120, 90, 50),
                             RGBX(200, 170, 120), RGBX(230, 190, 120), RGBX(90, 220, 110), RGBX(170, 120, 70),
                             RGBX(170, 170, 180), RGBX(150, 80, 60), RGBX(200, 90, 40), RGBX(120, 120, 130),
                             RGBX(180, 120, 90), RGBX(150, 100, 60), RGBX(210, 70, 70), RGBX(150, 110, 70),
                             RGBX(190, 190, 200), RGBX(160, 100, 70), RGBX(190, 50, 60), RGBX(255, 170, 60),
                             RGBX(240, 240, 250), RGBX(150, 100, 60) };
    static const u32 EXTRA[8] = { RGBX(210, 130, 70), RGBX(205, 205, 215), RGBX(110, 230, 240), RGBX(70, 40, 90), RGBX(120, 80, 45),
                                  RGBX(190, 110, 60), RGBX(150, 150, 160), RGBX(120, 110, 100) };
    if (i < I_CU_INGOT) return C[i];
    if (i < I_TIER) return EXTRA[i - I_CU_INGOT];
    if (i <= I_TIER_END) return METAL_COL[(i - I_TIER) / 7];
    if (i < I_PROD) return CROPS[i - I_SEED].leaf;
    return CROPS[i - I_PROD].ripe;
}

static bool isFood(int i) { return i >= I_COUNT || i == I_FISH || i == I_BREAD || i == I_MEAT || i == I_MEDICINE || (i >= I_PROD && i < I_COUNT); }
static bool isPlaceable(int i) { return i == I_STONE || i == I_PLANK || i == I_BRICK || i == I_CAMPFIRE || i == I_BED || i == I_TORCH || i == I_DOOR || i == I_CHEST || i == I_FURNACE; }

struct Ingredient { int item; int n; };
struct Recipe { int out; int outN; Ingredient in[3]; int inCount; bool tool; };
static const Recipe RECIPES[] = {
    { I_PLANK,       2, { { I_WOOD, 1 } },                                 1, false },
    { I_BRICK,       1, { { I_STONE, 2 } },                                1, false },
    { I_AXE,         1, { { I_PLANK, 2 }, { I_STONE, 1 } },                2, true },
    { I_PICKAXE,     1, { { I_PLANK, 2 }, { I_STONE, 2 } },                2, true },
    { I_HOE,         1, { { I_PLANK, 2 }, { I_STONE, 1 } },                2, true },
    { I_ROD,         1, { { I_PLANK, 3 } },                                1, true },
    { I_BREAD,       1, { { I_PROD + 0, 3 } },                             1, false },
    { I_WOOD_SWORD,  1, { { I_PLANK, 2 }, { I_WOOD, 1 } },                 2, true },
    { I_STONE_SWORD, 1, { { I_PLANK, 2 }, { I_STONE, 2 } },                2, true },
    { I_MACE,        1, { { I_BRICK, 2 }, { I_GEL, 2 }, { I_PLANK, 1 } },  3, true },
    { I_CAMPFIRE,    1, { { I_STONE, 4 }, { I_PLANK, 2 } },                2, false },
    { I_SPEAR,       1, { { I_PLANK, 2 }, { I_FLINT, 1 } },                2, true },
    { I_DAGGER,      1, { { I_PLANK, 1 }, { I_FLINT, 1 } },                2, true },
    { I_VEST,        1, { { I_HIDE, 3 } },                                 1, true },
    { I_BED,         1, { { I_PLANK, 3 }, { I_HIDE, 2 } },                 2, false },
    { I_TORCH,       3, { { I_WOOD, 1 }, { I_FLINT, 1 } },                 2, false },
    { I_BRICK,       1, { { I_CLAY, 3 } },                                 1, false },
    { I_MEDICINE,    1, { { I_PROD + 9, 3 } },                             1, false },
    { I_DOOR,        1, { { I_PLANK, 3 } },                                1, false },
    { I_CHEST,       1, { { I_PLANK, 8 } },                                1, false },
    { I_FURNACE,     1, { { I_STONE, 10 }, { I_PLANK, 4 } },               2, false },
    { I_TIER + 0, 1, { { I_CU_INGOT, 3 }, { I_PLANK, 2 } }, 2, true },  // Copper Pickaxe
    { I_TIER + 1, 1, { { I_CU_INGOT, 3 }, { I_PLANK, 2 } }, 2, true },  // Copper Axe
    { I_TIER + 2, 1, { { I_CU_INGOT, 2 }, { I_PLANK, 1 } }, 2, true },  // Copper Sword
    { I_TIER + 3, 1, { { I_CU_INGOT, 5 } }, 1, true },  // Copper Helmet
    { I_TIER + 4, 1, { { I_CU_INGOT, 8 } }, 1, true },  // Copper Chestplate
    { I_TIER + 5, 1, { { I_CU_INGOT, 7 } }, 1, true },  // Copper Leggings
    { I_TIER + 6, 1, { { I_CU_INGOT, 4 } }, 1, true },  // Copper Boots
    { I_TIER + 7, 1, { { I_FE_INGOT, 3 }, { I_PLANK, 2 } }, 2, true },  // Iron Pickaxe
    { I_TIER + 8, 1, { { I_FE_INGOT, 3 }, { I_PLANK, 2 } }, 2, true },  // Iron Axe
    { I_TIER + 9, 1, { { I_FE_INGOT, 2 }, { I_PLANK, 1 } }, 2, true },  // Iron Sword
    { I_TIER + 10, 1, { { I_FE_INGOT, 5 } }, 1, true },  // Iron Helmet
    { I_TIER + 11, 1, { { I_FE_INGOT, 8 } }, 1, true },  // Iron Chestplate
    { I_TIER + 12, 1, { { I_FE_INGOT, 7 } }, 1, true },  // Iron Leggings
    { I_TIER + 13, 1, { { I_FE_INGOT, 4 } }, 1, true },  // Iron Boots
    { I_TIER + 14, 1, { { I_DIA, 3 }, { I_PLANK, 2 } }, 2, true },  // Diamond Pickaxe
    { I_TIER + 15, 1, { { I_DIA, 3 }, { I_PLANK, 2 } }, 2, true },  // Diamond Axe
    { I_TIER + 16, 1, { { I_DIA, 2 }, { I_PLANK, 1 } }, 2, true },  // Diamond Sword
    { I_TIER + 17, 1, { { I_DIA, 5 } }, 1, true },  // Diamond Helmet
    { I_TIER + 18, 1, { { I_DIA, 8 } }, 1, true },  // Diamond Chestplate
    { I_TIER + 19, 1, { { I_DIA, 7 } }, 1, true },  // Diamond Leggings
    { I_TIER + 20, 1, { { I_DIA, 4 } }, 1, true },  // Diamond Boots
    { I_TIER + 21, 1, { { I_OBS, 3 }, { I_PLANK, 2 } }, 2, true },  // Obsidian Pickaxe
    { I_TIER + 22, 1, { { I_OBS, 3 }, { I_PLANK, 2 } }, 2, true },  // Obsidian Axe
    { I_TIER + 23, 1, { { I_OBS, 2 }, { I_PLANK, 1 } }, 2, true },  // Obsidian Sword
    { I_TIER + 24, 1, { { I_OBS, 5 } }, 1, true },  // Obsidian Helmet
    { I_TIER + 25, 1, { { I_OBS, 8 } }, 1, true },  // Obsidian Chestplate
    { I_TIER + 26, 1, { { I_OBS, 7 } }, 1, true },  // Obsidian Leggings
    { I_TIER + 27, 1, { { I_OBS, 4 } }, 1, true },  // Obsidian Boots
};
static const int NREC = (int)(sizeof(RECIPES) / sizeof(RECIPES[0]));

static bool canCraft(const Recipe& r) {
    if (r.tool && gCount[r.out] > 0) return false;
    for (int k = 0; k < r.inCount; k++) if (gCount[r.in[k].item] < r.in[k].n) return false;
    return true;
}

// ---------- RPG progression ----------
static int gXP = 0;
static int levelOf(int xp) { return 1 + xp / 50; }

struct Popup { std::string text; float life; u32 col; };
static std::vector<Popup> gPopups;
static void addPopup(const std::string& t, u32 col) { gPopups.push_back({ t, 1.4f, col }); }

static void addXP(int n) {
    int before = levelOf(gXP);
    gXP += n;
    addPopup("+" + std::to_string(n) + " XP", RGBX(255, 230, 120));
    if (levelOf(gXP) > before) addPopup("LEVEL UP! LV " + std::to_string(levelOf(gXP)), RGBX(120, 255, 160));
}

static void craftRecipe(int idx) {
    const Recipe& r = RECIPES[idx];
    if (!canCraft(r)) return;
    for (int k = 0; k < r.inCount; k++) gCount[r.in[k].item] -= r.in[k].n;
    gCount[r.out] += r.outN;
    addXP(3);
    addPopup("Crafted " + itemName(r.out), RGBX(160, 220, 255));
}

// ---------- noise and colour ----------
static uint32_t hash2(int x, int y, uint32_t s) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float rnd(int x, int y, uint32_t s) { return (hash2(x, y, s) & 0xFFFF) / 65535.0f; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float vnoise(float x, float y, uint32_t s) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    return lerpf(lerpf(rnd(xi, yi, s), rnd(xi + 1, yi, s), u),
                 lerpf(rnd(xi, yi + 1, s), rnd(xi + 1, yi + 1, s), u), v);
}
static float fbm(float x, float y, uint32_t s) {
    float v = 0, a = 0.5f;
    for (int i = 0; i < 4; i++) { v += a * vnoise(x, y, s + i); x *= 2; y *= 2; a *= 0.5f; }
    return v / 0.9375f;
}
static inline u32 mk(int r, int g, int b) {
    return 0xFF000000u | ((u32)std::clamp(r, 0, 255) << 16) | ((u32)std::clamp(g, 0, 255) << 8) | (u32)std::clamp(b, 0, 255);
}
static inline void unpack(u32 c, int& r, int& g, int& b) { r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; }
static inline u32 tweak(u32 c, int d) { int r, g, b; unpack(c, r, g, b); return mk(r + d, g + d, b + d); }

// ---------- framebuffer primitives ----------
static inline void setpx(int x, int y, u32 c) { if (x >= 0 && y >= 0 && x < gW && y < gH) gFrame[y * gW + x] = c; }

static void fillRect(int x, int y, int w, int h, u32 col) {
    for (int yy = std::max(0, y); yy < std::min(gH, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(gW, x + w); xx++)
            gFrame[yy * gW + xx] = col;
}

static void outlineRect(int x, int y, int w, int h, u32 col) {
    fillRect(x, y, w, 2, col); fillRect(x, y + h - 2, w, 2, col);
    fillRect(x, y, 2, h, col); fillRect(x + w - 2, y, 2, h, col);
}

static void fillEllipse(float cx, float cy, float rx, float ry, u32 col) {
    for (int y = (int)floorf(cy - ry); y <= (int)ceilf(cy + ry); y++)
        for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) setpx(x, y, col);
        }
}

static void drawLine(float x0, float y0, float x1, float y1, int thick, u32 col) {
    int steps = (int)std::max(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = i / (float)steps;
        int px = (int)(x0 + (x1 - x0) * t), py = (int)(y0 + (y1 - y0) * t);
        fillRect(px - thick / 2, py - thick / 2, thick, thick, col);
    }
}

// ---------- pixel font (3x5 glyphs) ----------
struct Glyph { char c; uint8_t r[5]; };
static const Glyph GLYPHS[] = {
    {'A',{2,5,7,5,5}}, {'B',{6,5,6,5,6}}, {'C',{3,4,4,4,3}}, {'D',{6,5,5,5,6}}, {'E',{7,4,6,4,7}},
    {'F',{7,4,6,4,4}}, {'G',{3,4,5,5,3}}, {'H',{5,5,7,5,5}}, {'I',{7,2,2,2,7}}, {'J',{1,1,1,5,2}},
    {'K',{5,5,6,5,5}}, {'L',{4,4,4,4,7}}, {'M',{5,7,7,5,5}}, {'N',{5,7,7,7,5}}, {'O',{2,5,5,5,2}},
    {'P',{6,5,6,4,4}}, {'Q',{2,5,5,7,3}}, {'R',{6,5,6,5,5}}, {'S',{3,4,2,1,6}}, {'T',{7,2,2,2,2}},
    {'U',{5,5,5,5,7}}, {'V',{5,5,5,5,2}}, {'W',{5,5,7,7,5}}, {'X',{5,5,2,5,5}}, {'Y',{5,5,2,2,2}},
    {'Z',{7,1,2,4,7}},
    {'0',{7,5,5,5,7}}, {'1',{2,6,2,2,7}}, {'2',{7,1,7,4,7}}, {'3',{7,1,7,1,7}}, {'4',{5,5,7,1,1}},
    {'5',{7,4,7,1,7}}, {'6',{7,4,7,5,7}}, {'7',{7,1,1,1,1}}, {'8',{7,5,7,5,7}}, {'9',{7,5,7,1,7}},
    {' ',{0,0,0,0,0}}, {'-',{0,0,7,0,0}}, {'.',{0,0,0,0,2}}, {':',{0,2,0,2,0}}, {'/',{1,1,2,4,4}},
    {'(',{2,4,4,4,2}}, {')',{2,1,1,1,2}}, {'+',{0,2,7,2,0}}, {'[',{6,4,4,4,6}}, {']',{3,1,1,1,3}},
    {'!',{2,2,2,0,2}}, {'%',{5,1,2,4,5}}, {'\'',{2,2,0,0,0}}, {',',{0,0,0,2,4}}, {'?',{6,1,2,0,2}},
};

static void drawText(int x, int y, const std::string& s, int sc, u32 col) {
    int cx = x;
    for (char raw : s) {
        char ch = (char)toupper((unsigned char)raw);
        const uint8_t* rows = nullptr;
        for (const Glyph& g : GLYPHS) if (g.c == ch) { rows = g.r; break; }
        if (rows) {
            for (int row = 0; row < 5; row++)
                for (int c = 0; c < 3; c++)
                    if ((rows[row] >> (2 - c)) & 1) fillRect(cx + c * sc, y + row * sc, sc, sc, col);
        }
        cx += 4 * sc;
    }
}
static int textWidth(const std::string& s, int sc) { return (int)s.size() * 4 * sc; }

// ---------- procedural textures ----------
static u32 gTex[G_COUNT][TILE * TILE];
static u32 gPlankTex[TILE * TILE], gBrickTex[TILE * TILE];
static u32 gTreeSpr[TILE * TILE], gRockSpr[TILE * TILE], gPineSpr[TILE * TILE], gCampfireSpr[TILE * TILE];
static u32 gBedSpr[TILE * TILE];
static u32 gChestSpr[TILE * TILE];
static u32 gFurnaceSpr[TILE * TILE];
static u32 gLadderSpr[TILE * TILE];
static u32 gOreSpr[4][TILE * TILE];
static u32 gDoorSpr[TILE * TILE], gDoorOpenSpr[TILE * TILE];
static u32 gPetSpr[3][2][2][TILE * TILE];
static u32 gBeastSpr[4][2][2][TILE * TILE];          // [kind: dog, cat, hamster][face: right, left][frame]
static u32 gPlayerSpr[4][5][TILE * TILE];          // [dir][frame: 0-3 walk, 4 attack]               // [dir][frame]
static u32 gZombieSpr[4][4][TILE * TILE];
static u32 gNpcSpr[3][4][4][TILE * TILE];               // [palette][dir][frame]
static u32 gSlimeSpr[3][TILE * TILE];
static u32 gCropSpr[NC][4][TILE * TILE];
static std::vector<float> gCloud(256 * 256);

static int texLevelEff() { return gSet.turbo ? 2 : gSet.texture; }
static int shaderEff() { return gSet.turbo ? 3 : gSet.shader; }

static void genGround(int type, u32* out) {
    int r1, g1, b1, r2, g2, b2;
    switch (type) {
        case G_WATER:    r1 = 18;  g1 = 66;  b1 = 146; r2 = 36;  g2 = 116; b2 = 198; break;
        case G_SAND:     r1 = 230; g1 = 208; b1 = 146; r2 = 204; g2 = 178; b2 = 112; break;
        case G_GRASS:    r1 = 104; g1 = 172; b1 = 74;  r2 = 60;  g2 = 120; b2 = 44;  break;
        case G_FOREST:   r1 = 56;  g1 = 104; b1 = 44;  r2 = 28;  g2 = 66;  b2 = 26;  break;
        case G_DIRT:     r1 = 146; g1 = 104; b1 = 66;  r2 = 100; g2 = 70;  b2 = 44;  break;
        case G_TILLED:   r1 = 104; g1 = 70;  b1 = 42;  r2 = 70;  g2 = 44;  b2 = 26;  break;
        case G_STONE:    r1 = 140; g1 = 140; b1 = 146; r2 = 92;  g2 = 92;  b2 = 102; break;
        case G_SNOW:     r1 = 248; g1 = 251; b1 = 255; r2 = 200; g2 = 214; b2 = 236; break;
        case G_DESERT:   r1 = 236; g1 = 198; b1 = 118; r2 = 198; g2 = 156; b2 = 84;  break;
        case G_JUNGLE:   r1 = 30;  g1 = 96;  b1 = 44;  r2 = 18;  g2 = 64;  b2 = 30;  break;
        case G_MEADOW:   r1 = 136; g1 = 204; b1 = 96;  r2 = 104; g2 = 170; b2 = 72;  break;
        case G_BADLANDS: r1 = 198; g1 = 112; b1 = 72;  r2 = 150; g2 = 74;  b2 = 48;  break;
        default:         r1 = 74;  g1 = 98;  b1 = 58;  r2 = 40;  g2 = 60;  b2 = 38;  break;  // swamp
    }
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        float n = fbm(x / 14.0f, y / 14.0f, 11u + type * 17u);
        float d = rnd(x, y, 101u + type);
        float macro = fbm(x / 40.0f, y / 40.0f, 600u + type * 7u);      // large patches of colour
        float t = std::clamp(n * 0.6f + d * 0.15f + macro * 0.25f, 0.0f, 1.0f);
        int r = (int)lerpf((float)r1, (float)r2, t);
        int g = (int)lerpf((float)g1, (float)g2, t);
        int b = (int)lerpf((float)b1, (float)b2, t);
        int shade = 0;
        if (texLevelEff() > 0) switch (type) {
            case G_GRASS:
                if (rnd(x, y, 555u) > 0.93f) shade = 22;
                else if (rnd(x, y, 556u) > 0.9f) shade = -20;
                if (rnd(x, y, 557u) > 0.996f) { r = 240; g = 220; b = 60; }
                if (rnd(x, y, 558u) > 0.997f) { r = 235; g = 110; b = 200; }
                break;
            case G_MEADOW:
                if (rnd(x, y, 560u) > 0.94f) {
                    float p = rnd(x, y, 561u);
                    if (p < 0.33f) { r = 250; g = 240; b = 90; }
                    else if (p < 0.66f) { r = 245; g = 245; b = 250; }
                    else { r = 200; g = 120; b = 230; }
                }
                if (rnd(x, y, 562u) > 0.9f) shade = 14;
                break;
            case G_JUNGLE:
                if (rnd(x, y, 570u) > 0.97f) { r = 22; g = 48; b = 24; }             // vines
                if (rnd(x, y, 571u) > 0.92f) shade = 18;
                if (fabsf(fbm(x / 7.0f, y / 7.0f, 572u) - 0.5f) < 0.02f) { r = 20; g = 50; b = 26; }
                break;
            case G_BADLANDS: {
                float c = fabsf(fbm(x / 8.0f, y / 8.0f, 580u) - 0.5f);
                if (c < 0.03f) shade = -36;                                          // clay cracks
                if (y % 20 < 2) shade -= 12;                                         // strata
                break;
            }
            case G_DIRT: {
                float p = vnoise(x / 2.5f, y / 2.5f, 400u);
                if (p > 0.72f) shade = 18; else if (p < 0.28f) shade = -18;
                break;
            }
            case G_SAND: shade = (int)(sinf(x * 0.35f + y * 0.12f + sinf(y * 0.25f) * 1.6f) * 7); break;
            case G_STONE: {
                float c = fabsf(fbm(x / 9.0f, y / 9.0f, 500u) - 0.5f);
                if (c < 0.025f) shade = -40;
                if (x + y < 8) shade += 14;
                if (rnd(x, y, 777u) > 0.98f) shade -= 18;
                break;
            }
            case G_SNOW:
                if (rnd(x, y, 600u) > 0.985f) { r = 255; g = 255; b = 255; }
                if (y > 48) shade = -8;
                break;
            case G_WATER: {
                float w = sinf(x * 0.32f + y * 0.11f + sinf(y * 0.2f) * 2.0f);
                if (w > 0.82f) { r += 30; g += 36; b += 40; }
                if (fbm(x / 9.0f, y / 9.0f + 0.5f, 3u) > 0.68f) { r += 12; g += 14; b += 16; }
                break;
            }
            case G_TILLED:
                if ((y % 10) < 2) shade = -18;
                else if (rnd(x, y, 610u) > 0.9f) shade = -10;
                break;
            case G_FOREST:
                if (rnd(x, y, 700u) > 0.95f) { r = 150; g = 92; b = 34; }
                else if (rnd(x, y, 701u) > 0.97f) { r = 90; g = 60; b = 30; }
                break;
            case G_DESERT: shade = (int)(sinf(y * 0.18f + sinf(x * 0.08f) * 2.5f) * 9); break;
            case G_SWAMP:
                if (n > 0.65f) { r -= 10; g += 6; b -= 8; }
                if (rnd(x, y, 800u) > 0.985f) { r = 80; g = 140; b = 60; }
                break;
            default: break;
        }
        r += shade; g += shade; b += shade;
        if (texLevelEff() >= 2) {
            float grain = (rnd(x, y, 999u) - 0.5f) * 12.0f;
            r += (int)grain; g += (int)grain; b += (int)grain;
        }
        int seam = texLevelEff() >= 2 ? 80 : 90;
        if (texLevelEff() >= 1 && (x == 0 || y == 0)) { r += 6; g += 6; b += 6; }   // light bevel on the top-left edge
        if (x == TILE - 1 || y == TILE - 1) { r = r * seam / 100; g = g * seam / 100; b = b * seam / 100; }
        out[y * TILE + x] = mk(r, g, b);
    }
}

static void genBlocks() {
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        int board = y / 16;
        float grain = vnoise(x / 18.0f, y / 2.0f, 900u + board);
        int r = (int)(170 + grain * 26), g = (int)(120 + grain * 18), b = (int)(70 + grain * 10);
        if (y % 16 == 0 || y % 16 == 15) { r -= 40; g -= 30; b -= 20; }
        if ((x == 8 || x == 56) && (y % 16 == 4 || y % 16 == 11)) { r -= 60; g -= 60; b -= 60; }
        gPlankTex[y * TILE + x] = mk(r, g, b);

        int row = y / 16;
        int off = (row % 2) * 16;
        bool mortar = (y % 16) < 2 || ((x + off) % 32) < 2;
        int br = (int)(160 + rnd(x / 8, row, 950u) * 30), bg = (int)(72 + rnd(x / 8, row, 951u) * 14);
        int bb = (int)(56 + rnd(x / 8, row, 952u) * 10);
        if (mortar) { br = 196; bg = 186; bb = 166; }
        gBrickTex[y * TILE + x] = mk(br, bg, bb);
    }
}

static void outlineSprite(u32* spr, u32 col) {
    static u32 copy[TILE * TILE];
    memcpy(copy, spr, sizeof(u32) * TILE * TILE);
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        if (copy[y * TILE + x] >> 24) continue;
        bool nb = (x > 0 && copy[y * TILE + x - 1] >> 24) || (x < TILE - 1 && copy[y * TILE + x + 1] >> 24) ||
                  (y > 0 && copy[(y - 1) * TILE + x] >> 24) || (y < TILE - 1 && copy[(y + 1) * TILE + x] >> 24);
        if (nb) spr[y * TILE + x] = col;
    }
}

static void genTree(u32* out) {
    static bool solid[TILE * TILE];
    memset(solid, 0, sizeof solid);
    memset(out, 0, sizeof(u32) * TILE * TILE);
    const int cs[5][3] = { {32, 24, 19}, {20, 33, 12}, {44, 33, 12}, {32, 14, 13}, {32, 36, 14} };
    for (int y = 0; y < 44; y++) for (int x = 0; x < TILE; x++)
        for (int k = 0; k < 5; k++) {
            float dx = (float)(x - cs[k][0]), dy = (float)(y - cs[k][1]);
            if (dx * dx + dy * dy <= (float)(cs[k][2] * cs[k][2])) { solid[y * TILE + x] = true; break; }
        }
    for (int y = 38; y < 64; y++) {
        int half = (y >= 56) ? 6 : 3;
        for (int x = 32 - half; x <= 32 + half - 1; x++) solid[y * TILE + x] = true;
    }
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        if (!solid[y * TILE + x]) continue;
        bool trunk = y >= 38;
        float n = rnd(x, y, 42u);
        if (trunk) {
            int b = (x % 3 == 0) ? -22 : 0;
            out[y * TILE + x] = mk(118 + b + (int)(n * 16), 74 + b + (int)(n * 10), 38 + b);
        } else {
            float shade = 1.0f - y / 60.0f;
            int hl = (x < 26 && y < 20) ? 30 : 0;
            int g = (int)(88 + 90 * shade + n * 30) + hl;
            int r = (int)(28 + 28 * shade + n * 12) + hl / 2;
            int b = (int)(26 + 10 * shade) + hl / 4;
            if (rnd(x / 3, y / 3, 43u) > 0.8f) g -= 22;
            out[y * TILE + x] = mk(r, g, b);
        }
    }
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        if (solid[y * TILE + x]) continue;
        bool edge = (x > 0 && solid[y * TILE + x - 1]) || (x < TILE - 1 && solid[y * TILE + x + 1]) ||
                    (y > 0 && solid[(y - 1) * TILE + x]) || (y < TILE - 1 && solid[(y + 1) * TILE + x]);
        if (edge) out[y * TILE + x] = RGBX(18, 40, 18);
    }
}

static void genRock(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    static bool solid[TILE * TILE];
    memset(solid, 0, sizeof solid);
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        float dx = (x - 32) / 24.0f, dy = (y - 40) / 18.0f;
        if (dx * dx + dy * dy <= 1.0f) solid[y * TILE + x] = true;
    }
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        if (!solid[y * TILE + x]) continue;
        float dx = (x - 32) / 24.0f, dy = (y - 40) / 18.0f;
        float l = 0.62f - 0.28f * (dx + dy) + (rnd(x, y, 3u) - 0.5f) * 0.12f;
        int v = (int)(118 + 110 * l);
        float c = fabsf(fbm(x / 6.0f, y / 6.0f, 60u) - 0.5f);
        if (c < 0.03f) v -= 45;
        out[y * TILE + x] = mk(v, v, v + 6);
    }
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        if (solid[y * TILE + x]) continue;
        bool edge = (x > 0 && solid[y * TILE + x - 1]) || (x < TILE - 1 && solid[y * TILE + x + 1]) ||
                    (y > 0 && solid[(y - 1) * TILE + x]) || (y < TILE - 1 && solid[(y + 1) * TILE + x]);
        if (edge) out[y * TILE + x] = RGBX(50, 50, 56);
    }
}

static void genPine(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    static bool solid[TILE * TILE];
    memset(solid, 0, sizeof solid);
    for (int y = 2; y < 46; y++) {
        int t = (y - 2) % 13;
        float half = (2.0f + t * 1.1f) * (0.6f + y / 90.0f);
        for (int x = 0; x < TILE; x++) if (fabsf(x + 0.5f - 32.0f) <= half) solid[y * TILE + x] = true;
    }
    for (int y = 42; y < 64; y++) {
        int half = (y >= 58) ? 4 : 2;
        for (int x = 32 - half; x < 32 + half; x++) solid[y * TILE + x] = true;
    }
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        if (!solid[y * TILE + x]) continue;
        float n = rnd(x, y, 52u);
        if (y >= 42) {
            int b = (x % 3 == 0) ? -20 : 0;
            out[y * TILE + x] = mk(110 + b + (int)(n * 14), 72 + b, 40 + b);
            continue;
        }
        int t = (y - 2) % 13;
        int g = 92 + (13 - t) * 4 + (int)(n * 14);
        if (x < 30 && t < 4) g += 18;
        out[y * TILE + x] = mk(22 + y / 8, g, 34 + (int)(n * 8));
    }
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        if (solid[y * TILE + x]) continue;
        bool edge = (x > 0 && solid[y * TILE + x - 1]) || (x < TILE - 1 && solid[y * TILE + x + 1]) ||
                    (y > 0 && solid[(y - 1) * TILE + x]) || (y < TILE - 1 && solid[(y + 1) * TILE + x]);
        if (edge) out[y * TILE + x] = RGBX(16, 36, 20);
    }
}

// Campfire: crossed logs and a stone ring. Flames are animated at render time.
static void genCampfire(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        float dx = (x - 32) / 26.0f, dy = (y - 50) / 9.0f;
        float d = dx * dx + dy * dy;
        if (d <= 1.0f && d > 0.55f) out[y * TILE + x] = mk(120 + (int)(rnd(x, y, 3u) * 30), 120, 128);
    }
    for (int t = 0; t < 36; t++) {
        int x = 14 + t, y1 = 46 + t / 6, y2 = 52 - t / 6;
        for (int th = 0; th < 5; th++) {
            if (x < TILE) out[(y1 + th) * TILE + x] = mk(110 + (th % 2) * 20, 70, 36);
            if (x < TILE) out[(y2 + th) * TILE + x] = mk(96 + (th % 2) * 18, 60, 30);
        }
    }
}

static void genDoor(bool open, u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        u32 c = 0;
        if (x < 4 || x > 59 || y < 2) c = mk(70, 46, 26);                       // frame
        if (!open) {
            if (x >= 8 && x <= 55 && y >= 2) {                                   // closed panel
                c = (y % 12 == 0) ? mk(104, 70, 38) : mk(140, 96, 54);
                if (x >= 46 && x <= 49 && y >= 30 && y <= 33) c = mk(230, 190, 80); // handle
            }
        } else {
            if (x >= 8 && x <= 16 && y >= 2) c = mk(120, 84, 46);                // door swung open
            else if (x >= 17 && x <= 55 && y >= 2) c = mk(18, 12, 8);            // dark doorway
        }
        out[y * TILE + x] = c;
    }
}

static void genBed(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    for (int y = 30; y < 58; y++) for (int x = 4; x < 60; x++) {
        u32 c = (y < 34 || y > 53 || x < 6 || x > 57) ? RGBX(120, 80, 40) : RGBX(190, 50, 60);
        if (y >= 34 && y <= 40 && x >= 9 && x <= 24) c = RGBX(240, 240, 240);
        out[y * TILE + x] = c;
    }
    for (int y = 56; y < 62; y++) for (int x : { 7, 8, 55, 56 }) out[y * TILE + x] = RGBX(80, 50, 26);
    outlineSprite(out, RGBX(50, 30, 16));
}

static void genRabbit(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        u32 c = 0;
        float bx = (x - 30) / 17.0f, by = (y - 42) / 12.0f;
        float hx = (x - 45) / 9.0f, hy = (y - 33) / 8.0f;
        float ex1 = (x - 42) / 3.0f, ey1 = (y - 20) / 10.0f, ex2 = (x - 50) / 3.0f;
        float tx = (x - 14) / 4.0f, ty = (y - 40) / 4.0f;
        if (bx * bx + by * by <= 1.0f) c = (y > 46) ? RGBX(225, 210, 190) : RGBX(170, 130, 90);
        if (hx * hx + hy * hy <= 1.0f) c = RGBX(170, 130, 90);
        if (ex1 * ex1 + ey1 * ey1 <= 1.0f || ex2 * ex2 + ey1 * ey1 <= 1.0f) c = RGBX(170, 130, 90);
        if (tx * tx + ty * ty <= 1.0f) c = RGBX(245, 245, 240);
        if (x >= 47 && x <= 48 && y >= 31 && y <= 32) c = RGBX(30, 20, 20);
        if ((x >= 24 && x <= 30) || (x >= 34 && x <= 40)) if (y >= 52 && y <= 58) c = RGBX(120, 90, 60);
        out[y * TILE + x] = c;
    }
    outlineSprite(out, RGBX(60, 42, 26));
}

// Gives a sprite a soft light from the top left, so it reads as rounded instead of flat.
static void shadePass(u32* out) {
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        u32 c = out[y * TILE + x];
        if (!(c >> 24)) continue;
        float k = 1.12f - 0.28f * (y / 63.0f) + (x < 32 ? 0.05f : 0.0f);
        int r, g, b;
        unpack(c, r, g, b);
        out[y * TILE + x] = mk((int)(r * k), (int)(g * k), (int)(b * k));
    }
}

// Quadruped pets. kind 0 dog, 1 cat, 2 hamster. Frames alternate leg positions.
static void genPet(int kind, int frame, u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    u32 body = kind == 0 ? mk(170, 120, 70) : kind == 1 ? mk(200, 200, 206) : mk(220, 170, 100);
    u32 dark = kind == 0 ? mk(120, 80, 40) : kind == 1 ? mk(120, 120, 130) : mk(170, 120, 70);
    u32 belly = kind == 0 ? mk(220, 190, 150) : kind == 1 ? mk(240, 240, 245) : mk(245, 225, 190);
    int leg = frame ? 2 : -2;
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        u32 c = 0;
        float bx = (x - 30) / 19.0f, by = (y - 40) / 11.0f;
        float hx = (x - 47) / 10.0f, hy = (y - 29) / 9.0f;
        if (kind == 2) { hx = (x - 42) / 12.0f; hy = (y - 36) / 11.0f; }
        if (bx * bx + by * by <= 1.0f) c = (y > 46) ? belly : body;
        if (hx * hx + hy * hy <= 1.0f) c = body;
        if (kind == 0) {
            float ex = (x - 44) / 4.0f, ey = (y - 26) / 9.0f;      // floppy ear
            if (ex * ex + ey * ey <= 1.0f) c = dark;
            float tx = (x - 12) / 4.0f, ty = (y - 30) / 7.0f;      // tail
            if (tx * tx + ty * ty <= 1.0f) c = dark;
        }
        if (kind == 1) {
            if (y >= 16 && y <= 23 && ((x >= 40 && x <= 44) || (x >= 50 && x <= 54))) c = body;  // pointed ears
            if (x >= 8 && x <= 11 && y >= 34 && y <= 52) c = body;                             // long tail
        }
        if (x >= 49 && x <= 50 && y >= 27 && y <= 28) c = mk(20, 20, 20);                      // eye
        if (x >= 55 && x <= 56 && y >= 33 && y <= 34) c = mk(30, 20, 20);                      // nose
        if (((x >= 22 && x <= 26) || (x >= 36 && x <= 40)) && y >= 50 + leg && y <= 58 + leg) c = dark;
        out[y * TILE + x] = c;
    }
    shadePass(out);
    outlineSprite(out, RGBX(30, 22, 18));
}

static void genPetSprites() {
    static u32 tmp[TILE * TILE];
    for (int kind = 0; kind < 3; kind++) for (int f = 0; f < 2; f++) {
        genPet(kind, f, tmp);
        memcpy(gPetSpr[kind][0][f], tmp, sizeof(u32) * TILE * TILE);
        for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++)
            gPetSpr[kind][1][f][y * TILE + x] = tmp[y * TILE + (TILE - 1 - x)];
    }
}

// Ores are rock with coloured veins. Copper, iron, diamond and obsidian.
static void genOres() {
    static const u32 OC[4] = { RGBX(210, 130, 70), RGBX(205, 205, 215), RGBX(110, 230, 240), RGBX(60, 30, 80) };
    for (int m = 0; m < 4; m++) {
        memcpy(gOreSpr[m], gRockSpr, sizeof(u32) * TILE * TILE);
        for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
            if (!(gRockSpr[y * TILE + x] >> 24)) continue;
            float n = rnd(x / 2, y / 2, 700u + m);
            if (n > 0.86f) gOreSpr[m][y * TILE + x] = OC[m];
            else if (n > 0.80f && m == 3) gOreSpr[m][y * TILE + x] = RGBX(120, 60, 150);
        }
        outlineSprite(gOreSpr[m], RGBX(40, 40, 46));
    }
}

// A wooden chest with a lid line and a metal clasp.
static void genChest(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    for (int y = 14; y < 56; y++) for (int x = 8; x < 56; x++) {
        u32 c = (y < 30) ? mk(170, 116, 60) : mk(140, 92, 46);
        if (y == 30 || y == 31) c = mk(90, 58, 30);                        // lid seam
        if (x == 8 || x == 55 || y == 14 || y == 55) c = mk(90, 58, 30);   // frame
        if (x >= 29 && x <= 34 && y >= 26 && y <= 36) c = mk(230, 190, 80); // clasp
        out[y * TILE + x] = c;
    }
    outlineSprite(out, RGBX(50, 30, 16));
}

static void genBeast(int kind, int frame, u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    u32 body = kind == 0 ? mk(240, 240, 235) : kind == 1 ? mk(238, 238, 242) : kind == 2 ? mk(140, 90, 50) : mk(205, 170, 110);
    u32 patch = kind == 0 ? mk(40, 30, 25) : kind == 1 ? mk(70, 62, 62) : kind == 2 ? mk(90, 55, 30) : mk(180, 140, 90);
    u32 dark = mk(80, 60, 45);
    bool tall = kind >= 2;
    int lg = frame ? 3 : -3;
    int legTop = 44, legBot = tall ? 62 : 56;
    int headY = tall ? 22 : 30;
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        u32 c = 0;
        float bx = (x - 28) / 20.0f, by = (y - (tall ? 36 : 40)) / (tall ? 9.0f : 11.0f);
        float hx = (x - 53) / (tall ? 7.0f : 9.0f), hy = (y - headY) / (tall ? 8.0f : 9.0f);
        if (bx * bx + by * by <= 1.0f) c = (((x * 7 + y * 3) % 13) < 2 && kind <= 1) ? patch : body;
        if (kind == 3) {                                      // camel hump
            float ux = (x - 27) / 7.0f, uy = (y - 26) / 6.0f;
            if (ux * ux + uy * uy <= 1.0f) c = body;
        }
        if (hx * hx + hy * hy <= 1.0f) c = body;
        if (kind == 2 && x >= 46 && x <= 54 && y >= 24 && y <= 40) c = body;             // horse neck
        if (kind == 0 && x >= 56 && x <= 60 && y >= headY - 2 && y <= headY + 2) c = mk(220, 150, 150);  // cow nose
        if (x >= 49 && x <= 50 && y >= headY - 2 && y <= headY - 1) c = mk(25, 25, 25);  // eye
        if (((x >= 18 && x <= 22) || (x >= 36 && x <= 40)) && y >= legTop && y <= legBot) {
            int off = (x < 30) ? lg : -lg;
            if (y >= legTop + off && y <= legBot + off) c = dark;
        }
        if (x >= 6 && x <= 9 && y >= 30 && y <= 40) c = patch;                            // tail
        out[y * TILE + x] = c;
    }
    shadePass(out);
    outlineSprite(out, RGBX(30, 24, 20));
}

static void genBeastSprites() {
    static u32 tmp[TILE * TILE];
    for (int kind = 0; kind < 4; kind++) for (int f = 0; f < 2; f++) {
        genBeast(kind, f, tmp);
        memcpy(gBeastSpr[kind][0][f], tmp, sizeof(u32) * TILE * TILE);
        for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++)
            gBeastSpr[kind][1][f][y * TILE + x] = tmp[y * TILE + (TILE - 1 - x)];
    }
}

// A stone furnace with a glowing fire mouth.
static void genFurnace(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    for (int y = 8; y < 60; y++) for (int x = 8; x < 56; x++) {
        u32 c = (y % 12 == 0) ? mk(88, 86, 94) : mk(120, 118, 126);
        if (x >= 22 && x <= 42 && y >= 36 && y <= 54) c = (x >= 26 && x <= 38 && y >= 44 && y <= 52) ? mk(255, 140, 40) : mk(40, 20, 10);
        out[y * TILE + x] = c;
    }
    outlineSprite(out, RGBX(50, 50, 56));
}

// A ladder: two rails with rungs, on a transparent background.
static void genLadder(u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    for (int y = 4; y < 62; y++) for (int x = 0; x < TILE; x++) {
        if (x >= 18 && x <= 21) out[y * TILE + x] = mk(140, 96, 52);
        if (x >= 42 && x <= 45) out[y * TILE + x] = mk(140, 96, 52);
        if (y % 10 == 0 && x >= 22 && x <= 41) out[y * TILE + x] = mk(170, 120, 70);
    }
}

// Humanoid palette: lets the player, zombies and villagers share one body generator.
struct Pal { u32 shirt, shirtD, pants, pantD, skin, skinD, hair, hairD, belt, boots, eye; bool hat; u32 hatCol; };
static const Pal PAL_PLAYER = { mk(58, 120, 214), mk(42, 92, 176), mk(66, 54, 44), mk(50, 40, 32),
                                mk(242, 204, 166), mk(214, 170, 134), mk(92, 58, 34), mk(64, 38, 22),
                                mk(70, 44, 24), mk(38, 28, 24), mk(28, 28, 40), false, 0 };
static const Pal PAL_ZOMBIE = { mk(70, 100, 130), mk(50, 75, 100), mk(60, 60, 70), mk(44, 44, 52),
                                mk(120, 170, 96), mk(92, 140, 72), mk(40, 50, 36), mk(28, 36, 26),
                                mk(30, 20, 15), mk(30, 24, 22), mk(210, 40, 40), false, 0 };
static const Pal PAL_VILLAGERS[3] = {
    { mk(150, 110, 70), mk(120, 86, 52), mk(110, 80, 60), mk(88, 62, 46), mk(236, 196, 160), mk(206, 166, 130),
      mk(120, 80, 40), mk(90, 58, 30), mk(60, 40, 20), mk(60, 40, 30), mk(28, 28, 40), true, mk(60, 140, 60) },
    { mk(90, 90, 100), mk(70, 70, 80), mk(60, 52, 48), mk(44, 38, 34), mk(232, 190, 150), mk(200, 160, 124),
      mk(40, 36, 34), mk(26, 24, 22), mk(30, 22, 16), mk(30, 24, 22), mk(28, 28, 40), true, mk(60, 60, 70) },
    { mk(240, 240, 244), mk(210, 210, 216), mk(110, 80, 60), mk(88, 62, 46), mk(240, 200, 170), mk(210, 170, 140),
      mk(170, 120, 60), mk(120, 80, 40), mk(70, 50, 30), mk(60, 40, 30), mk(28, 28, 40), true, mk(250, 250, 250) },
};

// Generic humanoid. Frames: 0 idle, 1 left step, 2 idle breath, 3 right step.
static void genHumanoid(const Pal& p, int dir, int frame, u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    int ly = frame == 1 ? -2 : (frame == 3 ? 2 : 0);
    int ay = -ly;
    int bob = frame == 0 ? 0 : 1;
    int hy = 20 - bob;
    auto in = [](int x, int y, int x0, int y0, int x1, int y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; };
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        u32 c = 0;
        float dx = (float)(x - 32), dy = (float)(y - hy);
        bool head = dx * dx + dy * dy <= 72.0f;
        if (p.hat && dx * dx + (dy + 3) * (dy + 3) <= 110.0f && y < hy - 2) c = p.hatCol;
        if (p.hat && in(x, y, 20, hy - 3, 44, hy - 2)) c = p.hatCol;   // brim
        if (head && c == 0) {
            bool hairTop = (y < hy - 2) || dir == 0;
            c = hairTop ? (x < 32 ? p.hair : p.hairD) : (x < 30 ? p.skin : p.skinD);
            if (!p.hat || y >= hy - 1) {
                if (dir == 2 && (in(x, y, 27, hy + 1, 28, hy + 3) || in(x, y, 35, hy + 1, 36, hy + 3))) c = p.eye;
                if (dir == 1 && in(x, y, 36, hy + 1, 37, hy + 3)) c = p.eye;
                if (dir == 3 && in(x, y, 27, hy + 1, 28, hy + 3)) c = p.eye;
                if (dir == 2 && in(x, y, 31, hy + 6, 33, hy + 6)) c = mk(170, 80, 70);
            }
        } else if (c == 0 && in(x, y, 23, 28 - bob, 40, 45 - bob)) {
            c = (x < 32) ? p.shirt : p.shirtD;
            if (y >= 41 - bob && y <= 43 - bob) c = p.belt;
        } else if (c == 0 && (in(x, y, 18, 30 - bob + ay, 22, 42 - bob + ay) || in(x, y, 41, 30 - bob - ay, 45, 42 - bob - ay))) {
            c = p.shirt;
            if (in(x, y, 18, 40 - bob + ay, 22, 42 - bob + ay) || in(x, y, 41, 40 - bob - ay, 45, 42 - bob - ay)) c = p.skin;
        } else if (c == 0 && (in(x, y, 25, 45 + ly, 31, 54 + ly) || in(x, y, 33, 45 - ly, 39, 54 - ly))) {
            c = (x < 32) ? p.pants : p.pantD;
            if (y >= 52 + ly && x >= 25 && x <= 31) c = p.boots;
            if (y >= 52 - ly && x >= 33 && x <= 39) c = p.boots;
        }
        out[y * TILE + x] = c;
    }
    if (frame == 4) {                        // attack pose: the sword arm thrusts out
        auto block = [&](int x0, int y0, int x1, int y1, u32 c) {
            for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
                if (x >= 0 && x < TILE && y >= 0 && y < TILE) out[y * TILE + x] = c;
        };
        if (dir == 1) { block(40, 26, 58, 31, p.shirt); block(56, 26, 60, 31, p.skin); }
        else if (dir == 3) { block(5, 26, 24, 31, p.shirt); block(4, 26, 8, 31, p.skin); }
        else if (dir == 0) { block(44, 4, 49, 22, p.shirt); block(44, 2, 49, 5, p.skin); }
        else { block(8, 26, 22, 31, p.shirt); block(41, 26, 56, 31, p.shirt); block(54, 26, 58, 31, p.skin); }
    }
    shadePass(out);
    outlineSprite(out, RGBX(24, 20, 30));
}

// Slime: a squashing green blob. Frame 0 wide, frame 1 tall.
static void genSlime(int frame, u32* out) {   // frame 0 squashed, 1 stretched, 2 round
    memset(out, 0, sizeof(u32) * TILE * TILE);
    float rx = frame == 0 ? 24.0f : frame == 1 ? 19.0f : 22.0f, ry = frame == 0 ? 17.0f : frame == 1 ? 25.0f : 20.0f;
    float cy = 54.0f - ry;
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
        float dx = (x + 0.5f - 32.0f) / rx, dy = (y + 0.5f - cy) / ry;
        if (dx * dx + dy * dy > 1.0f) continue;
        int r = 60, g = 190, b = 80;
        if (dx < -0.35f && dy < -0.35f) { r = 150; g = 240; b = 150; }      // gloss
        else if (dy > 0.45f) { r = 40; g = 140; b = 60; }                   // shadow
        out[y * TILE + x] = mk(r, g, b);
    }
    for (int y = (int)cy - 3; y <= (int)cy; y++) {
        for (int x = 25; x <= 27; x++) if (x < TILE && y >= 0) out[y * TILE + x] = RGBX(20, 30, 20);
        for (int x = 37; x <= 39; x++) if (x < TILE && y >= 0) out[y * TILE + x] = RGBX(20, 30, 20);
    }
    outlineSprite(out, RGBX(20, 90, 40));
}

static void genCrop(int ci, int stage, u32* out) {
    memset(out, 0, sizeof(u32) * TILE * TILE);
    const CropDef& cd = CROPS[ci];
    const u32 leaf = cd.leaf, ripe = cd.ripe;
    auto put = [&](int x, int y, u32 c) { if (x >= 0 && x < TILE && y >= 0 && y < TILE) out[y * TILE + x] = c; };
    switch (cd.style) {
        case 0: {
            const int hts[4] = { 10, 22, 34, 44 };
            for (int i = 0; i < 14; i++) {
                int sx = 5 + i * 4, h = hts[stage] - (i % 3) * 2;
                for (int k = 0; k <= h; k++) put(sx + (int)(sinf(k * 0.18f + i) * 1.2f), 63 - k, stage == 3 ? ripe : leaf);
                if (stage == 3) for (int ey = 0; ey < 6; ey++) for (int ex = -1; ex <= 1; ex++) put(sx + ex, 63 - h - ey, ripe);
            }
            break;
        }
        case 1: {
            int len = 6 + stage * 5;
            for (int i = 0; i < 7; i++) {
                int dd = i - 3; if (dd < 0) dd = -dd;
                int tipx = 32 + (i - 3) * (len / 3 + 1), tipy = 58 - len + dd;
                for (int k = 0; k <= len; k++) {
                    float t = k / (float)len;
                    put((int)(32 + (tipx - 32) * t), (int)(58 + (tipy - 58) * t), leaf);
                }
            }
            if (stage >= 2) {
                for (int y = 48; y <= 58; y++) for (int x = 25; x <= 39; x++) {
                    float dx = (x - 32) / 7.0f, dy = (y - 53) / 5.0f;
                    if (dx * dx + dy * dy <= 1.0f) put(x, y, ripe);
                }
            }
            break;
        }
        case 2: {
            float rad = (float)(5 + stage * 4 + (ci == 10 ? 4 : 0));
            for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) {
                float dx = (float)(x - 32), dy = (float)(y - 46);
                float d2 = dx * dx + dy * dy;
                if (d2 <= rad * rad && (rnd(x, y, (uint32_t)ci * 31u + 7u) > 0.12f || d2 < 4.0f)) {
                    int r, g, b; unpack(leaf, r, g, b);
                    int j = (int)(rnd(x, y, 8u) * 24 - 12);
                    put(x, y, mk(r + j, g + j, b + j));
                }
            }
            if (stage == 3) {
                for (int i = 0; i < 9; i++) {
                    int fx = 10 + (int)(rnd(i, ci, 91u) * 44), fy = 36 + (int)(rnd(i, ci, 92u) * 16);
                    for (int dy = 0; dy < 3; dy++) for (int dx = 0; dx < 3; dx++) put(fx + dx, fy + dy, ripe);
                }
            }
            break;
        }
        default: {
            int h = 20 + stage * 10, hy = 63 - h;
            for (int k = 0; k <= h; k++) put(32, 63 - k, leaf);
            for (int k = 0; k < 8; k++) { put(30 - k / 2, 52 + k / 2, leaf); put(34 + k / 2, 46 + k / 2, leaf); }
            if (stage >= 2) {
                int hr = 4 + stage * 2;
                for (int y = hy - hr; y <= hy + hr; y++) for (int x = 32 - hr; x <= 32 + hr; x++) {
                    float dx = (float)(x - 32), dy = (float)(y - hy);
                    float d2 = dx * dx + dy * dy;
                    if (d2 <= (float)(hr * hr)) put(x, y, d2 <= (float)(hr * hr) * 0.3f ? RGBX(90, 55, 20) : ripe);
                }
            }
            break;
        }
    }
}

// ---------- world ----------
struct Cell { uint8_t ground; uint8_t obj; uint8_t crop; uint8_t pad; float plantedAt; };   // crop: 0 none, else index+1
static std::vector<Cell> gMap(MAP_W * MAP_H);
static std::vector<Cell> gUnderMap(MAP_W * MAP_H);   // the level you are not on (surface or caves)
static bool gUnderground = false;
static bool inB(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
static Cell& cellAt(int x, int y) { return gMap[y * MAP_W + x]; }

struct Homestead { int x, y, cleared, activated; };

static void rebuildDoors() {
    gDoors.clear();
    for (int i = 0; i < MAP_W * MAP_H; i++)
        if (gMap[i].obj == O_DOOR_CLOSED || gMap[i].obj == O_DOOR_OPEN) gDoors.push_back(i);
}
static std::vector<Homestead> gHomesteads;

static bool homesteadSiteOK(int x0, int y0) {
    for (int y = y0; y < y0 + 6; y++) for (int x = x0; x < x0 + 15; x++) {
        if (!inB(x, y)) return false;
        uint8_t g = cellAt(x, y).ground;
        if (!(g == G_GRASS || g == G_MEADOW || g == G_FOREST || g == G_DIRT)) return false;
    }
    return true;
}

// The caves: rock walls with carved chambers and ores. Every surface cave hole gets an open
// shaft below it with a ladder back up.
static void generateUnderground(uint32_t seed) {
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
        Cell c = { G_STONE, O_ROCK, 0, 0, 0.0f };
        bool edge = x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1;
        if (!edge && fbm(x / 28.0f, y / 28.0f, seed + 1001u) > 0.56f) {
            c.ground = G_CAVE;
            c.obj = O_NONE;
        } else if (!edge) {
            float r = rnd(x, y, seed + 1002u);
            if (r < 0.05f) c.obj = O_ORE_COPPER;
            else if (r < 0.08f) c.obj = O_ORE_IRON;
            else if (r < 0.085f) c.obj = O_ORE_DIAMOND;
            else if (r < 0.09f) c.obj = O_ORE_OBSIDIAN;
        }
        gUnderMap[y * MAP_W + x] = c;
    }
    // gMap is the surface here: every cave hole on it opens a shaft down to a ladder.
    for (int y = 2; y < MAP_H - 2; y++) for (int x = 2; x < MAP_W - 2; x++) {
        if (gMap[y * MAP_W + x].ground != G_CAVE) continue;
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            Cell& u = gUnderMap[(y + dy) * MAP_W + (x + dx)];
            u.ground = G_CAVE;
            u.obj = O_NONE;
        }
        gUnderMap[y * MAP_W + x].obj = O_LADDER;
    }
}

static void placeHouse(int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) {
        if (!inB(x, y)) continue;
        Cell& c = cellAt(x, y);
        c.ground = G_DIRT; c.crop = 0;
        bool border = (x == x0 || x == x0 + w - 1 || y == y0 || y == y0 + h - 1);
        c.obj = border ? O_PLANK : O_NONE;
    }
    int dx = x0 + w / 2, dy = y0 + h - 1;
    if (inB(dx, dy)) cellAt(dx, dy).obj = O_DOOR_CLOSED;
}

static void placeHomestead(int x0, int y0, uint32_t seed) {
    placeHouse(x0, y0, 7, 5);
    for (int y = y0; y < y0 + 5; y++) for (int x = x0 + 8; x < x0 + 14; x++) {
        if (!inB(x, y)) continue;
        Cell& c = cellAt(x, y);
        c.ground = G_TILLED; c.obj = O_NONE; c.crop = 0;
        if (rnd(x, y, seed + 904u) < 0.6f) {
            c.crop = (uint8_t)(1 + (int)(rnd(x, y, seed + 905u) * NC) % NC);
            c.plantedAt = -rnd(x, y, seed + 906u) * 30.0f;
        }
    }
}

static void generateWorld(uint32_t seed) {
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
        float fx = x / 40.0f, fy = y / 40.0f;
        float dx = (x - MAP_W / 2) / (MAP_W / 2.0f), dy = (y - MAP_H / 2) / (MAP_H / 2.0f);
        float e = fbm(fx, fy, seed) - 0.25f * (dx * dx + dy * dy);
        float m = fbm(fx + 50, fy + 50, seed * 31u + 7u);
        Cell c = { G_GRASS, O_NONE, 0, 0, 0.0f };
        if (e < 0.30f) c.ground = G_WATER;
        else if (e < 0.33f) c.ground = G_SAND;
        else if (e > 0.78f) c.ground = G_SNOW;
        else if (e > 0.68f) c.ground = G_STONE;
        else if (e > 0.5f && e < 0.66f && m < 0.3f) c.ground = G_BADLANDS;
        else if (m < 0.38f) c.ground = G_DESERT;
        else if (m > 0.8f && e < 0.55f) c.ground = G_JUNGLE;
        else if (m > 0.66f && e < 0.5f) c.ground = G_SWAMP;
        else if (m > 0.58f) c.ground = G_FOREST;
        else if (fbm(fx * 2.5f + 9, fy * 2.5f + 9, seed + 77u) > 0.6f) c.ground = G_MEADOW;
        else c.ground = G_GRASS;

        float r = rnd(x, y, seed + 5u);
        if (c.ground == G_FOREST && r < 0.32f) c.obj = O_TREE;
        else if (c.ground == G_JUNGLE && r < 0.42f) c.obj = O_TREE;
        else if (c.ground == G_SWAMP && r < 0.14f) c.obj = O_TREE;
        else if (c.ground == G_GRASS && r < 0.02f) c.obj = O_TREE;
        else if (c.ground == G_STONE && r < 0.08f) c.obj = O_ROCK;
        else if (c.ground == G_BADLANDS && r < 0.10f) c.obj = O_ROCK;
        else if (c.ground == G_SNOW && r < 0.02f) c.obj = O_ROCK;
        if (c.ground == G_STONE) {
            float rr = rnd(x, y, seed + 911u);
            if (e > 0.74f && rr < 0.02f) c.obj = O_ORE_DIAMOND;
            else if (e > 0.76f && rr < 0.035f) c.obj = O_ORE_OBSIDIAN;
            else if (rr < 0.05f) c.obj = O_ORE_IRON;
            else if (rr < 0.10f) c.obj = O_ORE_COPPER;
            if (fbm(fx * 1.3f + 7, fy * 1.3f + 7, seed + 777u) > 0.6f) c.ground = G_CAVE;   // cave pockets
        }
        cellAt(x, y) = c;
    }
    // Safe, green spawn area around the centre.
    for (int y = MAP_H / 2 - 8; y <= MAP_H / 2 + 8; y++) for (int x = MAP_W / 2 - 8; x <= MAP_W / 2 + 8; x++) {
        float d = std::sqrt((float)((x - MAP_W / 2) * (x - MAP_W / 2) + (y - MAP_H / 2) * (y - MAP_H / 2)));
        if (d <= 8 && inB(x, y)) {
            Cell& c = cellAt(x, y);
            c.ground = G_GRASS;
            if (d <= 4) c.obj = O_NONE;
        }
    }
    // Village: a clear meadow with three houses.
    for (int y = VILLAGE_Y - 12; y <= VILLAGE_Y + 12; y++) for (int x = VILLAGE_X - 14; x <= VILLAGE_X + 14; x++) {
        if (!inB(x, y)) continue;
        Cell& c = cellAt(x, y);
        if (c.ground == G_WATER || c.ground == G_SAND) continue;
        c.ground = G_GRASS; c.obj = O_NONE;
    }
    placeHouse(VILLAGE_X - 9, VILLAGE_Y - 6, 5, 4);
    placeHouse(VILLAGE_X + 2, VILLAGE_Y - 7, 5, 4);
    placeHouse(VILLAGE_X - 2, VILLAGE_Y + 2, 5, 4);

    // Rival empires: each 300x300 zone has a 25% chance of one.
    gEmpires.clear();
    for (int gy = 0; gy < MAP_H / 300; gy++) for (int gx = 0; gx < MAP_W / 300; gx++) {
        if (rnd(gx, gy, seed + 921u) >= 0.25f) continue;
        int x0 = gx * 300 + 60 + (int)(rnd(gx, gy, seed + 922u) * 180);
        int y0 = gy * 300 + 60 + (int)(rnd(gx, gy, seed + 923u) * 180);
        float dsp = std::hypot((float)(x0 - MAP_W / 2), (float)(y0 - MAP_H / 2));
        float dvl = std::hypot((float)(x0 - VILLAGE_X), (float)(y0 - VILLAGE_Y));
        if (dsp < 60.0f || dvl < 40.0f) continue;
        if (!homesteadSiteOK(x0, y0) || !homesteadSiteOK(x0 + 9, y0) || !homesteadSiteOK(x0, y0 + 9)) continue;
        placeHouse(x0, y0, 7, 5);
        placeHouse(x0 + 9, y0, 7, 5);
        placeHouse(x0, y0 + 9, 7, 5);
        Empire e;
        e.x = x0 + 8; e.y = y0 + 6;
        e.friends = 15 + (int)(rnd(gx, gy, seed + 924u) * 35);
        e.pets = 5 + (int)(rnd(gx, gy, seed + 925u) * 15);
        e.food = 200 + (int)(rnd(gx, gy, seed + 926u) * 400);
        e.state = 0;
        gEmpires.push_back(e);
    }

    // Hostile homesteads: each 100x100 zone has a 20% chance of one.
    gHomesteads.clear();
    for (int gy = 0; gy < MAP_H / 100; gy++) for (int gx = 0; gx < MAP_W / 100; gx++) {
        if (rnd(gx, gy, seed + 901u) >= 0.20f) continue;
        int x0 = gx * 100 + 4 + (int)(rnd(gx, gy, seed + 902u) * 90);
        int y0 = gy * 100 + 4 + (int)(rnd(gx, gy, seed + 903u) * 90);
        float dsp = std::hypot((float)(x0 - MAP_W / 2), (float)(y0 - MAP_H / 2));
        float dvl = std::hypot((float)(x0 - VILLAGE_X), (float)(y0 - VILLAGE_Y));
        if (dsp < 25.0f || dvl < 25.0f) continue;
        if (!homesteadSiteOK(x0, y0)) continue;
        placeHomestead(x0, y0, seed);
        gHomesteads.push_back({ x0, y0, 0, 0 });
    }
}

// ---------- game state ----------
struct Player { float x, y; int dir; float energy; };
static Player gP;
static float gHP = 100.0f;
static float gHurtT = 0;
static int gSelItem = -1;
static int gPage = 0;
static bool gCraftOpen = false;
static bool gCookOpen = false;
static int gCookPage = 0;
static int gCampX = 0, gCampY = 0;
static int gPot[3] = { 0, 0, 0 };
static float gTime = 0;
static float gAutoT = 0;
static float gWalkT = 0;
static bool gMoving = false;
static bool gRain = false;
static float gRainT = 90.0f;

enum Weather { W_CLEAR, W_CLOUDY, W_RAIN, W_STORM, W_SNOW, W_SNOWSTORM, W_SANDSTORM, W_FOG, W_HEATWAVE,
                W_BLIZZARD, W_HAIL, W_MONSOON, W_COUNT_W };
static const char* WEATHER_NAMES[W_COUNT_W] = { "CLEAR", "CLOUDY", "RAIN", "STORM", "SNOW", "SNOWSTORM", "SANDSTORM", "FOG", "HEATWAVE", "BLIZZARD", "HAIL", "MONSOON" };
static const int WEATHER_WEIGHT[W_COUNT_W] = { 30, 15, 15, 6, 8, 4, 5, 6, 6, 3, 4, 3 };
static int gWeather = W_CLEAR;
static float gWeatherT = 60.0f;
static float gWindX = 0.0f;
static float gFlashT = 0.0f, gFlashCool = 6.0f;

enum Sick { S_NONE, S_FLU, S_DUST, S_HEAT, S_RABBIT, S_COUNT_S };
static const char* SICK_NAMES[S_COUNT_S] = { "Healthy", "Flu", "Dust Cough", "Heatstroke", "Rabbit Fever" };
static int gSick = S_NONE;
static float gSickT = 0.0f;
static void catchSick(int s);
static float gWave[64];
static float gSwingT = 0, gAttackCool = 0;
static float gDialogT = 0;
static std::string gDialog;
static std::string gSavePath;

// ---------- monsters and villagers ----------
struct Monster { float x, y, hp, cool, wander, flash, dx, dy; int type; int dir; int home; int byCompanion; };
static std::vector<Monster> gMonsters;
static const float MON_MAXHP[3] = { 22.0f, 36.0f, 30.0f };
static const float MON_SPEED[3] = { 1.3f, 2.0f, 1.8f };
static const float MON_DMG[3] = { 6.0f, 9.0f, 8.0f };
static const int MON_XP[3] = { 12, 20, 30 };
static float gSpawnT = 0;

struct Npc { float x, y; int pal; int kind; };
static std::vector<Npc> gNpcs;
static const char* NPC_NAMES[3] = { "ADA", "BROM", "LIA" };
struct Trade { int needItem, needN, giveItem, giveN; const char* line; };
static const Trade TRADES[3] = {
    { I_PROD + 0, 4, I_SEED + 3, 3, "ADA: Fresh wheat! Bring 4 and I'll trade you 3 carrot seeds." },
    { I_STONE,    5, I_BRICK,    2, "BROM: Bring me 5 stone and I'll forge you 2 bricks." },
    { I_FISH,     2, I_BREAD,    1, "LIA: Two fish make a fine loaf. Bring me 2 fish." },
};

static void initNpcs() {
    gNpcs.clear();
    gNpcs.push_back({ VILLAGE_X - 4.5f, VILLAGE_Y + 3.5f, 0, 0 });
    gNpcs.push_back({ VILLAGE_X + 6.5f, VILLAGE_Y + 3.5f, 1, 1 });
    gNpcs.push_back({ VILLAGE_X + 0.5f, VILLAGE_Y - 1.5f, 2, 2 });
}

static void setDialog(const std::string& s) { gDialog = s; gDialogT = 5.0f; }

static void talkTo(const Npc& n) {
    const Trade& t = TRADES[n.kind];
    if (gCount[t.needItem] >= t.needN) {
        gCount[t.needItem] -= t.needN;
        gCount[t.giveItem] += t.giveN;
        addXP(4);
        setDialog(std::string(NPC_NAMES[n.kind]) + ": Pleasure doing business! Here's your " + itemName(t.giveItem) + ".");
    } else {
        setDialog(t.line);
    }
}

struct WeaponDef { int item; int dmg; float cool; float reach; };
static const WeaponDef WEAPONS[] = {
    // Axes are tools and weapons: more damage than the sword of the same tier, a little slower.
    { I_AXE,         20, 0.55f, 1.2f },
    { I_TIER + 0 * 7 + 1, 24, 0.55f, 1.2f },
    { I_TIER + 1 * 7 + 1, 30, 0.55f, 1.2f },
    { I_TIER + 2 * 7 + 1, 38, 0.55f, 1.2f },
    { I_TIER + 3 * 7 + 1, 48, 0.55f, 1.2f },
    { I_TIER + 0 * 7 + 2, 18, 0.40f, 1.2f },
    { I_TIER + 1 * 7 + 2, 24, 0.40f, 1.2f },
    { I_TIER + 2 * 7 + 2, 32, 0.40f, 1.2f },
    { I_TIER + 3 * 7 + 2, 42, 0.40f, 1.2f },
    { I_MACE,        18, 0.55f, 1.2f },
    { I_STONE_SWORD, 14, 0.40f, 1.2f },
    { I_SPEAR,       12, 0.50f, 1.8f },
    { I_WOOD_SWORD,   8, 0.40f, 1.2f },
    { I_DAGGER,       6, 0.18f, 1.0f },
};
static const WeaponDef* bestWeapon() {
    const WeaponDef* best = nullptr;
    for (const WeaponDef& w : WEAPONS)
        if (gCount[w.item] > 0 && (!best || w.dmg > best->dmg)) best = &w;
    return best;
}

// ---------- particles (ambient animation) ----------
struct Particle { float x, y, vx, vy, life; u32 col; };
static std::vector<Particle> gParticles;
static std::vector<std::pair<float, float>> gLights;
static float gPartVX = 0.0f, gPartVY = 30.0f, gPartSpread = 30.0f;

static void updateParticles(float dt, u32 ambientCol, int spawnPerSec) {
    for (Particle& p : gParticles) {
        p.life -= dt;
        p.x += (p.vx + sinf(gTime * 2.0f + p.y * 0.05f) * 12.0f) * dt;
        p.y += p.vy * dt;
    }
    gParticles.erase(std::remove_if(gParticles.begin(), gParticles.end(),
                     [](const Particle& p) { return p.life <= 0 || p.x < -10 || p.y > gH + 10 || p.x > gW + 10; }),
                     gParticles.end());
    if (ambientCol == 0) return;
    static float acc = 0;
    acc += dt * spawnPerSec;
    while (acc >= 1.0f && gParticles.size() < 160) {
        acc -= 1.0f;
        uint32_t s = (uint32_t)(gTime * 1000) + (uint32_t)gParticles.size() * 7u;
        Particle p;
        p.x = (float)(hash2((int)s, 1, 3u) % (uint32_t)gW);
        p.y = -4;
        p.vx = gPartVX + (rnd((int)s, 2, 9u) - 0.5f) * gPartSpread;
        p.vy = gPartVY + rnd((int)s, 3, 11u) * 40.0f;
        p.life = 6.0f;
        p.col = ambientCol;
        gParticles.push_back(p);
    }
}

// ---------- sprite blitting ----------
static void blitOpaque(int dx, int dy, const u32* src, bool water) {
    for (int y = 0; y < TILE; y++) {
        int py = dy + y; if (py < 0 || py >= gH) continue;
        for (int x = 0; x < TILE; x++) {
            int px = dx + x; if (px < 0 || px >= gW) continue;
            u32 c = src[y * TILE + x];
            if (water) c = tweak(c, (int)gWave[(x + y) & 63]);
            gFrame[py * gW + px] = c;
        }
    }
}

static void blitAlpha(int dx, int dy, const u32* src, int xoff = 0) {
    for (int y = 0; y < TILE; y++) {
        int py = dy + y; if (py < 0 || py >= gH) continue;
        for (int x = 0; x < TILE; x++) {
            int px = dx + x + xoff; if (px < 0 || px >= gW) continue;
            u32 s = src[y * TILE + x];
            u32 a = s >> 24;
            if (!a) continue;
            u32& d = gFrame[py * gW + px];
            u32 r = (((s >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
            u32 g = (((s >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
            u32 b = ((s & 255) * a + (d & 255) * (255 - a)) / 255;
            d = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

// Like blitAlpha, but the sprite is pushed toward white by amt (0..1) for the hit flash.
static void blitAlphaTint(int dx, int dy, const u32* src, float amt) {
    for (int y = 0; y < TILE; y++) {
        int py = dy + y; if (py < 0 || py >= gH) continue;
        for (int x = 0; x < TILE; x++) {
            int px = dx + x; if (px < 0 || px >= gW) continue;
            u32 sc = src[y * TILE + x];
            u32 a = sc >> 24;
            if (!a) continue;
            u32 r0 = (sc >> 16) & 255, g0 = (sc >> 8) & 255, b0 = sc & 255;
            u32 r = (u32)(r0 + (255.0f - r0) * amt), g = (u32)(g0 + (255.0f - g0) * amt), b = (u32)(b0 + (255.0f - b0) * amt);
            u32& d = gFrame[py * gW + px];
            u32 rr = (r * a + ((d >> 16) & 255) * (255 - a)) / 255;
            u32 gg = (g * a + ((d >> 8) & 255) * (255 - a)) / 255;
            u32 bb = (b * a + (d & 255) * (255 - a)) / 255;
            d = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
        }
    }
}

// ---------- rules ----------
static int cropStage(const Cell& c) {
    float age = gTime - c.plantedAt;
    float grow = CROPS[c.crop - 1].grow;
    return std::clamp((int)(age / (grow / 3.0f)), 0, 3);
}

static bool solidAt(float x, float y) {
    int tx = (int)floorf(x), ty = (int)floorf(y);
    if (!inB(tx, ty)) return true;
    const Cell& c = cellAt(tx, ty);
    return c.ground == G_WATER || objSolid(c.obj, false);
}
static bool blockedAt(float x, float y) {
    const float R = 0.3f;
    return solidAt(x - R, y - R) || solidAt(x + R, y - R) || solidAt(x - R, y + R) || solidAt(x + R, y + R);
}
static bool playerBlockedAt(float x, float y) {
    const float R = 0.3f;
    auto b = [](float px, float py) {
        int tx = (int)floorf(px), ty = (int)floorf(py);
        if (!inB(tx, ty)) return true;
        if (!gInCave && !gJumping && cellAt(tx, ty).ground == G_CAVE) return true;   // cave holes must be jumped into
        return objSolid(cellAt(tx, ty).obj, true);
    };
    return b(x - R, y - R) || b(x + R, y - R) || b(x - R, y + R) || b(x + R, y + R);
}
static void moveBy(float dx, float dy) {
    if (!playerBlockedAt(gP.x + dx, gP.y)) gP.x += dx;
    if (!playerBlockedAt(gP.x, gP.y + dy)) gP.y += dy;
}
static bool isSwimming() {
    int tx = (int)floorf(gP.x), ty = (int)floorf(gP.y);
    return inB(tx, ty) && cellAt(tx, ty).ground == G_WATER;
}

static void setTitle(const std::string& msg) {
    if (!gHwnd) return;
    std::string t = "Horizons - " + msg + "   |   WASD move  Shift run  J jump  E use  Space attack  R place  Q eat  C craft  I inventory  Ctrl+S settings  F11 fullscreen  Esc menu";
    SetWindowTextA(gHwnd, t.c_str());
}

static std::vector<int> ownedItems() {
    std::vector<int> v;
    for (int i = 0; i < T_COUNT; i++) if (gCount[i] > 0) v.push_back(i);
    return v;
}

// Hotbar groups: 1-3 weapons and tools, 4-6 food, 7-8 materials, 9-0 healing.
static const int CAT_COUNT = 4;
static const int CAT_START[CAT_COUNT] = { 0, 3, 6, 8 };
static const int CAT_SIZE[CAT_COUNT] = { 3, 3, 2, 2 };
static const char* CAT_NAME[CAT_COUNT] = { "WEAPONS", "FOOD", "MATERIALS", "HEALING" };

static int itemCategory(int it) {
    if (it >= I_COUNT) return 1;                                   // cooked dishes are food
    if (it >= I_TIER && it <= I_TIER_END) return 0;                // metal tools and armour
    if (it == I_AXE || it == I_PICKAXE || it == I_HOE || it == I_ROD || it == I_WOOD_SWORD ||
        it == I_STONE_SWORD || it == I_MACE || it == I_SPEAR || it == I_DAGGER || it == I_VEST) return 0;
    if (it == I_FISH || it == I_BREAD || it == I_MEAT || (it >= I_PROD && it < I_COUNT)) return 1;
    if (it == I_MEDICINE) return 3;
    return 2;
}

static std::vector<int> categoryItems(int cat) {
    std::vector<int> v;
    for (int i = 0; i < T_COUNT; i++) if (gCount[i] > 0 && itemCategory(i) == cat) v.push_back(i);
    return v;
}

static int categoryPages(int cat) {
    int n = (int)categoryItems(cat).size();
    return std::max(1, (n + CAT_SIZE[cat] - 1) / CAT_SIZE[cat]);
}

static int maxPage() {
    int m = 1;
    for (int c = 0; c < CAT_COUNT; c++) m = std::max(m, categoryPages(c));
    return m;
}

static int slotCategory(int k) { return k < 3 ? 0 : k < 6 ? 1 : k < 8 ? 2 : 3; }

// The item in hotbar slot k (0-9), or -1 if the slot is empty on this page.
static int slotItem(int k) {
    int it = gHotbar[k];
    return (it >= 0 && gCount[it] > 0) ? it : -1;
}

// New items fill the first empty hotbar slot of their category, so the default layout is kept.
static void autoFillHotbar() {
    for (int i = 0; i < T_COUNT; i++) {
        if (gCount[i] <= 0) continue;
        bool placed = false;
        for (int k = 0; k < 10 && !placed; k++) if (gHotbar[k] == i) placed = true;
        if (placed) continue;
        int cat = itemCategory(i);
        for (int k = CAT_START[cat]; k < CAT_START[cat] + CAT_SIZE[cat]; k++)
            if (gHotbar[k] < 0) { gHotbar[k] = i; break; }
    }
}

static bool hasTierPiece(int piece) {
    for (int m = 0; m < 4; m++) if (gCount[I_TIER + m * 7 + piece] > 0) return true;
    return false;
}

// Mining level: base pickaxe 1, copper 2, iron 3, diamond 4, obsidian 5.
static int bestPickLevel() {
    int lv = gCount[I_PICKAXE] > 0 ? 1 : 0;
    for (int m = 0; m < 4; m++) if (gCount[I_TIER + m * 7] > 0) lv = std::max(lv, m + 2);
    return lv;
}

static void fixSelection() {
    std::vector<int> owned = ownedItems();
    if (owned.empty()) { gSelItem = -1; gPage = 0; return; }
    bool found = std::find(owned.begin(), owned.end(), gSelItem) != owned.end();
    if (!found) gSelItem = owned[0];
    gPage = std::clamp(gPage, 0, maxPage() - 1);
}

static std::vector<int> cookableCodes() {
    std::vector<int> v;
    for (int code = 1; code <= 13; code++) if (gCount[codeItem(code)] > 0) v.push_back(code);
    return v;
}

static void potRefund() {
    for (int k = 0; k < 3; k++) if (gPot[k]) { gCount[codeItem(gPot[k])]++; gPot[k] = 0; }
}

static void potAdd(int code) {
    if (gCount[codeItem(code)] <= 0) { setDialog("You have no " + codeName(code) + "."); return; }
    for (int k = 0; k < 3; k++) if (gPot[k] == 0) {
        gCount[codeItem(code)]--;
        gPot[k] = code;
        return;
    }
    setDialog("The pot is full (3 ingredients max). Press ENTER to cook.");
}

static void potCook() {
    std::vector<int> codes;
    for (int k = 0; k < 3; k++) if (gPot[k]) codes.push_back(gPot[k]);
    if (codes.empty()) { setDialog("Add ingredients first (1-9, 0)."); return; }
    int key = dishKey(codes);
    gCount[I_COUNT + key]++;
    addXP(4 + 2 * (int)codes.size());
    addPopup("Cooked " + dishName(key), RGBX(255, 200, 120));
    for (int k = 0; k < 3; k++) gPot[k] = 0;
}

// Friends and strangers. Up to 8 friends and 3 pets at once.
static const int MAX_FRIENDS = 8, MAX_PETS = 3, MAX_STRANGERS = 3;
struct Person { float x, y, dx, dy, wander, cool, hop; int gender, pal, friendship, isFriend, face; char name[12]; };
struct Pet { float x, y, dx, dy, wander, cool, hp, hop; int kind, face; char name[12]; };
static std::vector<Person> gPeople;
static std::vector<Pet> gPets;         // tamed, max 3
static std::vector<Pet> gStrays;       // wild animals that can be tamed
static float gStrangerT = 4.0f, gStrayT = 6.0f;
static const char* PET_KIND_NAME[3] = { "DOG", "CAT", "HAMSTER" };
static const char* PET_NAMES[12] = { "Biscuit", "Mochi", "Pebble", "Noodle", "Clover", "Waffles",
                                     "Tofu", "Pickle", "Ziggy", "Peanut", "Maple", "Bean" };
static const char* CHAT_LINES[5] = { "Nice weather today, isn't it?", "Want to chill by the water sometime?",
                                     "I'll fight beside you if monsters come!", "Got any seeds to spare?",
                                     "Let's grow something together." };
static const char* GREETS[3] = { "Hi! I'm ", "Hey there, I'm ", "Hello, the name's " };

static void makeName(char* out, int gender, uint32_t seed) {
    static const char* BOY_A[] = { "Ro", "Ka", "Mi", "Ta", "Lu", "Ben", "Jo", "Sam", "Eli", "Max", "Nor", "Kai", "Sol" };
    static const char* BOY_B[] = { "ko", "lan", "ton", "rik", "dy", "ven", "ro", "mir" };
    static const char* GIRL_A[] = { "Ma", "Li", "Se", "Ny", "Ev", "Ana", "Kia", "Zoe", "Ila", "Mae" };
    static const char* GIRL_B[] = { "ra", "bel", "ri", "lyn", "ta", "na", "sa" };
    std::string n = gender == 0 ? std::string(BOY_A[seed % 13]) + BOY_B[(seed / 13) % 8]
                                : std::string(GIRL_A[seed % 10]) + GIRL_B[(seed / 10) % 7];
    memset(out, 0, 12);
    strncpy(out, n.c_str(), 11);
}

static int maxFriends() { return gEmpireFounded ? 100 : MAX_FRIENDS; }
static int maxPets() { return gEmpireFounded ? 25 : 3; }

static void becomeFriend(Person& p) {
    int friends = 0;
    for (const Person& q : gPeople) if (q.isFriend) friends++;
    if (friends >= maxFriends()) { setDialog("Your friend limit is full."); return; }
    p.isFriend = 1;
    addPopup(std::string(p.name) + " is now your friend!", RGBX(255, 210, 110));
    addXP(5);
}

static void interactPerson(Person& p) {
    if (p.isFriend) {
        setDialog(std::string(p.name) + ": " + CHAT_LINES[(int)(rnd((int)gTime, 8, 101u) * 5) % 5]);
        return;
    }
    p.friendship++;
    if (p.friendship >= 3) { becomeFriend(p); return; }
    setDialog(std::string(GREETS[(int)(rnd((int)gTime, 9, 102u) * 3) % 3]) + p.name +
              ". Give me a gift (F) to get to know me.");
}

static void giftFacing() {
    float fx = gP.x + DXS[gP.dir] * 0.9f, fy = gP.y + DYS[gP.dir] * 0.9f;
    for (Person& p : gPeople) {
        float dx = p.x - fx, dy = p.y - fy;
        if (dx * dx + dy * dy >= 0.81f) continue;
        if (gSelItem < 0 || gCount[gSelItem] <= 0) { setDialog("Select an item in the hotbar to give as a gift."); return; }
        gCount[gSelItem]--;
        p.friendship += 2;
        setDialog(std::string(p.name) + ": Oh, thank you!");
        if (rnd((int)gTime, 10, 103u) < 0.6f) {
            int pick = (int)(rnd((int)gTime, 11, 104u) * 8) % 8;
            int item = pick == 0 ? I_WOOD : pick == 1 ? I_STONE : pick == 2 ? I_FLINT : pick == 3 ? I_CLAY : I_SEED + ((pick * 3) % NC);
            gCount[item]++;
            addPopup(std::string(p.name) + " gave you " + itemName(item), RGBX(170, 240, 140));
        }
        if (!p.isFriend && p.friendship >= 3) becomeFriend(p);
        return;
    }
    setTitle("No one in front of you to give a gift to");
}

static void assignPetName(char* out, uint32_t seed) {
    memset(out, 0, 12);
    strncpy(out, PET_NAMES[seed % 12], 11);
}

static int tamedCount() {
    int n = (int)gPets.size();
    for (const Beast& b : gBeasts) if (b.tame) n++;
    for (const Animal& a : gAnimals) if (a.tame) n++;
    return n;
}

static void tameStray(size_t i) {
    if (!isFood(gSelItem) || gCount[gSelItem] <= 0) {
        setDialog("Offer food (select it in the hotbar, then press E) to tame this animal.");
        return;
    }
    if ((int)gPets.size() >= maxPets()) { setDialog("Your pet limit is full."); return; }
    gCount[gSelItem]--;
    Pet p = gStrays[i];
    gStrays.erase(gStrays.begin() + i);
    memset(p.name, 0, sizeof p.name);
    strncpy(p.name, PET_NAMES[(int)(rnd((int)gTime, 12, 105u) * 12) % 12], 11);
    p.hp = 20.0f; p.cool = 0.0f; p.hop = 0.6f; p.wander = 0.0f;
    gPets.push_back(p);
    addXP(6);
    addPopup(std::string(p.name) + " the " + PET_KIND_NAME[p.kind] + " joined you!", RGBX(255, 220, 140));
}

static void playWithPet(Pet& p) {
    p.hop = 0.5f;
    addXP(1);
    setDialog(std::string(p.name) + " wags happily and bounces around you!");
}

static void sleepUntilMorning() {
    if (daylight() > 0.35f) { setDialog("You can only sleep at night."); return; }
    float f = gTime / DAY_LENGTH;
    f -= floorf(f);
    float delta = 0.27f - f;
    if (delta < 0) delta += 1.0f;
    gTime += delta * DAY_LENGTH;
    gHP = 100.0f;
    gP.energy = 100.0f;
    if (gSick != S_NONE && rnd((int)gTime, 31, 41u) < 0.5f) { gSick = S_NONE; addPopup("You feel rested and recovered", RGBX(170, 255, 200)); }
    addPopup("You slept until morning", RGBX(170, 200, 255));
    setTitle("Good morning!");
}

// Furnaces smelt copper and iron ore. Wood is the fuel: each piece gives four smelts.
struct Furnace { int cell; int inItem; int inN; int outItem; int outN; int fuel; float prog; };
static std::vector<Furnace> gFurnaces;
static bool gFurnOpen = false;
static int gFurnCell = -1;

static int smeltResult(int item) { return item == I_CU_ORE ? I_CU_INGOT : item == I_FE_ORE ? I_FE_INGOT : -1; }

static void addFurnace(int cell) {
    Furnace f;
    memset(&f, 0, sizeof f);
    f.cell = cell; f.inItem = -1; f.outItem = -1;
    gFurnaces.push_back(f);
}

static void removeFurnace(int cell) {
    gFurnaces.erase(std::remove_if(gFurnaces.begin(), gFurnaces.end(),
                    [cell](const Furnace& f) { return f.cell == cell; }), gFurnaces.end());
}

static Furnace* furnaceAt(int cell) {
    for (Furnace& f : gFurnaces) if (f.cell == cell) return &f;
    return nullptr;
}

static void updateFurnaces(float dt) {
    for (Furnace& f : gFurnaces) {
        int res = f.inN > 0 ? smeltResult(f.inItem) : -1;
        if (res < 0 || f.fuel <= 0 || (f.outItem >= 0 && f.outItem != res)) { f.prog = 0.0f; continue; }
        f.prog += dt;
        if (f.prog >= 3.0f) {
            f.prog = 0.0f;
            f.inN--; f.fuel--;
            if (f.inN == 0) f.inItem = -1;
            f.outItem = res; f.outN++;
        }
    }
}

static void furnaceDeposit(Furnace* f) {
    if (gSelItem < 0 || gCount[gSelItem] <= 0) { setDialog("Select an ore in the hotbar to smelt it."); return; }
    if (smeltResult(gSelItem) < 0) { setDialog("Only copper and iron ore can be smelted."); return; }
    if (f->inN > 0 && f->inItem != gSelItem) { setDialog("The furnace already holds a different ore."); return; }
    f->inItem = gSelItem;
    f->inN += gCount[gSelItem];
    gCount[gSelItem] = 0;
    setDialog("Ore loaded.");
}

static void furnaceFuel(Furnace* f) {
    if (gCount[I_WOOD] <= 0) { setDialog("You need wood for fuel."); return; }
    f->fuel += gCount[I_WOOD] * 4;
    gCount[I_WOOD] = 0;
    setDialog("Fuel added.");
}

static void furnaceCollect(Furnace* f) {
    if (f->outN <= 0) { setDialog("Nothing to collect yet."); return; }
    gCount[f->outItem] += f->outN;
    addXP(4);
    f->outN = 0;
    f->outItem = -1;
    setDialog("Collected.");
}

static void addChest(int cell) {
    ChestData cd;
    memset(&cd, 0, sizeof cd);
    cd.cell = cell;
    gChests.push_back(cd);
}

static void removeChest(int cell) {
    gChests.erase(std::remove_if(gChests.begin(), gChests.end(),
                  [cell](const ChestData& q) { return q.cell == cell; }), gChests.end());
}

static ChestData* chestAt(int cell) {
    for (ChestData& c : gChests) if (c.cell == cell) return &c;
    return nullptr;
}

static void chestDeposit(ChestData* c) {
    if (gSelItem < 0 || gCount[gSelItem] <= 0) { setDialog("Select an item in the hotbar to store it."); return; }
    for (int k = 0; k < 10; k++)
        if (c->n[k] > 0 && c->item[k] == gSelItem) { c->n[k] += gCount[gSelItem]; gCount[gSelItem] = 0; setDialog("Stored."); return; }
    for (int k = 0; k < 10; k++)
        if (c->n[k] == 0) { c->item[k] = gSelItem; c->n[k] = gCount[gSelItem]; gCount[gSelItem] = 0; setDialog("Stored."); return; }
    setDialog("The chest is full.");
}

static void chestTake(ChestData* c, int k) {
    if (c->n[k] <= 0) return;
    gCount[c->item[k]] += c->n[k];
    c->n[k] = 0;
}

static void useFacing() {
    float fx = gP.x + DXS[gP.dir] * 0.9f, fy = gP.y + DYS[gP.dir] * 0.9f;

    if (gRide >= 0) { gRide = -1; setTitle("You dismount"); return; }
    for (Animal& a : gAnimals) {
        float dx = a.x - fx, dy = a.y - fy;
        if (dx * dx + dy * dy >= 0.81f) continue;
        if (a.tame) { setTitle("That rabbit already follows you"); return; }
        if (!isFood(gSelItem) || gCount[gSelItem] <= 0) { setTitle("Select food in the hotbar to befriend this rabbit"); return; }
        if (tamedCount() >= maxPets()) { setDialog("Your pet limit is full."); return; }
        gCount[gSelItem]--;
        a.tame = 1;
        assignPetName(a.name, (uint32_t)(gTime * 1000.0f) + (uint32_t)gAnimals.size());
        addXP(5);
        addPopup("A rabbit is now your friend!", RGBX(255, 220, 140));
        return;
    }
    for (size_t i = 0; i < gBeasts.size(); i++) {
        Beast& b = gBeasts[i];
        float dx = b.x - fx, dy = b.y - fy;
        if (dx * dx + dy * dy >= 0.81f) continue;
        const char* nm = b.kind == 0 ? "cow" : b.kind == 1 ? "sheep" : b.kind == 2 ? "horse" : "camel";
        if (!b.tame && isFood(gSelItem) && gCount[gSelItem] > 0) {
            if (tamedCount() >= maxPets()) { setDialog("Your pet limit is full."); return; }
            gCount[gSelItem]--;
            b.tame = 1;
            assignPetName(b.name, (uint32_t)(gTime * 1000.0f) + (uint32_t)gBeasts.size());
            addXP(5);
            addPopup(std::string("A ") + nm + " is now your friend!", RGBX(255, 220, 140));
            return;
        }
        if (b.kind >= 2) {
            gRide = (int)i;
            setTitle(std::string("You mount the ") + nm + "  (E to dismount)");
            return;
        }
        setTitle(b.tame ? std::string("Your ") + nm + " follows you" : std::string("Select food in the hotbar to befriend this ") + nm);
        return;
    }

    // Friends, strangers, strays and pets.
    for (Person& p : gPeople) {
        float dx = p.x - fx, dy = p.y - fy;
        if (dx * dx + dy * dy < 0.9f * 0.9f) { interactPerson(p); return; }
    }
    for (size_t i = 0; i < gStrays.size(); i++) {
        float dx = gStrays[i].x - fx, dy = gStrays[i].y - fy;
        if (dx * dx + dy * dy < 0.9f * 0.9f) { tameStray(i); return; }
    }
    for (Pet& p : gPets) {
        float dx = p.x - fx, dy = p.y - fy;
        if (dx * dx + dy * dy < 0.9f * 0.9f) { playWithPet(p); return; }
    }

    // Talk to villagers first.
    for (const Npc& n : gNpcs) {
        float dx = n.x - fx, dy = n.y - fy;
        if (dx * dx + dy * dy < 0.9f * 0.9f) { talkTo(n); return; }
    }

    int tx = (int)floorf(fx), ty = (int)floorf(fy);
    if (!inB(tx, ty)) return;
    Cell& c = cellAt(tx, ty);

    if (c.ground == G_WATER) {
        if (gCount[I_ROD] <= 0) { setTitle("You need a Fishing Rod (press C to craft)"); return; }
        float roll = rnd(tx, ty, (uint32_t)(gTime * 1000.0f));
        gP.energy = std::max(0.0f, gP.energy - 2.0f);
        if (roll > 0.45f) { gCount[I_FISH]++; addXP(8); addPopup("+1 Raw Fish", RGBX(140, 200, 255)); setTitle("Caught a fish!"); }
        else setTitle("Nothing bit...");
        return;
    }
    if (c.obj == O_CAMPFIRE) {
        if (down(VK_SHIFT)) {
            c.obj = O_NONE;
            gCount[I_CAMPFIRE]++;
            setTitle("Picked up the campfire");
        } else {
            gCookOpen = true;
            gCraftOpen = false;
            gCookPage = 0;
            gCampX = tx; gCampY = ty;
            setTitle("Cooking pot open: add ingredients with 1-9 and 0");
        }
        return;
    }
    if (c.obj >= O_ORE_COPPER && c.obj <= O_ORE_OBSIDIAN) {
        int m = c.obj - O_ORE_COPPER;
        if (bestPickLevel() < m + 1) { setTitle(std::string("You need a better pickaxe to mine ") + METAL_NAME[m] + " ore"); return; }
        int got = (m <= 1) ? 1 + (int)(rnd(tx, ty, 41u) * 2) : 1;
        int item = (m == 0) ? I_CU_ORE : (m == 1) ? I_FE_ORE : (m == 2) ? I_DIA : I_OBS;
        c.obj = O_NONE;
        gCount[item] += got;
        playSfx(SFX_PICK, tx + 0.5f, ty + 0.5f, 0.7f);
        addXP(12);
        addPopup("+" + std::to_string(got) + " " + itemName(item), METAL_COL[m]);
        setTitle(std::string("Mined ") + METAL_NAME[m]);
        return;
    }
    if (c.obj == O_FURNACE) {
        int cell = ty * MAP_W + tx;
        if (down(VK_SHIFT)) {
            if (Furnace* fu = furnaceAt(cell)) {
                if (fu->inN > 0) gPendingDrops.push_back({ tx + 0.5f, ty + 0.5f, fu->inItem, fu->inN });
                if (fu->outN > 0) gPendingDrops.push_back({ tx + 0.5f, ty + 0.5f, fu->outItem, fu->outN });
            }
            removeFurnace(cell);
            c.obj = O_NONE;
            gCount[I_FURNACE]++;
            setTitle("Picked up the furnace");
        } else {
            gFurnOpen = true;
            gFurnCell = cell;
            gChestOpen = false; gCraftOpen = false; gCookOpen = false;
            setTitle("Furnace: T ore, G wood fuel, Y collect, C close");
        }
        return;
    }
    if (c.obj == O_CHEST) {
        int cell = ty * MAP_W + tx;
        if (down(VK_SHIFT)) {
            if (ChestData* cd = chestAt(cell))
                for (int k = 0; k < 10; k++)
                    if (cd->n[k] > 0) gPendingDrops.push_back({ tx + 0.5f, ty + 0.5f, cd->item[k], cd->n[k] });
            removeChest(cell);
            c.obj = O_NONE;
            gCount[I_CHEST]++;
            setTitle("Picked up the chest");
        } else {
            gChestOpen = true;
            gChestCell = cell;
            gCraftOpen = false;
            gCookOpen = false;
            setTitle("Chest open: 1-0 take, T store selected item, C close");
        }
        return;
    }
    if (c.obj == O_DOOR_CLOSED || c.obj == O_DOOR_OPEN) {
        if (down(VK_SHIFT)) { c.obj = O_NONE; gCount[I_DOOR]++; rebuildDoors(); setTitle("Picked up the door"); }
        else setTitle("Doors open when you, a friend or a pet walks up to them");
        return;
    }
    if (c.obj == O_BED) {
        if (down(VK_SHIFT)) { c.obj = O_NONE; gCount[I_BED]++; setTitle("Picked up the bed"); }
        else sleepUntilMorning();
        return;
    }
    if (c.obj == O_TORCH) { c.obj = O_NONE; gCount[I_TORCH]++; setTitle("Picked up a torch"); return; }
    if (c.obj == O_PLANK) { c.obj = O_NONE; gCount[I_PLANK]++; addPopup("+1 Plank", RGBX(230, 190, 130)); return; }
    if (c.obj == O_BRICK) { c.obj = O_NONE; gCount[I_BRICK]++; addPopup("+1 Stone Brick", RGBX(230, 140, 110)); return; }
    if (c.obj == O_TREE) {
        c.obj = O_NONE;
        int got = (gCount[I_AXE] > 0 || hasTierPiece(1)) ? 3 + (int)(rnd(tx, ty, 9u) * 2) : 1;
        gCount[I_WOOD] += got;
        addXP(5);
        addPopup("+" + std::to_string(got) + " Wood", RGBX(200, 150, 90));
        setTitle("Chopped a tree");
        return;
    }
    if (c.obj == O_ROCK) {
        c.obj = O_NONE;
        int got = (gCount[I_PICKAXE] > 0 || hasTierPiece(0)) ? 3 + (int)(rnd(tx, ty, 10u) * 2) : 1;
        gCount[I_STONE] += got;
        addXP(5);
        addPopup("+" + std::to_string(got) + " Stone", RGBX(190, 190, 200));
        setTitle("Mined a rock");
        if (rnd(tx, ty, 11u) < 0.35f) { gCount[I_FLINT]++; addPopup("+1 Flint", RGBX(200, 200, 210)); }
        return;
    }
    if (c.crop) {
        int ci = c.crop - 1;
        if (cropStage(c) >= 3) {
            c.crop = 0;
            int prod = 2 + (int)(rnd(tx, ty, 4u) * 3);
            int seeds = 1 + (int)(rnd(tx, ty, 5u) * 2);
            gCount[I_PROD + ci] += prod;
            gCount[I_SEED + ci] += seeds;
            addXP(10);
            addPopup("+" + std::to_string(prod) + " " + CROPS[ci].name, CROPS[ci].ripe);
            setTitle(std::string("Harvested ") + CROPS[ci].name);
        } else {
            setTitle(std::string(CROPS[ci].name) + " is still growing...");
        }
        return;
    }
    if (c.ground == G_TILLED) {
        if (gSelItem >= I_SEED && gSelItem < I_PROD && gCount[gSelItem] > 0) {
            int ci = gSelItem - I_SEED;
            c.crop = (uint8_t)(ci + 1);
            c.plantedAt = gTime;
            gCount[gSelItem]--;
            addXP(2);
            setTitle(std::string("Planted ") + CROPS[ci].name);
        } else {
            setTitle("Select a seed in the hotbar (1-0) to plant");
        }
        return;
    }
    if (c.ground == G_GRASS || c.ground == G_DIRT || c.ground == G_FOREST || c.ground == G_SWAMP ||
        c.ground == G_JUNGLE || c.ground == G_MEADOW || c.ground == G_BADLANDS) {
        if (c.ground == G_BADLANDS && rnd(tx, ty, 21u) < 0.5f) {
            int n = 1 + (int)(rnd(tx, ty, 22u) * 2);
            gCount[I_CLAY] += n;
            addPopup("+" + std::to_string(n) + " Clay", RGBX(200, 140, 110));
            setTitle("Dug up clay");
            return;
        }
        if (gCount[I_HOE] > 0) {
            c.ground = G_TILLED;
            addXP(1);
            setTitle("Tilled the soil");
        } else {
            float roll = rnd(tx, ty, (uint32_t)(gTime * 1000.0f) + 77u);
            if (roll < 0.25f) {
                int ci = (int)(rnd(tx, ty, (uint32_t)(gTime * 37.0f)) * NC) % NC;
                gCount[I_SEED + ci]++;
                addPopup(std::string("Found a ") + CROPS[ci].name + " seed!", RGBX(170, 240, 140));
                setTitle(std::string("Found a ") + CROPS[ci].name + " seed!");
            } else {
                setTitle("Nothing here... (craft a Hoe to till)");
            }
        }
    }
}

static void placeFacing() {
    int item = gSelItem;
    if (item < 0 || item >= I_COUNT || gCount[item] <= 0 || !isPlaceable(item)) { setTitle("Select a block (Stone, Plank, Brick, Campfire) to place"); return; }
    int tx = (int)floorf(gP.x + DXS[gP.dir] * 0.9f);
    int ty = (int)floorf(gP.y + DYS[gP.dir] * 0.9f);
    if (!inB(tx, ty)) return;
    Cell& c = cellAt(tx, ty);
    if (c.ground == G_WATER || c.obj != O_NONE || c.crop) { setTitle("Can't place a block there"); return; }
    if (tx == (int)floorf(gP.x) && ty == (int)floorf(gP.y)) { setTitle("Move away before placing"); return; }
    if (item == I_CAMPFIRE) c.obj = O_CAMPFIRE;
    else if (item == I_BED) c.obj = O_BED;
    else if (item == I_TORCH) c.obj = O_TORCH;
    else if (item == I_DOOR) c.obj = O_DOOR_CLOSED;
    else if (item == I_CHEST) { c.obj = O_CHEST; addChest(ty * MAP_W + tx); }
    else if (item == I_FURNACE) { c.obj = O_FURNACE; addFurnace(ty * MAP_W + tx); }
    else c.obj = (item == I_STONE) ? O_ROCK : (item == I_PLANK) ? O_PLANK : O_BRICK;
    gCount[item]--;
    rebuildDoors();
    setTitle("Placed " + itemName(item));
}

struct Tornado { float x, y, dx, dy, life; };
static std::vector<Tornado> gTornados;

// Livestock and mounts: 0 cow, 1 sheep, 2 horse, 3 camel. Horses and camels can be ridden.
static float gBeastSpawnT = 0.0f;
static const int MAX_BEASTS = 14;
static const float BEAST_HP[4] = { 14.0f, 12.0f, 26.0f, 30.0f };

static void eraseBeast(size_t i) {
    if (gRide == (int)i) gRide = -1;
    else if (gRide > (int)i) gRide--;
    gBeasts.erase(gBeasts.begin() + i);
}

static bool hitBeasts(float fx, float fy, float reach, int dmg) {
    bool hit = false;
    for (size_t i = 0; i < gBeasts.size();) {
        Beast& b = gBeasts[i];
        float dx = b.x - fx, dy = b.y - fy;
        if (dx * dx + dy * dy < reach * reach) {
            hit = true;
            b.hp -= dmg;
            playSfx(SFX_HIT, b.x, b.y, 0.6f);
            if (b.hp <= 0) {
                int meat = b.kind == 0 ? 3 : b.kind == 1 ? 2 : 0;
                if (meat) gCount[I_MEAT] += meat;
                if (b.kind >= 2) gCount[I_HIDE] += 2;
                addXP(6);
                addPopup("+ " + std::string(b.kind == 0 ? "beef" : b.kind == 1 ? "mutton" : b.kind == 2 ? "horse hide" : "camel hide"), RGBX(230, 160, 130));
                eraseBeast(i);
                continue;
            }
        }
        ++i;
    }
    return hit;
}

static float gAnimalSpawnT = 0.0f;
static u32 gRabbitSpr[2][TILE * TILE];

static bool hitAnimals(float fx, float fy, float reach, int dmg) {
    bool hit = false;
    for (size_t i = 0; i < gAnimals.size();) {
        Animal& a = gAnimals[i];
        float dx = a.x - fx, dy = a.y - fy;
        if (dx * dx + dy * dy < reach * reach) {
            hit = true;
            a.hp -= dmg;
            if (a.hp <= 0) {
                gCount[I_MEAT]++;
                addXP(4);
                addPopup("+1 Raw Meat", RGBX(230, 120, 120));
                if (rnd((int)(a.x * 10), (int)(a.y * 10), 44u) < 0.25f) catchSick(S_RABBIT);
                gAnimals.erase(gAnimals.begin() + i);
                continue;
            }
        }
        ++i;
    }
    return hit;
}

static bool hitStrays(float fx, float fy, float reach, int dmg) {
    bool hit = false;
    for (size_t i = 0; i < gStrays.size();) {
        Pet& q = gStrays[i];
        float dx = q.x - fx, dy = q.y - fy;
        if (dx * dx + dy * dy < reach * reach) {
            hit = true;
            q.hp -= dmg;
            if (q.hp <= 0) {
                gCount[I_MEAT]++;
                addXP(3);
                addPopup("+1 Raw Meat", RGBX(230, 120, 120));
                gStrays.erase(gStrays.begin() + i);
                continue;
            }
        }
        ++i;
    }
    return hit;
}

static bool hitTamePets(float fx, float fy, float reach, int dmg) {
    bool hit = false;
    for (size_t i = 0; i < gPets.size();) {
        Pet& q = gPets[i];
        float dx = q.x - fx, dy = q.y - fy;
        if (dx * dx + dy * dy < reach * reach) {
            hit = true;
            q.hp -= dmg;
            if (q.hp <= 0) {
                addPopup(std::string(q.name) + " was killed", RGBX(255, 120, 120));
                gPets.erase(gPets.begin() + i);
                continue;
            }
        }
        ++i;
    }
    return hit;
}

static void attackFacing() {
    if (gAttackCool > 0) return;
    const WeaponDef* w = bestWeapon();
    int dmg = w ? w->dmg : 3;
    float reach = w ? w->reach : 1.2f;
    gAttackCool = w ? w->cool : 0.35f;
    gSwingT = 0.2f;
    float fx = gP.x + DXS[gP.dir] * 0.9f, fy = gP.y + DYS[gP.dir] * 0.9f;
    bool hit = false;
    for (Monster& m : gMonsters) {
        float dx = m.x - fx, dy = m.y - fy;
        if (dx * dx + dy * dy < reach * reach) {
            m.hp -= dmg;
            m.flash = 0.2f;
            m.x += DXS[gP.dir] * 0.35f;
            m.y += DYS[gP.dir] * 0.35f;
            hit = true;
            addPopup("-" + std::to_string(dmg), RGBX(255, 120, 100));
        }
    }
    if (hitAnimals(fx, fy, reach, dmg)) hit = true;
    if (hitBeasts(fx, fy, reach, dmg)) hit = true;
    if (hitStrays(fx, fy, reach, dmg)) hit = true;
    if (hitTamePets(fx, fy, reach, dmg)) hit = true;
    if (hit) playSfx(SFX_HIT, fx, fy, 0.7f);
    if (!hit) setTitle("Swing! (no target)");
}

static void eatSelected() {
    if (gSelItem < 0 || gCount[gSelItem] <= 0) { setTitle("Select a food item first"); return; }
    if (gSelItem == I_MEDICINE) {
        gCount[I_MEDICINE]--;
        gHP = std::min(100.0f, gHP + 15.0f);
        if (gSick != S_NONE) { addPopup(std::string("Medicine cured your ") + SICK_NAMES[gSick], RGBX(170, 255, 200)); gSick = S_NONE; }
        else addPopup("Medicine: +15 HP", RGBX(170, 255, 200));
        setTitle("Took medicine");
        return;
    }
    if (!isFood(gSelItem)) { setTitle("You can't eat " + itemName(gSelItem)); return; }
    float energy, heal;
    if (gSelItem >= I_COUNT) {
        int c[3]; dishCodes(gSelItem - I_COUNT, c);
        int n = (c[0] != 0) + (c[1] != 0) + (c[2] != 0);
        energy = 20.0f + 10.0f * n;
        heal = 6.0f + 6.0f * n;
    } else {
        energy = gSelItem == I_BREAD ? 60.0f : gSelItem == I_FISH ? 40.0f : 25.0f;
        heal = gSelItem == I_BREAD ? 10.0f : 4.0f;
    }
    gCount[gSelItem]--;
    gP.energy = std::min(100.0f, gP.energy + energy);
    gHunger = std::min(100.0f, gHunger + energy);
    gHP = std::min(100.0f, gHP + heal);
    addPopup("+" + std::to_string((int)heal) + " HP  +" + std::to_string((int)energy) + " ENERGY", RGBX(120, 255, 150));
    setTitle("Ate " + itemName(gSelItem));
}

// ---------- monsters update ----------
// ---------- navigation: creatures swim, and route around obstacles ----------
static const int NAV_R = 20;                 // search radius in tiles
static const int NAV_N = 2 * NAV_R + 1;

static bool walkSolidTile(int tx, int ty, bool swim) {
    if (!inB(tx, ty)) return true;
    const Cell& c = cellAt(tx, ty);
    if (objSolid(c.obj, gOpenDoors)) return true;
    if (c.ground == G_WATER && !swim) return true;
    return false;
}

static bool walkBlocked(float x, float y, bool swim) {
    const float R = 0.3f;
    return walkSolidTile((int)floorf(x - R), (int)floorf(y - R), swim) ||
           walkSolidTile((int)floorf(x + R), (int)floorf(y - R), swim) ||
           walkSolidTile((int)floorf(x - R), (int)floorf(y + R), swim) ||
           walkSolidTile((int)floorf(x + R), (int)floorf(y + R), swim);
}

// Moves a creature, sliding along walls, and slows it down in water.
static void moveWalker(float& x, float& y, float mvx, float mvy, float dt, bool swim) {
    float sp = 1.0f;
    if (swim) {
        int tx = (int)floorf(x), ty = (int)floorf(y);
        if (inB(tx, ty) && cellAt(tx, ty).ground == G_WATER) sp = 0.6f;
    }
    float nx = x + mvx * dt * sp, ny = y + mvy * dt * sp;
    if (!walkBlocked(nx, y, swim)) x = nx;
    if (!walkBlocked(x, ny, swim)) y = ny;
}

// Breadth-first search from (sx,sy) toward (gx,gy). Returns the centre of the next tile on the way.
// If the goal is unreachable it heads for the reachable tile closest to the goal.
static bool navNext(float sx, float sy, float gx, float gy, bool swim, float& nxo, float& nyo) {
    int x0 = (int)floorf(sx), y0 = (int)floorf(sy);
    int gxi = (int)floorf(gx), gyi = (int)floorf(gy);
    if (abs(gxi - x0) > NAV_R - 1 || abs(gyi - y0) > NAV_R - 1) return false;
    if (x0 == gxi && y0 == gyi) return false;

    static int prev[NAV_N * NAV_N];
    static int queue[NAV_N * NAV_N];
    static unsigned char seen[NAV_N * NAV_N];
    memset(seen, 0, sizeof seen);
    const int startIdx = NAV_R * NAV_N + NAV_R;
    const int goalIdx = (gyi - y0 + NAV_R) * NAV_N + (gxi - x0 + NAV_R);
    int head = 0, tail = 0;
    queue[tail++] = startIdx;
    seen[startIdx] = 1;
    prev[startIdx] = -1;
    int best = startIdx;
    int bestD = (gxi - x0) * (gxi - x0) + (gyi - y0) * (gyi - y0);
    static const int DX4[4] = { 1, -1, 0, 0 };
    static const int DY4[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int cur = queue[head++];
        if (cur == goalIdx) break;
        int cx = cur % NAV_N, cy = cur / NAV_N;
        int wx = cx - NAV_R + x0, wy = cy - NAV_R + y0;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX4[d], ny = cy + DY4[d];
            if (nx < 0 || ny < 0 || nx >= NAV_N || ny >= NAV_N) continue;
            int idx = ny * NAV_N + nx;
            if (seen[idx]) continue;
            int tx = wx + DX4[d], ty = wy + DY4[d];
            if (walkSolidTile(tx, ty, swim) && idx != goalIdx) continue;
            seen[idx] = 1;
            prev[idx] = cur;
            queue[tail++] = idx;
            int ddx = tx - gxi, ddy = ty - gyi;
            int dd = ddx * ddx + ddy * ddy;
            if (dd < bestD) { bestD = dd; best = idx; }
        }
    }
    int target = seen[goalIdx] ? goalIdx : best;
    if (target == startIdx) return false;
    int cur = target;
    while (prev[cur] != startIdx) cur = prev[cur];
    nxo = (cur % NAV_N - NAV_R + x0) + 0.5f;
    nyo = (cur / NAV_N - NAV_R + y0) + 0.5f;
    return true;
}

// Desired velocity toward (gx,gy), following the search path when one exists.
static void steerToward(float x, float y, float gx, float gy, float spd, bool swim, float& mvx, float& mvy) {
    float dx = gx - x, dy = gy - y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.6f) { mvx = 0; mvy = 0; return; }
    float tx, ty;
    if (navNext(x, y, gx, gy, swim, tx, ty)) {
        dx = tx - x; dy = ty - y;
        d = sqrtf(dx * dx + dy * dy);
        if (d < 0.01f) { mvx = 0; mvy = 0; return; }
    }
    mvx = dx / d * spd;
    mvy = dy / d * spd;
}

static void raiderLoot(float x, float y) {
    gPendingDrops.push_back({ x, y, I_WOOD, 6 });
    gPendingDrops.push_back({ x, y, I_STONE, 6 });
    gPendingDrops.push_back({ x, y, I_PLANK, 4 });
    gPendingDrops.push_back({ x, y, I_BRICK, 2 });
    gPendingDrops.push_back({ x, y, I_FLINT, 2 });
    gPendingDrops.push_back({ x, y, I_HIDE, 2 });
    gPendingDrops.push_back({ x, y, I_BREAD, 2 });
    gPendingDrops.push_back({ x, y, I_MEDICINE, 1 });
    int ci = (int)(rnd((int)(x * 10), (int)(y * 10), 31u) * NC) % NC;
    int cj = (int)(rnd((int)(x * 10), (int)(y * 10), 32u) * NC) % NC;
    gPendingDrops.push_back({ x, y, I_SEED + ci, 3 });
    gPendingDrops.push_back({ x, y, I_PROD + cj, 3 });
    addPopup("The raiders dropped their loot!", RGBX(255, 220, 140));
}

// Armour: each metal piece cuts damage; the best metal owned counts for each slot.
static float armorFactor() {
    float red = gCount[I_VEST] > 0 ? 0.10f : 0.0f;
    for (int p = 3; p <= 6; p++)
        for (int m = 3; m >= 0; m--)
            if (gCount[I_TIER + m * 7 + p] > 0) { red += 0.05f + 0.03f * m; break; }
    return 1.0f - std::min(red, 0.8f);
}

static void removeRaiders(int idx) {
    int home = -1000 - idx;
    gMonsters.erase(std::remove_if(gMonsters.begin(), gMonsters.end(),
                    [home](const Monster& m) { return m.home == home; }), gMonsters.end());
}

// Balance: bigger empires field more defenders, and each defender is tougher.
static int raidDefenders(const Empire& e) { return std::clamp(6 + e.friends / 4, 6, 24); }
static float raidDefenderHP(const Empire& e) { return std::min(90.0f, 30.0f + e.food * 0.03f); }

static void startRaid(int idx) {
    const Empire& e = gEmpires[idx];
    int count = raidDefenders(e);
    for (int k = 0; k < count; k++) {
        Monster m;
        memset(&m, 0, sizeof m);
        m.x = e.x + 2.0f + (k % 4) * 1.2f;
        m.y = e.y + 2.0f + (k / 4) * 1.5f;
        m.type = 2; m.hp = raidDefenderHP(e); m.wander = 1.0f; m.dir = 2;
        m.home = -1000 - idx;
        gMonsters.push_back(m);
    }
    gRaidActive = true;
    gRaidIdx = idx;
    gRaidT = 240.0f;                 // four minutes to clear the defenders
    addPopup("Raid begun! Defeat the empire's defenders.", RGBX(255, 160, 120));
}

// Victory: the defeated empire yields 30% of its friends, pets and food.
static void raidWin() {
    if (gRaidIdx < 0 || gRaidIdx >= (int)gEmpires.size()) { gRaidActive = false; return; }
    Empire& e = gEmpires[gRaidIdx];
    int f = (int)(e.friends * 0.3f), p = (int)(e.pets * 0.3f), fd = (int)(e.food * 0.3f);
    e.state = 1;
    int gotF = 0, gotP = 0;
    for (int k = 0; k < f; k++) {
        int have = 0;
        for (const Person& q : gPeople) if (q.isFriend) have++;
        if (have >= maxFriends()) break;
        Person q;
        memset(&q, 0, sizeof q);
        q.x = gP.x + 1.0f; q.y = gP.y + 1.0f;
        q.gender = k % 2; q.pal = k % 3; q.face = 2; q.isFriend = 1; q.friendship = 5;
        makeName(q.name, q.gender, hash2(k, (int)gTime, 61u));
        gPeople.push_back(q);
        gotF++;
    }
    for (int k = 0; k < p; k++) {
        if ((int)gPets.size() >= maxPets()) break;
        Pet q;
        memset(&q, 0, sizeof q);
        q.x = gP.x - 1.0f; q.y = gP.y; q.hp = 20.0f; q.kind = k % 3;
        strncpy(q.name, PET_NAMES[k % 12], 11);
        gPets.push_back(q);
        gotP++;
    }
    gCount[I_BREAD] += fd;
    removeRaiders(gRaidIdx);
    gRaidActive = false;
    addPopup("VICTORY! +" + std::to_string(gotF) + " friends, +" + std::to_string(gotP) +
             " pets, +" + std::to_string(fd) + " food", RGBX(120, 255, 160));
}

// Defeat: lose 15% of your friends, pets and food.
static void raidLose() {
    int nF = 0;
    for (const Person& q : gPeople) if (q.isFriend) nF++;
    int lossF = (int)(nF * 0.15f + 0.5f);
    for (size_t i = gPeople.size(); i-- > 0 && lossF > 0;)
        if (gPeople[i].isFriend) { gPeople.erase(gPeople.begin() + i); lossF--; }
    int lossP = (int)(gPets.size() * 0.15f + 0.5f);
    for (int k = 0; k < lossP && !gPets.empty(); k++) gPets.pop_back();
    for (int i = 0; i < T_COUNT; i++)
        if (isFood(i) && i != I_MEDICINE && gCount[i] > 0) gCount[i] -= (int)(gCount[i] * 0.15f + 0.5f);
    removeRaiders(gRaidIdx);
    gRaidActive = false;
    addPopup("DEFEATED! Lost 15% of your pets, friends and food", RGBX(255, 120, 120));
}

// Raiders spawn when the player comes within 5 tiles of a homestead.
static void updateHomesteads() {
    for (size_t h = 0; h < gHomesteads.size(); h++) {
        Homestead& H = gHomesteads[h];
        if (H.cleared || H.activated) continue;
        float cx = H.x + 3.5f, cy = H.y + 2.5f;
        float dx = gP.x - cx, dy = gP.y - cy;
        if (dx * dx + dy * dy < 25.0f) {
            H.activated = 1;
            for (int k = 0; k < 3; k++) {
                Monster m;
                memset(&m, 0, sizeof m);
                m.x = H.x + 2.5f + k * 1.0f; m.y = H.y + 2.5f;
                m.type = 2; m.hp = MON_MAXHP[2]; m.wander = 1.0f; m.dir = 2;
                m.home = (int)h;
                gMonsters.push_back(m);
            }
            addPopup("Raiders attack from a homestead!", RGBX(255, 120, 100));
        }
    }
}

static void updateMonsters(float dt) {
    gOpenDoors = false;
    float dl = daylight();
    bool night = dl < 0.25f;
    gSpawnT -= dt;
    if (night && gSpawnT <= 0 && (int)gMonsters.size() < MAX_MON) {
        gSpawnT = 1.5f;
        uint32_t s = (uint32_t)(gTime * 1000.0f) ^ (uint32_t)(gMonsters.size() * 2654435761u);
        float ang = rnd((int)s, 1, 5u) * 2.0f * PI;
        float dist = 9.0f + rnd((int)s, 2, 6u) * 6.0f;
        float mx = gP.x + cosf(ang) * dist, my = gP.y + sinf(ang) * dist;
        int tx = (int)floorf(mx), ty = (int)floorf(my);
        if (inB(tx, ty) && !solidAt(mx, my) && cellAt(tx, ty).ground != G_WATER) {
            Monster m;
            m.x = mx; m.y = my;
            m.type = rnd((int)s, 3, 7u) < 0.6f ? 0 : 1;
            m.hp = MON_MAXHP[m.type];
            m.cool = 0; m.flash = 0; m.wander = 1.0f; m.dx = 0; m.dy = 0; m.dir = 2; m.home = -1; m.byCompanion = 0;
            gMonsters.push_back(m);
        }
    }

    for (size_t i = 0; i < gMonsters.size();) {
        Monster& m = gMonsters[i];
        float ddx = gP.x - m.x, ddy = gP.y - m.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        float mvx = 0, mvy = 0;
        float spd = MON_SPEED[m.type];
        if (d < 8.0f && d > 0.01f) {
            steerToward(m.x, m.y, gP.x, gP.y, spd, true, mvx, mvy);
        } else {
            m.wander -= dt;
            if (m.wander <= 0) {
                float a = rnd((int)(m.x * 100), (int)(m.y * 100), (uint32_t)(gTime * 100)) * 2.0f * PI;
                m.dx = cosf(a); m.dy = sinf(a);
                m.wander = 1.5f + rnd((int)m.x, (int)m.y, 9u) * 2.0f;
            }
            mvx = m.dx * spd * 0.4f; mvy = m.dy * spd * 0.4f;
        }
        moveWalker(m.x, m.y, mvx, mvy, dt, true);
        if (fabsf(mvx) > fabsf(mvy)) m.dir = mvx > 0 ? 1 : 3;
        else if (fabsf(mvy) > 0.01f) m.dir = mvy > 0 ? 2 : 0;

        m.cool -= dt;
        m.flash -= dt;
        if (d < 0.9f && m.cool <= 0) {
            gHP -= MON_DMG[m.type] * armorFactor();
            gHurtT = 0.4f;
            m.cool = 1.0f;
            addPopup("-" + std::to_string((int)MON_DMG[m.type]) + " HP", RGBX(255, 90, 90));
        }

        bool dead = m.hp <= 0;
        if (dead) {
            if (m.type == 0) { }   // slimes no longer drop anything
            else if (m.type == 2) { raiderLoot(m.x, m.y); }
            else { gCount[I_HIDE] += 1; addPopup("+1 Hide", RGBX(230, 160, 130)); }
            addXP(MON_XP[m.type]);
            if (m.byCompanion) gPendingDrops.push_back({ m.x, m.y, I_MEDICINE, 1 });
            int home = m.home;
            gMonsters.erase(gMonsters.begin() + i);
            if (home <= -1000 && gRaidActive) {
                bool anyDefender = false;
                for (const Monster& o : gMonsters) if (o.home == home) { anyDefender = true; break; }
                if (!anyDefender) raidWin();
            }
            if (home >= 0) {
                bool any = false;
                for (const Monster& o : gMonsters) if (o.home == home) { any = true; break; }
                if (!any) {
                    gHomesteads[home].cleared = 1;
                    addPopup("Homestead cleared!", RGBX(255, 230, 120));
                    addXP(40);
                }
            }
            continue;
        }
        if (m.home < 0 && ((dl > 0.5f && d > 18.0f) || d > 30.0f)) { gMonsters.erase(gMonsters.begin() + i); continue; }
        ++i;
    }
}


// ---------- dropped items (throw with Backspace) ----------
struct DroppedItem { float x, y, vx, vy, age; int item; int n; };
static std::vector<DroppedItem> gDrops;
static const float DROP_LIFE = 300.0f;

static void throwSelected(bool all) {
    if (gSelItem < 0 || gCount[gSelItem] <= 0) { setTitle("Nothing selected to throw"); return; }
    if (gDrops.size() >= 512) { setTitle("Too many items on the ground"); return; }
    int n = all ? gCount[gSelItem] : 1;
    gCount[gSelItem] -= n;
    DroppedItem d;
    d.x = gP.x + DXS[gP.dir] * 0.5f;
    d.y = gP.y + DYS[gP.dir] * 0.5f;
    if (solidAt(d.x, d.y)) { d.x = gP.x; d.y = gP.y; }
    d.vx = DXS[gP.dir] * 3.5f;
    d.vy = DYS[gP.dir] * 3.5f;
    d.age = 0;
    d.item = gSelItem;
    d.n = n;
    gDrops.push_back(d);
    setTitle("Threw " + std::to_string(n) + " " + itemName(gSelItem));
}

static void updateDrops(float dt) {
    for (const PendingDrop& pd : gPendingDrops) {
        if (gDrops.size() >= 512) break;
        DroppedItem d;
        d.x = pd.x; d.y = pd.y; d.vx = 0; d.vy = 0; d.age = 0; d.item = pd.item; d.n = pd.n;
        gDrops.push_back(d);
    }
    gPendingDrops.clear();
    float damp = powf(0.05f, dt);
    for (DroppedItem& d : gDrops) {
        d.age += dt;
        float nx = d.x + d.vx * dt, ny = d.y + d.vy * dt;
        if (!solidAt(nx, d.y)) d.x = nx; else d.vx = 0;
        if (!solidAt(d.x, ny)) d.y = ny; else d.vy = 0;
        d.vx *= damp; d.vy *= damp;
    }
    for (size_t i = 0; i < gDrops.size();) {
        const DroppedItem d = gDrops[i];
        float ddx = d.x - gP.x, ddy = d.y - gP.y;
        if (d.age > 0.8f && ddx * ddx + ddy * ddy < 0.36f) {
            gCount[d.item] += d.n;
            addPopup("+" + std::to_string(d.n) + " " + itemName(d.item), RGBX(255, 255, 255));
            gDrops.erase(gDrops.begin() + i);
        } else if (d.age > DROP_LIFE) {
            gDrops.erase(gDrops.begin() + i);
        } else {
            ++i;
        }
    }
}

static void drawIcon(int x, int y, int s, int it);

static void drawDrops(float camX, float camY) {
    for (size_t i = 0; i < gDrops.size(); i++) {
        const DroppedItem& d = gDrops[i];
        int sx = (int)(d.x * TILE - camX), sy = (int)(d.y * TILE - camY);
        float bob = sinf(gTime * 3.0f + i * 1.7f) * 3.0f;
        fillEllipse((float)sx, (float)(sy + 16), 9.0f, 3.0f, RGBX(16, 16, 20));
        drawIcon(sx - 12, (int)(sy - 12 + bob), 24, d.item);
        if (d.n > 1) drawText(sx - 6, sy + 6, std::to_string(d.n), 1, RGBX(255, 255, 255));
    }
}

static bool writeDrops(FILE* f) {
    uint32_t n = (uint32_t)gDrops.size();
    if (fwrite(&n, sizeof n, 1, f) != 1) return false;
    return n == 0 || fwrite(gDrops.data(), sizeof(DroppedItem), n, f) == n;
}
static bool readDrops(FILE* f, std::vector<DroppedItem>& out) {
    uint32_t n = 0;
    if (fread(&n, sizeof n, 1, f) != 1 || n > 512) return false;
    out.resize(n);
    return n == 0 || fread(out.data(), sizeof(DroppedItem), n, f) == n;
}

static bool writeCompanions(FILE* f) {
    uint32_t np = (uint32_t)gPeople.size();
    if (fwrite(&np, sizeof np, 1, f) != 1) return false;
    if (np && fwrite(gPeople.data(), sizeof(Person), np, f) != np) return false;
    uint32_t nq = (uint32_t)gPets.size();
    if (fwrite(&nq, sizeof nq, 1, f) != 1) return false;
    return nq == 0 || fwrite(gPets.data(), sizeof(Pet), nq, f) == nq;
}
static bool readCompanions(FILE* f, std::vector<Person>& pp, std::vector<Pet>& qq) {
    uint32_t np = 0, nq = 0;
    if (fread(&np, sizeof np, 1, f) != 1 || np > 200) return false;
    pp.resize(np);
    if (np && fread(pp.data(), sizeof(Person), np, f) != np) return false;
    if (fread(&nq, sizeof nq, 1, f) != 1 || nq > 25) return false;
    qq.resize(nq);
    return nq == 0 || fread(qq.data(), sizeof(Pet), nq, f) == nq;
}

static bool writeHomesteads(FILE* f) {
    uint32_t n = (uint32_t)gHomesteads.size();
    if (fwrite(&n, sizeof n, 1, f) != 1) return false;
    return n == 0 || fwrite(gHomesteads.data(), sizeof(Homestead), n, f) == n;
}
static void readHomesteads(FILE* f, std::vector<Homestead>& out) {
    uint32_t n = 0;
    out.clear();
    if (fread(&n, sizeof n, 1, f) != 1 || n > 64) return;
    out.resize(n);
    if (n && fread(out.data(), sizeof(Homestead), n, f) != n) out.clear();
}

struct ExtrasHeader { float hunger; int founded; uint32_t nEmp; uint32_t nChest; };

static bool writeRLE(FILE* f, const std::vector<Cell>& in) {
    long countPos = ftell(f);
    uint64_t recs = 0;
    if (fwrite(&recs, 8, 1, f) != 1) return false;
    size_t i = 0, n = in.size();
    while (i < n) {
        Cell c = in[i];
        uint32_t run = 1;
        while (i + run < n && run < 0xFFFFFFFFu && memcmp(&in[i + run], &c, sizeof c) == 0) run++;
        if (fwrite(&c, sizeof c, 1, f) != 1 || fwrite(&run, 4, 1, f) != 1) return false;
        recs++;
        i += run;
    }
    long endPos = ftell(f);
    fseek(f, countPos, SEEK_SET);
    if (fwrite(&recs, 8, 1, f) != 1) return false;
    fseek(f, endPos, SEEK_SET);
    return true;
}

static bool readRLE(FILE* f, std::vector<Cell>& out) {
    uint64_t recs = 0;
    if (fread(&recs, 8, 1, f) != 1) return false;
    out.resize((size_t)MAP_W * MAP_H);
    size_t pos = 0;
    for (uint64_t r = 0; r < recs; r++) {
        Cell c;
        uint32_t run = 0;
        if (fread(&c, sizeof c, 1, f) != 1 || fread(&run, 4, 1, f) != 1) return false;
        if (run == 0 || pos + run > out.size()) return false;
        for (uint32_t k = 0; k < run; k++) out[pos++] = c;
    }
    return pos == out.size();
}

// Tamed creatures are saved as an optional section at the end of the file.
static bool writeTamed(FILE* f) {
    std::vector<TamedSave> v;
    for (const Beast& b : gBeasts) if (b.tame) {
        TamedSave t;
        memset(&t, 0, sizeof t);
        t.type = 0; t.kind = b.kind; t.face = b.face; t.x = b.x; t.y = b.y; t.hp = b.hp;
        memcpy(t.name, b.name, 12);
        v.push_back(t);
    }
    for (const Animal& a : gAnimals) if (a.tame) {
        TamedSave t;
        memset(&t, 0, sizeof t);
        t.type = 1; t.kind = 0; t.face = a.face; t.x = a.x; t.y = a.y; t.hp = a.hp;
        memcpy(t.name, a.name, 12);
        v.push_back(t);
    }
    uint32_t n = (uint32_t)v.size();
    if (fwrite(&n, sizeof n, 1, f) != 1) return false;
    return n == 0 || fwrite(v.data(), sizeof(TamedSave), n, f) == n;
}

static void readTamed(FILE* f, std::vector<TamedSave>& out) {
    out.clear();
    uint32_t n = 0;
    if (fread(&n, sizeof n, 1, f) != 1 || n > 4096) return;
    out.resize(n);
    if (n && fread(out.data(), sizeof(TamedSave), n, f) != n) out.clear();
}

static bool writeUnderground(FILE* f) {
    return writeRLE(f, gUnderMap);
}
static bool readUnderground(FILE* f, std::vector<Cell>& out, int mapW) {
    out.resize((size_t)mapW * mapW);
    return fread(out.data(), sizeof(Cell), out.size(), f) == out.size();
}

static bool writeExtras(FILE* f) {
    ExtrasHeader h;
    h.hunger = gHunger;
    h.founded = gEmpireFounded ? 1 : 0;
    h.nEmp = (uint32_t)gEmpires.size();
    h.nChest = (uint32_t)gChests.size();
    if (fwrite(&h, sizeof h, 1, f) != 1) return false;
    if (h.nEmp && fwrite(gEmpires.data(), sizeof(Empire), h.nEmp, f) != h.nEmp) return false;
    if (h.nChest && fwrite(gChests.data(), sizeof(ChestData), h.nChest, f) != h.nChest) return false;
    if (fwrite(gHotbar, sizeof(int), 10, f) != 10) return false;
    uint32_t nf = (uint32_t)gFurnaces.size();
    if (fwrite(&nf, sizeof nf, 1, f) != 1) return false;
    return nf == 0 || fwrite(gFurnaces.data(), sizeof(Furnace), nf, f) == nf;
}

static void readExtras(FILE* f) {
    gHunger = 100.0f;
    gEmpireFounded = false;
    gEmpires.clear();
    gChests.clear();
    for (int k = 0; k < 10; k++) gHotbar[k] = -1;
    gFurnaces.clear();
    ExtrasHeader h;
    if (fread(&h, sizeof h, 1, f) != 1 || h.nEmp > 64 || h.nChest > 4096) return;
    std::vector<Empire> e(h.nEmp);
    std::vector<ChestData> c(h.nChest);
    if (h.nEmp && fread(e.data(), sizeof(Empire), h.nEmp, f) != h.nEmp) return;
    if (h.nChest && fread(c.data(), sizeof(ChestData), h.nChest, f) != h.nChest) return;
    gHunger = h.hunger;
    gEmpireFounded = h.founded != 0;
    gEmpires = e;
    gChests = c;
    int hb[10];
    if (fread(hb, sizeof(int), 10, f) == 10) memcpy(gHotbar, hb, sizeof hb);
    uint32_t nf = 0;
    if (fread(&nf, sizeof nf, 1, f) == 1 && nf <= 256) {
        gFurnaces.resize(nf);
        if (nf && fread(gFurnaces.data(), sizeof(Furnace), nf, f) != nf) gFurnaces.clear();
    }
}

// ---------- saving ----------
struct SaveHeader { char magic[8]; uint32_t version; uint32_t mapW, mapH; };
struct SaveState { float time; float px, py; int dir; float energy; int selItem; int page; int xp; float hp; int sick; float sickT; };
static const uint32_t SAVE_VERSION = 14;

static void initSavePath() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    size_t p = s.find_last_of("\\/");
    std::string dir = (p == std::string::npos ? std::string() : s.substr(0, p + 1));
    gWorldDir = dir + "worlds\\";
    CreateDirectoryA(gWorldDir.c_str(), nullptr);
    std::string legacy = dir + "horizons_save.dat";
    if (GetFileAttributesA(legacy.c_str()) != INVALID_FILE_ATTRIBUTES)
        MoveFileA(legacy.c_str(), (gWorldDir + "Original World.dat").c_str());
    gSavePath = gWorldDir + "Original World.dat";
    gSettingsPath = (p == std::string::npos ? std::string() : s.substr(0, p + 1)) + "horizons_settings.cfg";
}

static bool saveGame() {
    if (gScreen != SCR_GAME) return false;
    bool sw = gUnderground;
    if (sw) std::swap(gMap, gUnderMap);          // always write the surface first
    FILE* f = fopen(gSavePath.c_str(), "wb");
    if (!f) return false;
    SaveHeader h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "HORIZON1", 8);   // 8-byte magic; the version field identifies the format
    h.version = SAVE_VERSION; h.mapW = MAP_W; h.mapH = MAP_H;
    SaveState s = { gTime, gP.x, gP.y, gP.dir, gP.energy, gSelItem, gPage, gXP, gHP, gSick, gSickT };
    bool ok = fwrite(&h, sizeof h, 1, f) == 1
           && fwrite(&s, sizeof s, 1, f) == 1
           && fwrite(gCount, sizeof(int), T_COUNT, f) == (size_t)T_COUNT
           && writeRLE(f, gMap)
           && writeDrops(f)
           && writeCompanions(f)
           && writeHomesteads(f)
           && writeExtras(f)
           && writeUnderground(f)
           && writeTamed(f);
    fclose(f);
    if (sw) std::swap(gMap, gUnderMap);
    return ok;
}

// ---------- loading saves from any version ----------
// Each version wrote the same sections in the same order, but the map size, the item list, the
// dish codes and the optional sections changed over time. We try each known layout and accept
// the one that uses the file exactly to its last byte. Item ids are then mapped to the current list.
static bool gLoadedLegacy = false;

struct SaveLayout {
    int mapW;        // 256 (version 10) or 900 (versions 11+)
    int seed;        // I_SEED of that version's item list
    int dishBase;    // 14 or 15 ingredient codes per dish digit
    bool companions; // friends and pets (version 10+)
    int extras;      // 0 none, 1 empires and chests, 2 + hotbar, 3 + furnaces
    bool underground;// caves (version 13+)
};

static long fileSizeOf(FILE* f) {
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, cur, SEEK_SET);
    return n;
}

static int layoutOldCount(const SaveLayout& L) { return L.seed + 24; }
static int layoutDishCount(const SaveLayout& L) { return L.dishBase * L.dishBase * L.dishBase; }

// Walks the sections without reading the big ones and checks that they end exactly at the file's end.
static bool probeLayout(FILE* f, long countsStart, long fileSize, const SaveLayout& L) {
    long pos = countsStart;
    long T = layoutOldCount(L) + layoutDishCount(L);
    long cellBytes = (long)L.mapW * L.mapW * (long)sizeof(Cell);
    pos += 4 * T + cellBytes;
    if (pos > fileSize) return false;
    uint32_t n = 0;
    fseek(f, pos, SEEK_SET);
    if (fread(&n, 4, 1, f) != 1 || n > 512) return false;
    pos += 4 + (long)n * (long)sizeof(DroppedItem);
    if (L.companions) {
        fseek(f, pos, SEEK_SET);
        uint32_t np = 0, nq = 0;
        if (fread(&np, 4, 1, f) != 1 || np > 200) return false;
        pos += 4 + (long)np * (long)sizeof(Person);
        fseek(f, pos, SEEK_SET);
        if (fread(&nq, 4, 1, f) != 1 || nq > 25) return false;
        pos += 4 + (long)nq * (long)sizeof(Pet);
    }
    if (pos < fileSize) {                                   // homesteads
        fseek(f, pos, SEEK_SET);
        if (fread(&n, 4, 1, f) != 1 || n > 64) return false;
        pos += 4 + (long)n * (long)sizeof(Homestead);
    }
    if (L.extras >= 1) {
        fseek(f, pos, SEEK_SET);
        ExtrasHeader h;
        if (fread(&h, sizeof h, 1, f) != 1 || h.nEmp > 64 || h.nChest > 4096) return false;
        pos += (long)sizeof h + (long)h.nEmp * (long)sizeof(Empire) + (long)h.nChest * (long)sizeof(ChestData);
        if (L.extras >= 2) pos += 40;                        // hotbar
        if (L.extras >= 3) {
            if (pos >= fileSize) return false;
            fseek(f, pos, SEEK_SET);
            uint32_t nf = 0;
            if (fread(&nf, 4, 1, f) != 1 || nf > 256) return false;
            pos += 4 + (long)nf * (long)sizeof(Furnace);
        }
    }
    if (L.underground) pos += cellBytes;
    return pos == fileSize;
}

static int remapDish(int oldKey, const SaveLayout& L) {
    int b = L.dishBase;
    int c[3] = { oldKey / (b * b), (oldKey / b) % b, oldKey % b };
    std::vector<int> codes;
    for (int k : c) if (k) codes.push_back(k);
    return dishKey(codes);
}

static bool loadWithLayout(FILE* f, const SaveLayout& L, const SaveState& s, long countsStart) {
    fseek(f, countsStart, SEEK_SET);
    int oldCount = layoutOldCount(L);
    int T = oldCount + layoutDishCount(L);
    std::vector<int> counts(T);
    std::vector<Cell> cells((size_t)L.mapW * L.mapW);
    std::vector<DroppedItem> drops;
    std::vector<Person> people;
    std::vector<Pet> pets;
    std::vector<Homestead> homes;
    std::vector<Empire> empires;
    std::vector<ChestData> chests;
    std::vector<Furnace> furn;
    std::vector<Cell> ug;
    int hotbar[10] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    bool hasHotbar = false, founded = false;
    float hunger = 100.0f;
    if (fread(counts.data(), sizeof(int), counts.size(), f) != counts.size()) return false;
    if (fread(cells.data(), sizeof(Cell), cells.size(), f) != cells.size()) return false;
    if (!readDrops(f, drops)) return false;
    if (L.companions && !readCompanions(f, people, pets)) return false;
    readHomesteads(f, homes);
    if (L.extras >= 1) {
        ExtrasHeader h;
        if (fread(&h, sizeof h, 1, f) != 1) return false;
        empires.resize(h.nEmp);
        if (h.nEmp && fread(empires.data(), sizeof(Empire), h.nEmp, f) != h.nEmp) return false;
        chests.resize(h.nChest);
        if (h.nChest && fread(chests.data(), sizeof(ChestData), h.nChest, f) != h.nChest) return false;
        founded = h.founded != 0;
        hunger = h.hunger;
        if (L.extras >= 2 && fread(hotbar, sizeof(int), 10, f) == 10) hasHotbar = true;
        if (L.extras >= 3) {
            uint32_t nf = 0;
            if (fread(&nf, sizeof nf, 1, f) != 1 || nf > 256) return false;
            furn.resize(nf);
            if (nf && fread(furn.data(), sizeof(Furnace), nf, f) != nf) return false;
        }
    }
    if (L.underground && !readUnderground(f, ug, L.mapW)) return false;

    // ---- convert ----
    auto mapItem = [&](int i) -> int {
        if (i < 0) return -1;
        if (i >= oldCount) return I_COUNT + remapDish(i - oldCount, L);
        if (L.seed == 59) return i >= 31 ? i + 3 : i;          // before the furnace and ore items were added
        return i >= L.seed ? i + (I_SEED - L.seed) : i;
    };
    const int OFF = (MAP_W - L.mapW) / 2;                       // small old maps are centred in the new world
    gUnderground = false;
    gPendingTamed.clear();
    generateWorld((uint32_t)GetTickCount());
    for (int y = 0; y < L.mapW; y++) for (int x = 0; x < L.mapW; x++) cellAt(x + OFF, y + OFF) = cells[y * L.mapW + x];

    memset(gCount, 0, sizeof gCount);
    for (int i = 0; i < T; i++) {
        if (counts[i] <= 0) continue;
        int ni = mapItem(i);
        if (ni >= 0 && ni < T_COUNT) gCount[ni] += counts[i];
    }
    gTime = s.time;
    gP.x = s.px + OFF; gP.y = s.py + OFF; gP.dir = s.dir; gP.energy = s.energy;
    gHP = s.hp; gSick = s.sick; gSickT = s.sickT;
    gSelItem = (s.selItem >= 0) ? mapItem(s.selItem) : -1;
    if (gSelItem >= T_COUNT) gSelItem = -1;
    gPage = s.page; gXP = s.xp;
    gHunger = hunger;

    gDrops.clear();
    for (DroppedItem d : drops) {
        d.x += OFF; d.y += OFF;
        d.item = mapItem(d.item);
        if (d.item >= 0 && d.item < T_COUNT) gDrops.push_back(d);
    }
    for (Person& q : people) { q.x += OFF; q.y += OFF; }
    for (Pet& q : pets) { q.x += OFF; q.y += OFF; }
    gPeople = people;
    gPets = pets;
    for (Homestead& H : homes) { H.x += OFF; H.y += OFF; H.activated = 0; }
    gHomesteads = homes;

    if (L.extras >= 1) {
        for (Empire& e : empires) { e.x += OFF; e.y += OFF; }
        gEmpires = empires;
        gEmpireFounded = founded;
        gChests.clear();
        for (ChestData c : chests) {
            int cx = c.cell % L.mapW + OFF, cy = c.cell / L.mapW + OFF;
            c.cell = cy * MAP_W + cx;
            for (int k = 0; k < 10; k++) if (c.n[k] > 0) c.item[k] = mapItem(c.item[k]);
            gChests.push_back(c);
        }
        gFurnaces.clear();
        for (Furnace f2 : furn) {
            int cx = f2.cell % L.mapW + OFF, cy = f2.cell / L.mapW + OFF;
            f2.cell = cy * MAP_W + cx;
            f2.inItem = mapItem(f2.inItem);
            f2.outItem = mapItem(f2.outItem);
            gFurnaces.push_back(f2);
        }
    }
    if (hasHotbar) for (int k = 0; k < 10; k++) gHotbar[k] = mapItem(hotbar[k]);
    else for (int k = 0; k < 10; k++) gHotbar[k] = -1;

    generateUnderground((uint32_t)GetTickCount());          // fresh caves, then put the saved ones back
    if (L.underground) {
        for (int y = 0; y < L.mapW; y++) for (int x = 0; x < L.mapW; x++)
            gUnderMap[(y + OFF) * MAP_W + (x + OFF)] = ug[y * L.mapW + x];
    }
    return true;
}

static bool loadNative14(FILE* f, const SaveState& s) {
    std::vector<int> counts(T_COUNT);
    std::vector<Cell> cells, ug;
    std::vector<DroppedItem> drops;
    std::vector<Person> people;
    std::vector<Pet> pets;
    std::vector<Homestead> homes;
    if (fread(counts.data(), sizeof(int), counts.size(), f) != counts.size()) return false;
    if (!readRLE(f, cells)) return false;
    if (!readDrops(f, drops)) return false;
    if (!readCompanions(f, people, pets)) return false;
    readHomesteads(f, homes);
    readExtras(f);
    if (!readRLE(f, ug)) return false;
    readTamed(f, gPendingTamed);

    memcpy(gCount, counts.data(), sizeof(int) * T_COUNT);
    gMap = std::move(cells);
    gUnderMap = std::move(ug);
    gUnderground = false;
    gTime = s.time; gP.x = s.px; gP.y = s.py; gP.dir = s.dir; gP.energy = s.energy; gHP = s.hp;
    gSick = s.sick; gSickT = s.sickT;
    gSelItem = s.selItem; gPage = s.page; gXP = s.xp;
    gDrops = drops;
    gPeople = people;
    gPets = pets;
    gHomesteads = homes;
    for (Homestead& H : gHomesteads) H.activated = 0;
    return true;
}

static bool loadGame() {
    FILE* f = fopen(gSavePath.c_str(), "rb");
    if (!f) return false;
    SaveHeader h;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "HORIZON", 7) != 0) { fclose(f); return false; }
    SaveState s;
    if (fread(&s, sizeof s, 1, f) != 1) { fclose(f); return false; }
    if (h.version == 14 && h.mapW == (uint32_t)MAP_W && h.mapH == (uint32_t)MAP_H) {
        bool okN = loadNative14(f, s);
        fclose(f);
        gLoadedLegacy = false;
        return okN;
    }
    long countsStart = ftell(f);
    long fileSize = fileSizeOf(f);

    // Every layout the game has written, the current one first.
    std::vector<SaveLayout> cands;
    for (int mapW : { 900, 256 })
        for (int seed : { 62, 59, 26, 25 })
            for (int dish : { 15, 14 })
                for (int comp = 1; comp >= 0; comp--)
                    for (int extras = 3; extras >= 0; extras--)
                        for (int ug = 1; ug >= 0; ug--)
                            cands.push_back({ mapW, seed, dish, comp != 0, extras, ug != 0 });
    for (const SaveLayout& L : cands) {
        if (L.mapW != (int)h.mapW) continue;
        if (!probeLayout(f, countsStart, fileSize, L)) continue;
        bool native = L.mapW == MAP_W && L.seed == I_SEED && L.dishBase == 15 && L.companions &&
                      L.extras == 3 && L.underground;
        bool ok = loadWithLayout(f, L, s, countsStart);
        fclose(f);
        if (ok) gLoadedLegacy = !native;
        return ok;
    }
    fclose(f);
    return false;
}


// ---------- item icons ----------
static void drawIcon(int x, int y, int s, int it) {
    float cx = x + s * 0.5f, cy = y + s * 0.5f, R = s * 0.42f;
    u32 c = itemColor(it);
    const u32 WOOD = RGBX(140, 96, 50), STEEL = RGBX(190, 196, 206);
    if (it == I_FLINT) {
        fillEllipse(cx, cy, R * 0.8f, R * 0.55f, c);
        fillEllipse(cx - R * 0.2f, cy - R * 0.1f, R * 0.25f, R * 0.15f, RGBX(190, 190, 200));
    } else if (it == I_CLAY) {
        fillEllipse(cx, cy, R, R * 0.8f, c);
        fillEllipse(cx - R * 0.2f, cy - R * 0.2f, R * 0.4f, R * 0.25f, RGBX(210, 150, 120));
    } else if (it == I_HIDE) {
        fillRect((int)(x + s * 0.15f), (int)(y + s * 0.2f), (int)(s * 0.7f), (int)(s * 0.6f), c);
        fillEllipse(cx - R * 0.3f, cy, 2.5f, 2.5f, RGBX(100, 66, 36));
        fillEllipse(cx + R * 0.3f, cy + R * 0.2f, 2.5f, 2.5f, RGBX(100, 66, 36));
    } else if (it == I_MEAT) {
        fillEllipse(cx, cy, R * 0.9f, R * 0.6f, c);
        fillRect((int)(x + s * 0.7f), (int)(y + s * 0.45f), (int)(s * 0.2f), 3, RGBX(240, 230, 210));
    } else if (it == I_SPEAR) {
        drawLine(x + s * 0.15f, y + s * 0.85f, x + s * 0.7f, y + s * 0.3f, 3, WOOD);
        fillEllipse(x + s * 0.78f, y + s * 0.22f, s * 0.12f, s * 0.12f, STEEL);
    } else if (it == I_DAGGER) {
        drawLine(x + s * 0.3f, y + s * 0.7f, x + s * 0.75f, y + s * 0.25f, 4, STEEL);
        drawLine(x + s * 0.22f, y + s * 0.78f, x + s * 0.34f, y + s * 0.66f, 4, WOOD);
    } else if (it == I_VEST) {
        fillRect((int)(x + s * 0.2f), (int)(y + s * 0.25f), (int)(s * 0.6f), (int)(s * 0.55f), c);
        fillRect((int)(x + s * 0.2f), (int)(y + s * 0.25f), (int)(s * 0.6f), 4, RGBX(100, 66, 36));
    } else if (it == I_BED) {
        fillRect((int)(x + s * 0.1f), (int)(y + s * 0.4f), (int)(s * 0.8f), (int)(s * 0.3f), c);
        fillRect((int)(x + s * 0.1f), (int)(y + s * 0.3f), (int)(s * 0.25f), (int)(s * 0.15f), RGBX(240, 240, 240));
    } else if (it >= I_CU_INGOT && it < I_TIER) {
        u32 ic = itemColor(it);
        if (it == I_DIA || it == I_OBS) {
            fillEllipse(cx, cy, R * 0.7f, R * 0.7f, ic);
            fillEllipse(cx - R * 0.2f, cy - R * 0.2f, R * 0.25f, R * 0.2f, RGBX(240, 250, 255));
        } else if (it == I_CHEST) {
            fillRect((int)(x + s * 0.15f), (int)(y + s * 0.3f), (int)(s * 0.7f), (int)(s * 0.5f), RGBX(150, 100, 55));
            fillRect((int)(x + s * 0.15f), (int)(y + s * 0.3f), (int)(s * 0.7f), 3, RGBX(90, 58, 30));
        } else {
            fillRect((int)(x + s * 0.2f), (int)(y + s * 0.4f), (int)(s * 0.6f), (int)(s * 0.3f), ic);
            fillRect((int)(x + s * 0.2f), (int)(y + s * 0.4f), (int)(s * 0.6f), 2, RGBX(255, 255, 255));
        }
    } else if (it >= I_TIER && it <= I_TIER_END) {
        int m = (it - I_TIER) / 7, pc = (it - I_TIER) % 7;
        u32 mc = METAL_COL[m];
        if (pc == 0) {
            drawLine(x + s * 0.2f, y + s * 0.85f, x + s * 0.7f, y + s * 0.3f, 3, WOOD);
            drawLine(x + s * 0.1f, y + s * 0.32f, x + s * 0.9f, y + s * 0.5f, 4, mc);
        } else if (pc == 1) {
            drawLine(x + s * 0.2f, y + s * 0.85f, x + s * 0.7f, y + s * 0.3f, 3, WOOD);
            fillEllipse(x + s * 0.72f, y + s * 0.3f, s * 0.22f, s * 0.14f, mc);
        } else if (pc == 2) {
            drawLine(x + s * 0.2f, y + s * 0.8f, x + s * 0.78f, y + s * 0.2f, 4, mc);
            drawLine(x + s * 0.12f, y + s * 0.62f, x + s * 0.38f, y + s * 0.88f, 3, RGBX(120, 80, 40));
        } else if (pc == 3) {
            fillEllipse(cx, cy, R * 0.8f, R * 0.7f, mc);
            fillRect((int)(x + s * 0.2f), (int)(y + s * 0.5f), (int)(s * 0.6f), 3, RGBX(40, 40, 50));
        } else if (pc == 4) {
            fillRect((int)(x + s * 0.2f), (int)(y + s * 0.2f), (int)(s * 0.6f), (int)(s * 0.65f), mc);
            fillRect((int)(x + s * 0.45f), (int)(y + s * 0.2f), 3, (int)(s * 0.65f), RGBX(40, 40, 50));
        } else if (pc == 5) {
            fillRect((int)(x + s * 0.25f), (int)(y + s * 0.2f), (int)(s * 0.5f), (int)(s * 0.65f), mc);
        } else {
            fillRect((int)(x + s * 0.2f), (int)(y + s * 0.5f), (int)(s * 0.6f), (int)(s * 0.3f), mc);
        }
    } else if (it == I_MEDICINE) {
        // a white bottle with a red cross and a cork
        fillRect((int)(x + s * 0.4f), (int)(y + s * 0.08f), (int)(s * 0.2f), (int)(s * 0.14f), RGBX(150, 110, 60));
        fillRect((int)(x + s * 0.3f), (int)(y + s * 0.2f), (int)(s * 0.4f), (int)(s * 0.08f), RGBX(200, 200, 210));
        fillEllipse(cx, cy + R * 0.25f, R * 0.7f, R * 0.7f, RGBX(235, 245, 250));
        fillEllipse(cx - R * 0.2f, cy, R * 0.15f, R * 0.4f, RGBX(255, 255, 255));
        fillRect((int)(cx - s * 0.05f), (int)(cy + R * 0.25f - s * 0.16f), (int)(s * 0.1f), (int)(s * 0.32f), RGBX(220, 40, 50));
        fillRect((int)(cx - s * 0.16f), (int)(cy + R * 0.25f - s * 0.05f), (int)(s * 0.32f), (int)(s * 0.1f), RGBX(220, 40, 50));
    } else if (it == I_DOOR) {
        fillRect((int)(x + s * 0.25f), (int)(y + s * 0.15f), (int)(s * 0.5f), (int)(s * 0.7f), c);
        fillRect((int)(x + s * 0.6f), (int)(y + s * 0.5f), 3, 3, RGBX(230, 190, 80));
    } else if (it == I_TORCH) {
        drawLine(x + s * 0.5f, y + s * 0.85f, x + s * 0.5f, y + s * 0.4f, 4, WOOD);
        fillEllipse(x + s * 0.5f, y + s * 0.3f, s * 0.14f, s * 0.16f, RGBX(255, 150, 40));
        fillEllipse(x + s * 0.5f, y + s * 0.33f, s * 0.06f, s * 0.08f, RGBX(255, 240, 150));
    } else if (it >= I_COUNT) {
        fillEllipse(cx, cy + R * 0.25f, R, R * 0.55f, RGBX(150, 100, 60));
        fillEllipse(cx, cy + R * 0.05f, R * 0.8f, R * 0.4f, c);
        fillEllipse(cx - R * 0.2f, cy - R * 0.1f, R * 0.25f, R * 0.18f, RGBX(255, 240, 200));
    } else if (it == I_WOOD) {
        fillEllipse(cx, cy, R, R * 0.75f, c);
        fillEllipse(cx, cy, R * 0.6f, R * 0.45f, RGBX(222, 180, 118));
        fillEllipse(cx, cy, R * 0.25f, R * 0.2f, RGBX(170, 120, 70));
    } else if (it == I_STONE) {
        fillEllipse(cx, cy + 2, R, R * 0.8f, RGBX(96, 96, 104));
        fillEllipse(cx - R * 0.15f, cy - R * 0.15f, R * 0.75f, R * 0.6f, c);
    } else if (it == I_FISH) {
        fillEllipse(cx - R * 0.15f, cy, R * 0.7f, R * 0.42f, c);
        drawLine(cx + R * 0.45f, cy, cx + R * 0.95f, cy - R * 0.45f, 3, c);
        drawLine(cx + R * 0.45f, cy, cx + R * 0.95f, cy + R * 0.45f, 3, c);
        fillRect((int)(cx - R * 0.5f), (int)(cy - 2), 3, 3, RGBX(20, 30, 40));
    } else if (it == I_PLANK) {
        fillRect((int)(x + s * 0.12f), (int)(y + s * 0.25f), (int)(s * 0.76f), (int)(s * 0.5f), c);
        for (int k = 0; k < 3; k++) fillRect((int)(x + s * 0.12f), (int)(y + s * 0.25f + k * s * 0.17f), (int)(s * 0.76f), 2, RGBX(120, 80, 40));
    } else if (it == I_BRICK || it == I_MACE) {
        fillRect((int)(x + s * 0.12f), (int)(y + s * 0.22f), (int)(s * 0.76f), (int)(s * 0.56f), c);
        fillRect((int)(x + s * 0.12f), (int)(y + s * 0.5f), (int)(s * 0.76f), 2, RGBX(210, 200, 180));
        fillRect((int)(x + s * 0.5f), (int)(y + s * 0.22f), 2, (int)(s * 0.28f), RGBX(210, 200, 180));
        if (it == I_MACE) drawLine(x + s * 0.2f, y + s * 0.9f, x + s * 0.5f, y + s * 0.5f, 3, WOOD);
    } else if (it == I_AXE || it == I_PICKAXE || it == I_HOE) {
        drawLine(x + s * 0.2f, y + s * 0.85f, x + s * 0.7f, y + s * 0.3f, 3, WOOD);
        if (it == I_AXE) fillEllipse(x + s * 0.72f, y + s * 0.3f, s * 0.22f, s * 0.14f, STEEL);
        if (it == I_PICKAXE) drawLine(x + s * 0.1f, y + s * 0.32f, x + s * 0.9f, y + s * 0.5f, 3, STEEL);
        if (it == I_HOE) fillRect((int)(x + s * 0.6f), (int)(y + s * 0.12f), (int)(s * 0.24f), (int)(s * 0.14f), STEEL);
    } else if (it == I_WOOD_SWORD || it == I_STONE_SWORD) {
        u32 blade = it == I_WOOD_SWORD ? RGBX(190, 150, 96) : STEEL;
        drawLine(x + s * 0.2f, y + s * 0.8f, x + s * 0.78f, y + s * 0.2f, 4, blade);
        drawLine(x + s * 0.12f, y + s * 0.62f, x + s * 0.38f, y + s * 0.88f, 3, RGBX(120, 80, 40));
    } else if (it == I_ROD) {
        drawLine(x + s * 0.15f, y + s * 0.85f, x + s * 0.8f, y + s * 0.15f, 2, WOOD);
        drawLine(x + s * 0.8f, y + s * 0.15f, x + s * 0.8f, y + s * 0.62f, 1, RGBX(230, 230, 230));
        fillEllipse(x + s * 0.8f, y + s * 0.66f, 3, 3, RGBX(230, 60, 60));
    } else if (it == I_BREAD) {
        fillEllipse(cx, cy + R * 0.2f, R, R * 0.7f, c);
        fillEllipse(cx, cy, R * 0.8f, R * 0.45f, RGBX(244, 210, 156));
    } else if (it == I_GEL) {
        fillEllipse(cx, cy + R * 0.2f, R * 0.9f, R * 0.6f, c);
        fillEllipse(cx - R * 0.25f, cy - R * 0.1f, R * 0.25f, R * 0.18f, RGBX(200, 255, 210));
    } else if (it == I_CAMPFIRE) {
        fillRect((int)(x + s * 0.2f), (int)(y + s * 0.6f), (int)(s * 0.6f), 4, WOOD);
        fillRect((int)(x + s * 0.3f), (int)(y + s * 0.7f), (int)(s * 0.4f), 4, RGBX(100, 66, 34));
        fillEllipse(cx, cy - R * 0.2f, R * 0.35f, R * 0.5f, RGBX(255, 150, 40));
        fillEllipse(cx, cy - R * 0.05f, R * 0.18f, R * 0.3f, RGBX(255, 230, 120));
    } else if (it >= I_SEED && it < I_PROD) {
        for (int k = 0; k < 6; k++) fillEllipse(cx + cosf(k * 1.1f) * R * 0.5f, cy + sinf(k * 1.7f) * R * 0.4f, 2.5f, 2.5f, c);
    } else if (it >= I_PROD) {
        fillEllipse(cx, cy + R * 0.1f, R * 0.8f, R * 0.8f, c);
        fillEllipse(cx - R * 0.2f, cy - R * 0.6f, R * 0.3f, R * 0.15f, CROPS[it - I_PROD].leaf);
    }
}

// ---------- rendering ----------
static float daylight() {
    float f = gTime / DAY_LENGTH; f -= floorf(f);
    return 0.5f - 0.5f * cosf(f * 2 * PI);
}

static const std::vector<std::pair<float, float>> kNoLights;

static void applyLighting() {
    float s = daylight();
    float amb = 0.28f + 0.72f * s;
    float tr = lerpf(0.40f, 1.0f, s), tg = lerpf(0.46f, 0.96f, s), tb = lerpf(0.80f, 0.86f, s);
    float wAmb = 1.0f, wTr = 1.0f, wTg = 1.0f, wTb = 1.0f, fogAmt = 0.0f;
    switch (gWeather) {
        case W_CLOUDY:    wAmb = 0.85f; break;
        case W_RAIN:      wAmb = 0.8f; break;
        case W_STORM:     wAmb = 0.6f; break;
        case W_SNOW:      wAmb = 0.9f; wTb = 1.05f; break;
        case W_SNOWSTORM: wAmb = 0.65f; wTb = 1.12f; wTr = 0.95f; break;
        case W_SANDSTORM: wAmb = 0.6f; wTr = 1.12f; wTb = 0.7f; break;
        case W_FOG:       wAmb = 0.9f; fogAmt = 0.45f; break;
        case W_HEATWAVE:  wTr = 1.08f; wTb = 0.9f; break;
        case W_BLIZZARD:  wAmb = 0.55f; wTb = 1.15f; wTr = 0.92f; break;
        case W_HAIL:      wAmb = 0.8f; wTb = 1.05f; break;
        case W_MONSOON:   wAmb = 0.5f; wTb = 1.05f; wTg = 1.02f; break;
        default: break;
    }
    tr *= wTr; tg *= wTg; tb *= wTb; amb *= wAmb;
    if (gInCave) { amb = 0.30f; tr = 0.45f; tg = 0.55f; tb = 0.85f; }   // underground: cold, dim and blue
    {
        float fd = gTime / DAY_LENGTH;
        fd -= floorf(fd);
        float dusk = expf(-powf(fd - 0.25f, 2) * 120.0f) + expf(-powf(fd - 0.75f, 2) * 120.0f);
        if (shaderEff() >= 1) { tr *= 1.0f + 0.14f * dusk; tg *= 1.0f + 0.02f * dusk; tb *= 1.0f - 0.12f * dusk; }
    }
    float night = 1.0f - s;
    int ox = (int)(gTime * 10.0f), oy = (int)(gTime * 4.0f);
    float cx = gW * 0.5f, cy = gH * 0.5f;
    float invCx = 1.0f / cx, invCy = 1.0f / cy;

    if (shaderEff() == 0) {
        float lr = amb * tr, lg = amb * tg, lb = amb * tb;
        for (size_t i = 0; i < gFrame.size(); i++) {
            u32 c = gFrame[i];
            int r = (int)(((c >> 16) & 255) * lr), g = (int)(((c >> 8) & 255) * lg), b = (int)((c & 255) * lb);
            gFrame[i] = mk(r, g, b);
        }
        return;
    }

    for (int y = 0; y < gH; y++) {
        float vy = (y - cy) * invCy;
        for (int x = 0; x < gW; x++) {
            u32 c = gFrame[y * gW + x];
            float r = (float)((c >> 16) & 255), g = (float)((c >> 8) & 255), b = (float)(c & 255);

            (void)ox; (void)oy;
            float cloud = 1.0f;   // cloud shadows removed: the world stays still

            float vx = (x - cx) * invCx;
            float vig = 1.0f - 0.30f * std::clamp((vx * vx + vy * vy) * 0.5f, 0.0f, 1.0f);

            float dx = x - cx, dy = y - cy;
            float torch = night * std::clamp(1.0f - (dx * dx + dy * dy) / (320.0f * 320.0f), 0.0f, 1.0f);
            torch *= torch;

            float lr = (amb * tr * cloud + torch * 1.2f) * vig;
            float lg = (amb * tg * cloud + torch * 0.85f) * vig;
            float lb = (amb * tb * cloud + torch * 0.45f) * vig;
            float glow = 0;
            for (const auto& L : (shaderEff() >= 2 ? gLights : kNoLights)) {
                float ddx = x - L.first, ddy = y - L.second;
                float d2 = ddx * ddx + ddy * ddy;
                if (d2 < 12100.0f) { float t = 1.0f - d2 / 12100.0f; glow += t * t; }
            }
            lr += glow * 0.9f; lg += glow * 0.6f; lb += glow * 0.3f;
            lr += gFlashT * 3.0f; lg += gFlashT * 3.0f; lb += gFlashT * 3.4f;
            r *= lr; g *= lg; b *= lb;

            float gray = (r + g + b) / 3.0f;
            const float sat = shaderEff() >= 2 ? 1.12f : 1.0f;
            r = gray + (r - gray) * sat;
            g = gray + (g - gray) * sat;
            b = gray + (b - gray) * sat;

            if (shaderEff() >= 3) {
                float hot = std::max(0.0f, (r + g + b) / 3.0f - 170.0f) * 0.6f;
                r += hot; g += hot; b += hot;
            }
            if (shaderEff() >= 3) {
                float grain = ((hash2(x, y, (uint32_t)(gTime * 30.0f)) & 255) / 255.0f - 0.5f) * 6.0f;
                r += grain; g += grain; b += grain;
            }
            if (fogAmt > 0.0f) {
                r = r * (1.0f - fogAmt) + 190.0f * fogAmt;
                g = g * (1.0f - fogAmt) + 190.0f * fogAmt;
                b = b * (1.0f - fogAmt) + 200.0f * fogAmt;
            }
            gFrame[y * gW + x] = mk((int)r, (int)g, (int)b);
        }
    }
}

static int hotbarTop() { return gH - 16 - 84; }

static void drawStatus() {
    int top = gFullscreen ? KEYBAR_H + 10 : 16;
    fillRect(16, top, 220, 18, 0xFF101010);
    int ew = (int)(216 * gP.energy / 100.0f);
    fillRect(18, top + 2, ew, 14, gP.energy > 30 ? RGBX(64, 192, 96) : RGBX(208, 64, 64));
    drawText(16, top + 24, "STAMINA", 1, 0xFFFFFFFF);

    int lv = levelOf(gXP);
    int into = gXP - (lv - 1) * 50;
    fillRect(16, top + 40, 220, 18, 0xFF101010);
    fillRect(18, top + 42, (int)(216 * into / 50.0f), 14, RGBX(240, 200, 80));
    drawText(16, top + 64, "LV " + std::to_string(lv) + "  XP " + std::to_string(into) + "/50", 1, 0xFFFFFFFF);

    // Hearts: 10 hearts, 10 HP each.
    for (int i = 0; i < 10; i++) {
        int hx = 16 + i * 22, hy = top + 80;
        bool full = gHP >= (i + 1) * 10 - 0.5f;
        bool half = !full && gHP > i * 10 + 2;
        u32 col = full ? RGBX(230, 40, 60) : half ? RGBX(230, 130, 130) : RGBX(60, 30, 34);
        fillEllipse(hx + 5, hy + 5, 5, 4.5f, col);
        fillEllipse(hx + 12, hy + 5, 5, 4.5f, col);
        drawLine(hx + 1, hy + 7, hx + 8.5f, hy + 16, 4, col);
        drawLine(hx + 16, hy + 7, hx + 8.5f, hy + 16, 4, col);
    }

    fillRect(16, top + 104, 220, 8, 0xFF101010);
    fillRect(18, top + 106, (int)(216 * daylight()), 4, RGBX(240, 192, 64));
    fillRect(16, top + 150, 220, 8, 0xFF101010);
    fillRect(18, top + 152, (int)(216 * gHunger / 100.0f), 4, RGBX(210, 130, 60));
    drawText(16, top + 160, "HUNGER", 1, RGBX(255, 230, 200));
    if (gSick != S_NONE) drawText(16, top + 116, std::string("SICK: ") + SICK_NAMES[gSick], 1, RGBX(255, 140, 140));
    {
        int fr = 0;
        for (const Person& q : gPeople) if (q.isFriend) fr++;
        drawText(16, top + 128, "FRIENDS " + std::to_string(fr) + "/" + std::to_string(maxFriends()) + "   PETS " + std::to_string(tamedCount()) + "/" + std::to_string(maxPets()), 1, RGBX(255, 210, 110));
    }
}

static void drawClock() {
    float f = gTime / DAY_LENGTH;
    f -= floorf(f);
    float hours = f * 24.0f;
    int hour = (int)hours;
    int minute = (int)((hours - hour) * 60.0f);
    int dayNum = (int)(gTime / DAY_LENGTH) + 1;
    std::string clock = (hour < 10 ? "0" : "") + std::to_string(hour) + ":" + (minute < 10 ? "0" : "") + std::to_string(minute);
    bool isDay = hour >= 6 && hour < 18;

    int bw = 190, bh = 58;
    int x = gW - bw - 12, y = gFullscreen ? KEYBAR_H + 10 : 12;
    fillRect(x, y, bw, bh, 0xD0101018);
    outlineRect(x, y, bw, bh, RGBX(200, 180, 90));
    fillEllipse(x + 28, y + 29, 12, 12, isDay ? RGBX(255, 214, 80) : RGBX(200, 210, 235));
    if (!isDay) fillEllipse(x + 33, y + 25, 10, 10, RGBX(16, 16, 24));   // moon crescent
    drawText(x + 54, y + 8, "DAY " + std::to_string(dayNum), 2, RGBX(255, 255, 255));
    drawText(x + 54, y + 32, clock, 2, RGBX(255, 230, 120));
    drawText(x + 54, y + 46, WEATHER_NAMES[gWeather], 1, RGBX(160, 200, 255));
}

static int foodTotal() {
    int t = 0;
    for (int i = 0; i < T_COUNT; i++) if (isFood(i) && i != I_MEDICINE) t += gCount[i];
    return t;
}
static int materialTotal() {
    int t = 0;
    for (int i = 0; i < T_COUNT; i++) if (itemCategory(i) == 2) t += gCount[i];
    return t;
}
static int weaponTotal() {
    int t = 0;
    for (int i = 0; i < T_COUNT; i++) if (itemCategory(i) == 0) t += gCount[i];
    return t;
}

static int nearestEmpire() {
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < gEmpires.size(); i++) {
        if (gEmpires[i].state != 0) continue;
        float dx = gEmpires[i].x - gP.x, dy = gEmpires[i].y - gP.y;
        float d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = (int)i; }
    }
    return best;
}

static void updateChestUI() {
    if (!gChestOpen) return;
    ChestData* c = chestAt(gChestCell);
    float cx = gChestCell % MAP_W + 0.5f, cy = gChestCell / MAP_W + 0.5f;
    float dx = gP.x - cx, dy = gP.y - cy;
    if (!c || dx * dx + dy * dy > 6.25f) { gChestOpen = false; return; }
    for (int k = 0; k < 10; k++) if (gKeyEdge[k == 9 ? '0' : '1' + k]) chestTake(c, k);
    if (gKeyEdge['T']) chestDeposit(c);
    if (gKeyEdge['C']) gChestOpen = false;
}

static void updateEmpireUI() {
    if (gKeyEdge['K'] && !gChestOpen && !gCookOpen && !gCraftOpen) gEmpirePanel = !gEmpirePanel;
    if (!gEmpirePanel) return;
    if (gKeyEdge[VK_RETURN] && !gEmpireFounded) {
        if (foodTotal() >= 250 && materialTotal() >= 500 && weaponTotal() >= 8) {
            gEmpireFounded = true;
            addPopup("Your empire is founded! Up to 100 friends and 25 pets.", RGBX(255, 230, 120));
        } else {
            setDialog("You need 250 food, 500 materials and 8 weapons to found an empire.");
        }
    }
    if (gKeyEdge['1'] && gEmpireFounded) {
        if (gRaidActive) setDialog("A raid is already underway.");
        else {
            int idx = nearestEmpire();
            if (idx < 0) setDialog("No rival empires are left to raid.");
            else startRaid(idx);
        }
    }
}

static void drawEmpirePanel() {
    int w = std::min(640, gW - 40), h = 330;
    int x = (gW - w) / 2, y = std::max(KEYBAR_H + 10, (gH - h) / 2);
    fillRect(x, y, w, h, 0xF0101018);
    outlineRect(x, y, w, h, RGBX(200, 180, 90));
    drawText(x + 20, y + 16, "EMPIRE", 3, RGBX(255, 230, 120));
    int f = foodTotal(), m = materialTotal(), wp = weaponTotal();
    drawText(x + 20, y + 70, "FOOD       " + std::to_string(f) + " / 250", 2, f >= 250 ? RGBX(120, 230, 120) : RGBX(170, 170, 170));
    drawText(x + 20, y + 100, "MATERIALS  " + std::to_string(m) + " / 500", 2, m >= 500 ? RGBX(120, 230, 120) : RGBX(170, 170, 170));
    drawText(x + 20, y + 130, "WEAPONS    " + std::to_string(wp) + " / 8", 2, wp >= 8 ? RGBX(120, 230, 120) : RGBX(170, 170, 170));
    if (!gEmpireFounded) {
        drawText(x + 20, y + 175, "PRESS ENTER TO FOUND YOUR EMPIRE", 2, RGBX(255, 255, 255));
    } else {
        drawText(x + 20, y + 175, "EMPIRE FOUNDED: 100 FRIENDS, 25 PETS", 2, RGBX(255, 230, 120));
        int idx = nearestEmpire();
        if (gRaidActive) {
            drawText(x + 20, y + 205, "RAID IN PROGRESS: DEFEAT THE DEFENDERS", 2, RGBX(255, 140, 120));
        } else if (idx >= 0) {
            int dist = (int)std::sqrt((gEmpires[idx].x - gP.x) * (gEmpires[idx].x - gP.x) + (gEmpires[idx].y - gP.y) * (gEmpires[idx].y - gP.y));
            drawText(x + 20, y + 205, "NEAREST RIVAL EMPIRE: " + std::to_string(dist) + " TILES", 2, RGBX(255, 255, 255));
            drawText(x + 20, y + 235, "PRESS 1 TO RAID IT (DEFENDERS: " + std::to_string(raidDefenders(gEmpires[idx])) + ")", 2, RGBX(255, 220, 120));
        } else {
            drawText(x + 20, y + 205, "NO RIVAL EMPIRES LEFT", 2, RGBX(255, 255, 255));
        }
    }
    drawText(x + 20, y + h - 26, "K CLOSE", 1, RGBX(180, 180, 180));
}

static void drawChestUI() {
    ChestData* c = chestAt(gChestCell);
    if (!c) return;
    int w = std::min(560, gW - 40), h = 60 + 10 * 26 + 40;
    int x = (gW - w) / 2, y = std::max(KEYBAR_H + 10, (gH - h) / 2);
    fillRect(x, y, w, h, 0xF0101018);
    outlineRect(x, y, w, h, RGBX(200, 180, 90));
    drawText(x + 16, y + 14, "CHEST  1-0 TAKE  T STORE SELECTED  C CLOSE", 2, RGBX(255, 230, 120));
    for (int k = 0; k < 10; k++) {
        std::string label = std::string(k == 9 ? "0" : std::to_string(k + 1)) + ") ";
        label += c->n[k] > 0 ? itemName(c->item[k]) + " x" + std::to_string(c->n[k]) : std::string("(empty)");
        drawText(x + 16, y + 50 + k * 26, label, 2, c->n[k] > 0 ? RGBX(230, 230, 230) : RGBX(120, 120, 120));
    }
}

// ---------- inventory: 40 slots, each item type takes one slot (stacks are unlimited) ----------
static const int INV_COLS = 8, INV_ROWS = 5, INV_SLOT = 88;

static void invGeometry(int& px, int& py, int& pw, int& ph) {
    pw = INV_COLS * INV_SLOT + 32;
    ph = INV_ROWS * INV_SLOT + 110;
    px = (gW - pw) / 2;
    py = std::max(KEYBAR_H + 10, (gH - ph) / 2);
}

static int inventorySlotAt(int mx, int my) {
    int px, py, pw, ph;
    invGeometry(px, py, pw, ph);
    int gx = px + 16, gy = py + 60;
    if (mx < gx || my < gy) return -1;
    int col = (mx - gx) / INV_SLOT, row = (my - gy) / INV_SLOT;
    if (col >= INV_COLS || row >= INV_ROWS) return -1;
    return row * INV_COLS + col;
}

// Items beyond the 40th slot fall to the ground next to the player.
static void enforceSlotCap() {
    std::vector<int> v = ownedItems();
    if ((int)v.size() <= INV_SLOTS) return;
    for (size_t k = INV_SLOTS; k < v.size(); k++) {
        gPendingDrops.push_back({ gP.x + 1.5f, gP.y, v[k], gCount[v[k]] });
        gCount[v[k]] = 0;
    }
    addPopup("Inventory full: extra items dropped", RGBX(255, 160, 120));
}

static int hotbarSlotAt(int mx, int my) {
    int slotW = std::clamp((gW - 40) / HB_SLOTS, 64, 120);
    int x0 = (gW - slotW * HB_SLOTS) / 2, y0 = hotbarTop();
    if (my < y0 || my >= y0 + 84) return -1;
    int k = (mx - x0) / slotW;
    return (mx < x0 || k >= HB_SLOTS) ? -1 : k;
}

static bool insideInventory(int mx, int my) {
    int px, py, pw, ph;
    invGeometry(px, py, pw, ph);
    return mx >= px && mx < px + pw && my >= py && my < py + ph;
}

static void inventoryInput() {
    if (gClickPending) {
        gClickPending = false;
        int inv = inventorySlotAt(gClickX, gClickY);
        int hb = hotbarSlotAt(gClickX, gClickY);
        std::vector<int> v = ownedItems();
        if (inv >= 0 && inv < (int)v.size()) {
            gDragItem = v[inv]; gDragFrom = -1; gDragging = true;
            gSelItem = v[inv];
        } else if (hb >= 0 && gHotbar[hb] >= 0) {
            gDragItem = gHotbar[hb]; gDragFrom = hb; gDragging = true;
        }
    }
    if (gReleasePending) {
        gReleasePending = false;
        if (gDragging) {
            int target = hotbarSlotAt(gMouseX, gMouseY);
            if (target >= 0) {
                int old = gHotbar[target];
                if (gDragFrom >= 0) gHotbar[gDragFrom] = old;          // swap with the source slot
                for (int j = 0; j < HB_SLOTS; j++) if (gHotbar[j] == gDragItem && j != target) gHotbar[j] = -1;
                gHotbar[target] = gDragItem;
            } else if (gDragFrom >= 0 && !insideInventory(gMouseX, gMouseY)) {
                gHotbar[gDragFrom] = -1;                                // dragged out of the bar
            }
            gDragging = false;
        }
    }
    if (gKeyEdge[VK_BACK]) throwSelected(down(VK_CONTROL));
}

static void drawInventory() {
    int px, py, pw, ph;
    invGeometry(px, py, pw, ph);
    fillRect(px, py, pw, ph, 0xF0101018);
    outlineRect(px, py, pw, ph, RGBX(200, 180, 90));
    std::vector<int> v = ownedItems();
    drawText(px + 16, py + 14, "INVENTORY  " + std::to_string(std::min((int)v.size(), INV_SLOTS)) + "/40 SLOTS", 2, RGBX(255, 230, 120));
    drawText(px + 16, py + ph - 26, "I CLOSE   CLICK A SLOT TO SELECT   BKSP THROW", 1, RGBX(200, 200, 200));
    for (int k = 0; k < INV_SLOTS; k++) {
        int x = px + 16 + (k % INV_COLS) * INV_SLOT, y = py + 60 + (k / INV_COLS) * INV_SLOT;
        fillRect(x + 4, y + 4, INV_SLOT - 8, INV_SLOT - 8, 0xFF181818);
        if (k >= (int)v.size()) continue;
        int it = v[k];
        drawIcon(x + (INV_SLOT - 40) / 2, y + 8, 40, it);
        std::string count = std::to_string(gCount[it]);
        drawText(x + INV_SLOT - 10 - textWidth(count, 2), y + 8, count, 2, RGBX(255, 255, 255));
        std::string name = itemName(it);
        if (name.size() > 12) name = name.substr(0, 12);
        drawText(x + 8, y + 58, name, 1, RGBX(230, 230, 230));
        if (it == gSelItem) outlineRect(x + 2, y + 2, INV_SLOT - 4, INV_SLOT - 4, RGBX(255, 230, 80));
    }
}

static void drawBeasts(float camX, float camY) {
    for (size_t i = 0; i < gBeasts.size(); i++) {
        const Beast& b = gBeasts[i];
        int sx = (int)(b.x * TILE - camX) - 32, sy = (int)(b.y * TILE - camY) - 32;
        int f = ((int)(gTime * 5.0f + b.x * 2.0f)) % 2;
        blitAlpha(sx, sy, gBeastSpr[b.kind][b.face][f]);
        if (b.tame && b.name[0]) {
            std::string nm = b.name;
            drawText(sx + 32 - textWidth(nm, 1) / 2, sy - 6, nm, 1, RGBX(255, 240, 180));
        }
    }
}

static void drawFurnaceUI() {
    Furnace* f = furnaceAt(gFurnCell);
    if (!f) return;
    int w = std::min(560, gW - 40), h = 230;
    int x = (gW - w) / 2, y = std::max(KEYBAR_H + 10, (gH - h) / 2);
    fillRect(x, y, w, h, 0xF0101018);
    outlineRect(x, y, w, h, RGBX(200, 180, 90));
    drawText(x + 16, y + 14, "FURNACE  T ORE  G WOOD FUEL  Y COLLECT  C CLOSE", 2, RGBX(255, 230, 120));
    std::string in = f->inN > 0 ? "INPUT: " + itemName(f->inItem) + " x" + std::to_string(f->inN) : std::string("INPUT: (empty)");
    std::string out = f->outN > 0 ? "OUTPUT: " + itemName(f->outItem) + " x" + std::to_string(f->outN) : std::string("OUTPUT: (empty)");
    std::string fuel = "FUEL: " + std::to_string(f->fuel) + " SMELTS LEFT";
    std::string prog = f->inN > 0 ? "SMELTING " + std::to_string((int)(f->prog / 3.0f * 100.0f)) + "%" : std::string("IDLE");
    drawText(x + 16, y + 56, in, 2, RGBX(230, 230, 230));
    drawText(x + 16, y + 92, out, 2, RGBX(230, 230, 230));
    drawText(x + 16, y + 128, fuel, 2, RGBX(255, 190, 120));
    drawText(x + 16, y + 164, prog, 2, RGBX(160, 230, 160));
}

static u32 groundMapColor(int g) {
    switch (g) {
        case G_WATER: return RGBX(40, 100, 180);
        case G_SAND: return RGBX(220, 200, 140);
        case G_GRASS: return RGBX(100, 170, 80);
        case G_FOREST: return RGBX(50, 100, 45);
        case G_DIRT: return RGBX(140, 100, 65);
        case G_TILLED: return RGBX(100, 70, 45);
        case G_STONE: return RGBX(130, 130, 138);
        case G_SNOW: return RGBX(240, 245, 250);
        case G_DESERT: return RGBX(225, 190, 115);
        case G_SWAMP: return RGBX(70, 95, 60);
        case G_JUNGLE: return RGBX(30, 90, 40);
        case G_MEADOW: return RGBX(140, 200, 100);
        case G_BADLANDS: return RGBX(190, 110, 70);
        case G_CAVE: return RGBX(60, 60, 80);
        default: return RGBX(80, 80, 80);
    }
}

// A small overview of the area around you: terrain, buildings, friends and pets.
static void drawMinimap() {
    const int S = 150;
    int x0 = gW - S - 14, y0 = (gFullscreen ? KEYBAR_H + 10 : 12) + 100;
    fillRect(x0 - 2, y0 - 2, S + 4, S + 4, RGBX(20, 20, 26));
    int pcx = (int)floorf(gP.x), pcy = (int)floorf(gP.y);
    for (int py = 0; py < S; py++) for (int px = 0; px < S; px++) {
        int tx = pcx + px - S / 2, ty = pcy + py - S / 2;
        u32 c = RGBX(10, 10, 14);
        if (inB(tx, ty)) {
            const Cell& cl = cellAt(tx, ty);
            c = groundMapColor(cl.ground);
            if (cl.obj == O_TREE) c = RGBX(30, 70, 30);
            else if (cl.obj == O_ROCK) c = RGBX(160, 160, 165);
            else if (cl.obj >= O_ORE_COPPER && cl.obj <= O_ORE_OBSIDIAN) c = RGBX(230, 200, 90);
            else if (cl.obj == O_PLANK || cl.obj == O_BRICK || cl.obj == O_DOOR_CLOSED) c = RGBX(170, 120, 70);
        }
        setpx(x0 + px, y0 + py, c);
    }
    for (const Person& p : gPeople) {
        int px = (int)(p.x - gP.x) + S / 2, py = (int)(p.y - gP.y) + S / 2;
        if (px >= 0 && py >= 0 && px < S && py < S) fillRect(x0 + px - 1, y0 + py - 1, 3, 3, p.isFriend ? RGBX(255, 210, 110) : RGBX(230, 230, 240));
    }
    for (const Pet& q : gPets) {
        int px = (int)(q.x - gP.x) + S / 2, py = (int)(q.y - gP.y) + S / 2;
        if (px >= 0 && py >= 0 && px < S && py < S) fillRect(x0 + px - 1, y0 + py - 1, 3, 3, RGBX(255, 240, 180));
    }
    fillRect(x0 + S / 2 - 2, y0 + S / 2 - 2, 5, 5, RGBX(255, 255, 255));
    outlineRect(x0 - 2, y0 - 2, S + 4, S + 4, RGBX(200, 180, 90));
}

static void drawKeybar() {
    // All controls on one line, at the largest text size that fits the window.
    fillRect(0, 0, gW, KEYBAR_H, 0xF0000000);
    const std::string binds = "WASD MOVE SHIFT RUN J JUMP E USE SPACE ATTACK R PLACE Q EAT F GIFT I INVENTORY C CRAFT K EMPIRE M MAP 1-0 HOTBAR BKSP THROW CTRL+S SETTINGS F5 SAVE F9 LOAD F11 FULLSCREEN ESC MENU";
    int sc = textWidth(binds, 2) <= gW - 24 ? 2 : 1;
    int y = (KEYBAR_H - 5 * sc) / 2;
    drawText(12, y, binds, sc, RGBX(240, 236, 200));
}

static void drawHotbar() {
    int slotW = std::clamp((gW - 40) / HB_SLOTS, 64, 120);
    int total = slotW * HB_SLOTS;
    int x0 = (gW - total) / 2;
    int y0 = hotbarTop();
    int maxChars = (slotW - 10) / 8;
    drawText(x0, y0 - 16, "HOTBAR   1-0 SELECT   I INVENTORY (DRAG ITEMS IN)", 2, RGBX(200, 200, 200));
    for (int k = 0; k < HB_SLOTS; k++) {
        int x = x0 + k * slotW;
        fillRect(x + 2, y0, slotW - 4, 84, 0xFF181818);
        drawText(x + 6, y0 + 6, std::to_string((k + 1) % 10), 2, RGBX(140, 140, 140));
        int it = slotItem(k);
        if (it >= 0) {
            drawIcon(x + (slotW - 40) / 2, y0 + 8, 40, it);
            std::string count = std::to_string(gCount[it]);
            drawText(x + slotW - 8 - textWidth(count, 2), y0 + 6, count, 2, 0xFFFFFFFF);
            std::string name = itemName(it);
            if ((int)name.size() > maxChars) name = name.substr(0, maxChars);
            drawText(x + 6, y0 + 58, name, 1, 0xFFFFFFFF);
            if (it == gSelItem) outlineRect(x, y0, slotW, 84, RGBX(255, 230, 80));
        }
    }
}

static const char* craftKeyLabel(int r) {
    static const char* L[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Z", "V", "G", "T", "Y", "H" };
    return r < 18 ? L[r] : "";
}
static int craftKeyVk(int r) {
    static const int V[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', VK_OEM_MINUS, VK_OEM_PLUS,
                             'Z', 'V', 'G', 'T', 'Y', 'H' };
    return r < 18 ? V[r] : 0;
}

static void drawCraftMenu() {
    const int perPage = 18;
    int pages = (NREC + perPage - 1) / perPage;
    int w = std::min(760, gW - 40), h = 60 + perPage * 26 + 60;
    int x = (gW - w) / 2, y = std::max(KEYBAR_H + 10, gH / 2 - h / 2 - 40);
    fillRect(x, y, w, h, 0xE0101018);
    outlineRect(x, y, w, h, RGBX(200, 180, 90));
    int page = std::min(gCraftPage, pages - 1);
    drawText(x + 16, y + 14, "CRAFTING  C CLOSE  [ ] PAGE " + std::to_string(page + 1) + "/" + std::to_string(pages), 2, RGBX(255, 230, 120));
    int base = page * perPage;
    for (int r = 0; r < perPage && base + r < NREC; r++) {
        const Recipe& rc = RECIPES[base + r];
        bool ok = canCraft(rc);
        std::string line = std::string(craftKeyLabel(r)) + ") " + itemName(rc.out);
        if (rc.outN > 1) line += " X" + std::to_string(rc.outN);
        line += "  -  ";
        for (int k = 0; k < rc.inCount; k++) {
            if (k) line += ", ";
            line += std::to_string(rc.in[k].n) + " " + itemName(rc.in[k].item);
        }
        if (rc.tool) line += " (ONE)";
        drawText(x + 16, y + 52 + r * 26, line, 2, ok ? RGBX(120, 230, 120) : RGBX(150, 150, 150));
    }
    drawText(x + 16, y + h - 26, "MATERIALS: WOOD " + std::to_string(gCount[I_WOOD]) +
             "  STONE " + std::to_string(gCount[I_STONE]) + "  PLANKS " + std::to_string(gCount[I_PLANK]), 1, 0xFFDDDDDD);
}

static void drawCookMenu() {
    std::vector<int> codes = cookableCodes();
    int rows = std::clamp((int)codes.size(), 1, 12);
    int w = std::min(620, gW - 40), h = 150 + rows * 24;
    int x = (gW - w) / 2, y = std::max(KEYBAR_H + 10, gH / 2 - h / 2 - 40);
    fillRect(x, y, w, h, 0xE6181010);
    outlineRect(x, y, w, h, RGBX(230, 140, 60));
    drawText(x + 20, y + 14, "COOKING  (ENTER COOK  X CLEAR  C CLOSE)", 2, RGBX(255, 190, 110));

    std::string pot = "POT:";
    bool any = false;
    for (int k = 0; k < 3; k++) if (gPot[k]) { pot += " " + codeName(gPot[k]); any = true; }
    if (!any) pot += " (EMPTY)";
    drawText(x + 20, y + 44, pot, 2, RGBX(255, 255, 255));
    if (any) {
        std::vector<int> pc;
        for (int k = 0; k < 3; k++) if (gPot[k]) pc.push_back(gPot[k]);
        drawText(x + 20, y + 66, "WILL MAKE: " + dishName(dishKey(pc)), 2, RGBX(160, 240, 140));
    }
    if (codes.empty()) drawText(x + 20, y + 100, "YOU HAVE NO FOOD INGREDIENTS (CROPS, FISH)", 2, RGBX(200, 160, 160));
    int base = gCookPage * 12;
    for (int k = 0; k < 12 && base + k < (int)codes.size(); k++) {
        int code = codes[base + k];
        std::string label = std::string(craftKeyLabel(k)) + ") " + codeName(code) +
                            "  x" + std::to_string(gCount[codeItem(code)]);
        drawText(x + 20, y + 100 + k * 24, label, 2, codeColor(code));
    }
}

static std::string actionHint(u32& color) {
    color = 0xFFFFFFFF;
    float fx = gP.x + DXS[gP.dir] * 0.9f, fy = gP.y + DYS[gP.dir] * 0.9f;
    for (const Npc& n : gNpcs) {
        float dx = n.x - fx, dy = n.y - fy;
        if (dx * dx + dy * dy < 0.9f * 0.9f) return std::string("PRESS E TO TALK TO ") + NPC_NAMES[n.kind];
    }
    for (const Person& p : gPeople) {
        float dx = p.x - fx, dy = p.y - fy;
        if (dx * dx + dy * dy < 0.81f) return std::string(p.isFriend ? "PRESS E TO CHAT WITH " : "PRESS E TO MEET ") + p.name + "  |  F TO GIVE A GIFT";
    }
    for (const Pet& p : gStrays) {
        float dx = p.x - fx, dy = p.y - fy;
        if (dx * dx + dy * dy < 0.81f) return "PRESS E TO TAME (SELECT FOOD FIRST)";
    }
    for (const Pet& p : gPets) {
        float dx = p.x - fx, dy = p.y - fy;
        if (dx * dx + dy * dy < 0.81f) return std::string("PRESS E TO PLAY WITH ") + p.name;
    }
    int tx = (int)floorf(fx), ty = (int)floorf(fy);
    if (!inB(tx, ty)) return "PRESS C TO CRAFT  |  Q TO EAT";
    const Cell& c = cellAt(tx, ty);
    if (c.ground == G_WATER) {
        if (gCount[I_ROD] > 0) return "PRESS E TO FISH";
        color = RGBX(255, 200, 120);
        return "NEED A FISHING ROD - PRESS C TO CRAFT";
    }
    if (c.obj == O_CAMPFIRE) return "PRESS E TO COOK  |  SHIFT+E TO PICK UP";
    if (c.obj >= O_ORE_COPPER && c.obj <= O_ORE_OBSIDIAN) return std::string("PRESS E TO MINE ") + METAL_NAME[c.obj - O_ORE_COPPER] + " ORE";
    if (c.obj == O_LADDER) return gUnderground ? "PRESS J TO CLIMB UP TO THE SURFACE" : "A LADDER";
    if (c.ground == G_CAVE && !gUnderground) return "PRESS J TO JUMP DOWN INTO THE CAVE";
    if (c.obj == O_CHEST) return "PRESS E TO OPEN CHEST  |  SHIFT+E TO PICK UP";
    if (c.obj == O_FURNACE) return "PRESS E TO USE FURNACE  |  SHIFT+E TO PICK UP";
    if (c.obj == O_DOOR_CLOSED || c.obj == O_DOOR_OPEN) return "DOORS OPEN WHEN YOU WALK UP  |  SHIFT+E TO PICK UP";
    if (c.obj == O_BED) return "PRESS E TO SLEEP (NIGHT)  |  SHIFT+E TO PICK UP";
    if (c.obj == O_TORCH) return "PRESS E TO TAKE TORCH";
    if (isSwimming() && c.ground == G_WATER) return "YOU ARE SWIMMING - WALK TO THE SHORE";
    if (c.obj == O_PLANK) return "PRESS E TO BREAK PLANK";
    if (c.obj == O_BRICK) return "PRESS E TO BREAK BRICK";
    if (c.obj == O_TREE) return gCount[I_AXE] > 0 ? "PRESS E TO CHOP TREE" : "PRESS E TO CHOP TREE BY HAND (CRAFT AN AXE FOR MORE)";
    if (c.obj == O_ROCK) return gCount[I_PICKAXE] > 0 ? "PRESS E TO MINE ROCK" : "PRESS E TO MINE ROCK BY HAND (CRAFT A PICKAXE FOR MORE)";
    if (c.crop) {
        const char* nm = CROPS[c.crop - 1].name;
        if (cropStage(c) >= 3) return std::string("PRESS E TO HARVEST ") + nm;
        return std::string(nm) + " IS GROWING...";
    }
    if (c.ground == G_TILLED) {
        if (gSelItem >= I_SEED && gSelItem < I_PROD && gCount[gSelItem] > 0) return "PRESS E TO PLANT " + itemName(gSelItem);
        return "SELECT A SEED IN THE HOTBAR (1-0) TO PLANT";
    }
    if (c.obj == O_NONE && gSelItem >= 0 && gSelItem < I_COUNT && isPlaceable(gSelItem) && gCount[gSelItem] > 0)
        return "PRESS R TO PLACE " + itemName(gSelItem);
    if (c.ground == G_GRASS || c.ground == G_DIRT || c.ground == G_FOREST || c.ground == G_SWAMP ||
        c.ground == G_JUNGLE || c.ground == G_MEADOW || c.ground == G_BADLANDS) {
        return gCount[I_HOE] > 0 ? "PRESS E TO TILL SOIL" : "PRESS E TO SEARCH FOR SEEDS (CRAFT A HOE TO TILL)";
    }
    return "PRESS C TO CRAFT  |  Q TO EAT  |  SPACE TO ATTACK";
}

static void drawHint() {
    u32 color;
    std::string hint = actionHint(color);
    int w = textWidth(hint, 2) + 28;
    int x = (gW - w) / 2, y = hotbarTop() - 46;
    fillRect(x, y, w, 26, 0xC0000000);
    drawText(x + 14, y + 8, hint, 2, color);
    if (gDialogT > 0 && !gDialog.empty()) {
        int dw = std::min(gW - 40, textWidth(gDialog, 2) + 28);
        int dx = (gW - dw) / 2, dy = y - 36;
        fillRect(dx, dy, dw, 28, 0xE0202030);
        outlineRect(dx, dy, dw, 28, RGBX(230, 200, 120));
        drawText(dx + 14, dy + 8, gDialog, 2, RGBX(255, 240, 200));
    }
}

static void drawPopups() {
    int baseY = gH / 2 - 70;
    int idx = 0;
    for (const Popup& p : gPopups) {
        float a = std::clamp(p.life / 0.5f, 0.0f, 1.0f);
        float rise = (1.4f - p.life) * 40.0f;
        int r, g, b; unpack(p.col, r, g, b);
        u32 col = mk((int)(r * a), (int)(g * a), (int)(b * a));
        int w = textWidth(p.text, 2);
        drawText(gW / 2 - w / 2, baseY - idx * 22 - (int)rise, p.text, 2, col);
        idx++;
    }
}

static void drawAnimals(float camX, float camY);
static void drawCompanions(float camX, float camY);
static void drawSettings();
static void drawTornados(float camX, float camY);
static void drawTorch(int sx, int sy, int tx) {
    fillRect(sx + 30, sy + 34, 4, 26, RGBX(110, 74, 40));
    float flick = sinf(gTime * 14.0f + tx * 2.3f) * 0.12f;
    fillEllipse((float)(sx + 32), (float)(sy + 30), 6.0f + flick * 6.0f, 9.0f + flick * 5.0f, RGBX(255, 150, 40));
    fillEllipse((float)(sx + 32), (float)(sy + 32), 3.0f, 5.0f, RGBX(255, 240, 150));
    gLights.push_back({ (float)(sx + 32), (float)(sy + 30) });
}

static void render() {
    if (gScreen != SCR_GAME) { drawMenu(); return; }
    gLights.clear();
    float camX = gP.x * TILE - gW * 0.5f;
    float camY = gP.y * TILE - gH * 0.5f;
    int ix = (int)camX, iy = (int)camY;
    int tx0 = (int)floorf(camX / TILE) - 1, ty0 = (int)floorf(camY / TILE) - 1;
    int tx1 = (int)floorf((camX + gW) / TILE) + 1, ty1 = (int)floorf((camY + gH) / TILE) + 1;

    for (int ty = ty0; ty <= ty1; ty++) for (int tx = tx0; tx <= tx1; tx++) {
        int sx = tx * TILE - ix, sy = ty * TILE - iy;
        if (!inB(tx, ty)) { blitOpaque(sx, sy, gTex[G_WATER], true); continue; }
        const Cell& c = cellAt(tx, ty);
        blitOpaque(sx, sy, gTex[c.ground], c.ground == G_WATER);
        if (c.crop) {
            int sway = (CROPS[c.crop - 1].style == 1) ? 0 : (int)lroundf(sinf(gTime * 2.2f + tx * 0.7f + ty * 0.4f));
            blitAlpha(sx, sy, gCropSpr[c.crop - 1][cropStage(c)], sway);
        }
        if (c.obj == O_TREE) {
            if (gSet.turbo) fillEllipse((float)(sx + 32), (float)(sy + 58), 14.0f, 4.0f, RGBX(28, 46, 30));   // soft shadow
            int sway = (int)lroundf(sinf(gTime * 1.4f + tx * 0.9f + ty * 0.5f) * 1.4f);
            blitAlpha(sx, sy, ((tx * 73 + ty * 151) & 3) == 0 ? gPineSpr : gTreeSpr, sway);
        } else if (c.obj == O_ROCK) {
            blitAlpha(sx, sy, gRockSpr);
        } else if (c.obj == O_PLANK) {
            blitOpaque(sx, sy, gPlankTex, false);
        } else if (c.obj == O_BRICK) {
            blitOpaque(sx, sy, gBrickTex, false);
        } else if (c.obj >= O_ORE_COPPER && c.obj <= O_ORE_OBSIDIAN) {
            blitOpaque(sx, sy, gOreSpr[c.obj - O_ORE_COPPER], false);
        } else if (c.obj == O_FURNACE) {
            blitAlpha(sx, sy, gFurnaceSpr);
            Furnace* fu = furnaceAt(ty * MAP_W + tx);
            if (fu && fu->inN > 0 && fu->fuel > 0) {
                float flick = sinf(gTime * 13.0f + tx * 3.1f) * 0.12f;
                fillEllipse((float)(sx + 32), (float)(sy + 46), 7.0f + flick * 8.0f, 9.0f + flick * 6.0f, RGBX(255, 150, 40));
                gLights.push_back({ (float)(sx + 32), (float)(sy + 44) });
            }
        } else if (c.obj == O_LADDER) {
            blitAlpha(sx, sy, gLadderSpr);
        } else if (c.obj == O_CHEST) {
            blitAlpha(sx, sy, gChestSpr);
        } else if (c.obj == O_DOOR_CLOSED || c.obj == O_DOOR_OPEN) {
            blitAlpha(sx, sy, c.obj == O_DOOR_OPEN ? gDoorOpenSpr : gDoorSpr);
        } else if (c.obj == O_BED) {
            blitAlpha(sx, sy, gBedSpr);
        } else if (c.obj == O_TORCH) {
            drawTorch(sx, sy, tx);
        } else if (c.obj == O_CAMPFIRE) {
            blitAlpha(sx, sy, gCampfireSpr);
            gLights.push_back({ (float)(sx + 32), (float)(sy + 36) });
            float flick = sinf(gTime * 13.0f + tx * 3.1f) * 0.12f;
            fillEllipse((float)(sx + 32), (float)(sy + 36), 9.0f + flick * 12.0f, 15.0f + flick * 8.0f, RGBX(255, 140, 40));
            fillEllipse((float)(sx + 32), (float)(sy + 40), 5.0f + flick * 6.0f, 9.0f + flick * 5.0f, RGBX(255, 235, 130));
        }
    }

    int ftx = (int)floorf(gP.x + DXS[gP.dir] * 0.9f), fty = (int)floorf(gP.y + DYS[gP.dir] * 0.9f);
    outlineRect(ftx * TILE - ix, fty * TILE - iy, TILE, TILE, 0xFFFFFFFF);

    drawDrops(camX, camY);

    // Monsters.
    for (const Monster& m : gMonsters) {
        int sx = (int)(m.x * TILE - camX) - 32, sy = (int)(m.y * TILE - camY) - 32;
        const u32* spr;
        int oy = 0;
        if (m.type == 0) {
            float ph = fabsf(sinf(gTime * 3.0f + m.x));       // hop phase: squashed on the ground, stretched in the air
            spr = gSlimeSpr[ph < 0.2f ? 0 : (ph > 0.8f ? 1 : 2)];
            oy = -(int)(ph * 10.0f);
        } else {
            int wf = ((int)(gTime * 6.0f + m.x * 3.0f)) % 4;
            spr = m.type == 2 ? gNpcSpr[1][m.dir][wf] : gZombieSpr[m.dir][wf];
        }
        if (m.flash > 0) blitAlphaTint(sx, sy + oy, spr, 0.6f);
        else blitAlpha(sx, sy + oy, spr);
        float frac = std::clamp(m.hp / MON_MAXHP[m.type], 0.0f, 1.0f);
        fillRect(sx + 16, sy + 2, 32, 4, 0xFF300000);
        fillRect(sx + 16, sy + 2, (int)(32 * frac), 4, 0xFF40E040);
    }

    if (!gUnderground) { drawAnimals(camX, camY); drawTornados(camX, camY); }
    drawCompanions(camX, camY);
    if (!gUnderground) drawBeasts(camX, camY);

    // Villagers.
    for (const Npc& n : gNpcs) {
        int sx = (int)(n.x * TILE - camX) - 32, sy = (int)(n.y * TILE - camY) - 32;
        int frame = (((int)(gTime * 1.5f)) % 2) ? 2 : 0;
        blitAlpha(sx, sy, gNpcSpr[n.pal][0][frame]);
        std::string nm = NPC_NAMES[n.kind];
        drawText(sx + 32 - textWidth(nm, 1) / 2, sy - 6, nm, 1, RGBX(255, 255, 255));
    }

    // Player.
    int frame = gMoving ? ((int)(gWalkT * 8.0f) % 4) : (((int)(gTime * 1.5f)) % 2 ? 2 : 0);
    if (gSwingT > 0) frame = 4;
    int px = gW / 2 - TILE / 2, py = gH / 2 - TILE / 2 + (gMoving && frame % 2 == 1 ? -1 : 0) - (int)(gZ * 28.0f);
    bool hurt = gHurtT > 0 && ((int)(gTime * 20.0f) % 2 == 0);
    if (!hurt) {
        if (isSwimming()) {
            fillEllipse(gW * 0.5f, gH * 0.5f + 24.0f, 28.0f, 8.0f, RGBX(120, 180, 230));
            fillEllipse(gW * 0.5f, gH * 0.5f + 24.0f, 20.0f, 5.0f, RGBX(40, 100, 180));
        }
        blitAlpha(px, py + (isSwimming() ? 6 : 0), gPlayerSpr[gP.dir][frame]);
    }
    if (gSwingT > 0) {
        float cx = gW * 0.5f, cy = gH * 0.5f;
        float prog = 1.0f - gSwingT / 0.2f;                  // 0 at the start of the swing, 1 at the end
        const WeaponDef* w = bestWeapon();
        u32 blade = w ? itemColor(w->item) : RGBX(235, 235, 245);
        float base = gP.dir == 0 ? -PI / 2 : gP.dir == 2 ? PI / 2 : gP.dir == 1 ? 0.0f : PI;
        if (w && w->item == I_SPEAR) {
            float len = 18.0f + 30.0f * sinf(prog * PI);     // a thrust forward
            drawLine(cx + DXS[gP.dir] * 18.0f, cy + DYS[gP.dir] * 18.0f,
                     cx + DXS[gP.dir] * (18.0f + len), cy + DYS[gP.dir] * (18.0f + len), 4, blade);
        } else {
            for (int k = 0; k < 4; k++) {                    // a sweeping arc with a fading trail
                float pk = prog - k * 0.12f;
                if (pk < 0.0f) continue;
                float ang = base + (-1.1f + 2.2f * pk);
                float x0 = cx + cosf(ang) * 16.0f, y0 = cy + sinf(ang) * 16.0f;
                float x1 = cx + cosf(ang) * 44.0f, y1 = cy + sinf(ang) * 44.0f;
                int r, g, b2; unpack(blade, r, g, b2);
                float fade = 1.0f - k * 0.25f;
                u32 col = mk((int)(r * fade), (int)(g * fade), (int)(b2 * fade));
                drawLine(x0, y0, x1, y1, k == 0 ? 5 : 3, col);
            }
        }
    }

    applyLighting();

    for (const Particle& p : gParticles) fillRect((int)p.x, (int)p.y, 2, 2, p.col);

    drawStatus();
    drawClock();
    drawHotbar();
    drawHint();
    if (gMinimap) drawMinimap();
    drawPopups();
    if (gFullscreen) drawKeybar();
    if (gCraftOpen) drawCraftMenu();
    if (gCookOpen) drawCookMenu();
    if (gChestOpen) drawChestUI();
    if (gFurnOpen) drawFurnaceUI();
    if (gEmpirePanel) drawEmpirePanel();
    if (gSet.showFps) {
        int x = gW - 120, y = (gFullscreen ? KEYBAR_H + 10 : 12) + 64;
        drawText(x, y, "FPS " + std::to_string((int)gFps), 2, RGBX(180, 255, 180));
    }
    if (gInvOpen) drawInventory();
    if (gInvOpen && gDragging) drawIcon(gMouseX - 20, gMouseY - 20, 40, gDragItem);
    if (gSettingsOpen) drawSettings();
}

// ---------- window / fullscreen ----------
static void toggleFullscreen() {
    if (!gFullscreen) {
        gPrevStyle = GetWindowLongPtrA(gHwnd, GWL_STYLE);
        gPrevPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(gHwnd, &gPrevPlacement);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(gHwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongPtrA(gHwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(gHwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        gFullscreen = true;
    } else {
        SetWindowLongPtrA(gHwnd, GWL_STYLE, gPrevStyle | WS_VISIBLE);
        SetWindowPlacement(gHwnd, &gPrevPlacement);
        SetWindowPos(gHwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        gFullscreen = false;
    }
}


static void readKeys() {
    static bool prev[256];
    static const int keys[] = { 'E', 'F', 'Q', 'C', 'R', 'X', 'S', 'N', 'K', 'I', 'J', 'M', VK_DELETE, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_BACK, VK_SPACE, VK_RETURN, VK_F5, VK_F9, VK_F11,
                                VK_OEM_4, VK_OEM_6, VK_OEM_MINUS, VK_OEM_PLUS, 'Z', 'V', 'G', 'T', 'Y', 'H',
                                '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' };
    for (int k : keys) {
        bool d = down(k);
        gKeyEdge[k] = d && !prev[k];
        prev[k] = d;
    }
}

// ---------- chill music: a procedural 8-song playlist that loops, streamed with waveOut ----------
static const int MUS_RATE = 44100;
static const int MUS_BLOCK = 4410;
static const int MUS_BLOCKS = 6;
static const int MUS_TRACKS = 8;
static const double MUS_TRACK_LEN = 32.0;
static const double MUS_CHORD_LEN = 8.0;
static const double MUS_TAU = 6.283185307179586;
static const char* MUS_NAMES[MUS_TRACKS] = { "MORNING HAZE", "LANTERN DRIFT", "QUIET MEADOW", "SUNDAY PORCH",
                                             "SOFT RAIN LOFT", "BLUE HOUR", "TIDE POOL", "SLOW ORBIT" };
static const int MUS_ROOT[4][4] = { { 0, 9, 5, 7 }, { 9, 5, 0, 7 }, { 2, 10, 0, 9 }, { 5, 2, 10, 0 } };
static const int MUS_MINOR[4][4] = { { 0, 1, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 0, 1 }, { 0, 1, 0, 0 } };
static HWAVEOUT gWaveOut = nullptr;
static WAVEHDR gWaveHdr[MUS_BLOCKS];
static short gWaveBuf[MUS_BLOCKS][MUS_BLOCK * 2];
static long long gMusSample = 0;
static float gMusLP = 0.0f;
static float gMusLPR = 0.0f;
static bool gMusicOk = false;
static int gMusChord = -1;
static double gMusF[3] = { 130.81, 164.81, 196.0 };
static double gMusBass = 65.41;

static double midiF(int semi) { return 130.81 * pow(2.0, semi / 12.0); }

static void musChordSetup(int ci) {
    int track = ci / 4, slot = ci % 4;
    int t4 = track % 4;
    int keyOff = (track / 4) * 5;
    int root = MUS_ROOT[t4][slot] + keyOff;
    int iv[3] = { 0, MUS_MINOR[t4][slot] ? 3 : 4, 7 };
    for (int k = 0; k < 3; k++) gMusF[k] = midiF(root + iv[k]);
    gMusBass = midiF(root - 12);
}

// ---------- sound effects: synthesised, mixed into the audio stream ----------
struct Voice { int type; double t; double dur; float gl, gr; uint32_t seed; bool used; };
static Voice gVoices[24];
static const double SFX_TAU = 6.283185307179586;

static double sfxDuration(int type) {
    switch (type) {
        case SFX_MEOW: return 0.7;
        case SFX_BARK: return 0.5;
        case SFX_MOO: return 1.6;
        case SFX_BAA: return 0.9;
        case SFX_NEIGH: return 1.2;
        case SFX_CAMEL: return 1.3;
        case SFX_SQUEAK: return 0.35;
        case SFX_PICK: return 0.12;
        case SFX_HIT: return 0.15;
        case SFX_JUMP: return 0.25;
        default: return 0.4;
    }
}

static float sfxSample(int type, double t, double dur, uint32_t seed) {
    double env = std::min(1.0, t / 0.02) * std::max(0.0, 1.0 - t / dur);
    double n = (double)(hash2((int)(t * 44100.0), (int)seed, 77u) & 0xFFFF) / 65535.0 * 2.0 - 1.0;
    double ph;
    switch (type) {
        case SFX_MEOW: {                         // rising then falling "mew"
            double f = 520 + 380 * sin((double)PI * t / dur);
            return (float)((sin(SFX_TAU * f * t) + 0.15 * sin(SFX_TAU * 2 * f * t)) * env * 0.4);
        }
        case SFX_BARK: {                         // two short barks
            double cyc = fmod(t, 0.22);
            double burst = cyc < 0.12 ? 1.0 : 0.0;
            double sq = sin(SFX_TAU * 260 * t) > 0 ? 1.0 : -1.0;
            return (float)((sq * 0.45 + n * 0.15) * burst * exp(-cyc * 14.0) * (t < 0.45 ? 1.0 : 0.0));
        }
        case SFX_MOO: {                          // low, drooping, vibrato
            double f = 130 - 25 * (t / dur) + 4 * sin(SFX_TAU * 5 * t);
            ph = fmod(f * t, 1.0);
            return (float)((2 * ph - 1) * 0.3 * env + sin(SFX_TAU * f * 2 * t) * 0.08 * env);
        }
        case SFX_BAA: {                          // bleat
            double f = 300 + 20 * sin(SFX_TAU * 6 * t);
            return (float)((sin(SFX_TAU * f * t) > 0 ? 0.3 : -0.3) * env * 0.7);
        }
        case SFX_NEIGH:                          // harmonic whinny
            return (float)((sin(SFX_TAU * (800 + 500 * t / dur) * t) + 0.5 * sin(SFX_TAU * 2 * (800 + 500 * t / dur) * t)
                          + 0.3 * sin(SFX_TAU * 3 * (800 + 500 * t / dur) * t)) * 0.22 * env + n * 0.04 * env);
        case SFX_CAMEL: {                        // groan
            double f = 160 - 40 * (t / dur);
            ph = fmod(f * t, 1.0);
            return (float)((2 * ph - 1) * 0.28 * env + n * 0.08 * env);
        }
        case SFX_SQUEAK: {                       // hamster squeak
            double gate = fmod(t, 0.08) < 0.05 ? 1.0 : 0.0;
            return (float)(sin(SFX_TAU * (2800 + 400 * sin(SFX_TAU * 18 * t)) * t) * 0.3 * gate * env);
        }
        case SFX_PICK:
            return (float)(n * exp(-t * 120.0) * 0.8 + sin(SFX_TAU * 1800 * t) * exp(-t * 40.0) * 0.3);
        case SFX_HIT:
            return (float)(n * exp(-t * 35.0) * 0.6);
        case SFX_JUMP:
            return (float)(sin(SFX_TAU * (180 + 300 * t / dur) * t) * 0.25 * env);
        default:                                 // splash
            return (float)(n * exp(-t * 9.0) * 0.35);
    }
}

static void addVoice(int type, double dur, double delay, float gl, float gr, uint32_t seed) {
    for (Voice& v : gVoices) {
        if (v.used) continue;
        v.used = true; v.type = type; v.t = -delay; v.dur = dur; v.gl = gl; v.gr = gr; v.seed = seed;
        return;
    }
}

// Sound physics: distance falloff, stereo pan, muffling through walls, and an echo in caves.
static void playSfx(int type, float x, float y, float volume) {
    if (!gSet.sfx) return;
    float dx = x - gP.x, dy = y - gP.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > 30.0f) return;
    float g = volume / (1.0f + d * d * 0.02f);
    int blockers = 0;
    for (int k = 1; k <= 4; k++) {                    // occlusion: solid blocks between us muffle the sound
        float t = k / 5.0f;
        int tx = (int)floorf(gP.x + dx * (1.0f - t)), ty = (int)floorf(gP.y + dy * (1.0f - t));
        if (inB(tx, ty) && objSolid(cellAt(tx, ty).obj, false)) blockers++;
    }
    g *= blockers ? 0.45f : 1.0f;
    float pan = std::clamp(dx / 12.0f, -1.0f, 1.0f) * 0.5f + 0.5f;
    double dur = sfxDuration(type);
    uint32_t seed = (uint32_t)(gTime * 1000.0f) ^ (uint32_t)(x * 131.0f) ^ (uint32_t)(y * 17.0f);
    addVoice(type, dur, 0.0, g * (1.0f - pan), g * pan, seed);
    if (gInCave) addVoice(type, dur, 0.25, g * 0.4f * (1.0f - pan), g * 0.4f * pan, seed + 1);
}

static void mixSfx(short* dst, int frames) {
    for (Voice& v : gVoices) {
        if (!v.used) continue;
        for (int i = 0; i < frames; i++) {
            double tt = v.t + i / (double)MUS_RATE;
            if (tt < 0) continue;
            if (tt >= v.dur) { v.used = false; break; }
            float smp = sfxSample(v.type, tt, v.dur, v.seed) * 0.5f;
            int L = dst[2 * i] + (int)(smp * v.gl * 32767.0f);
            int R = dst[2 * i + 1] + (int)(smp * v.gr * 32767.0f);
            dst[2 * i] = (short)std::clamp(L, -32767, 32767);
            dst[2 * i + 1] = (short)std::clamp(R, -32767, 32767);
        }
        v.t += frames / (double)MUS_RATE;
    }
}

static void musicRenderBlock(short* dst, int frames, float vol) {
    for (int i = 0; i < frames; i++) {
        if (vol <= 0.0f) { dst[2 * i] = 0; dst[2 * i + 1] = 0; continue; }
        double t = (double)(gMusSample + i) / MUS_RATE;
        if (gMusMenu) {
            // Calm menu music: slow, warm chords with a soft bell and a low bass.
            double mt = fmod(t, 64.0);
            int mc = (int)(mt / 16.0);
            if (mc != gMenuChord) {
                gMenuChord = mc;
                static const int MR[4] = { 0, 9, 5, 7 };
                static const int MM[4] = { 0, 1, 0, 0 };
                int root = MR[mc];
                gMusF[0] = midiF(root + 12);
                gMusF[1] = midiF(root + (MM[mc] ? 15 : 16) + 12);
                gMusF[2] = midiF(root + 19 + 12);
                gMusBass = midiF(root - 12);
            }
            double ct = mt - mc * 16.0;
            double env = std::min(1.0, ct / 3.0) * std::min(1.0, (16.0 - ct) / 3.0);
            double vib = 1.0 + 0.003 * sin(MUS_TAU * 0.3 * t);
            double pad = 0.0;
            for (int k = 0; k < 3; k++) pad += sin(MUS_TAU * gMusF[k] * vib * t);
            pad *= 0.03 * env;
            double bt = fmod(t, 4.0);
            double bell = sin(MUS_TAU * gMusF[0] * 2.0 * bt) * exp(-bt * 1.1) * 0.035;
            double bb = fmod(t, 8.0);
            double bass = sin(MUS_TAU * gMusBass * bb) * exp(-bb * 0.5) * 0.06;
            double sum = pad + bell + bass;
            gMusLP += 0.08f * ((float)sum - gMusLP);
            gMusLPR += 0.08f * ((float)sum - gMusLPR);
            double o = gMusLP * vol * 0.9;
            o = o / (1.0 + fabs(o));
            short s16 = (short)std::clamp(o * 32767.0 * 1.6, -32767.0, 32767.0);
            dst[2 * i] = s16;
            dst[2 * i + 1] = s16;
            continue;
        }
        double tt = fmod(t, MUS_TRACK_LEN * MUS_TRACKS);
        int track = std::clamp((int)(tt / MUS_TRACK_LEN), 0, MUS_TRACKS - 1);
        int ci = (int)(tt / MUS_CHORD_LEN);
        if (ci < 0) ci = 0;
        if (ci >= 32) ci = 31;
        if (ci != gMusChord) { gMusChord = ci; musChordSetup(ci); }
        double ct = tt - ci * MUS_CHORD_LEN;
        double env = std::min(1.0, ct / 1.5) * std::min(1.0, (MUS_CHORD_LEN - ct) / 1.5);

        // Pads: three detuned voices with a slow "breathing" swell.
        double lfo = 0.8 + 0.2 * sin(MUS_TAU * 0.07 * t + track);
        double pad = 0.0;
        for (int k = 0; k < 3; k++)
            pad += sin(MUS_TAU * gMusF[k] * t) + 0.5 * sin(MUS_TAU * gMusF[k] * 1.004 * t)
                 + 0.25 * sin(MUS_TAU * gMusF[k] * 0.996 * t);
        pad *= 0.04 * env * lfo;

        // Arpeggio: speed changes from song to song and the notes wander left and right.
        double rate = 2.0 + (track % 3) * 0.5;
        double at = t * rate;
        long long step = (long long)floor(at);
        double tn = (at - step) / rate;
        int k = (int)(((step % 3) + 3) % 3);
        double pf = gMusF[k] * 2.0;
        double pluck = (sin(MUS_TAU * pf * tn) + 0.3 * sin(MUS_TAU * 2.0 * pf * tn) * exp(-tn * 6.0)) * exp(-tn * 3.5) * 0.09;
        double pan = 0.5 + 0.5 * sin(MUS_TAU * 0.25 * t);

        // Bell: an FM tone once every 4 seconds on the chord root.
        double tbell = fmod(t + 1.0, 4.0);
        double bell = sin(MUS_TAU * gMusF[0] * 2.0 * tbell + 2.0 * sin(MUS_TAU * gMusF[0] * 3.0 * tbell))
                    * exp(-tbell * 1.6) * 0.05;

        // Soft shaker: a short noise tick on every off-beat.
        double tsh = fmod(t, 0.5);
        double hat = (rnd((int)(gMusSample + i), 0, 55u) - 0.5) * exp(-tsh * 60.0) * 0.03;

        // Bass pulse.
        double tb = fmod(t, 2.0);
        double bass = sin(MUS_TAU * gMusBass * tb) * exp(-tb * 1.2) * 0.12;

        double l = pad + pluck * (1.0 - pan) * 1.4 + bell * 0.7 + hat + bass;
        double r = pad + pluck * pan * 1.4 + bell * 1.3 + hat * 0.8 + bass;

        // Each song has its own brightness.
        float cutoff = 0.18f + 0.05f * (track % 4);
        gMusLP += cutoff * ((float)l - gMusLP);
        gMusLPR += cutoff * ((float)r - gMusLPR);

        double outL = gMusLP * vol * 0.9;
        double outR = gMusLPR * vol * 0.9;
        outL = outL / (1.0 + fabs(outL));
        outR = outR / (1.0 + fabs(outR));
        dst[2 * i] = (short)std::clamp(outL * 32767.0 * 1.8, -32767.0, 32767.0);
        dst[2 * i + 1] = (short)std::clamp(outR * 32767.0 * 1.8, -32767.0, 32767.0);
    }
    mixSfx(dst, frames);
    gMusSample += frames;
}

static float musVolume() { return gSet.music ? gSet.musicVol / 100.0f : 0.0f; }

static void musicWrite(int b) {
    musicRenderBlock(gWaveBuf[b], MUS_BLOCK, musVolume());
    WAVEHDR& h = gWaveHdr[b];
    memset(&h, 0, sizeof h);
    h.lpData = (LPSTR)gWaveBuf[b];
    h.dwBufferLength = MUS_BLOCK * 4;
    waveOutPrepareHeader(gWaveOut, &h, sizeof(WAVEHDR));
    waveOutWrite(gWaveOut, &h, sizeof(WAVEHDR));
}

static void musicInit() {
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = MUS_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = MUS_RATE * 4;
    if (waveOutOpen(&gWaveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { gWaveOut = nullptr; return; }
    gMusicOk = true;
    for (int b = 0; b < MUS_BLOCKS; b++) musicWrite(b);
}

static void musicPump() {
    if (!gMusicOk) return;
    for (int b = 0; b < MUS_BLOCKS; b++) {
        if (gWaveHdr[b].dwFlags & WHDR_DONE) {
            waveOutUnprepareHeader(gWaveOut, &gWaveHdr[b], sizeof(WAVEHDR));
            musicWrite(b);
        }
    }
}

static void musicShutdown() {
    if (!gMusicOk) return;
    waveOutReset(gWaveOut);
    for (int b = 0; b < MUS_BLOCKS; b++)
        if (gWaveHdr[b].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(gWaveOut, &gWaveHdr[b], sizeof(WAVEHDR));
    waveOutClose(gWaveOut);
    gWaveOut = nullptr;
    gMusicOk = false;
}

static int currentTrack() {
    double tt = fmod(gMusSample / (double)MUS_RATE, MUS_TRACK_LEN * MUS_TRACKS);
    return std::clamp((int)(tt / MUS_TRACK_LEN), 0, MUS_TRACKS - 1);
}

// ---------- settings menu (Ctrl+S) ----------
static int gSetSel = 0;
static const int SET_ITEMS = 10;

static void settingsChange(int item, int dir) {
    switch (item) {
        case 0: gSet.shader = (gSet.shader + dir + 4) % 4; break;
        case 1: gSet.texture = (gSet.texture + dir + 3) % 3; genAllSprites(); break;
        case 2: gSet.fpsCap = (gSet.fpsCap + dir + 5) % 5; break;
        case 3: gSet.vsync = !gSet.vsync; break;
        case 4: gSet.music = !gSet.music; break;
        case 5: gSet.musicVol = std::clamp(gSet.musicVol + dir * 10, 0, 100); break;
        case 6: gSet.showFps = !gSet.showFps; break;
        case 7: gSet.sfx = !gSet.sfx; break;
        case 8: gSet.turbo = !gSet.turbo; genAllSprites(); break;
        default: break;
    }
}

static void settingsInput() {
    if (gKeyEdge[VK_UP]) gSetSel = (gSetSel + SET_ITEMS - 1) % SET_ITEMS;
    if (gKeyEdge[VK_DOWN]) gSetSel = (gSetSel + 1) % SET_ITEMS;
    if (gKeyEdge[VK_RIGHT]) settingsChange(gSetSel, +1);
    if (gKeyEdge[VK_LEFT]) settingsChange(gSetSel, -1);
    if (gKeyEdge[VK_RETURN]) {
        if (gSetSel == SET_ITEMS - 1) { gSettingsOpen = false; saveSettings(); }
        else settingsChange(gSetSel, +1);
    }
}

static void drawSettings() {
    int w = std::min(640, gW - 40), h = 110 + SET_ITEMS * 34 + 60;
    int x = (gW - w) / 2, y = std::max(KEYBAR_H + 10, gH / 2 - h / 2);
    fillRect(x, y, w, h, 0xF0101018);
    outlineRect(x, y, w, h, RGBX(200, 180, 90));
    drawText(x + 20, y + 16, "SETTINGS", 3, RGBX(255, 230, 120));

    std::string labels[SET_ITEMS];
    labels[0] = std::string("SHADER QUALITY    ") + SHADER_NAMES[gSet.shader];
    labels[1] = std::string("TEXTURE QUALITY   ") + TEX_NAMES[gSet.texture];
    labels[2] = std::string("FPS CAP           ") + (gSet.fpsCap == 0 ? std::string("UNLIMITED") : std::to_string(FPS_VALUES[gSet.fpsCap]));
    labels[3] = std::string("V-SYNC            ") + (gSet.vsync ? "ON" : "OFF");
    labels[4] = std::string("MUSIC             ") + (gSet.music ? "ON" : "OFF");
    labels[5] = std::string("MUSIC VOLUME      ") + std::to_string(gSet.musicVol) + "%";
    labels[6] = std::string("SHOW FPS          ") + (gSet.showFps ? "ON" : "OFF");
    labels[7] = std::string("SOUND EFFECTS     ") + (gSet.sfx ? "ON" : "OFF");
    labels[8] = std::string("GRAPHICS TURBO     ") + (gSet.turbo ? "ON" : "OFF");
    labels[9] = "CLOSE";

    for (int i = 0; i < SET_ITEMS; i++) {
        int ry = y + 60 + i * 34;
        if (i == gSetSel) fillRect(x + 12, ry - 6, w - 24, 28, 0xFF2A2A40);
        drawText(x + 24, ry, labels[i], 2, i == gSetSel ? RGBX(255, 255, 255) : RGBX(190, 190, 200));
    }
    int tr = currentTrack();
    drawText(x + 20, y + h - 52, std::string("NOW PLAYING: ") + MUS_NAMES[tr] + "  (" +
             std::to_string(tr + 1) + "/" + std::to_string(MUS_TRACKS) + ")", 1, RGBX(160, 220, 255));
    drawText(x + 20, y + h - 34, "UP/DOWN SELECT   LEFT/RIGHT CHANGE   ENTER   CTRL+S OR ESC CLOSE", 1, RGBX(200, 200, 200));
}

static void setWeather(int w) {
    gWeather = w;
    gRain = (w == W_RAIN || w == W_STORM || w == W_MONSOON);
    gWindX = (w == W_SNOWSTORM) ? 0.5f : (w == W_SANDSTORM) ? -0.7f : 0.0f;
    addPopup(std::string("Weather: ") + WEATHER_NAMES[w], RGBX(200, 220, 255));
    if (w == W_STORM && rnd((int)gTime, 61, 67u) < 0.35f) {
        float ang = rnd((int)gTime, 62, 68u) * 2.0f * PI;
        Tornado t;
        t.x = gP.x + cosf(ang) * 22.0f; t.y = gP.y + sinf(ang) * 22.0f;
        t.dx = -cosf(ang) * 2.2f; t.dy = -sinf(ang) * 2.2f; t.life = 30.0f;
        gTornados.push_back(t);
        addPopup("A TORNADO IS APPROACHING!", RGBX(255, 200, 120));
    }
}

static void updateWeather(float dt) {
    gWeatherT -= dt;
    if (gWeatherT <= 0) {
        uint32_t s = (uint32_t)(gTime * 1000.0f);
        int total = 0;
        for (int i = 0; i < W_COUNT_W; i++) total += WEATHER_WEIGHT[i];
        int roll = (int)(rnd((int)s, 4, 13u) * total), acc = 0, pick = W_CLEAR;
        for (int i = 0; i < W_COUNT_W; i++) { acc += WEATHER_WEIGHT[i]; if (roll < acc) { pick = i; break; } }
        setWeather(pick);
        gWeatherT = 60.0f + rnd((int)s, 5, 17u) * 90.0f;
    }
    if (gWeather == W_STORM) {
        gFlashCool -= dt;
        if (gFlashCool <= 0) { gFlashT = 0.25f; gFlashCool = 3.0f + rnd((int)gTime, 6, 19u) * 8.0f; }
    }
    gFlashT = std::max(0.0f, gFlashT - dt);
    if (gWindX != 0.0f) moveBy(gWindX * dt, 0.0f);
    if (gWeather == W_HAIL) gHP -= 0.25f * dt;     // hail stings while it falls
}

static void updateWeatherParticles(float dt, u32 pcol) {
    switch (gWeather) {
        case W_RAIN: case W_STORM:
            gPartVX = -20.0f; gPartVY = 260.0f; gPartSpread = 30.0f;
            updateParticles(dt, RGBX(170, 200, 255), gWeather == W_STORM ? 110 : 80);
            return;
        case W_SNOW: case W_SNOWSTORM:
            gPartVX = gWeather == W_SNOWSTORM ? 120.0f : 10.0f; gPartVY = 40.0f; gPartSpread = 30.0f;
            updateParticles(dt, RGBX(245, 248, 255), gWeather == W_SNOWSTORM ? 90 : 35);
            return;
        case W_SANDSTORM:
            gPartVX = 260.0f; gPartVY = 20.0f; gPartSpread = 60.0f;
            updateParticles(dt, RGBX(220, 190, 130), 120);
            return;
        case W_BLIZZARD:
            gPartVX = 180.0f; gPartVY = 60.0f; gPartSpread = 60.0f;
            updateParticles(dt, RGBX(250, 252, 255), 150);
            return;
        case W_HAIL:
            gPartVX = 40.0f; gPartVY = 400.0f; gPartSpread = 30.0f;
            updateParticles(dt, RGBX(230, 240, 255), 120);
            return;
        case W_MONSOON:
            gPartVX = -40.0f; gPartVY = 320.0f; gPartSpread = 40.0f;
            updateParticles(dt, RGBX(150, 180, 230), 160);
            return;
        case W_FOG:
            gPartVX = 8.0f; gPartVY = 4.0f; gPartSpread = 10.0f;
            updateParticles(dt, RGBX(210, 210, 220), 8);
            return;
        default:
            gPartVX = 0.0f; gPartVY = 30.0f; gPartSpread = 30.0f;
            updateParticles(dt, pcol, 18);
            return;
    }
}

static void catchSick(int s) {
    if (gSick != S_NONE) return;
    gSick = s;
    gSickT = 150.0f;
    addPopup(std::string("You caught the ") + SICK_NAMES[s], RGBX(255, 120, 120));
}

static void updateSickness(float dt) {
    uint32_t s = (uint32_t)(gTime * 1000.0f);
    float rf = rnd((int)s, 7, 23u);
    float rate = 0.0f;
    int what = S_NONE;
    if (gWeather == W_SNOW)            { what = S_FLU;  rate = 0.02f; }
    else if (gWeather == W_SNOWSTORM)  { what = S_FLU;  rate = 0.05f; }
    else if (gWeather == W_RAIN || gWeather == W_STORM) { what = S_FLU; rate = 0.01f; }
    else if (gWeather == W_BLIZZARD)   { what = S_FLU;  rate = 0.08f; }
    else if (gWeather == W_MONSOON)    { what = S_FLU;  rate = 0.02f; }
    else if (gWeather == W_SANDSTORM)  { what = S_DUST; rate = 0.04f; }
    else if (gWeather == W_HEATWAVE && daylight() > 0.6f) { what = S_HEAT; rate = 0.03f; }
    if (what != S_NONE && rf < rate * dt) catchSick(what);

    if (gSick != S_NONE) {
        gSickT -= dt;
        if (gSick == S_FLU) { gHP -= 0.4f * dt; gP.energy = std::max(0.0f, gP.energy - dt * 0.8f); }
        if (gSick == S_DUST) { gP.energy = std::max(0.0f, gP.energy - dt * 1.5f); }
        if (gSick == S_HEAT) { gHP -= 0.8f * dt; }
        if (gSick == S_RABBIT) { gHP -= 0.5f * dt; }
        if (gSickT <= 0) {
            addPopup(std::string("You recovered from ") + SICK_NAMES[gSick], RGBX(170, 255, 200));
            gSick = S_NONE;
        }
    }
}

static void updateAnimals(float dt) {
    gOpenDoors = false;
    gAnimalSpawnT -= dt;
    bool day = daylight() > 0.35f;
    if (day && gAnimalSpawnT <= 0 && gAnimals.size() < 8) {
        gAnimalSpawnT = 6.0f;
        uint32_t s = (uint32_t)(gTime * 1000.0f) + (uint32_t)gAnimals.size() * 97u;
        float ang = rnd((int)s, 1, 51u) * 2.0f * PI;
        float dist = 10.0f + rnd((int)s, 2, 52u) * 10.0f;
        float ax = gP.x + cosf(ang) * dist, ay = gP.y + sinf(ang) * dist;
        int tx = (int)floorf(ax), ty = (int)floorf(ay);
        if (inB(tx, ty) && !solidAt(ax, ay)) {
            uint8_t g = cellAt(tx, ty).ground;
            if (g == G_GRASS || g == G_MEADOW || g == G_FOREST) {
                Animal a = { ax, ay, 0, 0, 0, 6.0f, 0 };
                gAnimals.push_back(a);
            }
        }
    }
    for (size_t i = 0; i < gAnimals.size();) {
        Animal& a = gAnimals[i];
        float ddx = a.x - gP.x, ddy = a.y - gP.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        float mvx, mvy;
        if (a.tame) {
            if (d > 2.5f) steerToward(a.x, a.y, gP.x, gP.y, 2.6f, true, mvx, mvy);
            else { mvx = 0; mvy = 0; }
        } else if (d < 3.0f && d > 0.01f) {
            steerToward(a.x, a.y, a.x + ddx / d * 4.0f, a.y + ddy / d * 4.0f, 2.4f, true, mvx, mvy);
        } else {
            a.wander -= dt;
            if (a.wander <= 0) {
                float an = rnd((int)(a.x * 10), (int)(a.y * 10), (uint32_t)(gTime * 10.0f)) * 2.0f * PI;
                a.dx = cosf(an); a.dy = sinf(an);
                a.wander = 1.5f + rnd((int)a.x, (int)a.y, 9u) * 2.0f;
            }
            mvx = a.dx * 0.6f; mvy = a.dy * 0.6f;
        }
        if (fabsf(mvx) > 0.05f) a.face = mvx < 0 ? 1 : 0;
        moveWalker(a.x, a.y, mvx, mvy, dt, true);
        if (!a.tame && (d > 30.0f || (!day && d > 12.0f))) { gAnimals.erase(gAnimals.begin() + i); continue; }
        ++i;
    }
}

static void updateTornados(float dt) {
    for (size_t i = 0; i < gTornados.size();) {
        Tornado& t = gTornados[i];
        t.x += t.dx * dt; t.y += t.dy * dt; t.life -= dt;
        float ddx = t.x - gP.x, ddy = t.y - gP.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        if (d < 1.3f && gHurtT <= 0) {
            gHP -= 12.0f;
            gHurtT = 0.6f;
            addPopup("-12 HP  TORNADO!", RGBX(255, 90, 90));
            float ux = (gP.x - t.x) / std::max(d, 0.01f), uy = (gP.y - t.y) / std::max(d, 0.01f);
            for (int k = 0; k < 8; k++) moveBy(ux * 0.25f, uy * 0.25f);
        }
        if (t.life <= 0) { gTornados.erase(gTornados.begin() + i); continue; }
        ++i;
    }
}

static void drawTornados(float camX, float camY) {
    for (const Tornado& t : gTornados) {
        int cx = (int)(t.x * TILE - camX), cy = (int)(t.y * TILE - camY);
        fillEllipse((float)cx, (float)(cy + 20), 30.0f, 8.0f, RGBX(70, 70, 80));
        for (int k = 0; k < 12; k++) {
            float a = gTime * 8.0f + k * 0.52f;
            float r = 8.0f + k * 2.5f;
            fillEllipse(cx + cosf(a) * r, cy + sinf(a) * r * 0.35f - k * 3.0f, 3.0f, 3.0f, RGBX(180, 180, 190));
        }
    }
}

static void drawAnimals(float camX, float camY) {
    for (const Animal& a : gAnimals) {
        int sx = (int)(a.x * TILE - camX) - 32, sy = (int)(a.y * TILE - camY) - 32;
        float bob = fabsf(sinf(gTime * 8.0f + a.x)) * 6.0f;
        blitAlpha(sx, sy - (int)bob, gRabbitSpr[a.face]);
        if (a.tame && a.name[0]) {
            std::string nm = a.name;
            drawText(sx + 32 - textWidth(nm, 1) / 2, sy - 6, nm, 1, RGBX(255, 240, 180));
        }
    }
}

// Idle wandering: a random walk that stays close to the player.
static void idleWander(float& timer, float& dx, float& dy, float& mvx, float& mvy, float dt, float seedX) {
    timer -= dt;
    if (timer <= 0) {
        float an = rnd((int)(seedX * 10), (int)(gTime * 3), 21u) * 2.0f * PI;
        dx = cosf(an); dy = sinf(an);
        timer = 1.5f + rnd((int)(seedX * 10), 22, 23u) * 2.0f;
    }
    mvx = dx * 0.7f; mvy = dy * 0.7f;
}

static void updatePeople(float dt) {
    gOpenDoors = true;
    float dl = daylight();
    gStrangerT -= dt;
    int strangers = 0;
    for (const Person& p : gPeople) if (!p.isFriend) strangers++;
    if (dl > 0.35f && gStrangerT <= 0 && strangers < MAX_STRANGERS && gPeople.size() < 16) {
        gStrangerT = 15.0f;
        uint32_t s = (uint32_t)(gTime * 1000.0f) + (uint32_t)gPeople.size() * 131u;
        float ang = rnd((int)s, 1, 71u) * 2.0f * PI, dist = 12.0f + rnd((int)s, 2, 72u) * 8.0f;
        float ax = gP.x + cosf(ang) * dist, ay = gP.y + sinf(ang) * dist;
        int tx = (int)floorf(ax), ty = (int)floorf(ay);
        if (inB(tx, ty) && !solidAt(ax, ay)) {
            uint8_t g = cellAt(tx, ty).ground;
            if (g == G_GRASS || g == G_MEADOW || g == G_FOREST) {
                Person p;
                memset(&p, 0, sizeof p);
                p.x = ax; p.y = ay;
                p.gender = rnd((int)s, 3, 73u) < 0.5f ? 0 : 1;
                p.pal = (int)(rnd((int)s, 4, 74u) * 3) % 3;
                p.face = 2;
                makeName(p.name, p.gender, hash2((int)s, 5, 75u));
                gPeople.push_back(p);
            }
        }
    }
    for (size_t i = 0; i < gPeople.size();) {
        Person& p = gPeople[i];
        if (!p.isFriend && dl < 0.3f) { gPeople.erase(gPeople.begin() + i); continue; }
        float ddx = gP.x - p.x, ddy = gP.y - p.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        float mvx = 0, mvy = 0;
        if (p.isFriend) {
            if (d > 25.0f) { p.x = gP.x - 1.5f; p.y = gP.y; }     // beyond 25 blocks: catch up
            if (d > 6.0f) steerToward(p.x, p.y, gP.x, gP.y, 3.0f, true, mvx, mvy);
            else if (d < 1.4f && d > 0.01f) { mvx = -ddx / d * 0.8f; mvy = -ddy / d * 0.8f; }
            else idleWander(p.wander, p.dx, p.dy, mvx, mvy, dt, p.x);
            if (d < 3.5f) {                          // chilling together builds the friendship
                p.cool += dt;
                if (p.cool >= 30.0f) { p.cool = 0; p.friendship++; addPopup(std::string(p.name) + " enjoys hanging out", RGBX(255, 210, 150)); }
            }
            p.hop = std::max(0.0f, p.hop - dt);
            for (Monster& m : gMonsters) {               // fight alongside the player
                float mx = m.x - p.x, my = m.y - p.y;
                if (p.hop <= 0 && mx * mx + my * my < 9.0f) { m.hp -= 6; m.flash = 0.2f; p.hop = 1.2f; if (m.hp <= 0) m.byCompanion = 1; break; }
            }
        } else {
            p.wander -= dt;
            if (p.wander <= 0) {
                float an = rnd((int)(p.x * 10), (int)(p.y * 10), (uint32_t)(gTime * 10.0f)) * 2.0f * PI;
                p.dx = cosf(an); p.dy = sinf(an);
                p.wander = 2.0f + rnd((int)p.x, (int)p.y, 12u) * 2.0f;
            }
            mvx = p.dx * 0.5f; mvy = p.dy * 0.5f;
            if (d < 3.0f && d > 0.01f) { mvx = -ddx / d * 1.8f; mvy = -ddy / d * 1.8f; }
        }
        if (fabsf(mvx) > 0.05f || fabsf(mvy) > 0.05f) {
            if (fabsf(mvx) > fabsf(mvy)) p.face = mvx > 0 ? 1 : 3;
            else p.face = mvy > 0 ? 2 : 0;
        }
        moveWalker(p.x, p.y, mvx, mvy, dt, true);
        ++i;
    }
}

static void updatePets(float dt) {
    gOpenDoors = true;
    for (size_t i = 0; i < gPets.size();) {
        Pet& p = gPets[i];
        float ddx = gP.x - p.x, ddy = gP.y - p.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        float mvx = 0, mvy = 0;
        if (d > 25.0f) { p.x = gP.x - 1.2f; p.y = gP.y; }     // beyond 25 blocks: catch up
        if (d > 6.0f) steerToward(p.x, p.y, gP.x, gP.y, 3.2f, true, mvx, mvy);
        else if (d < 1.2f && d > 0.01f) { mvx = -ddx / d * 0.8f; mvy = -ddy / d * 0.8f; }
        else idleWander(p.wander, p.dx, p.dy, mvx, mvy, dt, p.x);
        p.cool -= dt;
        p.hop = std::max(0.0f, p.hop - dt);
        float dmg = p.kind == 0 ? 6.0f : p.kind == 1 ? 4.0f : 2.0f;
        bool threatened = false;
        for (Monster& m : gMonsters) {
            float mx = m.x - p.x, my = m.y - p.y, md2 = mx * mx + my * my;
            if (md2 < 2.2f * 2.2f && p.cool <= 0) { m.hp -= dmg; m.flash = 0.2f; p.cool = 1.0f; p.hop = 0.3f; if (m.hp <= 0) m.byCompanion = 1; }
            if (md2 < 1.0f) threatened = true;
        }
        if (threatened) p.hp -= dt * 2.0f;
        else p.hp = std::min(20.0f, p.hp + dt * 0.2f);
        if (fabsf(mvx) > 0.05f || fabsf(mvy) > 0.05f) p.face = mvx < 0 ? 1 : 0;
        moveWalker(p.x, p.y, mvx, mvy, dt, true);
        if (p.hp <= 0) {
            addPopup(std::string(p.name) + " ran off hurt", RGBX(255, 120, 120));
            gPets.erase(gPets.begin() + i);
            continue;
        }
        ++i;
    }
}

static void updateStrays(float dt) {
    gOpenDoors = false;
    gStrayT -= dt;
    bool day = daylight() > 0.35f;
    if (day && gStrayT <= 0 && gStrays.size() < 10) {
        gStrayT = 3.0f;
        uint32_t s = (uint32_t)(gTime * 1000.0f) + (uint32_t)gStrays.size() * 59u;
        float ang = rnd((int)s, 6, 91u) * 2.0f * PI, dist = 10.0f + rnd((int)s, 7, 92u) * 10.0f;
        float ax = gP.x + cosf(ang) * dist, ay = gP.y + sinf(ang) * dist;
        int tx = (int)floorf(ax), ty = (int)floorf(ay);
        if (inB(tx, ty) && !solidAt(ax, ay)) {
            uint8_t g = cellAt(tx, ty).ground;
            if (g == G_GRASS || g == G_MEADOW || g == G_FOREST) {
                Pet q;
                memset(&q, 0, sizeof q);
                q.x = ax; q.y = ay; q.hp = 20.0f;
                q.kind = (int)(rnd((int)s, 8, 93u) * 3) % 3;
                gStrays.push_back(q);
            }
        }
    }
    for (size_t i = 0; i < gStrays.size();) {
        Pet& q = gStrays[i];
        float ddx = q.x - gP.x, ddy = q.y - gP.y;
        float d = sqrtf(ddx * ddx + ddy * ddy);
        float mvx, mvy;
        if (d < 3.0f && d > 0.01f) steerToward(q.x, q.y, q.x + ddx / d * 4.0f, q.y + ddy / d * 4.0f, 2.4f, true, mvx, mvy);
        else {
            q.wander -= dt;
            if (q.wander <= 0) {
                float an = rnd((int)(q.x * 10), (int)(q.y * 10), (uint32_t)(gTime * 10.0f)) * 2.0f * PI;
                q.dx = cosf(an); q.dy = sinf(an);
                q.wander = 1.5f + rnd((int)q.x, (int)q.y, 14u) * 2.0f;
            }
            mvx = q.dx * 0.6f; mvy = q.dy * 0.6f;
        }
        if (fabsf(mvx) > 0.05f) q.face = mvx < 0 ? 1 : 0;
        moveWalker(q.x, q.y, mvx, mvy, dt, true);
        if (d > 30.0f || (!day && d > 12.0f)) { gStrays.erase(gStrays.begin() + i); continue; }
        ++i;
    }
}

static void drawCompanions(float camX, float camY) {
    for (const Person& p : gPeople) {
        int sx = (int)(p.x * TILE - camX) - 32, sy = (int)(p.y * TILE - camY) - 32;
        int f = (((int)(gTime * 1.5f + p.x)) % 2) ? 2 : 0;
        blitAlpha(sx, sy, gNpcSpr[p.pal][p.face][f]);
        std::string nm = p.name;
        drawText(sx + 32 - textWidth(nm, 1) / 2, sy - 6, nm, 1, p.isFriend ? RGBX(255, 210, 110) : RGBX(230, 230, 240));
    }
    auto drawPetAt = [&](const Pet& p) {
        int sx = (int)(p.x * TILE - camX) - 32, sy = (int)(p.y * TILE - camY) - 32;
        int f = ((int)(gTime * 6.0f + p.x * 3.0f)) % 2;
        int bob = p.hop > 0 ? (int)(fabsf(sinf(gTime * 12.0f)) * 6.0f) : 0;
        blitAlpha(sx, sy - bob, gPetSpr[p.kind][p.face][f]);
        if (p.name[0]) {
            std::string nm = p.name;
            drawText(sx + 32 - textWidth(nm, 1) / 2, sy - 6, nm, 1, RGBX(255, 240, 180));
        }
    };
    for (const Pet& p : gPets) drawPetAt(p);
    if (!gUnderground) for (const Pet& p : gStrays) drawPetAt(p);
}

// Every new day an empire eats into its food stock.
static void applyUpkeep() {
    int stock = foodTotal();
    int cost = std::max(15, (int)(stock * 0.08f));
    int start = cost;
    for (int i = 0; i < T_COUNT && cost > 0; i++) {
        if (!isFood(i) || i == I_MEDICINE || gCount[i] <= 0) continue;
        int take = std::min(gCount[i], cost);
        gCount[i] -= take;
        cost -= take;
    }
    if (start > 0) addPopup("Empire upkeep: -" + std::to_string(start - cost) + " food", RGBX(255, 210, 150));
}

static void updateDoors(float dt) {
    (void)dt;
    for (int idx : gDoors) {
        Cell& c = gMap[idx];
        float cx = idx % MAP_W + 0.5f, cy = idx / MAP_W + 0.5f;
        bool nearby = false;
        float dx = gP.x - cx, dy = gP.y - cy;
        if (dx * dx + dy * dy < 1.96f) nearby = true;
        for (const Person& p : gPeople) {
            if (!p.isFriend || nearby) continue;
            float ex = p.x - cx, ey = p.y - cy;
            if (ex * ex + ey * ey < 1.96f) nearby = true;
        }
        for (const Pet& q : gPets) {
            if (nearby) break;
            float ex = q.x - cx, ey = q.y - cy;
            if (ex * ex + ey * ey < 1.96f) nearby = true;
        }
        c.obj = nearby ? O_DOOR_OPEN : O_DOOR_CLOSED;
    }
}

static void updateBeasts(float dt) {
    gBeastSpawnT -= dt;
    bool day = daylight() > 0.35f;
    if (day && gBeastSpawnT <= 0 && (int)gBeasts.size() < MAX_BEASTS) {
        gBeastSpawnT = 8.0f;
        uint32_t s = (uint32_t)(gTime * 1000.0f) + (uint32_t)gBeasts.size() * 211u;
        float ang = rnd((int)s, 1, 401u) * 2.0f * PI, dist = 12.0f + rnd((int)s, 2, 402u) * 14.0f;
        float ax = gP.x + cosf(ang) * dist, ay = gP.y + sinf(ang) * dist;
        int tx = (int)floorf(ax), ty = (int)floorf(ay);
        if (inB(tx, ty) && !solidAt(ax, ay)) {
            uint8_t g = cellAt(tx, ty).ground;
            int kind = (int)(rnd((int)s, 3, 403u) * 4) % 4;
            bool ok = kind == 3 ? (g == G_DESERT) : (g == G_GRASS || g == G_MEADOW || (kind <= 1 && g == G_FOREST));
            if (ok) {
                Beast b;
                memset(&b, 0, sizeof b);
                b.x = ax; b.y = ay; b.kind = kind; b.hp = BEAST_HP[kind]; b.face = 2;
                gBeasts.push_back(b);
            }
        }
    }
    for (size_t i = 0; i < gBeasts.size();) {
        if (gRide == (int)i) { ++i; continue; }        // a ridden beast is steered by the player
        Beast& b = gBeasts[i];
        float ddx = gP.x - b.x, ddy = gP.y - b.y, d = sqrtf(ddx * ddx + ddy * ddy);
        float spd = b.kind >= 2 ? 1.2f : 0.9f;
        float mvx = 0, mvy = 0;
        if (b.tame) {
            b.cool -= dt;
            if (d > 2.5f) steerToward(b.x, b.y, gP.x, gP.y, spd * 1.8f, true, mvx, mvy);
            for (Monster& m : gMonsters) {          // defend the player
                float mx = m.x - b.x, my = m.y - b.y;
                if (b.cool <= 0 && mx * mx + my * my < 6.25f) {
                    m.hp -= 5; m.flash = 0.2f; b.cool = 1.2f;
                    if (m.hp <= 0) m.byCompanion = 1;
                    break;
                }
            }
        } else if (d < 3.0f && d > 0.01f) { mvx = -ddx / d * spd * 1.6f; mvy = -ddy / d * spd * 1.6f; }
        else {
            b.wander -= dt;
            if (b.wander <= 0) {
                float an = rnd((int)(b.x * 10), (int)(b.y * 10), (uint32_t)(gTime * 10.0f)) * 2.0f * PI;
                b.dx = cosf(an); b.dy = sinf(an);
                b.wander = 2.0f + rnd((int)b.x, (int)b.y, 15u) * 3.0f;
            }
            mvx = b.dx * spd * 0.5f; mvy = b.dy * spd * 0.5f;
        }
        if (fabsf(mvx) > 0.05f) b.face = mvx < 0 ? 1 : 0;
        gOpenDoors = false;
        moveWalker(b.x, b.y, mvx, mvy, dt, true);
        if (!b.tame && (d > 30.0f || (!day && d > 14.0f))) { eraseBeast(i); continue; }
        ++i;
    }
}

// Background animal calls: a random nearby creature makes its sound now and then.
static void updateSounds(float dt) {
    static float tmr = 0.0f;
    tmr -= dt;
    if (tmr > 0.0f) return;
    tmr = 0.8f + rnd((int)gTime, 201, 301u) * 1.5f;
    struct Cand { float x, y; int type; };
    std::vector<Cand> c;
    for (const Pet& q : gPets) c.push_back({ q.x, q.y, q.kind == 0 ? SFX_BARK : q.kind == 1 ? SFX_MEOW : SFX_SQUEAK });
    for (const Pet& q : gStrays) c.push_back({ q.x, q.y, q.kind == 0 ? SFX_BARK : q.kind == 1 ? SFX_MEOW : SFX_SQUEAK });
    for (const Beast& b : gBeasts) c.push_back({ b.x, b.y, b.kind == 0 ? SFX_MOO : b.kind == 1 ? SFX_BAA : b.kind == 2 ? SFX_NEIGH : SFX_CAMEL });
    if (c.empty()) return;
    const Cand& pick = c[(int)(rnd((int)gTime, 202, 302u) * c.size()) % c.size()];
    playSfx(pick.type, pick.x, pick.y, 0.9f);
}

// Friends mine rock and ore near them and the loot goes straight into your inventory.
static std::vector<float> gMineT;
static std::vector<int> gMineCell;

// What a friend can mine: trees need an axe, rock and ore need a pickaxe at the right level.
static bool mineableCell(const Cell& c, int level, bool axe) {
    (void)axe;
    if (c.obj == O_TREE || c.obj == O_ROCK) return true;          // wood and stone need no tools
    if (c.obj >= O_ORE_COPPER && c.obj <= O_ORE_OBSIDIAN) return (c.obj - O_ORE_COPPER + 1) <= level;
    return false;
}

static void updateFriendMining(float dt) {
    gMineT.resize(gPeople.size(), 0.0f);
    gMineCell.resize(gPeople.size(), -1);
    int level = bestPickLevel();
    bool axe = gCount[I_AXE] > 0 || hasTierPiece(1);
    for (size_t i = 0; i < gPeople.size(); i++) {
        Person& p = gPeople[i];
        if (!p.isFriend) continue;
        int cell = gMineCell[i];
        if (cell >= 0 && !mineableCell(gMap[cell], level, axe)) { gMineCell[i] = -1; gMineT[i] = 0.0f; cell = -1; }
        if (cell < 0) {
            if (rnd((int)(gTime * 20.0f), (int)i, 501u) > 0.04f) continue;   // search now and then
            int best = -1;
            float bestD = 1e9f;
            int cx = (int)floorf(p.x), cy = (int)floorf(p.y);
            // scan the 25-block zone around the player (friends are never more than 25 blocks away)
            int px = (int)floorf(gP.x), py = (int)floorf(gP.y);
            (void)cx; (void)cy;
            for (int y = py - 25; y <= py + 25; y++) for (int x = px - 25; x <= px + 25; x++) {
                if (!inB(x, y)) continue;
                int idx = y * MAP_W + x;
                if (!mineableCell(gMap[idx], level, axe)) continue;
                float ex = x + 0.5f - p.x, ey = y + 0.5f - p.y;
                float d2 = ex * ex + ey * ey;
                if (d2 < bestD) { bestD = d2; best = idx; }
            }
            gMineCell[i] = best;
            continue;
        }
        float tx = cell % MAP_W + 0.5f, ty = cell / MAP_W + 0.5f;
        float ddx = tx - p.x, ddy = ty - p.y, d = sqrtf(ddx * ddx + ddy * ddy);
        if (d > 1.1f) {
            gOpenDoors = true;
            moveWalker(p.x, p.y, ddx / d * 1.8f, ddy / d * 1.8f, dt, true);
        } else {
            gMineT[i] += dt;
            Cell& c0 = gMap[cell];
            float need = c0.obj == O_TREE ? 2.0f : c0.obj == O_ROCK ? 2.5f : 3.0f;
            if (gMineT[i] >= need) {
                Cell& c = gMap[cell];
                int item, n = 1;
                if (c.obj == O_TREE) { item = I_WOOD; n = 3; }
                else if (c.obj == O_ROCK) { item = I_STONE; n = 2; }
                else {
                    int m = c.obj - O_ORE_COPPER;
                    item = (m == 0) ? I_CU_ORE : (m == 1) ? I_FE_ORE : (m == 2) ? I_DIA : I_OBS;
                }
                c.obj = O_NONE;
                gCount[item] += n;                   // straight into your inventory
                addPopup(std::string(p.name) + " gathered " + std::to_string(n) + " " + itemName(item), RGBX(200, 230, 255));
                gMineT[i] = 0.0f;
                gMineCell[i] = -1;
            }
        }
    }
}

static void updateFurnaceUI() {
    if (!gFurnOpen) return;
    Furnace* f = furnaceAt(gFurnCell);
    float cx = gFurnCell % MAP_W + 0.5f, cy = gFurnCell / MAP_W + 0.5f;
    float dx = gP.x - cx, dy = gP.y - cy;
    if (!f || dx * dx + dy * dy > 6.25f) { gFurnOpen = false; return; }
    if (gKeyEdge['T']) furnaceDeposit(f);
    if (gKeyEdge['G']) furnaceFuel(f);
    if (gKeyEdge['Y']) furnaceCollect(f);
    if (gKeyEdge['C']) gFurnOpen = false;
}

static void gatherCompanions() {
    int k = 0;
    for (Person& p : gPeople) {
        if (!p.isFriend) continue;
        p.x = gP.x + ((k % 3) - 1) * 0.8f;
        p.y = gP.y + (((k / 3) % 3) - 1) * 0.8f;
        k++;
    }
    for (Pet& q : gPets) { q.x = gP.x + 0.6f; q.y = gP.y + 0.6f; }
}

// Jumping into a surface cave hole takes the player down. Monsters are left behind.
static void descendCave() {
    std::swap(gMap, gUnderMap);
    gUnderground = true;
    gMonsters.clear();
    gRide = -1;
    gatherCompanions();
    rebuildDoors();
    addPopup("You descend into the caves", RGBX(170, 200, 255));
}

// Climbing a ladder brings the player back to the surface, beside the hole.
static void ascendCave() {
    int lx = (int)floorf(gP.x), ly = (int)floorf(gP.y);
    std::swap(gMap, gUnderMap);
    gUnderground = false;
    gMonsters.clear();
    float ox = lx + 0.5f, oy = ly + 0.5f;
    const int cand[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (const auto& c : cand) {
        int x = lx + c[0], y = ly + c[1];
        if (!inB(x, y)) continue;
        const Cell& cc = cellAt(x, y);
        if (objSolid(cc.obj, false) || cc.ground == G_WATER || cc.ground == G_CAVE) continue;
        ox = x + 0.5f; oy = y + 0.5f;
        break;
    }
    gP.x = ox; gP.y = oy;
    gatherCompanions();
    rebuildDoors();
    addPopup("You climb back up to the surface", RGBX(200, 230, 170));
}

static void update(float dt) {
    readKeys();
    updateFps(dt);
    musicPump();
    if (gScreen != SCR_GAME) { updateMenu(dt); return; }
    bool ctrl = down(VK_CONTROL);
    if (gKeyEdge['S'] && ctrl) {
        gSettingsOpen = !gSettingsOpen;
        if (!gSettingsOpen) saveSettings();
    }
    if (gSettingsOpen) { settingsInput(); return; }

    enforceSlotCap();
    autoFillHotbar();
    if (gKeyEdge['M']) gMinimap = !gMinimap;
    if (gKeyEdge['I']) {
        if (gInvOpen) gInvOpen = false;
        else if (!gChestOpen && !gCookOpen && !gCraftOpen && !gEmpirePanel) gInvOpen = true;
    }
    if (!gInvOpen) { gClickPending = false; gReleasePending = false; gDragging = false; }
    if (gInvOpen) { inventoryInput(); return; }

    gTime += dt;
    for (int k = 0; k < 64; k++) gWave[k] = 10.0f * sinf(gTime * 2.0f + k * 0.2f);

    float dx = 0, dy = 0;
    if (down('A') || down(VK_LEFT))  dx -= 1;
    if (down('D') || down(VK_RIGHT)) dx += 1;
    if (down('W') || down(VK_UP))    dy -= 1;
    if (down('S') || down(VK_DOWN))  dy += 1;

    if (gKeyEdge['J'] && gUnderground && cellAt((int)floorf(gP.x), (int)floorf(gP.y)).obj == O_LADDER) ascendCave();
    if (gKeyEdge['J'] && gZ <= 0.0f && !gJumping && gRide < 0) {
        gJumping = true;
        gVz = 5.2f;
        gJumpSX = gP.x; gJumpSY = gP.y;
        int hx = (int)floorf(gP.x + DXS[gP.dir] * 0.9f), hy = (int)floorf(gP.y + DYS[gP.dir] * 0.9f);
        gHoleJump = inB(hx, hy) && cellAt(hx, hy).ground == G_CAVE && !gInCave;
        playSfx(SFX_JUMP, gP.x, gP.y, 0.6f);
    }
    if (gJumping) {
        gZ += gVz * dt;
        gVz -= 14.0f * dt;
        if (gZ <= 0.0f) {
            gZ = 0.0f;
            gJumping = false;
            if (gHoleJump) {
                gP.x = (int)floorf(gJumpSX + DXS[gP.dir] * 0.9f) + 0.5f;
                gP.y = (int)floorf(gJumpSY + DYS[gP.dir] * 0.9f) + 0.5f;
                gHoleJump = false;
                descendCave();
            } else if (playerBlockedAt(gP.x, gP.y)) {
                gP.x = gJumpSX; gP.y = gJumpSY;                 // landed inside something: step back
            }
        }
    }
    if (ctrl) { dx = 0; dy = 0; }
    gMoving = (dx != 0 || dy != 0);
    if (gMoving) {
        float len = sqrtf(dx * dx + dy * dy); dx /= len; dy /= len;
        if (fabsf(dx) > fabsf(dy)) gP.dir = dx > 0 ? 1 : 3;
        else gP.dir = dy > 0 ? 2 : 0;
        bool run = down(VK_SHIFT) && gP.energy > 5.0f;
        float spd = run ? 7.0f : 4.0f;
        bool swim = isSwimming();
        if (swim) spd = run ? 3.0f : 2.2f;
        if (gP.energy <= 0.0f) spd = 2.0f;
        if (run) gP.energy = std::max(0.0f, gP.energy - dt * 8.0f * (gWeather == W_HEATWAVE ? 1.6f : 1.0f));
        if (swim) {
            gP.energy = std::max(0.0f, gP.energy - dt * 3.0f);
            if (gP.energy <= 0.0f) gHP -= dt * 2.0f;   // exhausted in the water
        }
        if (gRide >= 0 && gRide < (int)gBeasts.size()) {
            Beast& rb = gBeasts[gRide];
            float sp2 = rb.kind == 2 ? 6.0f : 3.5f;
            gOpenDoors = true;
            moveWalker(rb.x, rb.y, dx * sp2, dy * sp2, dt, true);
            gP.x = rb.x; gP.y = rb.y;
        } else if (gJumping) {
            gP.x += dx * spd * dt;
            gP.y += dy * spd * dt;
        } else {
            moveBy(dx * spd * dt, dy * spd * dt);
        }
        gWalkT += dt * (run ? 1.6f : 1.0f);
    } else {
        gP.energy = std::min(100.0f, gP.energy + dt * 4.0f);
    }

    // Health regenerates slowly after a rest from damage.
    gHurtT = std::max(0.0f, gHurtT - dt);
    gInCave = gUnderground;
    gHunger = std::max(0.0f, gHunger - dt * (gMoving ? 0.35f : 0.2f));
    if (gHunger <= 0.0f) gHP -= dt * 1.0f;
    if (gHurtT <= 0 && gHP < 100.0f) gHP = std::min(100.0f, gHP + dt * 1.5f);
    if (gHP <= 0) {
        if (gRaidActive) raidLose();
        gP.x = SPAWN_X; gP.y = SPAWN_Y; gHP = 100.0f; gP.energy = 100.0f; gSick = S_NONE; gHunger = 60.0f;
        gMonsters.clear();
        addPopup("You were defeated! Back at spawn.", RGBX(255, 120, 120));
    }

    gSwingT = std::max(0.0f, gSwingT - dt);
    gAttackCool = std::max(0.0f, gAttackCool - dt);
    gDialogT = std::max(0.0f, gDialogT - dt);

    if (gKeyEdge['C'] && !gChestOpen && !gFurnOpen) {
        if (gCookOpen) { potRefund(); gCookOpen = false; }
        else gCraftOpen = !gCraftOpen;
    }
    if (gKeyEdge[VK_F11]) toggleFullscreen();

    if (gCookOpen) {
        // Walking away from the campfire closes the pot and returns ingredients.
        float cdx = gP.x - (gCampX + 0.5f), cdy = gP.y - (gCampY + 0.5f);
        if (cdx * cdx + cdy * cdy > 4.0f) { potRefund(); gCookOpen = false; }
        std::vector<int> codes = cookableCodes();
        int base = gCookPage * 12;
        for (int k = 0; k < 12; k++)
            if (gKeyEdge[craftKeyVk(k)] && base + k < (int)codes.size()) potAdd(codes[base + k]);
        if (gKeyEdge[VK_OEM_6] && (gCookPage + 1) * 12 < (int)codes.size()) gCookPage++;
        if (gKeyEdge[VK_OEM_4] && gCookPage > 0) gCookPage--;
        if (gKeyEdge[VK_RETURN]) potCook();
        if (gKeyEdge['X']) potRefund();
    } else if (gCraftOpen) {
        int pages = (NREC + 17) / 18;
        if (gKeyEdge[VK_OEM_6] && gCraftPage < pages - 1) gCraftPage++;
        if (gKeyEdge[VK_OEM_4] && gCraftPage > 0) gCraftPage--;
        for (int r = 0; r < 18; r++) {
            int idx = gCraftPage * 18 + r;
            if (idx >= NREC) break;
            if (gKeyEdge[craftKeyVk(r)]) {
                if (canCraft(RECIPES[idx])) craftRecipe(idx);
                else setTitle("Not enough materials for " + itemName(RECIPES[idx].out));
            }
        }
    } else {
        for (int k = 0; k < HB_SLOTS; k++) {
            int vk = (k == HB_SLOTS - 1) ? '0' : '1' + k;
            if (gKeyEdge[vk]) {
                int it = slotItem(k);
                if (it >= 0) gSelItem = it;
            }
        }
        int pages = maxPage();
        if (gKeyEdge[VK_OEM_4] && gPage > 0) gPage--;
        if (gKeyEdge[VK_OEM_6] && gPage < pages - 1) gPage++;
    }

    if (gKeyEdge['F'] && !gCookOpen && !gCraftOpen) giftFacing();
    updateChestUI();
    updateFurnaceUI();
    updateEmpireUI();
    if (gKeyEdge['E'] && !gCookOpen) useFacing();
    if (gKeyEdge['E'] && gCookOpen) setDialog("Press C or walk away to leave the pot.");
    if (gKeyEdge[VK_SPACE]) attackFacing();
    if (gKeyEdge['R']) placeFacing();
    if (gKeyEdge[VK_BACK]) throwSelected(down(VK_CONTROL));
    if (gKeyEdge['Q']) eatSelected();
    fixSelection();

    if (gKeyEdge[VK_F5]) setTitle(saveGame() ? "Game saved" : "Save failed!");
    if (gKeyEdge[VK_F9]) setTitle(loadGame() ? "Game loaded" : "No valid save found");

    updateWeather(dt);
    updateSickness(dt);
    if (!gUnderground) updateAnimals(dt);
    if (!gUnderground) updateTornados(dt);
    if (!gUnderground) updateBeasts(dt);
    updateSounds(dt);
    updateFriendMining(dt);
    updateFurnaces(dt);
    updatePeople(dt);
    updatePets(dt);
    if (!gUnderground) updateStrays(dt);
    if (gRaidActive) {
        gRaidT -= dt;
        if (gRaidT <= 0.0f) { addPopup("The raid ran out of time!", RGBX(255, 120, 120)); raidLose(); }
    }
    if (gEmpireFounded) {
        int day = (int)(gTime / DAY_LENGTH);
        if (day > gLastUpkeepDay) { gLastUpkeepDay = day; applyUpkeep(); }
    }
    updateDoors(dt);
    if (!gUnderground) { updateHomesteads(); updateMonsters(dt); }
    updateDrops(dt);

    for (Popup& p : gPopups) p.life -= dt;
    gPopups.erase(std::remove_if(gPopups.begin(), gPopups.end(), [](const Popup& p) { return p.life <= 0; }), gPopups.end());

    const Cell& here = cellAt((int)floorf(gP.x), (int)floorf(gP.y));
    u32 pcol = 0;
    if (here.ground == G_SNOW) pcol = RGBX(240, 245, 255);
    else if (here.ground == G_FOREST || here.ground == G_GRASS || here.ground == G_MEADOW) pcol = RGBX(150, 190, 80);
    else if (here.ground == G_DESERT || here.ground == G_BADLANDS) pcol = RGBX(225, 195, 130);
    else if (here.ground == G_SWAMP || here.ground == G_JUNGLE) pcol = RGBX(190, 255, 120);
    updateWeatherParticles(dt, pcol);

    gAutoT += dt;
    if (gAutoT >= AUTOSAVE_EVERY) {
        gAutoT = 0;
        if (saveGame()) setTitle("Autosaved");
    }
}

// ---------- setup and Win32 plumbing ----------
static void newGame(uint32_t seed) {
    gUnderground = false;
    gPendingTamed.clear();
    generateWorld(seed);
    generateUnderground(seed ^ 0x9e37u);
    gP.x = SPAWN_X; gP.y = SPAWN_Y; gP.dir = 2; gP.energy = 100.0f;
    gHP = 100.0f;
    gTime = DAY_LENGTH * 0.3f;
    memset(gCount, 0, sizeof gCount);
    gSelItem = -1; gPage = 0; gXP = 0;
    for (int k = 0; k < 10; k++) gHotbar[k] = -1;
    gHunger = 100.0f; gEmpireFounded = false; gChests.clear(); gFurnaces.clear(); gBeasts.clear(); gRide = -1; gJumping = false; gZ = 0.0f; gFurnOpen = false; gEmpirePanel = false; gChestOpen = false; gRaidActive = false; gRaidIdx = -1;
}

static void genAllSprites() {
    for (int t = 0; t < G_COUNT; t++) genGround(t, gTex[t]);
    genBlocks();
    genTree(gTreeSpr);
    genPine(gPineSpr);
    genRock(gRockSpr);
    genOres();
    genCampfire(gCampfireSpr);
    genBed(gBedSpr);
    genChest(gChestSpr);
    genFurnace(gFurnaceSpr);
    genLadder(gLadderSpr);
    genBeastSprites();
    genDoor(false, gDoorSpr);
    genDoor(true, gDoorOpenSpr);
    genRabbit(gRabbitSpr[0]);
    genPetSprites();
    for (int y = 0; y < TILE; y++) for (int x = 0; x < TILE; x++) gRabbitSpr[1][y * TILE + x] = gRabbitSpr[0][y * TILE + (TILE - 1 - x)];
    for (int c = 0; c < NC; c++) for (int s = 0; s < 4; s++) genCrop(c, s, gCropSpr[c][s]);
    for (int d = 0; d < 4; d++) for (int f = 0; f < 4; f++) {
        genHumanoid(PAL_PLAYER, d, f, gPlayerSpr[d][f]);
        genHumanoid(PAL_ZOMBIE, d, f, gZombieSpr[d][f]);
        for (int p = 0; p < 3; p++) genHumanoid(PAL_VILLAGERS[p], d, f, gNpcSpr[p][d][f]);
    }
    for (int d = 0; d < 4; d++) genHumanoid(PAL_PLAYER, d, 4, gPlayerSpr[d][4]);
    genSlime(0, gSlimeSpr[0]);
    genSlime(1, gSlimeSpr[1]);
    genSlime(2, gSlimeSpr[2]);
    for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) gCloud[y * 256 + x] = fbm(x / 36.0f, y / 36.0f, 4242u);
}

static void refreshWorlds() {
    gWorldList.clear();
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((gWorldDir + "*.dat").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName;
            if (n.size() > 4) gWorldList.push_back(n.substr(0, n.size() - 4));
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(gWorldList.begin(), gWorldList.end());
    gWorldSel = std::clamp(gWorldSel, 0, std::max(0, (int)gWorldList.size() - 1));
}

static std::string nextWorldName() {
    for (int i = 1; i < 1000; i++) {
        std::string n = "World " + std::to_string(i);
        if (GetFileAttributesA((gWorldDir + n + ".dat").c_str()) == INVALID_FILE_ATTRIBUTES) return n;
    }
    return "World X";
}

static void enterWorld(const std::string& name) {
    gWorldName = name;
    gSavePath = gWorldDir + name + ".dat";
    gLoadedLegacy = false;
    for (int k = 0; k < 10; k++) gHotbar[k] = -1;
    bool exists = GetFileAttributesA(gSavePath.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (exists && !loadGame()) {
        setTitle("This save format is not recognised, so the world cannot be opened");
        refreshWorlds();
        return;
    }
    if (gLoadedLegacy) {
        // Keep the original file as a backup, then rewrite the world in the current format.
        CopyFileA(gSavePath.c_str(), (gSavePath + ".v10.bak").c_str(), 0);
        setScreen(SCR_GAME);
        saveGame();
        setTitle("Converted this world from the previous save format");
    }
    gMonsters.clear(); gAnimals.clear(); gStrays.clear(); gTornados.clear();
    gParticles.clear(); gPopups.clear(); gLights.clear(); gPendingDrops.clear();
    gCraftOpen = false; gCookOpen = false; gSettingsOpen = false; gDialogT = 0;
    gChestOpen = false; gEmpirePanel = false; gRaidActive = false; gRaidIdx = -1; gNaming = false;
    gUnderground = false;
    gFurnOpen = false; gRide = -1; gJumping = false; gZ = 0.0f; gBeasts.clear(); gMineT.clear(); gMineCell.clear();
    for (const TamedSave& t : gPendingTamed) {
        if (t.type == 0) {
            Beast b;
            memset(&b, 0, sizeof b);
            b.x = t.x; b.y = t.y; b.kind = t.kind; b.hp = t.hp; b.face = t.face; b.tame = 1;
            memcpy(b.name, t.name, 12);
            gBeasts.push_back(b);
        } else {
            Animal a;
            memset(&a, 0, sizeof a);
            a.x = t.x; a.y = t.y; a.hp = t.hp; a.face = t.face; a.tame = 1;
            memcpy(a.name, t.name, 12);
            gAnimals.push_back(a);
        }
    }
    gPendingTamed.clear();
    if (!exists) {
        newGame((uint32_t)GetTickCount() ^ (uint32_t)(name.size() * 2654435761u));
        gSavePath = gWorldDir + name + ".dat";
        setScreen(SCR_GAME);
        saveGame();
    }
    setScreen(SCR_GAME);
    initNpcs();
    rebuildDoors();
    fixSelection();
    gAutoT = 0;
    gLastUpkeepDay = (int)(gTime / DAY_LENGTH);
    setTitle("Welcome to " + name);
}

static void updateMenu(float dt) {
    (void)dt;
    if (gScreen == SCR_TITLE) {
        if (gKeyEdge[VK_RETURN] || gKeyEdge[VK_SPACE]) { refreshWorlds(); setScreen(SCR_WORLDS); }
        return;
    }
    if (gNaming) {
        if (gKeyEdge[VK_RETURN]) {
            std::string nm = gNameInput;
            while (!nm.empty() && nm.front() == ' ') nm.erase(nm.begin());
            while (!nm.empty() && nm.back() == ' ') nm.pop_back();
            gNaming = false;
            if (nm.empty()) nm = nextWorldName();
            else {
                std::string base = nm;
                int k = 2;
                while (GetFileAttributesA((gWorldDir + nm + ".dat").c_str()) != INVALID_FILE_ATTRIBUTES)
                    nm = base + " " + std::to_string(k++);
            }
            enterWorld(nm);
        }
        return;
    }
    int n = (int)gWorldList.size();
    if (gKeyEdge[VK_UP] && n) gWorldSel = (gWorldSel + n - 1) % n;
    if (gKeyEdge[VK_DOWN] && n) gWorldSel = (gWorldSel + 1) % n;
    if (gKeyEdge[VK_RETURN] && n) enterWorld(gWorldList[gWorldSel]);
    if (gKeyEdge['N']) { gNaming = true; gNameInput.clear(); }
    if (gKeyEdge[VK_DELETE] && n) {
        DeleteFileA((gWorldDir + gWorldList[gWorldSel] + ".dat").c_str());
        refreshWorlds();
    }
}

static void drawMenuBackground() {
    for (int y = 0; y < gH; y++) {
        float f = y / (float)gH;
        int r = (int)(24 + 200 * f * f), g = (int)(40 + 150 * f), b = (int)(90 + 40 * (1.0f - f));
        fillRect(0, y, gW, 1, mk(r, g, b));
    }
    fillEllipse(gW * 0.5f, gH * 0.62f, gH * 0.12f, gH * 0.12f, RGBX(255, 220, 150));
    for (int x = 0; x < gW; x++) {
        float hy = gH * 0.72f + sinf(x * 0.004f + gTime * 0.05f) * 30.0f + sinf(x * 0.011f) * 12.0f;
        fillRect(x, (int)hy, 1, gH - (int)hy, RGBX(70, 130, 60));
    }
}

static void drawMenu() {
    drawMenuBackground();
    if (gScreen == SCR_TITLE) {
        std::string title = "HORIZONS";
        drawText((gW - textWidth(title, 8)) / 2, (int)(gH * 0.12f), title, 8, RGBX(255, 240, 190));
        static const char* LINES[] = {
            "A block world of farms, forests, deserts and snowy peaks.",
            "Grow dozens of crops from seeds, craft tools and weapons, and cook meals.",
            "Fight monsters at night, befriend villagers and wanderers,",
            "tame dogs, cats and hamsters, and clear raider homesteads.",
            "Storms, sandstorms and snow test your health. Sleep in a bed to recover.",
            "Press Ctrl+S at any time for graphics and sound settings.",
        };
        int y = (int)(gH * 0.42f);
        for (const char* line : LINES) {
            std::string l = line;
            drawText((gW - textWidth(l, 2)) / 2, y, l, 2, RGBX(240, 236, 220));
            y += 30;
        }
        if (((int)(gTime * 2.0f)) % 2 == 0) {
            std::string prompt = "PRESS ENTER TO PLAY";
            drawText((gW - textWidth(prompt, 3)) / 2, (int)(gH * 0.85f), prompt, 3, RGBX(255, 255, 255));
        }
        return;
    }
    int w = std::min(620, gW - 40), h = 120 + 10 * 36 + 60;
    int x = (gW - w) / 2, y = std::max(40, (gH - h) / 2);
    fillRect(x, y, w, h, 0xE8101018);
    outlineRect(x, y, w, h, RGBX(200, 180, 90));
    drawText(x + 20, y + 16, "WORLDS", 3, RGBX(255, 230, 120));
    if (gWorldList.empty()) {
        drawText(x + 20, y + 80, "NO WORLDS YET - PRESS N TO CREATE ONE", 2, RGBX(220, 220, 220));
    }
    for (int i = 0; i < (int)gWorldList.size() && i < 10; i++) {
        int ry = y + 80 + i * 36;
        if (i == gWorldSel) fillRect(x + 12, ry - 6, w - 24, 30, 0xFF2A2A40);
        drawText(x + 24, ry, gWorldList[i], 2, i == gWorldSel ? RGBX(255, 255, 255) : RGBX(190, 190, 200));
    }
    drawText(x + 20, y + h - 40, "ENTER PLAY   N NEW   DEL DELETE   ESC BACK", 1, RGBX(200, 200, 200));
    if (gNaming) {
        int bw = std::min(620, gW - 40), bh = 110, bx = (gW - bw) / 2, by = gH / 2 - 55;
        fillRect(bx, by, bw, bh, 0xF0101018);
        outlineRect(bx, by, bw, bh, RGBX(200, 180, 90));
        drawText(bx + 20, by + 16, "NAME YOUR WORLD", 2, RGBX(255, 230, 120));
        std::string shown = gNameInput + (((int)(gTime * 2.0f)) % 2 ? "_" : " ");
        drawText(bx + 20, by + 50, shown, 3, RGBX(255, 255, 255));
        drawText(bx + 20, by + bh - 22, "ENTER CREATE   ESC CANCEL   (BLANK = AUTO NAME)", 1, RGBX(200, 200, 200));
    }
}

static void initGame() {
    loadSettings();
    genAllSprites();
    setScreen(SCR_TITLE);
}

static void saveSettings() {
    FILE* f = fopen(gSettingsPath.c_str(), "w");
    if (!f) return;
    fprintf(f, "shader=%d\ntexture=%d\nfps=%d\nvsync=%d\nmusic=%d\nvolume=%d\nshowfps=%d\nsfx=%d\nturbo=%d\n",
            gSet.shader, gSet.texture, gSet.fpsCap, gSet.vsync ? 1 : 0, gSet.music ? 1 : 0, gSet.musicVol,
            gSet.showFps ? 1 : 0, gSet.sfx ? 1 : 0, gSet.turbo ? 1 : 0);
    fclose(f);
}

static void loadSettings() {
    FILE* f = fopen(gSettingsPath.c_str(), "r");
    if (!f) return;
    char key[32];
    int v;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%31[^=]=%d", key, &v) != 2) continue;
        if (!strcmp(key, "shader")) gSet.shader = std::clamp(v, 0, 3);
        else if (!strcmp(key, "texture")) gSet.texture = std::clamp(v, 0, 2);
        else if (!strcmp(key, "fps")) gSet.fpsCap = std::clamp(v, 0, 4);
        else if (!strcmp(key, "vsync")) gSet.vsync = v != 0;
        else if (!strcmp(key, "music")) gSet.music = v != 0;
        else if (!strcmp(key, "volume")) gSet.musicVol = std::clamp(v, 0, 100);
        else if (!strcmp(key, "showfps")) gSet.showFps = v != 0;
        else if (!strcmp(key, "sfx")) gSet.sfx = v != 0;
        else if (!strcmp(key, "turbo")) gSet.turbo = v != 0;
    }
    fclose(f);
}

static void limitFrame(LARGE_INTEGER start, LARGE_INTEGER freq) {
    if (gSet.vsync) { DwmFlush(); return; }
    if (gSet.fpsCap == 0) return;
    double target = 1.0 / FPS_VALUES[gSet.fpsCap];
    LARGE_INTEGER now;
    for (;;) {
        QueryPerformanceCounter(&now);
        double elapsed = (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
        if (elapsed >= target) break;
        if (target - elapsed > 0.002) Sleep(1);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_LBUTTONDOWN) {
        gClickX = gMouseX = (int)(short)LOWORD(l);
        gClickY = gMouseY = (int)(short)HIWORD(l);
        gClickPending = true;
        return 0;
    }
    if (m == WM_MOUSEMOVE) { gMouseX = (int)(short)LOWORD(l); gMouseY = (int)(short)HIWORD(l); return 0; }
    if (m == WM_LBUTTONUP) {
        gMouseX = (int)(short)LOWORD(l);
        gMouseY = (int)(short)HIWORD(l);
        gReleasePending = true;
        return 0;
    }
    if (m == WM_KEYDOWN && w == VK_ESCAPE && gInvOpen) { gInvOpen = false; return 0; }
    if (m == WM_CHAR && gNaming) {
        if (w == 8) { if (!gNameInput.empty()) gNameInput.pop_back(); }
        else if (w >= 32 && w < 127 && gNameInput.size() < 20 && (isalnum((int)w) || w == ' ' || w == '-' || w == '_'))
            gNameInput.push_back((char)w);
        return 0;
    }
    if (m == WM_KEYDOWN && w == VK_ESCAPE && gNaming) { gNaming = false; return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE && gSettingsOpen) { gSettingsOpen = false; saveSettings(); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) {
        if (gScreen == SCR_GAME) { saveGame(); refreshWorlds(); setScreen(SCR_WORLDS); }
        else if (gScreen == SCR_WORLDS) setScreen(SCR_TITLE);
        else SendMessageA(h, WM_CLOSE, 0, 0);
        return 0;
    }
    if (m == WM_CLOSE) { saveGame(); DestroyWindow(h); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    initSavePath();

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "HorizonsWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(1));   // icon embedded from resources.rc
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    gHwnd = CreateWindowExA(0, "HorizonsWnd", "Horizons",
                            WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
                            nullptr, nullptr, hInst, nullptr);
    ShowWindow(gHwnd, SW_SHOW);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    initGame();
    musicInit();
    setTitle("Welcome to Horizons! Gather wood and stone, craft tools, and watch out for monsters at night.");

    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;

        RECT cr;
        GetClientRect(gHwnd, &cr);
        int nw = std::max(1, (int)(cr.right - cr.left)), nh = std::max(1, (int)(cr.bottom - cr.top));
        if (nw != gW || nh != gH) {
            gW = nw; gH = nh;
            gFrame.assign((size_t)gW * gH, 0xFF000000u);
            gParticles.clear();
        }
        bmi.bmiHeader.biWidth = gW;
        bmi.bmiHeader.biHeight = -gH;

        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - prev.QuadPart) / (float)freq.QuadPart;
        prev = now;
        if (dt > 0.1f) dt = 0.1f;

        update(dt);
        render();

        HDC dc = GetDC(gHwnd);
        StretchDIBits(dc, 0, 0, gW, gH, 0, 0, gW, gH, gFrame.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(gHwnd, dc);
        limitFrame(prev, freq);
    }
    musicShutdown();
    saveSettings();
    return 0;
}
