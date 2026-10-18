// Switch-only debug plumbing: stdout/stderr over nxlink, and a crash handler that
// dumps registers and a frame-pointer backtrace there before the process dies.
// Kept in its own file because <switch.h>'s u64 clashes with the game's ultratypes.

#ifdef __SWITCH__

#include <switch.h>

#include "SwitchDebug.h"

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int sNxlinkSock = -1;

// Runs before main() and before static constructors, so the earliest log lines
// make it across. nxlinkStdio only connects when the NRO was sent by nxlink.
void userAppInit(void) {
    romfsInit(); // paperboat.o2r is bundled in the NRO
    if (R_SUCCEEDED(socketInitializeDefault())) {
        sNxlinkSock = nxlinkStdio();
        if (sNxlinkSock >= 0) {
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
            printf("[switch] nxlink stdio connected\n");
            fflush(stdout);
        }
    }
}

void userAppExit(void) {
    if (sNxlinkSock >= 0) {
        close(sNxlinkSock);
        sNxlinkSock = -1;
    }
    socketExit();
    romfsExit();
}

// Bumped by LUS's GL backend each time it compiles a shader program.
extern uint32_t gLusShaderCompileCount;
extern uint64_t gLusShaderCompileMicros;
// std::atomic counters in LUS gfx_opengl.cpp (background shader compiler).
extern volatile uint32_t gLusShaderAsyncCompiled;
extern volatile uint32_t gLusShaderAsyncSkippedDraws;
// std::atomic counters in LUS O2rArchive.cpp; same representation as the plain integers.
extern volatile uint32_t gLusArchiveLoadCount;
extern volatile uint64_t gLusArchiveLoadMicros;
extern volatile uint64_t gLusArchiveLoadBytes;
extern volatile uint32_t gLusArchiveOpenCount;
extern volatile uint64_t gLusArchiveOpenMicros;
// LUS interpreter.cpp: render-thread texture import (decode + upload) time.
extern uint32_t gLusTexImportCount;
extern uint64_t gLusTexImportMicros;
// LUS gfx_sdl2.cpp: presents, frame-pacer sleep and SDL_GL_SwapWindow (vsync) time.
extern uint32_t gLusPresentCount;
extern uint64_t gLusPaceMicros;
extern uint64_t gLusSwapMicros;
extern uint64_t gLusSwapMaxMicros;

uint64_t gSwitchLoopPhaseMicros[2];

// LUS: GPU frame time (gfx_opengl.cpp) and CPU render time (Fast3dWindow.cpp), running totals.
extern uint32_t gLusGpuFrames;
extern uint32_t gLusGpuDropped;
extern uint64_t gLusGpuMicros;
extern uint64_t gLusGpuGameMicros;
extern uint64_t gLusRenderCpuMicros;
extern uint32_t gLusSlowPresents;
// LUS render-cost attribution: Interpreter::Run (Fast3dWindow.cpp), backend draws and batch breaks
// (interpreter.cpp), per-draw uniform and vertex upload time (gfx_opengl.cpp). Running totals.
extern uint64_t gLusRunMicros;
extern uint64_t gLusDrawMicros;
extern uint64_t gLusUniformMicros;
extern uint64_t gLusVboMicros;
extern uint64_t gLusVboMaxMicros;
extern uint64_t gLusDrawMaxMicros;
extern uint32_t gLusTriFast;     // triangles that took the GfxSpTri1 fast path
extern uint32_t gLusTriFastMiss; // verify mode: fast-path candidates whose setup changed something
extern uint32_t gLusDraws;
extern uint32_t gLusTris;
extern uint32_t gLusOps;
extern uint32_t gLusTexMiss;
extern uint32_t gLusTexSame;
extern uint32_t gLusPalUploads;
extern uint32_t gLusPrgSwitch;
extern uint32_t gLusFlushWhy[8]; // tex, cc, prg, unif, mtx, light, pal, other

static void MonitorLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

