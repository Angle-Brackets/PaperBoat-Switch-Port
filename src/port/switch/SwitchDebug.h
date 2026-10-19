#pragma once

// Switch-only debug hooks (port/switch/SwitchDebug.c). The macros compile away elsewhere.

#ifdef __SWITCH__
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void SwitchDebug_LoopTick(void);
// Graphics benchmark (SwitchGfxBench.cpp): Start from the menu, Tick once per main-loop iteration.
void SwitchGfxBench_Start(void);
// Same, but keeps the resolution and toggles the LUS draw-overhead cuts one at a time.
void SwitchGfxBench_StartRenderAB(void);
void SwitchGfxBench_Tick(void);
int SwitchGfxBench_Running(void);
// Per-mode (handheld/docked) resolution + MSAA (SwitchGfxProfile.cpp): Init before the window
// is created, Tick once per main-loop iteration.
void SwitchGfxProfile_Init(void);
void SwitchGfxProfile_Tick(void);
const char* SwitchGfxProfile_ModeName(void);
// Switch tab tweaks (SwitchTweaks.cpp): each is a CVar, 1 = on (the default). Tick applies the renderer
// ones to LUS once per main-loop iteration, except while a benchmark owns them.
#define SWITCH_TWEAK_HOLD_SHADERS "gSwitch.Tweaks.HoldForShaders"
#define SWITCH_TWEAK_SMOOTH_FADE "gSwitch.Tweaks.SmoothFadeIn"
#define SWITCH_TWEAK_BACKGROUND_SHADERS "gSwitch.Tweaks.BackgroundShaders"
#define SWITCH_TWEAK_STREAM_VBO "gSwitch.Tweaks.StreamVbo"
#define SWITCH_TWEAK_UNIFORM_CACHE "gSwitch.Tweaks.UniformCache"
#define SWITCH_TWEAK_SKIP_SAME_BIND "gSwitch.Tweaks.SkipSameBind"
#define SWITCH_TWEAK_THREAD_PINNING "gSwitch.Tweaks.ThreadPinning"
#define SWITCH_TWEAK_TRI_FAST_PATH "gSwitch.Tweaks.TriFastPath"
#define SWITCH_TWEAK_PRELOAD_TEXTURES "gSwitch.Tweaks.PreloadTextures"
int SwitchTweak_Enabled(const char* cvar);
// Updater (SwitchUpdater.cpp): checks the latest GitHub release and downloads its NRO, swapped in on exit.
#define SWITCH_CHECK_UPDATES "gSwitch.CheckForUpdates"
enum {
    SWITCH_UPDATE_IDLE,
    SWITCH_UPDATE_CHECKING,
    SWITCH_UPDATE_CURRENT,
    SWITCH_UPDATE_AVAILABLE,
    SWITCH_UPDATE_DOWNLOADING,
    SWITCH_UPDATE_READY,
    SWITCH_UPDATE_FAILED,
};
int SwitchUpdater_State(void);
int SwitchUpdater_Progress(void);
const char* SwitchUpdater_OwnVersion(void);
void SwitchUpdater_Info(char* latestTag, size_t tagSize, char* error, size_t errorSize);
void SwitchUpdater_Check(void);
void SwitchUpdater_Download(void);
void SwitchUpdater_Tick(void);
void SwitchUpdater_ApplyPending(void);
void SwitchTweaks_Tick(void);
// Logs malloc usage and the delta since the previous checkpoint, to attribute startup memory.
void SwitchDebug_MemCheckpoint(const char* label);
// Main-loop time per phase, accumulated by Game.cpp and reported with each hitch:
// [0] StartFrame + input + render service, [1] ServiceRcp (draw + present).
extern uint64_t gSwitchLoopPhaseMicros[2];
#ifdef __cplusplus
}
#endif
#define SWITCH_MEM_CHECKPOINT(label) SwitchDebug_MemCheckpoint(label)
#else
#define SWITCH_MEM_CHECKPOINT(label) ((void) 0)
#endif
