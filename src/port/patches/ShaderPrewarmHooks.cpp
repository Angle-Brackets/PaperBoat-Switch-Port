// Feeds LUS's ShaderPrewarm the current area, so each area's shaders compile during the fade
// into it rather than mid-gameplay. See fast/ShaderPrewarm.h.

#include <chrono>
#include <cstdio>
#include <string>

#include <fast/ShaderPrewarm.h>
#include <libultraship/libultraship.h>

#include "port/Engine.h"
#include "port/hooks/Events.h"
#include "port/ShipInit.hpp"
#include "port/switch/SwitchDebug.h"

#include "common.h"
#include "filemenu.h"
#include "fio.h"
#include "game_modes.h"
#include "map.h"

#ifdef __SWITCH__
// Compiles cost 40-100 ms each and the driver can't cache them; prewarming is the only fix.
#define SHADER_PREWARM_DEFAULT 1
#else
#define SHADER_PREWARM_DEFAULT 0
#endif

// The world area to return to after a battle.
static std::string sWorldArea;
// Each save slot's area, read as file select loads the slots' metadata.
static std::string sSlotArea[4];

static std::string AreaOf(s32 areaID, s32 mapID) {
    if (areaID < 0 || areaID >= (s32) ARRAY_COUNT(gAreas) || gAreas[areaID].maps == nullptr || mapID < 0 ||
        mapID >= gAreas[areaID].mapCount) {
        return {};
    }
    return std::string(gAreas[areaID].maps[mapID].id).substr(0, 3);
}

// Battles are keyed by battle area ("btl_" + UNPACK_BTL_AREA); their debug names aren't usable.
static std::string BattleKey(int32_t battleID) {
    char key[8];
    snprintf(key, sizeof(key), "btl_%02x", (battleID >> 8) & 0xFF);
    return key;
}

static void RegisterShaderPrewarmHooks_Init() {
    if (!CVarGetInteger("gShaderPrewarm", SHADER_PREWARM_DEFAULT)) {
        return;
    }
    Fast::ShaderPrewarm::Init(Ship::Context::GetPathRelativeToAppDirectory("shaderlist.txt"));
    // Title screen, file select and anything common: compiled on the first frame.
    Fast::ShaderPrewarm::RequestGlobal();

    // Map IDs are "<area>_<nn>" with a three-letter area, the same prefix the game uses for the
    // area's texture archive (wMapTexName).
    // Before the fade-out: compile the destination while frames go undrawn, and once it's dark.
    // 250 ms is about 8 of the fade's 14 frames.
    REGISTER_LISTENER(OnMapChangeStart, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        if (const char* mapName = ((OnMapChangeStart*) event)->mapName) {
            Fast::ShaderPrewarm::PrepareArea(std::string(mapName).substr(0, 3), 250);
        }
    });
    // Fires from load_map_by_IDs while the screen is faded out, including loading a save.
    REGISTER_LISTENER(OnMapLoad, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        if (const char* mapName = ((OnMapLoad*) event)->mapName) {
            const std::string area = std::string(mapName).substr(0, 3);
            if (gGameStatusPtr->demoState == DEMO_STATE_NONE) { // the title's attract demo isn't a route
                Fast::ShaderPrewarm::NoteLink(sWorldArea, area);
            }
            sWorldArea = area;
            Fast::ShaderPrewarm::EnterArea(sWorldArea);
        }
    });
    // The pre-battle swirl runs ~1 s; past 600 ms it's mostly covered, then 10 black frames follow.
    REGISTER_LISTENER(OnBattlePrepare, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        const std::string key = BattleKey(((OnBattlePrepare*) event)->battleID);
        Fast::ShaderPrewarm::NoteLink(sWorldArea, key); // which battles happen in this area
        Fast::ShaderPrewarm::PrepareArea(key, 600);
    });
    REGISTER_LISTENER(OnBattleStart, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        Fast::ShaderPrewarm::EnterArea(BattleKey(((OnBattleStart*) event)->battleID));
    });
    // File select reads every slot's save to show it; note each one's area. (OnSaveFileLoad itself
    // is cancelled by the SaveManager, which replaces the load and then fires this.)
    REGISTER_LISTENER(OnPostSaveFileLoad, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        const int32_t slot = ((OnPostSaveFileLoad*) event)->saveSlot;
        if (slot >= 0 && slot < 4) {
            sSlotArea[slot] = AreaOf(gCurrentSaveFile.areaID, gCurrentSaveFile.mapID);
        }
    });
    // For the title screen, before file select has read anything: the area each slot was saved in.
    REGISTER_LISTENER(OnSaveFileSave, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        const SaveData* save = (const SaveData*) ((OnSaveFileSave*) event)->saveData;
        if (save != nullptr) {
            Fast::ShaderPrewarm::Remember("slot" + std::to_string(gGameStatusPtr->saveSlot),
                                          AreaOf(save->areaID, save->mapID));
        }
    });
    // Back to the world's shaders (already built, so nothing recompiles) and its recording key.
    REGISTER_LISTENER(OnBattleEnd, EVENT_PRIORITY_NORMAL, [](IEvent*) {
        if (!sWorldArea.empty()) {
            Fast::ShaderPrewarm::EnterArea(sWorldArea);
        }
    });
}