static void PrintFrameStats(const char* tag) {
    static uint32_t sFrames, sDropped, sPresents, sSlow;
    static uint64_t sGpu, sGpuGame, sCpu;
    static uint64_t sRun, sDraw, sUnif, sVbo;
    static uint32_t sTriFast, sTriFastMiss;
    static uint32_t sDraws, sTris, sOps, sTexImports, sTexMiss, sTexSame, sPal, sPrg, sWhy[8];
    const uint32_t frames = gLusGpuFrames - sFrames, presents = gLusPresentCount - sPresents;
    if (frames && presents) {
        const double gpu = (gLusGpuMicros - sGpu) / 1000.0 / frames;
        const double game = (gLusGpuGameMicros - sGpuGame) / 1000.0 / frames;
        MonitorLog("%s frames: gpu avg %.2f ms (game %.2f, composite %.2f) over %u (+%u unmeasured) | "
                   "cpu render avg %.2f ms | %u presents, %u slow",
                   tag, gpu, game, gpu - game, frames, gLusGpuDropped - sDropped,
                   (gLusRenderCpuMicros - sCpu) / 1000.0 / presents, presents, gLusSlowPresents - sSlow);
        // Per present: with frame interpolation the game list is rendered once per present, not per tick.
        // "binds" counts every texture import call (cache hits included); "same" are hits on the
        // texture already bound in that slot.
        const double p = presents;
        double why[8];
        for (int i = 0; i < 8; i++) {
            why[i] = (gLusFlushWhy[i] - sWhy[i]) / p;
        }
        MonitorLog("%s draw: run %.2f ms (gl %.2f: unif %.2f vbo %.2f; max draw %.1f vbo %.1f) | %.0f draws %.0f tris %.0f ops | "
                   "tex %.1f binds %.1f miss %.1f same | pal %.1f prg %.1f | fast %.0f miss %u | why tex %.1f cc %.1f prg %.1f "
                   "unif %.1f mtx %.1f light %.1f pal %.1f other %.1f",
                   tag, (gLusRunMicros - sRun) / 1000.0 / p, (gLusDrawMicros - sDraw) / 1000.0 / p,
                   (gLusUniformMicros - sUnif) / 1000.0 / p, (gLusVboMicros - sVbo) / 1000.0 / p,
                   gLusDrawMaxMicros / 1000.0, gLusVboMaxMicros / 1000.0,
                   (gLusDraws - sDraws) / p, (gLusTris - sTris) / p, (gLusOps - sOps) / p,
                   (gLusTexImportCount - sTexImports) / p, (gLusTexMiss - sTexMiss) / p,
                   (gLusTexSame - sTexSame) / p, (gLusPalUploads - sPal) / p, (gLusPrgSwitch - sPrg) / p,
                   (gLusTriFast - sTriFast) / p, gLusTriFastMiss - sTriFastMiss, why[0],
                   why[1], why[2], why[3], why[4], why[5], why[6], why[7]);
    }
    sRun = gLusRunMicros;
    sDraw = gLusDrawMicros;
    sUnif = gLusUniformMicros;
    sVbo = gLusVboMicros;
    sTriFast = gLusTriFast;
    sTriFastMiss = gLusTriFastMiss;
    gLusDrawMaxMicros = 0;
    gLusVboMaxMicros = 0;
    sDraws = gLusDraws;
    sTris = gLusTris;
    sOps = gLusOps;
    sTexImports = gLusTexImportCount;
    sTexMiss = gLusTexMiss;
    sTexSame = gLusTexSame;
    sPal = gLusPalUploads;
    sPrg = gLusPrgSwitch;
    memcpy(sWhy, gLusFlushWhy, sizeof(sWhy));
    sFrames = gLusGpuFrames;
    sDropped = gLusGpuDropped;
    sPresents = gLusPresentCount;
    sSlow = gLusSlowPresents;
    sGpu = gLusGpuMicros;
    sGpuGame = gLusGpuGameMicros;
    sCpu = gLusRenderCpuMicros;
}

#define HITCH_THRESHOLD_NS (40ULL * 1000000ULL) // a 60 fps frame is 16.7 ms
#define STATS_INTERVAL_NS (10ULL * 1000000000ULL)

// spdlog sink (port/Game.cpp): reaches both nxlink and logs/Paperboat.log.
void SwitchDebug_Log(const char* msg);
void SwitchDebug_LogWarn(const char* msg);

