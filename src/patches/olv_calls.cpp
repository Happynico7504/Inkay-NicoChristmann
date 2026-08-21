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

#include <vector>
#include <cstdint>

#include <function_patcher/function_patching.h>

std::vector<PatchedFunctionHandle> olv_call_patches;

// nn::Result StartPortalApp(const nn::olv::StartPortalAppParam *param = NULL);
// We don't need the real struct layout to log this - a pointer in, a 32-bit
// nn::Result out (returned in r3, same as every other opaque Cafe SDK result
// type already hooked in this codebase).
DECL_FUNCTION(uint32_t, StartPortalApp, const void *param) {
    uint32_t result = real_StartPortalApp(param);

    if (param) {
        const uint32_t *words = (const uint32_t *) param;
        DEBUG_FUNCTION_LINE("Inkay/OLV: StartPortalApp(param=%p [%08X %08X %08X %08X]) -> result=0x%08X",
                             param, words[0], words[1], words[2], words[3], result);
    } else {
        DEBUG_FUNCTION_LINE("Inkay/OLV: StartPortalApp(NULL) -> result=0x%08X", result);
    }

    return result;
}

// nn::Result PreloadPostApp();
// Runs before StartPortalApp to warm up the post applet - a documented
// ResultPostAppPreloadFailed exists in the SDK for exactly this call.
DECL_FUNCTION(uint32_t, PreloadPostApp) {
    uint32_t result = real_PreloadPostApp();
    DEBUG_FUNCTION_LINE("Inkay/OLV: PreloadPostApp() -> result=0x%08X", result);
    return result;
}

// nn::Result DownloadCommunityDataList(DownloadedCommunityData *pOutData, uint32_t *pOutCount,
//                                       uint32_t maxCount, const DownloadCommunityDataListParam *param);
// This is how the club-info screen looks up a community by its numeric ID
// before it can hand that ID to StartPortalApp - if our stored community_id
// doesn't match what the game actually asks for, this fails first.
DECL_FUNCTION(uint32_t, DownloadCommunityDataList, void *pOutData, uint32_t *pOutCount, uint32_t maxCount, const void *param) {
    uint32_t result = real_DownloadCommunityDataList(pOutData, pOutCount, maxCount, param);
    uint32_t outCount = pOutCount ? *pOutCount : 0xFFFFFFFF;

    if (param) {
        const uint32_t *words = (const uint32_t *) param;
        DEBUG_FUNCTION_LINE("Inkay/OLV: DownloadCommunityDataList(maxCount=%u, param=[%08X %08X %08X %08X]) -> result=0x%08X outCount=%u",
                             maxCount, words[0], words[1], words[2], words[3], result, outCount);
    } else {
        DEBUG_FUNCTION_LINE("Inkay/OLV: DownloadCommunityDataList(maxCount=%u, param=NULL) -> result=0x%08X outCount=%u",
                             maxCount, result, outCount);
    }

    return result;
}

void patchOlvCalls() {
    olv_call_patches.reserve(3);

    auto add_patch = [](function_replacement_data_t repl, const char *name) {
        PatchedFunctionHandle handle = 0;
        if (FunctionPatcher_AddFunctionPatch(&repl, &handle, nullptr) != FUNCTION_PATCHER_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE("Inkay/OLV: Failed to patch %s!", name);
        }
        olv_call_patches.push_back(handle);
    };

    add_patch(REPLACE_FUNCTION_FOR_PROCESS(StartPortalApp, LIBRARY_NN_OLV, StartPortalApp, FP_TARGET_PROCESS_GAME), "StartPortalApp");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(PreloadPostApp, LIBRARY_NN_OLV, PreloadPostApp, FP_TARGET_PROCESS_GAME), "PreloadPostApp");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(DownloadCommunityDataList, LIBRARY_NN_OLV, DownloadCommunityDataList, FP_TARGET_PROCESS_GAME), "DownloadCommunityDataList");
}

void unpatchOlvCalls() {
    for (const auto handle: olv_call_patches) {
        FunctionPatcher_RemoveFunctionPatch(handle);
    }
    olv_call_patches.clear();
}
