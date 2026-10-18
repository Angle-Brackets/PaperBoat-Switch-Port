#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <fast/ShaderPrewarm.h>
#include <libultraship.h>

#include <chrono>
#include <mutex>
#include <unordered_map>

#include "Engine.h"
#include "port/DevTools/ThreadWatchdog.h"
#include "port/interpolation/FrameInterpolation.h"
#include "port/os/OS.h"

#ifdef __EMSCRIPTEN__
#include <SDL2/SDL.h>
#include "port/web/WebUtils.h"
#endif
#include "port/switch/SwitchDebug.h"
#ifdef __SWITCH__
extern "C" void SwitchDebug_Log(const char* msg) {
    SPDLOG_INFO("[switch] {}", msg);
}
// Startup runs at the release log level (warn) until FinishInit lowers it to info.
extern "C" void SwitchDebug_LogWarn(const char* msg) {
    SPDLOG_WARN("[switch] {}", msg);
}
#endif
#ifdef __ANDROID__
// Redefines main() to SDL_main(), which SDLActivity calls into. The game data
// is unpacked before this process starts, by GameAssets.kt.
#include <SDL2/SDL_main.h>
#endif

MtxF sInterpolationMatrixStack[0x1000];
MtxF* gInterpolationMatrix = &sInterpolationMatrixStack[0];

extern "C" {
void load_engine_data(void);
void init_game_globals(void);

void Graphics_EnableNusysThreads(void);
void Graphics_Start(void);
void* Graphics_TaskFromList(const void* osTask);
uint32_t Graphics_TaskFlags(const void* nuTask);
int32_t Graphics_ShouldCapturePrevFrame(void);
void Graphics_DrawFrame(Gfx* backgroundList, Gfx* mainList, int32_t capturePrevFrame);
extern int32_t gOverrideFlags;
}

#define NU_SC_SWAPBUFFER 0x0001 /* nusys.h */
constexpr int32_t kOverrideDisableDrawFrame = 0x8; /* GLOBAL_OVERRIDES_DISABLE_DRAW_FRAME, enums.h */
#define NU_SC_NORDP      0x0002

namespace {

struct InterpPair {
    int prev = -1;
    int curr = -1;
    bool should = false;
    bool capturePrevFrame = true;
};
std::mutex sInterpMutex;
std::unordered_map<const void*, InterpPair> sTaskInterp;
Gfx* sPendingNoSwap = nullptr;
InterpPair sPendingNoSwapPair;

InterpPair TakePair(const void* nuTask) {
    std::lock_guard<std::mutex> lock(sInterpMutex);
    InterpPair pair;
    auto it = sTaskInterp.find(nuTask);
    if (it != sTaskInterp.end()) {
        pair = it->second;
        sTaskInterp.erase(it);
    }
    return pair;
}

int ServiceRcp() {
    OSTask* task = OS_SpTakePendingTask();
    if (task == nullptr) {
        return 0;
    }
    const void* nuTask = Graphics_TaskFromList(task);
    const uint32_t flags = Graphics_TaskFlags(nuTask);
    InterpPair pair = TakePair(nuTask);
    Gfx* list = (Gfx*) task->t.data_ptr;

    if (flags & NU_SC_SWAPBUFFER) {
        FrameInterpolation_BeginRenderPass(pair.prev, pair.curr, pair.should);
        Graphics_DrawFrame(sPendingNoSwap, list, pair.capturePrevFrame);
        FrameInterpolation_ReleasePair(pair.prev, pair.curr);
        if (sPendingNoSwap != nullptr) {
            FrameInterpolation_ReleasePair(sPendingNoSwapPair.prev, sPendingNoSwapPair.curr);
            sPendingNoSwap = nullptr;
        }
    } else {
        if (sPendingNoSwap != nullptr) {
            FrameInterpolation_ReleasePair(sPendingNoSwapPair.prev, sPendingNoSwapPair.curr);
        }
        sPendingNoSwap = list;
        sPendingNoSwapPair = pair;
    }

    if (!(flags & NU_SC_NORDP)) {
        OS_SendEventMesg(OS_EVENT_DP);
    }
    OS_SendEventMesg(OS_EVENT_SP);
    return 1;
}

} // namespace

extern "C" void port_nuGfxOnSubmit(void* nuTask) {
    InterpPair pair;
    FrameInterpolation_GetRecordingPair(&pair.prev, &pair.curr, &pair.should);
    FrameInterpolation_ClaimPair(pair.prev, pair.curr);
    pair.capturePrevFrame = Graphics_ShouldCapturePrevFrame() != 0;
    if (Graphics_TaskFlags(nuTask) & NU_SC_SWAPBUFFER) {
        FrameInterpolation_StopRecord();
    }
    std::lock_guard<std::mutex> lock(sInterpMutex);
    auto [it, inserted] = sTaskInterp.emplace(nuTask, pair);
    if (!inserted) {
        FrameInterpolation_ReleasePair(it->second.prev, it->second.curr);
        it->second = pair;
    }
}

