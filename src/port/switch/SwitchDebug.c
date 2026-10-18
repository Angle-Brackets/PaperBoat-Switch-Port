// Switch-only debug plumbing: stdout/stderr over nxlink, and a crash handler that
// dumps registers and a frame-pointer backtrace there before the process dies.
// Kept in its own file because <switch.h>'s u64 clashes with the game's ultratypes.

#ifdef __SWITCH__

#include <switch.h>

#include "SwitchDebug.h"

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int sNxlinkSock = -1;

// Runs before main() and before static constructors, so the earliest log lines
// make it across. nxlinkStdio only connects when the NRO was sent by nxlink.
void userAppInit(void) {
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
}

// Bumped by LUS's GL backend each time it compiles a shader program.
extern uint32_t gLusShaderCompileCount;
extern uint64_t gLusShaderCompileMicros;
extern uint32_t gLusShaderCacheHits; // programs loaded from shadercache/ instead of compiled
// std::atomic counters in LUS O2rArchive.cpp; same representation as the plain integers.
extern volatile uint32_t gLusArchiveLoadCount;
extern volatile uint64_t gLusArchiveLoadMicros;
extern volatile uint64_t gLusArchiveLoadBytes;
extern volatile uint32_t gLusArchiveOpenCount;
extern volatile uint64_t gLusArchiveOpenMicros;

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

static void PrintMemoryStats(const char* tag) {
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    struct mallinfo mi = mallinfo();
    MonitorLog("%s mem: used %lu / %lu MiB (%lu MiB free), malloc in use %u MiB, heap arena %u MiB, "
           "shaders compiled %u (%lu ms total), from cache %u",
           tag, used >> 20, total >> 20, (total - used) >> 20, (unsigned) (mi.uordblks >> 20),
           (unsigned) (mi.arena >> 20), gLusShaderCompileCount, gLusShaderCompileMicros / 1000, gLusShaderCacheHits);
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

// Size of LUS's on-disk shader cache (<app dir>/shadercache, cwd is the app dir).
static void PrintShaderCacheStats(void) {
    DIR* dir = opendir("shadercache");
    if (dir == NULL) {
        MonitorLog("shadercache: none yet");
        return;
    }
    unsigned files = 0;
    unsigned long long bytes = 0, largest = 0;
    struct dirent* ent;
    char path[300];
    while ((ent = readdir(dir)) != NULL) {
        snprintf(path, sizeof(path), "shadercache/%s", ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            files++;
            bytes += st.st_size;
            if ((unsigned long long) st.st_size > largest) {
                largest = st.st_size;
            }
        }
    }
    closedir(dir);
    MonitorLog("shadercache: %u files, %llu KiB total, avg %llu KiB, largest %llu KiB", files, bytes >> 10,
               files ? (bytes / files) >> 10 : 0, largest >> 10);
}

// Called once per main-loop iteration. The main thread renders, so a long
// iteration is a visible hitch; shader compiles inside it are reported with it.
void SwitchDebug_LoopTick(void) {
    static u64 sLastTick = 0, sLastStatsTick = 0;
    static uint32_t sLastShaderCount = 0;
    static uint64_t sLastShaderMicros = 0;
    static uint32_t sLastLoadCount = 0, sLastOpenCount = 0;
    static uint64_t sLastLoadMicros = 0, sLastLoadBytes = 0, sLastOpenMicros = 0;

    const u64 now = armGetSystemTick();
    if (sLastTick == 0) {
        sLastTick = sLastStatsTick = now;
        PrintMemoryStats("start");
        PrintShaderCacheStats();
        return;
    }

    const u64 elapsedNs = armTicksToNs(now - sLastTick);
    if (elapsedNs > HITCH_THRESHOLD_NS) {
        // Archive loads can run on other threads; their time is reported, not assumed to be on this one.
        MonitorLog("hitch: %lu ms | shaders +%u (%lu ms) | archive loads +%u (%lu KiB, %lu ms) | zip opens +%u "
               "(%lu ms)",
               elapsedNs / 1000000, gLusShaderCompileCount - sLastShaderCount,
               (gLusShaderCompileMicros - sLastShaderMicros) / 1000, gLusArchiveLoadCount - sLastLoadCount,
               (gLusArchiveLoadBytes - sLastLoadBytes) >> 10, (gLusArchiveLoadMicros - sLastLoadMicros) / 1000,
               gLusArchiveOpenCount - sLastOpenCount, (gLusArchiveOpenMicros - sLastOpenMicros) / 1000);
    }
    sLastShaderCount = gLusShaderCompileCount;
    sLastShaderMicros = gLusShaderCompileMicros;
    sLastLoadCount = gLusArchiveLoadCount;
    sLastLoadMicros = gLusArchiveLoadMicros;
    sLastLoadBytes = gLusArchiveLoadBytes;
    sLastOpenCount = gLusArchiveOpenCount;
    sLastOpenMicros = gLusArchiveOpenMicros;
    sLastTick = now;

    if (armTicksToNs(now - sLastStatsTick) > STATS_INTERVAL_NS) {
        sLastStatsTick = now;
        PrintMemoryStats("periodic");
    }
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