static void MonitorLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void MonitorLog(const char* fmt, ...) {
    char buf[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    SwitchDebug_Log(buf);
}

// withMalloc: mallinfo() walks the whole heap under the malloc lock (~40 ms), stalling every thread
// that allocates, so the periodic report leaves it out.
static void PrintMemoryStats(const char* tag, bool withMalloc) {
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (withMalloc) {
        struct mallinfo mi = mallinfo();
        MonitorLog("%s mem: used %lu / %lu MiB (%lu MiB free), malloc in use %u MiB, heap arena %u MiB", tag,
                   used >> 20, total >> 20, (total - used) >> 20, (unsigned) (mi.uordblks >> 20),
                   (unsigned) (mi.arena >> 20));
    }
    MonitorLog("%s shaders: inline %u (%lu ms), background %u, draws skipped %u", tag, gLusShaderCompileCount,
               gLusShaderCompileMicros / 1000, gLusShaderAsyncCompiled, gLusShaderAsyncSkippedDraws);
    MonitorLog("%s io: %u archive loads (%lu KiB, %lu ms), %u zip opens (%lu ms)", tag,
           gLusArchiveLoadCount, gLusArchiveLoadBytes >> 10, gLusArchiveLoadMicros / 1000, gLusArchiveOpenCount,
           gLusArchiveOpenMicros / 1000);
}

void SwitchDebug_MemCheckpoint(const char* label) {
    static size_t sLastInUse = 0;
    struct mallinfo mi = mallinfo();
    const size_t inUse = mi.uordblks;
    char buf[160];
    snprintf(buf, sizeof(buf), "checkpoint %-26s malloc in use %6.1f MiB (%+7.1f MiB)", label, inUse / 1048576.0,
             ((double) inUse - (double) sLastInUse) / 1048576.0);
    SwitchDebug_LogWarn(buf);
    sLastInUse = inUse;
}

// Called once per main-loop iteration. The main thread renders, so a long
// iteration is a visible hitch; shader compiles inside it are reported with it.
static u64 ThreadCpuTicks(void) {
    u64 ticks = 0;
    // Pseudo-handle for the calling thread; 13.0.0+ id, with the 1.0.0-12.1.0 one as fallback.
    if (R_FAILED(svcGetInfo(&ticks, InfoType_ThreadTickCount, 0xFFFF8000, UINT64_MAX))) {
        svcGetInfo(&ticks, InfoType_ThreadTickCountDeprecated, 0xFFFF8000, UINT64_MAX);
    }
    return ticks;
}

void SwitchDebug_LoopTick(void) {
    static u64 sLastTick = 0, sLastStatsTick = 0, sLastCpu = 0;
    static uint32_t sLastTexCount = 0, sLastPresents = 0;
    static uint64_t sLastTexMicros = 0, sLastPaceMicros = 0, sLastSwapMicros = 0;
    static uint32_t sLastShaderCount = 0;
    static uint64_t sLastShaderMicros = 0;
    static uint32_t sLastLoadCount = 0, sLastOpenCount = 0, sLastAsync = 0;
    static uint64_t sLastLoadMicros = 0, sLastLoadBytes = 0, sLastOpenMicros = 0;

    const u64 now = armGetSystemTick();
    if (sLastTick == 0) {
        sLastTick = sLastStatsTick = now;
        sLastCpu = ThreadCpuTicks();
        PrintMemoryStats("start", true);
        return;
    }

    const u64 elapsedNs = armTicksToNs(now - sLastTick);
    const u64 cpu = ThreadCpuTicks();
    if (elapsedNs > HITCH_THRESHOLD_NS) {
        // cpu < wall - pace - swap means this thread was runnable but another thread had its core.
        MonitorLog("hitch: %lu ms | cpu %lu ms | presents +%u, pace %lu ms, swap %lu ms (max %lu ms) | "
                   "tex imports +%u (%lu ms) | phases frame %lu ms, rcp %lu ms",
                   elapsedNs / 1000000, armTicksToNs(cpu - sLastCpu) / 1000000, gLusPresentCount - sLastPresents,
                   (gLusPaceMicros - sLastPaceMicros) / 1000, (gLusSwapMicros - sLastSwapMicros) / 1000,
                   gLusSwapMaxMicros / 1000, gLusTexImportCount - sLastTexCount,
                   (gLusTexImportMicros - sLastTexMicros) / 1000, gSwitchLoopPhaseMicros[0] / 1000,
                   gSwitchLoopPhaseMicros[1] / 1000);
        // Archive loads can run on other threads; their time is reported, not assumed to be on this one.
        MonitorLog("hitch: %lu ms | shaders +%u (%lu ms), background +%u | archive loads +%u (%lu KiB, %lu ms) | "
               "zip opens +%u (%lu ms)",
               elapsedNs / 1000000, gLusShaderCompileCount - sLastShaderCount,
               (gLusShaderCompileMicros - sLastShaderMicros) / 1000, gLusShaderAsyncCompiled - sLastAsync,
               gLusArchiveLoadCount - sLastLoadCount,
               (gLusArchiveLoadBytes - sLastLoadBytes) >> 10, (gLusArchiveLoadMicros - sLastLoadMicros) / 1000,
               gLusArchiveOpenCount - sLastOpenCount, (gLusArchiveOpenMicros - sLastOpenMicros) / 1000);
    }
    sLastShaderCount = gLusShaderCompileCount;
    sLastAsync = gLusShaderAsyncCompiled;
    sLastShaderMicros = gLusShaderCompileMicros;
    sLastLoadCount = gLusArchiveLoadCount;
    sLastLoadMicros = gLusArchiveLoadMicros;
    sLastLoadBytes = gLusArchiveLoadBytes;
    sLastOpenCount = gLusArchiveOpenCount;
    sLastOpenMicros = gLusArchiveOpenMicros;
    sLastTexCount = gLusTexImportCount;
    sLastTexMicros = gLusTexImportMicros;
    sLastPresents = gLusPresentCount;
    sLastPaceMicros = gLusPaceMicros;
    sLastSwapMicros = gLusSwapMicros;
    gLusSwapMaxMicros = 0;
    gSwitchLoopPhaseMicros[0] = gSwitchLoopPhaseMicros[1] = 0;

    if (armTicksToNs(now - sLastStatsTick) > STATS_INTERVAL_NS) {
        sLastStatsTick = now;
        PrintMemoryStats("periodic", false);
        PrintFrameStats("periodic");
    }
    // Taken after the report so its own logging isn't measured as the next iteration's hitch.
    sLastTick = armGetSystemTick();
    sLastCpu = ThreadCpuTicks();
}

alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

// snprintf + write rather than printf: the faulting thread may hold stdout's lock.
static void CrashPrint(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void CrashPrint(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        write(STDERR_FILENO, buf, len < (int) sizeof(buf) ? len : (int) sizeof(buf) - 1);
    }
}

static const char* ExceptionName(u32 desc) {
    switch (desc) {
        case ThreadExceptionDesc_InstructionAbort:
            return "Instruction Abort";
        case ThreadExceptionDesc_MisalignedPC:
            return "Misaligned PC";
        case ThreadExceptionDesc_MisalignedSP:
            return "Misaligned SP";
        case ThreadExceptionDesc_SError:
            return "SError";
        case ThreadExceptionDesc_BadSVC:
            return "Bad SVC";
        case ThreadExceptionDesc_Trap:
            return "Trap (data abort / undefined instruction)";
        default:
            return "Other";
    }
}

// Prints "addr (Paperboat+0xoff)" when addr is inside our module; run.sh symbolizes those.
static void CrashPrintAddr(const char* label, u64 addr, u64 base, u64 size) {
    if (addr >= base && addr < base + size) {
        CrashPrint("%s 0x%016lx (Paperboat+0x%lx)\n", label, addr, addr - base);
    } else {
        CrashPrint("%s 0x%016lx\n", label, addr);
    }
}

void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    // The module's code segment starts at its base (first PT_LOAD is at 0).
    MemoryInfo info;
    u32 page;
    u64 base = 0, size = 0;
    if (R_SUCCEEDED(svcQueryMemory(&info, &page, (u64) (uintptr_t) &__libnx_exception_handler))) {
        base = info.addr;
        size = 0x2000000; // generous upper bound; covers text + rodata + data
    }

    CrashPrint("\n========== PAPERBOAT CRASH ==========\n");
    CrashPrint("Exception: %s (0x%x)\n", ExceptionName(ctx->error_desc), ctx->error_desc);
    CrashPrint("Module base: 0x%016lx\n", base);
    CrashPrintAddr("PC: ", ctx->pc.x, base, size);
    CrashPrintAddr("LR: ", ctx->lr.x, base, size);
    CrashPrint("SP:  0x%016lx  FP: 0x%016lx  FAR: 0x%016lx\n", ctx->sp.x, ctx->fp.x, ctx->far.x);
    for (int i = 0; i < 29; i++) {
        CrashPrint("X%02d: 0x%016lx%s", i, ctx->cpu_gprs[i].x, (i % 2) ? "\n" : "   ");
    }
    CrashPrint("\nBacktrace:\n");

    // AArch64 frame record: fp[0] = caller's fp, fp[1] = return address.
    u64 fp = ctx->fp.x;
    u64 stackLo = ctx->sp.x;
    u64 stackHi = ctx->sp.x + 0x400000;
    for (int i = 0; i < 48 && fp >= stackLo && fp < stackHi && (fp & 0xF) == 0; i++) {
        const u64* frame = (const u64*) (uintptr_t) fp;
        char label[16];
        snprintf(label, sizeof(label), "  #%02d", i);
        CrashPrintAddr(label, frame[1], base, size);
        if (frame[0] <= fp) {
            break;
        }
        fp = frame[0];
    }
    CrashPrint("=====================================\n");
    // Returning hands the exception back to the kernel, so Atmosphère still writes its report.
}

#endif // __SWITCH__
