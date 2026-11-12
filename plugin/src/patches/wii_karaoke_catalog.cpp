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

#include "wii_karaoke_catalog.h"
#include "config.h"
#include "lang.h"
#include "Notification.h"
#include "utils/logger.h"

#include <coreinit/filesystem.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <mocha/mocha.h>
#include <curl/curl.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

// WUPS_LOADER_HOOK_INIT_PLUGIN ("called when exiting the plugin loader")
// fires once per title LAUNCH, injected into that title's own about-to-start
// process - not once at system boot. We want this running as early as
// possible regardless of which title is launching (so the catalog has the
// longest possible head start before Wii Karaoke U itself ever launches),
// not gated to that title specifically.
//
// Confirmed on real hardware: letting this thread's Mocha/FS/curl work run
// through an application-start transition (any title's, not just Wii
// Karaoke U's) makes the UI hang right at that transition - almost
// certainly contention on Mocha's shared FSClient/kernel access between
// this thread and whatever the newly-starting title's own process needs at
// that exact moment. A one-shot "stop requested" flag wasn't enough: even
// signalling it couldn't interrupt a curl_easy_perform() already in
// progress, which can run for its own full timeout (up to 180s for a big
// file) regardless. g_giveUp is for stopping for good (process exiting, or
// Wii Karaoke U itself has started so further writes can't help this
// session); g_pauseUntil is a rolling "don't touch Mocha/curl before this
// tick" deadline, pushed forward by every single application start and
// checked both between attempts AND inside curl's own progress callback
// (abortIfPausedOrGivingUp below), so an in-progress transfer actually
// gets cut short immediately instead of running out its timeout.
std::atomic<bool> g_giveUp{false};
// Milliseconds-since-boot deadline, not OSTime ticks directly: this target
// has no libatomic, so a 64-bit std::atomic<OSTime> fails to link
// (__atomic_load_8/__atomic_store_8 undefined). A plain uint32_t avoids
// that; nowMs()/isPaused() below compare it wraparound-safely, which is
// all that's needed for a deadline only ever a few seconds out.
std::atomic<uint32_t> g_pauseUntilMs{0};
std::atomic<bool> g_threadStarted{false};
// Set once, the first time OSCreateThread actually succeeds this boot, and
// never cleared - unlike g_threadStarted (which the thread clears on exit
// so karaokeApplyCatalogOverlay knows not to spawn a second one), this is
// what tells shutdown whether sCatalogThread is a real, OSJoinThread-able
// handle at all. A Cafe OS thread that has already run to completion is
// still safely joinable (it just returns immediately with its result), so
// this never needs to go back to false.
std::atomic<bool> g_threadCreated{false};

constexpr uint32_t APPLICATION_START_PAUSE_MS = 5000;

uint32_t nowMs() {
    return (uint32_t) OSTicksToMilliseconds(OSGetSystemTime());
}

bool isPaused() {
    return (int32_t) (g_pauseUntilMs.load(std::memory_order_acquire) - nowMs()) > 0;
}

void pauseFor(uint32_t ms) {
    g_pauseUntilMs.store(nowMs() + ms, std::memory_order_release);
}

constexpr char SAVE_DIR[] = "/vol/storage_mlc01/usr/save/00050000/10149600/user/common";
constexpr char VERSION_URL[] = "http://wahp.nicoch.net/content/karaoke_nand/version.txt";

struct CatalogFile {
    const char *name;
    const char *url;
};

// Full-size, zero-padded, correctly-checksummed data_base01-04.bin, rebuilt
// server-side by tools/build_karaoke_nand.py whenever the catalog changes.
constexpr CatalogFile CATALOG_FILES[] = {
        {"data_base01.bin", "http://wahp.nicoch.net/content/karaoke_nand/data_base01.bin"},
        {"data_base02.bin", "http://wahp.nicoch.net/content/karaoke_nand/data_base02.bin"},
        {"data_base03.bin", "http://wahp.nicoch.net/content/karaoke_nand/data_base03.bin"},
        {"data_base04.bin", "http://wahp.nicoch.net/content/karaoke_nand/data_base04.bin"},
};

