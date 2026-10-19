#include "PaperboatMenu.h"

#ifdef __SWITCH__
#include "port/Engine.h"
#include "port/switch/SwitchDebug.h"
#endif

namespace PaperboatGui {

using namespace UIWidgets;

// Switch tab: the port's Switch-specific loading and rendering tweaks, each one switchable in case it
// misbehaves, plus the benchmarks. Empty elsewhere.
void PaperboatMenu::AddMenuSwitch() {
#ifdef __SWITCH__
    AddMenuEntry("Switch", CVAR_SETTING("Menu.SwitchSidebarSection"));

    AddSidebarEntry("Switch", "Performance", 2);
    WidgetPath path = { "Switch", "Performance", SECTION_COLUMN_1 };
    AddWidget(path, "Loading", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Hold Loads for Shaders", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_HOLD_SHADERS)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Keeps the black screen of map loads and battle starts up until the area's shaders are "
            "compiled, so they don't stutter in once the scene appears. Capped at 8 seconds."));
    AddWidget(path, "Smooth Fade-In", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_SMOOTH_FADE)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Waits for the first (slow) frames of a new map or battle to pass before fading in, so the "
            "fade plays smoothly instead of popping. Capped at 2 seconds."));
    AddWidget(path, "Preload Textures", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_PRELOAD_TEXTURES)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Reads a new area's textures and characters' sprites from the SD card during the black screen, "
            "instead of when they first appear (which hitches). Capped at 3 extra seconds."));
    AddWidget(path, "Predict Next Area's Shaders", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_BACKGROUND_SHADERS)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Compiles shaders for areas you're likely to visit next while nothing is being drawn "
            "(loads, title and file select), so their black screens are shorter."));
    AddWidget(path, "Spread Threads Across Cores", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_THREAD_PINNING)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Runs the game, audio and asset loading on their own CPU cores, leaving one core to the "
            "renderer. Applies after restarting the game."));

    path.column = SECTION_COLUMN_2;
    AddWidget(path, "Renderer", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Stream Vertex Buffers", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_STREAM_VBO)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Uploads vertices into a few reused buffers instead of a new one per draw. Saves about 1 ms "
            "of CPU per frame."));
    AddWidget(path, "Skip Unchanged Shader Settings", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_UNIFORM_CACHE)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Only sends shader settings to the GPU when they changed."));
    AddWidget(path, "Keep Batches on Same Texture", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_SKIP_SAME_BIND)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Doesn't split a draw when a texture is re-selected but hasn't changed."));
    AddWidget(path, "Skip Repeated Triangle Setup", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_TWEAK_TRI_FAST_PATH)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Reuses the previous triangle's draw setup when nothing changed in between, instead of "
            "rebuilding it for every triangle. Turn off if something draws with the wrong colors or textures."));

    AddSidebarEntry("Switch", "Benchmarks", 1);
    path = { "Switch", "Benchmarks", SECTION_COLUMN_1 };
    AddWidget(path, "Run Graphics Benchmark", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { SwitchGfxBench_Start(); })
        .Options(ButtonOptions().Tooltip(
            "Close the menu and stand somewhere busy: steps through resolution and MSAA settings for "
            "the current mode (~12 s each), logs frame times, then restores your settings."));
    AddWidget(path, "Run Render A/B Test", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { SwitchGfxBench_StartRenderAB(); })
        .Options(ButtonOptions().Tooltip(
            "Developer test: close the menu and stand still somewhere busy. Toggles the renderer tweaks "
            "one at a time (~12 s each, about 80 s total) and logs frame times."));

    AddSidebarEntry("Switch", "Updates", 1);
    path = { "Switch", "Updates", SECTION_COLUMN_1 };
    AddWidget(path, "", WIDGET_TEXT).PreFunc([](WidgetInfo& info) {
        char tag[64], error[256];
        SwitchUpdater_Info(tag, sizeof(tag), error, sizeof(error));
        std::string status = std::string("Installed: ") + SwitchUpdater_OwnVersion() + "\n";
        switch (SwitchUpdater_State()) {
            case SWITCH_UPDATE_CHECKING:
                status += "Checking for updates...";
                break;
            case SWITCH_UPDATE_CURRENT:
                status += std::string("Up to date (latest release: ") + tag + ").";
                break;
            case SWITCH_UPDATE_AVAILABLE:
                status += std::string("Update available: ") + tag + ".";
                break;
            case SWITCH_UPDATE_DOWNLOADING:
                status += "Downloading " + std::string(tag) + "... " + std::to_string(SwitchUpdater_Progress()) + "%";
                break;
            case SWITCH_UPDATE_READY:
                status += std::string(tag) + " is downloaded. Restart PaperBoat to finish updating.";
                break;
            case SWITCH_UPDATE_FAILED:
                status += std::string("Update check failed: ") + error;
                break;
            default:
                break;
        }
        info.name = status;
    });
    AddWidget(path, "Check for Updates on Launch", WIDGET_CVAR_CHECKBOX)
        .CVar(SWITCH_CHECK_UPDATES)
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Asks GitHub for the latest release once per launch and shows a notification if there's a "
            "newer one. Nothing is downloaded until you choose to."));
    AddWidget(path, "Check Now", WIDGET_BUTTON)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) {
            const int state = SwitchUpdater_State();
            info.isHidden = state == SWITCH_UPDATE_AVAILABLE || state == SWITCH_UPDATE_DOWNLOADING ||
                            state == SWITCH_UPDATE_READY;
        })
        .Callback([](WidgetInfo& info) { SwitchUpdater_Check(); });
    AddWidget(path, "Download Update", WIDGET_BUTTON)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { info.isHidden = SwitchUpdater_State() != SWITCH_UPDATE_AVAILABLE; })
        .Callback([](WidgetInfo& info) { SwitchUpdater_Download(); })
        .Options(ButtonOptions().Tooltip(
            "Downloads the new paperboat.nro next to the current one. Your saves, settings and game files "
            "are not touched."));
    AddWidget(path, "Restart and Update", WIDGET_BUTTON)
        .RaceDisable(false)
        .PreFunc([](WidgetInfo& info) { info.isHidden = SwitchUpdater_State() != SWITCH_UPDATE_READY; })
        .Callback([](WidgetInfo& info) {
            Ship::Context::GetRawInstance()->GetConsoleVariables()->Save();
            GameEngine::RequestRelaunch();
            Ship::Context::GetRawInstance()->GetWindow()->Close();
        });
#endif
}

} // namespace PaperboatGui