#ifdef _WIN32
int SDL_main(int argc, char** argv) {
#else
#if defined(__cplusplus) && defined(PLATFORM_IOS)
extern "C"
#endif
    int main(int argc, char* argv[]) {
#endif
#ifdef __EMSCRIPTEN__
    // Everything the engine writes lives under /storage, an IndexedDB mount.
    // Both calls must precede anything that looks for a file there.
    WebCache_Mount("/storage");
    WebCache_Load();
#endif

    Graphics_EnableNusysThreads();
    GameEngine::Create(argc, argv);

    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());

    // Initialize game systems
    init_game_globals();
    SWITCH_MEM_CHECKPOINT("init_game_globals");
    load_engine_data();
    SWITCH_MEM_CHECKPOINT("load_engine_data");
    // Before the game draws anything: the queued global shader set, behind a progress screen.
    GameEngine::Instance->PrewarmShadersAtBoot();

    Graphics_Start();
    SWITCH_MEM_CHECKPOINT("Graphics_Start");

    // Idle-time shader prewarm: when the last one compiled while frames were being drawn.
    auto lastDrawingCompile = std::chrono::steady_clock::now();

    while (wnd->IsRunning()) {
        ThreadWatchdog_Beat(WATCHDOG_MAIN_LOOP);
#ifdef __SWITCH__
        SwitchDebug_LoopTick();
        SwitchGfxBench_Tick();
#endif
#ifdef __SWITCH__
        // Phase times for the hitch monitor: [0] StartFrame+input+render service, [1] ServiceRcp.
        auto phaseStart = std::chrono::steady_clock::now();
        auto phaseEnd = [&phaseStart](int phase) {
            const auto now = std::chrono::steady_clock::now();
            gSwitchLoopPhaseMicros[phase] +=
                std::chrono::duration_cast<std::chrono::microseconds>(now - phaseStart).count();
            phaseStart = now;
        };
#define SWITCH_PHASE_END(i) phaseEnd(i)
#else
#define SWITCH_PHASE_END(i)
#endif
        GameEngine::Instance->StartFrame();
        OS_SiService();
        GameEngine::DrainRenderService();
        SWITCH_PHASE_END(0);
        const bool ranTask = ServiceRcp();
        SWITCH_PHASE_END(1);
#undef SWITCH_PHASE_END
        if (!ranTask) {
            if (ThreadWatchdog_IsStalled(WATCHDOG_GAME_TICK)) {
                GameEngine::Instance->RenderGuiFrame();
                SDL_Delay(16);
                continue;
            }
            // While the game skips drawing (map/battle loads) queued shaders compile back to back;
            // while it draws, at most one per 30 Hz frame. (Audio tasks keep arriving during loads,
            // so "no task lately" can't tell the two apart; the game's own flag can.)
            const auto now = std::chrono::steady_clock::now();
            const bool drawing = !(gOverrideFlags & kOverrideDisableDrawFrame);
            // Predicted areas' shaders: whenever nothing is drawn, and sparsely on static screens.
            const bool background = port_shaderPrewarmBackground(drawing);
            const auto gap = std::chrono::milliseconds(drawing && background ? 200 : 33);
            bool took = false;
            if (!drawing || now - lastDrawingCompile >= gap) {
                auto interpreter = wnd->GetInterpreterWeak().lock();
                std::pair<uint64_t, uint64_t> shader;
                size_t compiled = 0;
                // Skip ones a draw has built in the meantime, so a slot isn't spent on a no-op.
                for (int tries = 0; interpreter && compiled == 0 && tries < 16 &&
                                    Fast::ShaderPrewarm::TakeOne(shader, drawing, background);
                     tries++) {
                    took = true;
                    const std::vector<std::pair<uint64_t, uint64_t>> one { shader };
                    size_t cursor = 0;
                    compiled = interpreter->PrewarmSome(one, cursor, 0.0);
                }
                if (drawing && compiled > 0) {
                    lastDrawingCompile = std::chrono::steady_clock::now();
                }
            }
            if (!took) {
                SDL_Delay(1);
            }
        }
#ifdef __EMSCRIPTEN__
        // A tab can close without warning, so sync periodically, not just on exit.
        static uint32_t lastSync = 0;
        const uint32_t now = SDL_GetTicks();
        if (now - lastSync > 5000) {
            lastSync = now;
            WebCache_Save();
        }
#endif
    }

    OS_RequestThreadExit();
    GameEngine::ShutdownRenderService();
    OS_BeginShutdown();
    OS_StopViTicker();

    GameEngine::Instance->Destroy();
    GameEngine::RelaunchIfRequested(argc, argv);
#ifdef __EMSCRIPTEN__
    // Destroy() wrote the config after the last periodic sync. Not awaited: the
    // write finishes in the page after the runtime exits.
    WebCache_SaveNoWait();
#endif
    return 0;
}
