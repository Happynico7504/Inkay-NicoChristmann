/*  Copyright 2026 NicoChristmann

    Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
    granted, provided that the above copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
    INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
    IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
    PERFORMANCE OF THIS SOFTWARE.

    content/app/HexagonRe_for_1.0j/js/tlb/nwf.js is a REAL shipped disc file,
    not something we serve - and it turns out to be Nintendo's own
    desktop-browser development polyfill for the native `window.nwf` API
    (keyboard-to-GamePad-button mapping, console.log everywhere, and a real
    visible <iframe> standing in for the GamePad's second screen since a
    desktop browser has no such thing). Its own guard
    (`if (typeof window.nwf !== "undefined") return;`) should make this
    no-op on real hardware once the console's native ext-* WebKit extensions
    inject the real nwf object - but on this revival, for a reason not yet
    understood (likely something about how/when those native extensions
    initialize for this specific title), the real nwf never shows up in time
    and this entire polyfill runs for real, including its iframe - which
    Nintendo's own code only ever positioned at left:1000px (not fully
    off-screen), clearly written assuming a narrower dev browser viewport
    than our 1280px TV canvas. That's what was bleeding the GamePad's own
    body background (the hex-dot world map) and some of its content onto
    the real TV output - confirmed live via a screenshot comparison.

    Same technique as wiistreetu_ca.cpp: swap the file's bytes via FSOpenFile/
    FSReadFile/FSReadFileWithPos, same 3 regional title IDs. The replacement
    (data/wiistreetu_tlb_nwf.js) is byte-identical to the original except the
    iframe's left/top are pushed to -9999px instead of 1000px/0 - minimal,
    reversible, and padded (by shortening one harmless console.log call) to
    the EXACT original length, same reason as the cert bundle: no
    FSGetStatFile patch exists, so the game's read buffer is sized from the
    real file's stat size. */

#include "config.h"
#include "utils/logger.h"

#include <function_patcher/function_patching.h>

#include <coreinit/filesystem.h>
#include <coreinit/title.h>

#include <cstring>
#include <vector>
#include <optional>

#include "wiistreetu_tlb_nwf_js.h" // generated at buildtime from data/wiistreetu_tlb_nwf.js

#define WII_STREET_U_TID_J 0x000500001012D300
#define WII_STREET_U_TID_E 0x0005000010132000
#define WII_STREET_U_TID_P 0x0005000010132100

static bool isWiiStreetUTitle() {
    return (OSGetTitleID() != 0 && (
        OSGetTitleID() == WII_STREET_U_TID_J ||
        OSGetTitleID() == WII_STREET_U_TID_E ||
        OSGetTitleID() == WII_STREET_U_TID_P
        ));
}

static std::optional<FSFileHandle> wiistreetu_tlb_handle{};
std::vector<PatchedFunctionHandle> wiistreetu_tlb_patches;

DECL_FUNCTION(int, FSOpenFile_wiiStreetUTlb, FSClient *client, FSCmdBlock *block, char *path, const char *mode, uint32_t *handle,
              int error) {
    if (!isWiiStreetUTitle() || !Config::connect_to_network) {
        return real_FSOpenFile_wiiStreetUTlb(client, block, path, mode, handle, error);
    }

    // Substring match, not exact - wiistreetu_ca.cpp's ca-bundle.pem path
    // turned out to have an extra leading/double slash versus what a naive
    // reading of config.xml suggested, so this one is deliberately loose
    // from the start rather than guessing the exact string again.
    if (strstr(path, "tlb/nwf.js") != nullptr) {
        int ret = real_FSOpenFile_wiiStreetUTlb(client, block, path, mode, handle, error);
        wiistreetu_tlb_handle = *handle;
        DEBUG_FUNCTION_LINE("Inkay/WiiStreetU: Found tlb/nwf.js (path=\"%s\" handle=%u), replacing...", path, *handle);
        return ret;
    }
    return real_FSOpenFile_wiiStreetUTlb(client, block, path, mode, handle, error);
}

static size_t substituteTlbBytes(uint32_t posBefore, FSStatus ret, uint32_t size, uint8_t *buffer) {
    if (ret <= 0) return 0;
    size_t bytesRead = (size_t) ret * size;
    size_t avail = (posBefore < wiistreetu_tlb_nwf_js_size) ? (wiistreetu_tlb_nwf_js_size - posBefore) : 0;
    size_t toCopy = bytesRead < avail ? bytesRead : avail;
    if (toCopy > 0) {
        memcpy(buffer, wiistreetu_tlb_nwf_js + posBefore, toCopy);
    }
    return toCopy;
}

