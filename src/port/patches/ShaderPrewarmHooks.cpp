// Feeds LUS's ShaderPrewarm the current area, so each area's shaders compile during the fade
// into it rather than mid-gameplay. See fast/ShaderPrewarm.h.

#include <cstdio>
#include <string>

#include <fast/ShaderPrewarm.h>
#include <libultraship/libultraship.h>

#include "port/hooks/Events.h"
#include "port/ShipInit.hpp"

#ifdef __SWITCH__
// Compiles cost 40-100 ms each and the driver can't cache them; prewarming is the only fix.
#define SHADER_PREWARM_DEFAULT 1
#else
#define SHADER_PREWARM_DEFAULT 0
#endif

// The world area to return to after a battle.
static std::string sWorldArea;

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
            sWorldArea = std::string(mapName).substr(0, 3);
            Fast::ShaderPrewarm::EnterArea(sWorldArea);
        }
    });
    // The pre-battle swirl runs ~1 s; past 600 ms it's mostly covered, then 10 black frames follow.
    REGISTER_LISTENER(OnBattlePrepare, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        Fast::ShaderPrewarm::PrepareArea(BattleKey(((OnBattlePrepare*) event)->battleID), 600);
    });
    REGISTER_LISTENER(OnBattleStart, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        Fast::ShaderPrewarm::EnterArea(BattleKey(((OnBattleStart*) event)->battleID));
    });
    // Back to the world's shaders (already built, so nothing recompiles) and its recording key.
    REGISTER_LISTENER(OnBattleEnd, EVENT_PRIORITY_NORMAL, [](IEvent*) {
        if (!sWorldArea.empty()) {
            Fast::ShaderPrewarm::EnterArea(sWorldArea);
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterShaderPrewarmHooks_Init);
