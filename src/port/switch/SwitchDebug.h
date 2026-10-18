#pragma once

// Switch-only debug hooks (port/switch/SwitchDebug.c). The macros compile away elsewhere.

#ifdef __SWITCH__
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void SwitchDebug_LoopTick(void);
// Graphics benchmark (SwitchGfxBench.cpp): Start from the menu, Tick once per main-loop iteration.
void SwitchGfxBench_Start(void);
void SwitchGfxBench_Tick(void);
int SwitchGfxBench_Running(void);
// Per-mode (handheld/docked) resolution + MSAA (SwitchGfxProfile.cpp): Init before the window
// is created, Tick once per main-loop iteration.
void SwitchGfxProfile_Init(void);
void SwitchGfxProfile_Tick(void);
const char* SwitchGfxProfile_ModeName(void);
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