DECL_FUNCTION(FSStatus, FSReadFile_wiiStreetUTlb, FSClient *client, FSCmdBlock *block, uint8_t *buffer, uint32_t size, uint32_t count,
              FSFileHandle handle, uint32_t unk1, uint32_t flags) {
    if (!isWiiStreetUTitle()) {
        return real_FSReadFile_wiiStreetUTlb(client, block, buffer, size, count, handle, unk1, flags);
    }
    if (wiistreetu_tlb_handle && *wiistreetu_tlb_handle == handle) {
        uint32_t posBefore = 0;
        FSGetPosFile(client, block, handle, &posBefore, FS_ERROR_FLAG_ALL);
        FSStatus ret = real_FSReadFile_wiiStreetUTlb(client, block, buffer, size, count, handle, unk1, flags);
        substituteTlbBytes(posBefore, ret, size, buffer);
        return ret;
    }
    return real_FSReadFile_wiiStreetUTlb(client, block, buffer, size, count, handle, unk1, flags);
}

DECL_FUNCTION(FSStatus, FSReadFileWithPos_wiiStreetUTlb, FSClient *client, FSCmdBlock *block, uint8_t *buffer, uint32_t size, uint32_t count,
              uint32_t pos, FSFileHandle handle, uint32_t unk1, FSErrorFlag errorMask) {
    if (!isWiiStreetUTitle()) {
        return real_FSReadFileWithPos_wiiStreetUTlb(client, block, buffer, size, count, pos, handle, unk1, errorMask);
    }
    if (wiistreetu_tlb_handle && *wiistreetu_tlb_handle == handle) {
        FSStatus ret = real_FSReadFileWithPos_wiiStreetUTlb(client, block, buffer, size, count, pos, handle, unk1, errorMask);
        substituteTlbBytes(pos, ret, size, buffer);
        return ret;
    }
    return real_FSReadFileWithPos_wiiStreetUTlb(client, block, buffer, size, count, pos, handle, unk1, errorMask);
}

DECL_FUNCTION(FSStatus, FSCloseFile_wiiStreetUTlb, FSClient *client, FSCmdBlock *block, FSFileHandle handle, FSErrorFlag errorMask) {
    if (!isWiiStreetUTitle()) {
        return real_FSCloseFile_wiiStreetUTlb(client, block, handle, errorMask);
    }
    if (handle == wiistreetu_tlb_handle) {
        wiistreetu_tlb_handle.reset();
    }
    return real_FSCloseFile_wiiStreetUTlb(client, block, handle, errorMask);
}

bool patchWiiStreetUTlb() {
    wiistreetu_tlb_patches.reserve(4);

    auto add_patch = [](function_replacement_data_t repl, const char *name) {
        PatchedFunctionHandle handle = 0;
        if (FunctionPatcher_AddFunctionPatch(&repl, &handle, nullptr) != FUNCTION_PATCHER_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE("Inkay/WiiStreetU: Failed to patch %s!", name);
        }
        wiistreetu_tlb_patches.push_back(handle);
    };

    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSOpenFile_wiiStreetUTlb, LIBRARY_COREINIT, FSOpenFile, FP_TARGET_PROCESS_GAME), "FSOpenFile_wiiStreetUTlb");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSReadFile_wiiStreetUTlb, LIBRARY_COREINIT, FSReadFile, FP_TARGET_PROCESS_GAME), "FSReadFile_wiiStreetUTlb");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSReadFileWithPos_wiiStreetUTlb, LIBRARY_COREINIT, FSReadFileWithPos, FP_TARGET_PROCESS_GAME), "FSReadFileWithPos_wiiStreetUTlb");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSCloseFile_wiiStreetUTlb, LIBRARY_COREINIT, FSCloseFile, FP_TARGET_PROCESS_GAME), "FSCloseFile_wiiStreetUTlb");

    return true;
}

void unpatchWiiStreetUTlb() {
    for (const auto handle: wiistreetu_tlb_patches) {
        FunctionPatcher_RemoveFunctionPatch(handle);
    }
    wiistreetu_tlb_patches.clear();
}
