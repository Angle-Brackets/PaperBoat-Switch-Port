// Switch graphics benchmark: steps through internal resolution x MSAA settings for the current
// mode (handheld/docked), ~10 s each, and logs GPU/CPU frame times per setting, to choose the
// Switch defaults. Started from Settings > Graphics; runs once the menu has been closed for 2 s
// and restores the previous settings afterwards. Nothing is saved to the config.

#ifdef __SWITCH__

#include <switch.h>

#include <atomic>
#include <spdlog/spdlog.h>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include "port/switch/SwitchDebug.h"

extern "C" {
// LUS counters (running totals; the benchmark owns resetting the maxes).
extern uint32_t gLusGpuFrames;
extern uint64_t gLusGpuMicros;
extern uint64_t gLusGpuGameMicros;
extern uint64_t gLusGpuMaxMicros;
extern uint64_t gLusRenderCpuMicros;
extern uint64_t gLusRenderCpuMaxMicros;
extern uint32_t gLusPresentCount;
extern uint64_t gLusPresentIntervalMicros;
extern uint64_t gLusPresentIntervalMaxMicros;
extern uint32_t gLusSlowPresents;
}

namespace {
struct Config {
    float res;
    int msaa;
};
constexpr Config kHandheld[] = { { 1.0f, 1 },  { 1.0f, 2 },  { 1.0f, 4 },  { 1.25f, 1 }, { 1.25f, 2 },
                                 { 1.25f, 4 }, { 1.5f, 1 },  { 1.5f, 2 },  { 1.5f, 4 } };
constexpr Config kDocked[] = { { 0.75f, 1 }, { 0.75f, 2 }, { 1.0f, 1 }, { 1.0f, 2 } };

constexpr u64 kStartDelayNs = 2000000000ULL; // after the menu closes
constexpr u64 kWarmupNs = 1500000000ULL;     // framebuffer reallocation, texture re-imports
constexpr u64 kMeasureNs = 10000000000ULL;

struct Snapshot {
    uint32_t gpuFrames, presents, slowPresents;
    uint64_t gpuMicros, gpuGameMicros, cpuMicros, intervalMicros;
};

std::atomic<bool> sRequested { false };
int sIndex = -1;
const Config* sConfigs = nullptr;
int sConfigCount = 0;
bool sMeasuring = false;
u64 sPhaseStart = 0;
float sSavedRes = 1.0f;
int sSavedMsaa = 1;
AppletOperationMode sMode;
Snapshot sStart;

Snapshot Take() {
    return { gLusGpuFrames,     gLusPresentCount,    gLusSlowPresents,         gLusGpuMicros,
             gLusGpuGameMicros, gLusRenderCpuMicros, gLusPresentIntervalMicros };
}

void Apply(float res, int msaa) {
    // The renderer only reads these CVars at init; the window setters apply them live.
    CVarSetFloat(CVAR_INTERNAL_RESOLUTION, res);
    CVarSetInteger(CVAR_MSAA_VALUE, msaa);
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    window->SetResolutionMultiplier(res);
    window->SetMsaaLevel(msaa);
}

void Report(const Config& config) {
    const Snapshot end = Take();
    const uint32_t gpuFrames = end.gpuFrames - sStart.gpuFrames;
    const uint32_t presents = end.presents - sStart.presents;
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    SPDLOG_INFO("[gfxbench] {} {}x{} res {:.2f} msaa {} | gpu avg {:.2f} ms (game {:.2f}) max {:.2f} | "
                "cpu avg {:.2f} max {:.2f} | present avg {:.2f} max {:.2f} ms | {} presents, {} slow",
                sMode == AppletOperationMode_Console ? "docked" : "handheld", window->GetWidth(),
                window->GetHeight(), config.res, config.msaa,
                gpuFrames ? (end.gpuMicros - sStart.gpuMicros) / 1000.0 / gpuFrames : 0.0,
                gpuFrames ? (end.gpuGameMicros - sStart.gpuGameMicros) / 1000.0 / gpuFrames : 0.0,
                gLusGpuMaxMicros / 1000.0, presents ? (end.cpuMicros - sStart.cpuMicros) / 1000.0 / presents : 0.0,
                gLusRenderCpuMaxMicros / 1000.0,
                presents ? (end.intervalMicros - sStart.intervalMicros) / 1000.0 / presents : 0.0,
                gLusPresentIntervalMaxMicros / 1000.0, presents, end.slowPresents - sStart.slowPresents);
}

void Finish(const char* why) {
    Apply(sSavedRes, sSavedMsaa);
    sIndex = -1;
    SPDLOG_INFO("[gfxbench] {}; restored res {:.2f} msaa {}", why, sSavedRes, sSavedMsaa);
}
} // namespace

extern "C" void SwitchGfxBench_Start(void) {
    sRequested = true;
}

extern "C" void SwitchGfxBench_Tick(void) {
    const u64 now = armGetSystemTick();
    if (sIndex < 0) {
        if (!sRequested) {
            return;
        }
        auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
        if (gui->GetMenuOrMenubarVisible()) {
            sPhaseStart = now; // wait for the menu to close, so ImGui and nav input don't skew it
            return;
        }
        if (armTicksToNs(now - sPhaseStart) < kStartDelayNs) {
            return;
        }
        sRequested = false;
        sMode = appletGetOperationMode();
        const bool docked = sMode == AppletOperationMode_Console;
        sConfigs = docked ? kDocked : kHandheld;
        sConfigCount = docked ? (int) std::size(kDocked) : (int) std::size(kHandheld);
        sSavedRes = CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1.0f);
        sSavedMsaa = CVarGetInteger(CVAR_MSAA_VALUE, 1);
        SPDLOG_INFO("[gfxbench] start: {} configs, {} mode", sConfigCount, docked ? "docked" : "handheld");
        sIndex = 0;
        Apply(sConfigs[0].res, sConfigs[0].msaa);
        sPhaseStart = now;
        sMeasuring = false;
        return;
    }
    if (appletGetOperationMode() != sMode) {
        Finish("aborted: docked/undocked mid-run");
        return;
    }
    if (!sMeasuring) {
        if (armTicksToNs(now - sPhaseStart) >= kWarmupNs) {
            sStart = Take();
            gLusGpuMaxMicros = gLusRenderCpuMaxMicros = gLusPresentIntervalMaxMicros = 0;
            sMeasuring = true;
            sPhaseStart = now;
        }
        return;
    }
    if (armTicksToNs(now - sPhaseStart) < kMeasureNs) {
        return;
    }
    Report(sConfigs[sIndex]);
    if (++sIndex < sConfigCount) {
        Apply(sConfigs[sIndex].res, sConfigs[sIndex].msaa);
        sPhaseStart = now;
        sMeasuring = false;
        return;
    }
    Finish("done");
}

#endif