// Our own marker, not a real game file - lets us skip the ~51MB download on
// every boot and only re-fetch when the server's version.txt actually changed.
constexpr char VERSION_PATH[] = "/vol/storage_mlc01/usr/save/00050000/10149600/user/common/data_base_version.txt";

// Streams curl's received bytes straight into the open (unlocked-FSClient)
// FSFileHandle instead of buffering a whole 16MB file in RAM first - plugins
// don't have that much heap to spare. FSWriteFile advances the file position
// itself, like fwrite, so repeated calls here just append in order.
struct StreamSink {
    FSClient *client;
    FSCmdBlock *block;
    FSFileHandle handle;
    bool failed = false;
};

size_t onChunk(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *sink = (StreamSink *) userdata;
    size_t bytes = size * nmemb;
    if (sink->failed || bytes == 0) return bytes;
    auto res = FSWriteFile(sink->client, sink->block, (uint8_t *) ptr, 1, (uint32_t) bytes, sink->handle, 0, FS_ERROR_FLAG_ALL);
    if (res != (FSStatus) bytes) {
        sink->failed = true;
        return 0; // non-zero-vs-consumed mismatch tells curl to abort the transfer
    }
    return bytes;
}

struct MemSink {
    char *buf;
    size_t cap;
    size_t len = 0;
};

// Only ever used for the tiny version.txt marker, so silently capping at cap
// instead of failing is fine - a truncated version string just won't match
// and we'll re-check next boot.
size_t onMemChunk(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *sink = (MemSink *) userdata;
    size_t bytes = size * nmemb;
    if (sink->len + bytes > sink->cap) bytes = sink->cap - sink->len;
    memcpy(sink->buf + sink->len, ptr, bytes);
    sink->len += bytes;
    return size * nmemb;
}

// Confirmed on real hardware: curl_easy_perform was observed blocking
// indefinitely, past CURLOPT_CONNECTTIMEOUT - that option only bounds the
// TCP connect phase, not the name lookup, and this toolchain's libcurl
// resolver isn't threaded, so a DNS query with nowhere to go (or no answer
// coming back) can hang with no timeout of its own at all. Side-stepping
// DNS entirely with CURLOPT_RESOLVE, pinning the hostname straight to our
// own server's known IP, removes that hang outright. Update this if the
// server ever moves.
constexpr char SERVER_HOST[] = "wahp.nicoch.net";
constexpr char SERVER_IP[] = "45.157.178.35";

curl_slist *makeResolveList() {
    char entry[96];
    snprintf(entry, sizeof(entry), "%s:80:%s", SERVER_HOST, SERVER_IP);
    return curl_slist_append(nullptr, entry);
}

// Lets an application-start pause (or a permanent give-up) cut an
// in-progress transfer short immediately instead of waiting out its own
// timeout - CURLOPT_NOPROGRESS must be explicitly turned off for this to
// fire at all.
int abortIfPausedOrGivingUp(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    if (g_giveUp.load(std::memory_order_acquire)) return 1;
    if (isPaused()) return 1;
    return 0;
}

bool curlGetToMemory(const char *url, char *buf, size_t cap, size_t &outLen) {
    CURL *c = curl_easy_init();
    if (!c) return false;
    MemSink sink{buf, cap};
    curl_slist *resolve = makeResolveList();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_RESOLVE, resolve);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onMemChunk);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, abortIfPausedOrGivingUp);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    curl_slist_free_all(resolve);
    if (rc != CURLE_OK || status != 200) return false;
    outLen = sink.len;
    return true;
}

bool curlGetToFile(const char *url, FSClient *client, FSCmdBlock *block, FSFileHandle handle) {
    CURL *c = curl_easy_init();
    if (!c) return false;
    StreamSink sink{client, block, handle};
    curl_slist *resolve = makeResolveList();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_RESOLVE, resolve);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onChunk);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, abortIfPausedOrGivingUp);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 180L); // ~16MB over a slow/retried connection
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    curl_slist_free_all(resolve);
    return rc == CURLE_OK && status == 200 && !sink.failed;
}

