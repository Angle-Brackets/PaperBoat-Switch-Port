// Feeds LUS's ShaderPrewarm the current area, so each area's shaders compile during the fade
// into it rather than mid-gameplay. See fast/ShaderPrewarm.h.

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

static void RegisterShaderPrewarmHooks_Init() {
    if (!CVarGetInteger("gShaderPrewarm", SHADER_PREWARM_DEFAULT)) {
        return;
    }
    Fast::ShaderPrewarm::Init(Ship::Context::GetPathRelativeToAppDirectory("shaderlist.txt"));
    // Title screen, file select and anything common: compiled on the first frame.
    Fast::ShaderPrewarm::RequestGlobal();

    // Fires from load_map_by_IDs while the screen is faded out, including loading a save.
    REGISTER_LISTENER(OnMapLoad, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        const char* mapName = ((OnMapLoad*) event)->mapName;
        if (mapName == nullptr) {
            return;
        }
        // Map IDs are "<area>_<nn>" with a three-letter area, the same prefix the game uses for
        // the area's texture archive (wMapTexName).
        Fast::ShaderPrewarm::EnterArea(std::string(mapName).substr(0, 3));
    });
}

static RegisterShipInitFunc initFunc(RegisterShaderPrewarmHooks_Init);
