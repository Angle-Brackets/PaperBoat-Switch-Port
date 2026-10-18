// Separate handheld and docked graphics settings on Switch. Internal resolution and MSAA are single
// CVars shared by both modes, so each mode keeps its own copy and the active one is applied on
// docking/undocking. Changing them in the menu updates the current mode's copy.

#ifdef __SWITCH__

#include <switch.h>

#include <spdlog/spdlog.h>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>

#include "port/switch/SwitchDebug.h"

namespace {
struct Profile {
    const char* name;
    const char* resCVar;
    const char* msaaCVar;
    float defaultRes;
    int defaultMsaa;
};
// Handheld (720p) from the benchmark: 1.5x renders at 1080p for ~6.4 ms GPU, well within budget.
// Docked (1080p) isn't benchmarked yet; native is the same pixel count as handheld 1.5x.
constexpr Profile kHandheld = { "handheld", "gSwitch.Handheld.InternalResolution", "gSwitch.Handheld.MSAA", 1.5f, 1 };
constexpr Profile kDocked = { "docked", "gSwitch.Docked.InternalResolution", "gSwitch.Docked.MSAA", 1.0f, 1 };

const Profile* sActive = nullptr;

const Profile& Current() {
    return appletGetOperationMode() == AppletOperationMode_Console ? kDocked : kHandheld;
}

float ProfileRes(const Profile& p) {
    return CVarGetFloat(p.resCVar, p.defaultRes);
}

int ProfileMsaa(const Profile& p) {
    return CVarGetInteger(p.msaaCVar, p.defaultMsaa);
}
} // namespace

extern "C" void SwitchGfxProfile_Init(void) {
    // Before the window exists: the renderer reads the shared CVars at init.
    sActive = &Current();
    CVarSetFloat(CVAR_INTERNAL_RESOLUTION, ProfileRes(*sActive));
    CVarSetInteger(CVAR_MSAA_VALUE, ProfileMsaa(*sActive));
}

extern "C" const char* SwitchGfxProfile_ModeName(void) {
    return sActive != nullptr ? sActive->name : Current().name;
}

extern "C" void SwitchGfxProfile_Tick(void) {
    if (sActive == nullptr || SwitchGfxBench_Running()) {
        return; // the benchmark drives the shared CVars itself and restores them
    }
    const Profile& mode = Current();
    const float res = CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1.0f);
    const int msaa = CVarGetInteger(CVAR_MSAA_VALUE, 1);

    if (&mode == sActive) {
        // Changed in the menu: that's this mode's setting now.
        if (res != ProfileRes(mode) || msaa != ProfileMsaa(mode)) {
            CVarSetFloat(mode.resCVar, res);
            CVarSetInteger(mode.msaaCVar, msaa);
            CVarSave();
        }
        return;
    }

    sActive = &mode;
    const float newRes = ProfileRes(mode);
    const int newMsaa = ProfileMsaa(mode);
    SPDLOG_INFO("[switch] {} mode: internal resolution {:.2f}, MSAA {}", mode.name, newRes, newMsaa);
    CVarSetFloat(CVAR_INTERNAL_RESOLUTION, newRes);
    CVarSetInteger(CVAR_MSAA_VALUE, newMsaa);
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    window->SetResolutionMultiplier(newRes);
    window->SetMsaaLevel(newMsaa);
}

#endif
