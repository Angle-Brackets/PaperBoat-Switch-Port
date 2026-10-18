// Switch tab tweaks: the port's Switch-specific load/render changes, each behind a CVar so players can
// turn one off if it misbehaves (Settings menu > Switch). Defaults are on.

#ifdef __SWITCH__

#include <libultraship/bridge/consolevariablebridge.h>

#include "port/switch/SwitchDebug.h"

extern "C" {
// LUS draw-overhead cuts (1 = on); also driven by the render A/B benchmark.
extern int gLusStreamVbo;
extern int gLusUniformCache;
extern int gLusSkipSameBind;
extern int gLusTriFastPath; // 0 off, 1 on, 2 verify
}

extern "C" int SwitchTweak_Enabled(const char* cvar) {
    return CVarGetInteger(cvar, 1) != 0;
}

extern "C" void SwitchTweaks_Tick(void) {
    if (SwitchGfxBench_Running()) {
        return;
    }
    gLusStreamVbo = SwitchTweak_Enabled(SWITCH_TWEAK_STREAM_VBO);
    gLusUniformCache = SwitchTweak_Enabled(SWITCH_TWEAK_UNIFORM_CACHE);
    gLusSkipSameBind = SwitchTweak_Enabled(SWITCH_TWEAK_SKIP_SAME_BIND);
    gLusTriFastPath = CVarGetInteger(SWITCH_TWEAK_TRI_FAST_PATH, 1);
}

#endif
