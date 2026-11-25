/*  Copyright 2026 NicoChristmann

    Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
    granted, provided that the above copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
    INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
    IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
    PERFORMANCE OF THIS SOFTWARE.

    Wii Street U validates whx.cdn.nintendo.net's TLS cert against its own
    bundled file:///vol/content/app/certificate/ca-bundle.pem, independent of
    whatever CA Inkay's other patches (account_settings.cpp, olv_applet, ...)
    get trusted into nn::olv/nn::act's shared trust store - confirmed live:
    the console rejected our whx.cdn.nintendo.net cert with "unknown
    certificate authority" over a connection that otherwise proved
    dns_hooks.cpp's redirect worked (right SNI, right IP, legacy Wii U TLS
    fingerprint). This mirrors account_settings.cpp's rootca.pem swap, but for
    a 3-cert bundle instead of a single root: data/wiistreetu_ca_bundle.pem
    (-> wiistreetu_ca_bundle_pem[] below) is the original bundle's Nintendo
    Class 2 CA G3 cert plus our own CA, with the obsolete Equifax/Entrust
    roots dropped to make room, padded with a comment line to the EXACT byte
    length of the original 4562-byte file. That padding matters twice over:
    nothing here patches FSGetStatFile, so the game's own read buffer is
    sized from the REAL file's stat size; and FSReadFile_wiiStreetU below
    substitutes bytes by mirroring the real file's position/EOF/return-value
    behavior rather than tracking its own offset, which only lines up
    correctly because both files are exactly the same length. */

#include "config.h"
#include "utils/logger.h"

#include <function_patcher/function_patching.h>

#include <coreinit/filesystem.h>
#include <coreinit/title.h>

#include <cstring>
#include <vector>
#include <optional>

#include "wiistreetu_ca_bundle_pem.h" // generated at buildtime from data/wiistreetu_ca_bundle.pem

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

static std::optional<FSFileHandle> wiistreetu_ca_bundle_handle{};
std::vector<PatchedFunctionHandle> wiistreetu_ca_patches;

DECL_FUNCTION(int, FSOpenFile_wiiStreetU, FSClient *client, FSCmdBlock *block, char *path, const char *mode, uint32_t *handle,
              int error) {
    if (!isWiiStreetUTitle()) {
        return real_FSOpenFile_wiiStreetU(client, block, path, mode, handle, error);
    }

    // One-shot confirmation that title-ID detection itself is engaging at
    // all - cheap sanity check before trusting any path-matching logic below.
    static bool loggedTitleMatch = false;
    if (!loggedTitleMatch) {
        loggedTitleMatch = true;
        DEBUG_FUNCTION_LINE("Inkay/WiiStreetU: title match (titleId=%016llX), CA patch engaging", OSGetTitleID());
    }

    if (!Config::connect_to_network) {
        DEBUG_FUNCTION_LINE("Inkay: Wii Street U CA patches skipped (connect_to_network off).");
        return real_FSOpenFile_wiiStreetU(client, block, path, mode, handle, error);
    }

    // Always-on (not VERBOSE, which compiles out entirely in release builds -
    // confirmed via utils/logger.h that DEBUG_FUNCTION_LINE_VERBOSE is a
    // total no-op unless DEBUG is defined, which the release CI build never
    // sets) so we can actually see this on real hardware. Logs every path
    // containing "pem"/"cert" rather than every FSOpenFile call, which would
    // flood the log with every image/HTML asset this title opens.
    if (strstr(path, "pem") != nullptr || strstr(path, "cert") != nullptr) {
        DEBUG_FUNCTION_LINE("Inkay/WiiStreetU: FSOpenFile path=\"%s\"", path);
    }

    // Confirmed live (2026-11-25) via the pem/cert path log above: the real
    // path has a leading slash AND a double slash ("app//certificate") -
    // config.xml's sslCertificatePath value ("/certificate/ca-bundle.pem",
    // itself leading-slashed) gets naively concatenated onto a
    // "/vol/content/app/" base that already ends in a slash, unlike
    // account_settings.cpp's rootca.pem path which had neither. Do not
    // "clean" this to a single slash - it has to match what the title
    // actually requests byte for byte.
    if (strcmp("/vol/content/app//certificate/ca-bundle.pem", path) == 0) {
        int ret = real_FSOpenFile_wiiStreetU(client, block, path, mode, handle, error);
        wiistreetu_ca_bundle_handle = *handle;
        DEBUG_FUNCTION_LINE("Inkay/WiiStreetU: Found ca-bundle.pem (handle=%u), replacing...", *handle);
        return ret;
    }
    return real_FSOpenFile_wiiStreetU(client, block, path, mode, handle, error);
}