extern "C" int port_shaderPrewarmBusy(void) {
    // A cap so a stuck queue (or a list far bigger than expected) can't hold a load forever.
    constexpr auto kMaxHold = std::chrono::seconds(8);
    static bool sHolding = false;
    static std::chrono::steady_clock::time_point sHoldStart;
#ifdef __SWITCH__
    if (!SwitchTweak_Enabled(SWITCH_TWEAK_HOLD_SHADERS)) {
        return 0;
    }
#endif
    if (!Fast::ShaderPrewarm::Busy()) {
        sHolding = false;
        return 0;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!sHolding) {
        sHolding = true;
        sHoldStart = now;
    }
    return now - sHoldStart < kMaxHold;
}

// Reads game-thread state from the main thread without locking; it only steers a hint.
extern "C" int port_shaderPrewarmBackground(int drawing) {
#ifdef __SWITCH__
    if (!SwitchTweak_Enabled(SWITCH_TWEAK_BACKGROUND_SHADERS)) {
        return 0;
    }
#endif
    static std::string sHinted;
    static auto sLastInput = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const s32 mode = get_game_mode();
    const bool fileSelect = mode == GAME_MODE_FILE_SELECT && filemenu_currentMenu == FILE_MENU_MAIN &&
                            filemenu_menus[0] != nullptr && filemenu_menus[0]->initialized;

    // The save about to be loaded: highlighted in file select, else the last one used.
    std::string want;
    if (mode == GAME_MODE_TITLE_SCREEN) {
        want = Fast::ShaderPrewarm::Recall("slot" + std::to_string(gSaveGlobals.lastFileSelected & 3));
    } else if (fileSelect) {
        const s32 slot = filemenu_menus[0]->selected;
        if (slot >= 0 && slot < 4 && gSaveSlotMetadata[slot].hasData) {
            want = sSlotArea[slot];
        }
    }
    if (!want.empty() && want != sHinted) {
        SPDLOG_INFO("[shaders] background prewarm: hinting {} (game mode {})", want, mode);
        sHinted = want;
        Fast::ShaderPrewarm::Hint({ want });
    }

    if (!drawing) {
        return 1;
    }
    if (gGameStatusPtr->pressedButtons[0] | gGameStatusPtr->heldButtons[0]) {
        sLastInput = now;
    }
    switch (mode) {
        case GAME_MODE_TITLE_SCREEN:
            return gGameStatusPtr->startupState == 2; // TITLE_STATE_HOLD: static but for PRESS START
        case GAME_MODE_FILE_SELECT:
            // Static until a button moves the cursor.
            return fileSelect && filemenu_menus[0]->state == FM_MAIN_SELECT_FILE &&
                   now - sLastInput > std::chrono::milliseconds(300);
        default:
            return 0;
    }
}

static RegisterShipInitFunc initFunc(RegisterShaderPrewarmHooks_Init);
