/*  Copyright 2026 Pretendo Network contributors <pretendo.network>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "wii_karaoke_catalogfix.h"
#include "utils/logger.h"

#include <kernel/kernel.h>
#include <coreinit/cache.h>
#include <coreinit/dynload.h>
#include <coreinit/memorymap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint64_t KARAOKE_EU = 0x0005000010149600;

// Same title-base-finding approach as wiikaraoke-debug/wiikaraoke-catalogfix:
// find the loaded .text/.rodata delta by locating the known base-URL string.
constexpr uint32_t ELF_URL_VA = 0x1001D0E4;
constexpr char URL[] = "https://ssl.wahp.wah.wup.app.nintendo.net/";

// The GD0001/GF0001 reply-verification routine (the 0x0205xxxx task-object
// family) gates success on a comparison between a cached baseline value
// (task+0x70c) and a value derived from the current reply (via
// FUN_02131ec8), done at 0x0210e2a0 and stored as a bool at task+0x9c0.
// Confirmed via Cemu + live register reads: this comparison never passes
// against this (or presumably any) private server. Forcing the result to
// true (replacing the call itself with `li r3,1`) makes the game believe
// its own online-update sequence succeeded - this routine is dedicated
// entirely to that one flow, so no vtable gating is needed, unlike the
// (now-unused) Cancel()-race patch that used to also live here.
constexpr uint32_t PATCH2_VA = 0x020505c4;    // the `bl 0x0210e2a0` call
constexpr uint32_t PATCH2_INSTR = 0x38600001u; // li r3,1

// kernelCopyToGame: a plain DCStoreRange+ICInvalidateRange after a direct
// pointer write only synchronizes the calling core's own caches; this
// title's own WUPS callback doesn't necessarily run on the same core that
// executes the patched code, so the write can be invisible there. Going
// through KernelCopyData propagates the write correctly to every core.
bool kernelCopyToGame(uint32_t targetEffectiveAddr, const void *srcBuf, uint32_t len) {
    DCStoreRange((void *) srcBuf, len);
    KernelCopyData(OSEffectiveToPhysical(targetEffectiveAddr), OSEffectiveToPhysical((uint32_t) (uintptr_t) srcBuf), len);
    DCStoreRange((void *) (uintptr_t) targetEffectiveAddr, len);
    ICInvalidateRange((void *) (uintptr_t) targetEffectiveAddr, len);
    return true;
}

// DEINITIALIZE_PLUGIN must not return while this thread could still be
// running - the plugin's own code/memory can be unloaded right after,
// which it would then be executing out from under.
std::atomic<bool> g_shouldExit{false};
std::atomic<bool> g_threadCreated{false};
// True for as long as patchThreadMain's body is actually executing - set at
// its very start, cleared right before it returns. Used to bound shutdown's
// wait instead of ever blocking on OSJoinThread indefinitely (a plain
// blocking join is a confirmed real deadlock risk if this thread is ever
// genuinely stuck inside an uninterruptible OSDynLoad/KernelCopyData call).
std::atomic<bool> g_threadRunning{false};

bool applyPatchIfTitleMatches() {
    if (OSGetTitleID() != KARAOKE_EU) {
        return false;
    }

    int rpls = OSDynLoad_GetNumberOfRPLs();
    if (rpls <= 0 || rpls > 64) {
        DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: bad rpl count %d", rpls);
        return false;
    }
    static OSDynLoad_NotifyData info[64];
    if (!OSDynLoad_GetRPLInfo(0, rpls, info)) {
        DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: OSDynLoad_GetRPLInfo failed");
        return false;
    }

    for (int i = 0; i < rpls; i++) {
        if (!info[i].readAddr || info[i].readSize <= ELF_URL_VA - 0x10000000) continue;
        uint32_t at = info[i].readAddr + (ELF_URL_VA - 0x10000000);
        if (!OSIsAddressValid(at) || memcmp((void *) at, URL, sizeof(URL) - 1) != 0) continue;

        uint32_t textDelta = info[i].textAddr - 0x02000000;
        uint32_t target2 = PATCH2_VA + textDelta;
        if (!OSIsAddressValid(target2) || !OSIsAddressValid(target2 + 3)) {
            DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: patch2 target address invalid");
            return false;
        }
        uint32_t patch2Instr = PATCH2_INSTR;
        if (!kernelCopyToGame(target2, &patch2Instr, sizeof(patch2Instr))) return false;

        DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: patch 2 applied");
        return true;
    }
    return false;
}

// Patching directly from ON_APPLICATION_START can stall the system's own
// loading overlay if the hook itself blocks for the RPL scan + memory
// write. Return from the hook immediately, and do the actual work on a
// background thread that briefly waits before polling.
OSThread sPatchThread;
uint8_t sPatchStack[0x4000] __attribute__((aligned(16)));

// Sleeps up to ms in short slices, returning early (false) the moment
// g_shouldExit is set, so shutdown doesn't have to wait out a long sleep.
bool sleepOrExit(uint32_t ms) {
    constexpr uint32_t SLICE_MS = 250;
    for (uint32_t slept = 0; slept < ms; slept += SLICE_MS) {
        if (g_shouldExit.load(std::memory_order_acquire)) return false;
        OSSleepTicks(OSMillisecondsToTicks(SLICE_MS));
    }
    return !g_shouldExit.load(std::memory_order_acquire);
}

// RAII so g_threadRunning is cleared on every return path below, not just
// the final one at the bottom of the function.
struct ThreadRunningGuard {
    ThreadRunningGuard() { g_threadRunning.store(true, std::memory_order_release); }
    ~ThreadRunningGuard() { g_threadRunning.store(false, std::memory_order_release); }
};

bool waitForThreadToExit(uint32_t timeoutMs) {
    constexpr uint32_t SLICE_MS = 100;
    for (uint32_t waited = 0; waited < timeoutMs; waited += SLICE_MS) {
        if (!g_threadRunning.load(std::memory_order_acquire)) return true;
        OSSleepTicks(OSMillisecondsToTicks(SLICE_MS));
    }
    return !g_threadRunning.load(std::memory_order_acquire);
}

// One-shot: bounded retries only while waiting for the title's RPL info to
// become available, then exits for good - no perpetual watch/reassert
// loop. Confirmed the game never reverts this byte once applied, and a
// loop that keeps running and touching this memory for the entire
// lifetime of the title was itself a risk, not a benefit, during a
// separate boot-hang isolation pass.
int patchThreadMain(int, const char **) {
    ThreadRunningGuard runningGuard;
    if (!sleepOrExit(3000)) return 0;
    for (int attempt = 0; attempt < 20 && !g_shouldExit.load(std::memory_order_acquire); attempt++) {
        if (applyPatchIfTitleMatches()) break;
        if (!sleepOrExit(500)) return 0;
    }
    return 0;
}

} // namespace

void karaokeApplyCatalogFix() {
    if (OSGetTitleID() != KARAOKE_EU) {
        return;
    }

    if (!OSCreateThread(&sPatchThread, patchThreadMain, 0, nullptr, sPatchStack + sizeof(sPatchStack),
                         sizeof(sPatchStack), 0, OS_THREAD_ATTRIB_AFFINITY_CPU2)) {
        DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: OSCreateThread failed");
        return;
    }
    OSSetThreadName(&sPatchThread, "InkayWiiKaraokeCatalogFix");
    g_threadCreated.store(true, std::memory_order_release);
    OSResumeThread(&sPatchThread);
}

void karaokeCatalogFixShutdown() {
    g_shouldExit.store(true, std::memory_order_release);
    if (g_threadCreated.load(std::memory_order_acquire)) {
        if (!waitForThreadToExit(3000)) {
            DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: shutdown timed out waiting for the patch thread");
        }
    }
}