enum class ApplyResult {
    NoSave,      // title never launched - nothing to do, don't retry
    NetworkFail, // couldn't even fetch the small version marker - network
                 // likely isn't up yet this early in boot, worth retrying
    Done,        // already current, or an update was attempted (succeeded
                 // or failed) - either way nothing more to do this boot
};

ApplyResult tryApplyCatalogOverlay() {
    if (auto res = Mocha_InitLibrary(); res != MOCHA_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: Mocha init failed (%d)", res);
        return ApplyResult::NetworkFail;
    }

    FSInit();

    FSClient client;
    FSCmdBlock block;
    FSInitCmdBlock(&block);

    if (auto res = FSAddClient(&client, FS_ERROR_FLAG_ALL); res != FS_STATUS_OK) {
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: FSAddClient failed (%d)", res);
        Mocha_DeInitLibrary();
        return ApplyResult::NetworkFail;
    }

    if (auto res = Mocha_UnlockFSClient(&client); res != MOCHA_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: failed to unlock FSClient (%d), not touching the save", res);
        FSDelClient(&client, FS_ERROR_FLAG_ALL);
        Mocha_DeInitLibrary();
        return ApplyResult::Done;
    }

    // Only ever touch a save that already exists - never create one for a
    // title that hasn't been launched yet. The directory alone isn't a
    // strong enough signal (it can exist before the game has ever written
    // its own initial catalog there), so check for the actual first catalog
    // file instead.
    char firstFilePath[160];
    snprintf(firstFilePath, sizeof(firstFilePath), "%s/%s", SAVE_DIR, CATALOG_FILES[0].name);
    FSFileHandle existsHandle;
    if (auto res = FSOpenFile(&client, &block, firstFilePath, "r", &existsHandle, FS_ERROR_FLAG_ALL); res != FS_STATUS_OK) {
        DEBUG_FUNCTION_LINE_VERBOSE("Inkay/Karaoke: no existing Wii Karaoke U save files, skipping (%d)", res);
        FSDelClient(&client, FS_ERROR_FLAG_ALL);
        Mocha_DeInitLibrary();
        return ApplyResult::NoSave;
    }
    FSCloseFile(&client, &block, existsHandle, FS_ERROR_FLAG_ALL);

    char remoteVersion[64] = {};
    size_t remoteLen = 0;
    if (!curlGetToMemory(VERSION_URL, remoteVersion, sizeof(remoteVersion) - 1, remoteLen)) {
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: failed to fetch version marker (network not up yet?)");
        FSDelClient(&client, FS_ERROR_FLAG_ALL);
        Mocha_DeInitLibrary();
        return ApplyResult::NetworkFail;
    }
    remoteVersion[remoteLen] = '\0';

    char localVersion[64] = {};
    FSFileHandle versionHandle;
    if (FSOpenFile(&client, &block, VERSION_PATH, "r", &versionHandle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        auto readBytes = FSReadFile(&client, &block, (uint8_t *) localVersion, 1, sizeof(localVersion) - 1, versionHandle, 0, FS_ERROR_FLAG_ALL);
        if (readBytes > 0) localVersion[readBytes] = '\0';
        FSCloseFile(&client, &block, versionHandle, FS_ERROR_FLAG_ALL);
    }
    // else: no marker yet (first run) - localVersion stays "", guaranteed to differ.

    if (strcmp(remoteVersion, localVersion) == 0) {
        DEBUG_FUNCTION_LINE_VERBOSE("Inkay/Karaoke: catalog already up to date (%s)", localVersion);
        FSDelClient(&client, FS_ERROR_FLAG_ALL);
        Mocha_DeInitLibrary();
        return ApplyResult::Done;
    }

    DEBUG_FUNCTION_LINE("Inkay/Karaoke: catalog update %s -> %s, downloading", localVersion, remoteVersion);

    bool allOk = true;
    for (const auto &file: CATALOG_FILES) {
        char path[160];
        snprintf(path, sizeof(path), "%s/%s", SAVE_DIR, file.name);

        FSFileHandle handle;
        auto openRes = FSOpenFile(&client, &block, path, "w", &handle, FS_ERROR_FLAG_ALL);
        if (openRes != FS_STATUS_OK) {
            DEBUG_FUNCTION_LINE("Inkay/Karaoke: failed to open %s (%d)", path, openRes);
            allOk = false;
            break;
        }

        bool ok = curlGetToFile(file.url, &client, &block, handle);
        FSCloseFile(&client, &block, handle, FS_ERROR_FLAG_ALL);

        if (!ok) {
            DEBUG_FUNCTION_LINE("Inkay/Karaoke: failed to download %s", file.name);
            allOk = false;
            break;
        }
        DEBUG_FUNCTION_LINE_VERBOSE("Inkay/Karaoke: wrote %s", file.name);
    }

    if (allOk) {
        FSFileHandle vHandle;
        if (FSOpenFile(&client, &block, VERSION_PATH, "w", &vHandle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
            FSWriteFile(&client, &block, (uint8_t *) remoteVersion, 1, (uint32_t) strlen(remoteVersion), vHandle, 0, FS_ERROR_FLAG_ALL);
            FSCloseFile(&client, &block, vHandle, FS_ERROR_FLAG_ALL);
        }
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: catalog updated to %s", remoteVersion);
        if (Config::show_startup_toast) {
            ShowNotification(get_config_strings(Config::current_language).karaoke_catalog_updated.data());
        }
    } else {
        // Leave the local version marker untouched so this retries next boot
        // rather than being stuck believing a half-applied update finished.
        // A partially-written data_baseNN.bin is harmless either way: the
        // game's own CRC32 check rejects anything that doesn't verify whole.
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: catalog update failed partway, will retry next boot");
    }

    FSDelClient(&client, FS_ERROR_FLAG_ALL);
    Mocha_DeInitLibrary();
    return ApplyResult::Done;
}

// INITIALIZE_PLUGIN fires very early in boot, before the network stack is
// necessarily up - a synchronous curl call there can fail every single boot
// for that reason alone, and blocking it on retries would stall the whole
// system. Do the actual work on a background thread instead (same pattern
// wiikaraoke-catalogfix already uses), retrying only the "couldn't even
// reach the server" case with backoff, for a bounded time, before giving up
// until next boot.
OSThread sCatalogThread;
uint8_t sCatalogStack[0x4000] __attribute__((aligned(16)));

// Sleeps up to ms, in short slices, returning early (false) the moment
// g_giveUp is set instead of always sleeping the full duration.
bool sleepOrGiveUp(uint32_t ms) {
    constexpr uint32_t SLICE_MS = 250;
    for (uint32_t slept = 0; slept < ms; slept += SLICE_MS) {
        if (g_giveUp.load(std::memory_order_acquire)) return false;
        OSSleepTicks(OSMillisecondsToTicks(SLICE_MS));
    }
    return !g_giveUp.load(std::memory_order_acquire);
}

// Blocks (checking g_giveUp) until the current application-start pause
// window has fully elapsed - called before every attempt, on top of the
// curl progress callback aborting anything already in flight when a new
// pause window gets set mid-transfer.
bool waitOutPause() {
    while (isPaused()) {
        if (g_giveUp.load(std::memory_order_acquire)) return false;
        OSSleepTicks(OSMillisecondsToTicks(250));
    }
    return !g_giveUp.load(std::memory_order_acquire);
}

int catalogThreadMain(int, const char **) {
    DEBUG_FUNCTION_LINE_VERBOSE("Inkay/Karaoke: catalog thread started");
    pauseFor(APPLICATION_START_PAUSE_MS);

    // Confirmed on real hardware (2026-11-11/12): neither this thread's own
    // curl calls nor Inkay's existing (purely local, no-network) TVii root-
    // CA UDP log lines showed up within the first ~90s of boot, even though
    // the console was online and playing an online game by the time it was
    // checked a few minutes later - so it's not just DNS/routing, something
    // about the network (or even local broadcast) genuinely isn't usable
    // for a while after INITIALIZE_PLUGIN fires. Budget several minutes of
    // retries, pausing (not just slowing down) around every application
    // start along the way - see g_pauseUntil's own comment above.
    for (int attempt = 0; attempt < 40; attempt++) {
        if (!waitOutPause()) break;
        auto result = tryApplyCatalogOverlay();
        if (g_giveUp.load(std::memory_order_acquire)) break;
        if (result != ApplyResult::NetworkFail) break;
        DEBUG_FUNCTION_LINE_VERBOSE("Inkay/Karaoke: network not ready yet, retrying (attempt %d)", attempt);
        if (!sleepOrGiveUp(15000)) break;
    }

    if (g_giveUp.load(std::memory_order_acquire)) {
        DEBUG_FUNCTION_LINE_VERBOSE("Inkay/Karaoke: gave up (deinit, or Wii Karaoke U itself started)");
    }

    curl_global_cleanup();
    g_threadStarted.store(false, std::memory_order_release);
    return 0;
}

} // namespace

void karaokeApplyCatalogOverlay() {
    if (!Config::connect_to_network) {
        return;
    }

    // INITIALIZE_PLUGIN fires once per title LAUNCH, but the thread it
    // starts now persists (pausing, not exiting) across those launches
    // within the same boot - so only ever start it once, not once per
    // title. g_threadStarted resets itself (and g_giveUp/g_pauseUntil stay
    // at their fresh defaults) on every real reboot, since that's a fresh
    // memory space.
    if (g_threadStarted.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: curl_global_init failed");
        g_threadStarted.store(false, std::memory_order_release);
        return;
    }

    if (!OSCreateThread(&sCatalogThread, catalogThreadMain, 0, nullptr, sCatalogStack + sizeof(sCatalogStack),
                         sizeof(sCatalogStack), 0, OS_THREAD_ATTRIB_AFFINITY_CPU2)) {
        DEBUG_FUNCTION_LINE("Inkay/Karaoke: OSCreateThread failed");
        curl_global_cleanup();
        g_threadStarted.store(false, std::memory_order_release);
        return;
    }
    OSSetThreadName(&sCatalogThread, "InkayWiiKaraokeCatalog");
    g_threadCreated.store(true, std::memory_order_release);
    OSResumeThread(&sCatalogThread);
}

void karaokeCatalogRequestStop() {
    g_giveUp.store(true, std::memory_order_release);
}

void karaokeCatalogShutdown() {
    // DEINITIALIZE_PLUGIN must not return while this thread could still be
    // executing - the plugin's own code/memory can be unloaded right after,
    // which a still-running thread would then be executing out from under.
    // g_giveUp makes it stop promptly (the curl progress callback aborts
    // anything in flight instead of waiting out a timeout), but signalling
    // alone doesn't guarantee it's actually finished by the time this
    // returns - only OSJoinThread does that.
    g_giveUp.store(true, std::memory_order_release);
    if (g_threadCreated.load(std::memory_order_acquire)) {
        int result = 0;
        OSJoinThread(&sCatalogThread, &result);
    }
}

void karaokeCatalogOnApplicationStart() {
    // Confirmed real bug: calling what's now karaokeCatalogRequestStop()
    // unconditionally from here stopped the catalog thread for good within
    // seconds of ANY title's own launch, including the Home Menu's - never
    // giving the network a real chance to come up. This now just pauses
    // (extends g_pauseUntil) for every application start, which the curl
    // progress callback also uses to cut an in-progress transfer short
    // immediately - and only gives up for good once Wii Karaoke U itself
    // has actually started, since that's the only case where further
    // writes genuinely can't help this session anymore.
    pauseFor(APPLICATION_START_PAUSE_MS);

    constexpr uint64_t KARAOKE_EU = 0x0005000010149600;
    if (OSGetTitleID() == KARAOKE_EU) {
        karaokeCatalogRequestStop();
    }
}
