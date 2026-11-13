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
#include <cstdio>
#include <cstring>

namespace {

constexpr uint64_t KARAOKE_EU = 0x0005000010149600;

// kernelCopyToGame: a plain DCStoreRange+ICInvalidateRange after a direct
// pointer write only synchronizes the calling core's own caches; this
// title's own WUPS callback doesn't necessarily run on the same core that
// executes the patched code, so the write can be invisible there. Going
// through KernelCopyData (real header here, unlike the standalone plugin's
// dynload workaround for a toolchain that was missing libkernel) propagates
// the write correctly to every core.
bool kernelCopyToGame(uint32_t targetEffectiveAddr, const void *srcBuf, uint32_t len) {
    DCStoreRange((void *) srcBuf, len);
    KernelCopyData(OSEffectiveToPhysical(targetEffectiveAddr), OSEffectiveToPhysical((uint32_t) (uintptr_t) srcBuf), len);
    DCStoreRange((void *) (uintptr_t) targetEffectiveAddr, len);
    ICInvalidateRange((void *) (uintptr_t) targetEffectiveAddr, len);
    return true;
}

// Same title-base-finding approach as wiikaraoke-debug/wiikaraoke-catalogfix:
// find the loaded .text/.rodata delta by locating the known base-URL string.
constexpr uint32_t ELF_URL_VA = 0x1001D0E4;
constexpr char URL[] = "https://ssl.wahp.wah.wup.app.nintendo.net/";

// First bug: a global network-command-manager housekeeping pass
// (FUN_02127264) runs every frame and, as part of routine list bookkeeping
// unrelated to any individual download's own progress, unconditionally
// calls Cancel() (vtable slot +0x34) on any cGameNetCmd*-family request
// object the first time it gets mirrored into its tracking list -
// including the GD0001/GF0001 catalog-database-update requests,
// mid-download. Confirmed via decompile + live hardware memory scan (the
// request's own "done" flag reads true while its own byte-count field is
// still far below the real file size). This is a race, not a timeout or
// size check, so it can't be worked around from the server side.
//
// The fix: NOP out the 5 instructions that load the Cancel() vtable slot
// and call it (0x021276e8-0x021276f8 in the EU binary's own address
// space), right after the FUN_021271e0 call that adds the request to the
// tracking list - this keeps that bookkeeping intact and only removes the
// harmful premature-cancel side effect. Nothing after that call site reads
// the registers it used, so this is safe.
constexpr uint32_t PATCH_VA = 0x021276e8;     // first of the 5 original instructions
constexpr uint32_t RETURN_VA = 0x021276fc;    // untouched loop-continuation code
constexpr uint32_t VTABLE_VA = 0x1001cd24;    // cGameNetCmdGetData's own final vtable

// Second, independent bug: the GD0001/GF0001 reply-verification routine
// (the 0x0205xxxx task-object family) gates success on a comparison
// between a cached baseline value (task+0x70c) and a value derived from
// the current reply (via FUN_02131ec8), done at 0x0210e2a0 and stored as a
// bool at task+0x9c0. Confirmed via Cemu + live register reads: this
// comparison never passes against this (or presumably any) private
// server, which is what ultimately produces ecode=32/"140-1760" after the
// first bug above is fixed. Forcing the result to true (replacing the call
// itself with `li r3,1`) let a full GD0001->GL0001->GF0001 sequence
// complete end-to-end for the first time. Unlike the Cancel() bug, this
// routine isn't a generic/shared helper used by unrelated features, so no
// vtable gating is needed - it only ever runs as part of this one
// catalog-verification flow.
constexpr uint32_t PATCH2_VA = 0x020505c4;    // the `bl 0x0210e2a0` call
constexpr uint32_t PATCH2_INSTR = 0x38600001u; // li r3,1

// RETRACTED third patch (used to live here as PATCH3A/3B): originally
// believed FUN_021702d0 was the NAND-cache *writer*, gated on task+0x9bc so
// it only ran once ever, and patched that gate to force it to re-run every
// invocation. CONFIRMED WRONG by reading FUN_021702d0's real decompiled
// body (with proper WUT types): it's the *loader* - reads the 4
// data_baseNN.bin files into a scratch buffer purely to checksum-validate
// and copy into the live in-memory catalog, with no write side effect at
// all. The real writer is FUN_0217038c, called only from FUN_02221ffc
// (patch 4 below) and from a catalog rebuild state machine whose caller
// was never located statically or via heap-scan. So this patch only forced
// a redundant re-*read* on every invocation - harmless, but it never
// caused anything to actually persist. Left here as a note so a future
// attempt doesn't re-discover the same wrong lead.

// Fourth patch - DISABLED, see below. FUN_02221ffc deletes
// data_base01-04.bin and data_fb.bin, then reads the disc's own bundled
// content/global/tsv/<lang>/wiiudb.dat and song_begin_db.dat straight into
// a fresh buffer and writes that straight back out - i.e. it's a
// disc-content factory-reload, with no reference anywhere in its body to
// any downloaded/merged catalog data. Confirmed empirically (direct FTP
// byte compare) that data_base01.bin's content could be made byte-identical
// to the disc's own de/wiiudb.dat, consistent with this function (or an
// equivalent) being what undoes any custom/downloaded content.
//
// Disabled 2026-11-13: the standalone `wiikaraoke-installer` app now writes
// the catalog directly into the save data entirely outside the game's own
// process/boot sequence (no online update ever runs for it to undo). With
// this no-op active, the game hung on its own boot screen when launched
// against a catalog written that way - leading theory is that the game's
// boot sequence also relies on FUN_02221ffc running for some
// normalization/integrity step the first time it sees unfamiliar catalog
// content, which no-opping it entirely also skips, not just the disc-reload
// side effect. Only patches 1 and 2 (the race fix and the forced-success
// reply check) are still needed/applied - those two alone are what the
// user asked to keep, to let the game's own online-update sequence (run
// from inside its own process, when relevant) believe it completed
// successfully, without this patch's broader, apparently load-bearing,
// side effects.

extern "C" void wkcf_cave();
extern "C" uint32_t wkcf_vtable_const;
extern "C" uint32_t wkcf_return_addr;

// If the game re-copies this code region from the original RPX at some
// later loading stage, our patch could get silently reverted before the
// player ever triggers a catalog-update attempt. These let the thread
// below keep watching the patched bytes after the initial write and
// reassert them if they ever change back.
volatile bool g_watchActive = false;
// DEINITIALIZE_PLUGIN must not return while this thread could still be
// running - the plugin's own code/memory can be unloaded right after,
// which it would then be executing out from under. g_watchActive alone
// never becomes false on its own (the watch loop below runs until the
// title itself exits), so a separate signal is needed to actually stop it.
std::atomic<bool> g_shouldExit{false};
std::atomic<bool> g_threadCreated{false};
// True for as long as patchThreadMain's body is actually executing - set at
// its very start, cleared right before it returns. Used to bound shutdown's
// wait instead of ever blocking on OSJoinThread indefinitely (see
// karaokeCatalogFixShutdown's own comment).
std::atomic<bool> g_threadRunning{false};
uint32_t g_watchTarget = 0;
uint32_t g_watchExpected[4];
uint32_t g_watch2Target = 0;
uint32_t g_watch2Expected = 0;

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
        uint32_t dataDelta = info[i].readAddr - 0x10000000; // .rodata/.data share this delta
        uint32_t target = PATCH_VA + textDelta;
        if (!OSIsAddressValid(target) || !OSIsAddressValid(target + 15)) {
            DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: target address range invalid");
            return false;
        }