DECL_FUNCTION(FSStatus, FSReadFile_wiiStreetU, FSClient *client, FSCmdBlock *block, uint8_t *buffer, uint32_t size, uint32_t count,
              FSFileHandle handle, uint32_t unk1, uint32_t flags) {
    if (!isWiiStreetUTitle()) {
        return real_FSReadFile_wiiStreetU(client, block, buffer, size, count, handle, unk1, flags);
    }

    if (wiistreetu_ca_bundle_handle && *wiistreetu_ca_bundle_handle == handle) {
        // Confirmed live (2026-11-25): this file is read across multiple
        // FSReadFile calls with size != 1, not the single whole-file call
        // account_settings.cpp's rootca.pem swap assumed - a one-shot
        // strlcpy() on every call just re-served the same bytes from offset
        // 0 each time instead of advancing. Since both the real file and our
        // replacement are exactly wiistreetu_ca_bundle_pem_size (4562) bytes
        // (see data/wiistreetu_ca_bundle.pem's padding), letting the REAL
        // read drive position/EOF/return-value semantics and just
        // substituting the bytes it would have returned keeps us correct
        // regardless of how the caller chunks its reads.
        uint32_t posBefore = 0;
        FSGetPosFile(client, block, handle, &posBefore, FS_ERROR_FLAG_ALL);

        FSStatus ret = real_FSReadFile_wiiStreetU(client, block, buffer, size, count, handle, unk1, flags);
        size_t toCopy = 0;
        if (ret > 0) {
            size_t bytesRead = (size_t) ret * size;
            size_t avail = (posBefore < wiistreetu_ca_bundle_pem_size) ? (wiistreetu_ca_bundle_pem_size - posBefore) : 0;
            toCopy = bytesRead < avail ? bytesRead : avail;
            if (toCopy > 0) {
                memcpy(buffer, wiistreetu_ca_bundle_pem + posBefore, toCopy);
            }
        }
        DEBUG_FUNCTION_LINE("Inkay/WiiStreetU: FSReadFile ca-bundle size=%u count=%u posBefore=%u ret=%d copied=%u",
            size, count, posBefore, ret, (uint32_t) toCopy);
        return ret;
    }
    return real_FSReadFile_wiiStreetU(client, block, buffer, size, count, handle, unk1, flags);
}

DECL_FUNCTION(FSStatus, FSCloseFile_wiiStreetU, FSClient *client, FSCmdBlock *block, FSFileHandle handle, FSErrorFlag errorMask) {
    if (!isWiiStreetUTitle()) {
        return real_FSCloseFile_wiiStreetU(client, block, handle, errorMask);
    }
    if (handle == wiistreetu_ca_bundle_handle) {
        wiistreetu_ca_bundle_handle.reset();
    }
    return real_FSCloseFile_wiiStreetU(client, block, handle, errorMask);
}

bool patchWiiStreetU() {
    wiistreetu_ca_patches.reserve(3);

    auto add_patch = [](function_replacement_data_t repl, const char *name) {
        PatchedFunctionHandle handle = 0;
        if (FunctionPatcher_AddFunctionPatch(&repl, &handle, nullptr) != FUNCTION_PATCHER_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE("Inkay/WiiStreetU: Failed to patch %s!", name);
        }
        wiistreetu_ca_patches.push_back(handle);
    };

    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSOpenFile_wiiStreetU, LIBRARY_COREINIT, FSOpenFile, FP_TARGET_PROCESS_GAME), "FSOpenFile_wiiStreetU");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSReadFile_wiiStreetU, LIBRARY_COREINIT, FSReadFile, FP_TARGET_PROCESS_GAME), "FSReadFile_wiiStreetU");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSCloseFile_wiiStreetU, LIBRARY_COREINIT, FSCloseFile, FP_TARGET_PROCESS_GAME), "FSCloseFile_wiiStreetU");

    return true;
}

void unpatchWiiStreetU() {
    for (const auto handle: wiistreetu_ca_patches) {
        FunctionPatcher_RemoveFunctionPatch(handle);
    }
    wiistreetu_ca_patches.clear();
}
