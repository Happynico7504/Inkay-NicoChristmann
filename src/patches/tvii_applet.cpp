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

#include "config.h"
#include "utils/logger.h"

#include <cstring>
#include <vector>
#include <optional>
#include <array>
#include <algorithm>

#include <function_patcher/function_patching.h>
#include <coreinit/filesystem.h>

#include "tvii_rootca_der.h" // generated at buildtime from data/tvii_rootca.der

// TVii ships two Go Daddy root CA DER files and pins its connections to Nintendo's TVii
// backend against them. We swap both for our own CA so the applet trusts nicoch.net.
// Unlike the eShop/Miiverse/account settings rootca.pem swaps, this is raw DER, not
// NUL-terminated PEM text, so we can't just strlcpy() it in: we track a read offset per
// handle and memcpy() the real bytes, since the game may call FSReadFile in more than
// one chunk and a short/garbage-padded copy would corrupt the ASN.1 structure.
constexpr const char *tvii_rootca_paths[] = {
        "vol/content/rootca/Go_Daddy_Class_2_Certification_Authority.der",
        "vol/content/rootca/Go_Daddy_Root_Certificate_Authority_-_G2.der",
};

struct tvii_rootca_handle {
    FSFileHandle handle;
    size_t offset;
};

static std::array<std::optional<tvii_rootca_handle>, std::size(tvii_rootca_paths)> rootca_handles{};

std::vector<PatchedFunctionHandle> tvii_patches;

static bool isTviiRootCAPath(const char *path) {
    for (const auto *candidate: tvii_rootca_paths) {
        if (strcmp(candidate, path) == 0) {
            return true;
        }
    }
    return false;
}

DECL_FUNCTION(int, FSOpenFile_Tvii, FSClient *client, FSCmdBlock *block, char *path, const char *mode, uint32_t *handle,
              int error) {
    if (!Config::connect_to_network) {
        DEBUG_FUNCTION_LINE_VERBOSE("Inkay: TVii patches skipped.");
        return real_FSOpenFile_Tvii(client, block, path, mode, handle, error);
    }

    if (isTviiRootCAPath(path)) {
        int ret = real_FSOpenFile_Tvii(client, block, path, mode, handle, error);
        for (auto &slot: rootca_handles) {
            if (!slot) {
                slot = tvii_rootca_handle{*handle, 0};
                break;
            }
        }
        DEBUG_FUNCTION_LINE_VERBOSE("Inkay: Found TVii root CA %s, replacing...", path);
        return ret;
    }

    return real_FSOpenFile_Tvii(client, block, path, mode, handle, error);
}

DECL_FUNCTION(FSStatus, FSReadFile_Tvii, FSClient *client, FSCmdBlock *block, uint8_t *buffer, uint32_t size, uint32_t count,
              FSFileHandle handle, uint32_t unk1, uint32_t flags) {
    for (auto &slot: rootca_handles) {
        if (slot && slot->handle == handle) {
            size_t requested = (size_t) size * (size_t) count;
            size_t remaining = tvii_rootca_der_size - std::min(slot->offset, tvii_rootca_der_size);
            size_t copy = std::min(requested, remaining);

            memcpy(buffer, tvii_rootca_der + slot->offset, copy);
            slot->offset += copy;

            if (size == 0) {
                return (FSStatus) 0;
            }
            return (FSStatus)(copy / size);
        }
    }

    return real_FSReadFile_Tvii(client, block, buffer, size, count, handle, unk1, flags);
}

DECL_FUNCTION(FSStatus, FSCloseFile_Tvii, FSClient *client, FSCmdBlock *block, FSFileHandle handle, FSErrorFlag errorMask) {
    for (auto &slot: rootca_handles) {
        if (slot && slot->handle == handle) {
            slot.reset();
            break;
        }
    }

    return real_FSCloseFile_Tvii(client, block, handle, errorMask);
}

void patchTviiApplet() {
    tvii_patches.reserve(3);

    auto add_patch = [](function_replacement_data_t repl, const char *name) {
        PatchedFunctionHandle handle = 0;
        if (FunctionPatcher_AddFunctionPatch(&repl, &handle, nullptr) != FUNCTION_PATCHER_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE("Inkay/TVii: Failed to patch %s!", name);
        }
        tvii_patches.push_back(handle);
    };

    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSOpenFile_Tvii, LIBRARY_COREINIT, FSOpenFile, FP_TARGET_PROCESS_TVII), "FSOpenFile_Tvii");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSReadFile_Tvii, LIBRARY_COREINIT, FSReadFile, FP_TARGET_PROCESS_TVII), "FSReadFile_Tvii");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(FSCloseFile_Tvii, LIBRARY_COREINIT, FSCloseFile, FP_TARGET_PROCESS_TVII), "FSCloseFile_Tvii");
}

void unpatchTviiApplet() {
    for (auto handle: tvii_patches) {
        FunctionPatcher_RemoveFunctionPatch(handle);
    }
    tvii_patches.clear();
}