        wkcf_vtable_const = VTABLE_VA + dataDelta;
        wkcf_return_addr = RETURN_VA + textDelta;

        uint32_t caveAddr = (uint32_t) (uintptr_t) &wkcf_cave;
        uint32_t farBranch[4] = {
                0x3C000000u | ((caveAddr >> 16) & 0xFFFFu), // lis   r0, caveAddr@h
                0x60000000u | (caveAddr & 0xFFFFu),         // ori   r0, r0, caveAddr@l
                0x7C0903A6u,                                 // mtctr r0
                0x4E800420u,                                 // bctr
        };

        if (!kernelCopyToGame(target, farBranch, sizeof(farBranch))) return false;

        uint32_t target2 = PATCH2_VA + textDelta;
        if (!OSIsAddressValid(target2) || !OSIsAddressValid(target2 + 3)) {
            DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: patch2 target address invalid");
            return false;
        }
        uint32_t patch2Instr = PATCH2_INSTR;
        if (!kernelCopyToGame(target2, &patch2Instr, sizeof(patch2Instr))) return false;

        // Patch 4 deliberately NOT applied anymore - see its own comment
        // above for why.

        DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: patches 1 and 2 applied");

        g_watchTarget = target;
        memcpy(g_watchExpected, farBranch, sizeof(farBranch));
        g_watch2Target = target2;
        g_watch2Expected = patch2Instr;
        g_watchActive = true;
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
// the final one at the bottom of the function - there are several early
// returns, and a plain store right before "the" return would miss them.
struct ThreadRunningGuard {
    ThreadRunningGuard() { g_threadRunning.store(true, std::memory_order_release); }
    ~ThreadRunningGuard() { g_threadRunning.store(false, std::memory_order_release); }
};

// Confirmed real bug on hardware: a plain blocking OSJoinThread() in
// karaokeCatalogFixShutdown can hang forever. kernelCopyToGame (KernelCopyData)
// and the OSDynLoad calls in applyPatchIfTitleMatches aren't interruptible
// like a curl transfer would be, so if this thread is ever genuinely stuck
// inside one of those - waiting on some IOSU-level resource that only frees
// up once this process finishes exiting - OSJoinThread blocking for it IS
// the deadlock: shutdown can't return until this thread exits, this thread
// can't get past its blocked call until the process finishes exiting, and
// the process can't finish exiting until shutdown returns.
bool waitForThreadToExit(uint32_t timeoutMs) {
    constexpr uint32_t SLICE_MS = 100;
    for (uint32_t waited = 0; waited < timeoutMs; waited += SLICE_MS) {
        if (!g_threadRunning.load(std::memory_order_acquire)) return true;
        OSSleepTicks(OSMillisecondsToTicks(SLICE_MS));
    }
    return !g_threadRunning.load(std::memory_order_acquire);
}

int patchThreadMain(int, const char **) {
    ThreadRunningGuard runningGuard;
    if (!sleepOrExit(3000)) return 0;
    for (int attempt = 0; attempt < 20 && !g_shouldExit.load(std::memory_order_acquire); attempt++) {
        if (applyPatchIfTitleMatches()) break;
        if (!sleepOrExit(500)) return 0;
    }

    // Keep watching: if something later re-copies this code region from the
    // original RPX, reassert the patch and log it so we know this happened.
    while (g_watchActive && !g_shouldExit.load(std::memory_order_acquire)) {
        if (!sleepOrExit(2000)) break;
        volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) g_watchTarget;
        bool reverted = false;
        for (int w = 0; w < 4; w++) {
            if (p[w] != g_watchExpected[w]) { reverted = true; break; }
        }
        if (reverted) {
            DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: patch 1 was reverted, reapplying");
            kernelCopyToGame(g_watchTarget, g_watchExpected, sizeof(g_watchExpected));
        }

        volatile uint32_t *p2 = (volatile uint32_t *) (uintptr_t) g_watch2Target;
        if (*p2 != g_watch2Expected) {
            DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: patch 2 was reverted, reapplying");
            uint32_t v = g_watch2Expected;
            kernelCopyToGame(g_watch2Target, &v, sizeof(v));
        }

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
            DEBUG_FUNCTION_LINE("Inkay/KaraokeFix: shutdown timed out waiting for the watch thread");
        }
    }
}
