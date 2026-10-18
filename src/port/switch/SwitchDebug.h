#pragma once

// Switch-only debug hooks (port/switch/SwitchDebug.c). The macros compile away elsewhere.

#ifdef __SWITCH__
#ifdef __cplusplus
extern "C" {
#endif
void SwitchDebug_LoopTick(void);
// Logs malloc usage and the delta since the previous checkpoint, to attribute startup memory.
void SwitchDebug_MemCheckpoint(const char* label);
#ifdef __cplusplus
}
#endif
#define SWITCH_MEM_CHECKPOINT(label) SwitchDebug_MemCheckpoint(label)
#else
#define SWITCH_MEM_CHECKPOINT(label) ((void) 0)
#endif
