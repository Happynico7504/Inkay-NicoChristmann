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

// Catch-all sweep: log every other "action" nn::olv call (not the trivial
// Set*/Get*-field accessors or constructors) so we can see exactly what WSC
// calls between the community-list match and StartPortalApp, since none of
// the three functions above fire for the current failure.

DECL_FUNCTION(uint32_t, Initialize, const void *param) {
    uint32_t result = real_Initialize(param);
    DEBUG_FUNCTION_LINE("Inkay/OLV: Initialize(param=%p) -> result=0x%08X", param, result);
    return result;
}

DECL_FUNCTION(uint32_t, Cancel) {
    uint32_t result = real_Cancel();
    DEBUG_FUNCTION_LINE("Inkay/OLV: Cancel() -> 0x%08X", result);
    return result;
}

DECL_FUNCTION(void, Finalize) {
    real_Finalize();
    DEBUG_FUNCTION_LINE("Inkay/OLV: Finalize() called");
}

DECL_FUNCTION(uint32_t, SwitchToOnlineMode) {
    uint32_t result = real_SwitchToOnlineMode();
    DEBUG_FUNCTION_LINE("Inkay/OLV: SwitchToOnlineMode() -> 0x%08X", result);
    return result;
}

DECL_FUNCTION(uint32_t, DownloadPostDataList, void *pOutTopic, void *pOutPost, uint32_t *pOutCount, uint32_t maxCount, const void *param) {
    uint32_t result = real_DownloadPostDataList(pOutTopic, pOutPost, pOutCount, maxCount, param);
    DEBUG_FUNCTION_LINE("Inkay/OLV: DownloadPostDataList(maxCount=%u) -> result=0x%08X outCount=%u",
                         maxCount, result, pOutCount ? *pOutCount : 0xFFFFFFFF);
    return result;
}

DECL_FUNCTION(uint32_t, DownloadCommentDataList, void *pOutComment, uint32_t *pOutCount, uint32_t maxCount, const void *param) {
    uint32_t result = real_DownloadCommentDataList(pOutComment, pOutCount, maxCount, param);
    DEBUG_FUNCTION_LINE("Inkay/OLV: DownloadCommentDataList(maxCount=%u) -> result=0x%08X outCount=%u",
                         maxCount, result, pOutCount ? *pOutCount : 0xFFFFFFFF);
    return result;
}

DECL_FUNCTION(uint32_t, UploadPostData, void *pOutUploadedPostData, const void *param) {
    uint32_t result = real_UploadPostData(pOutUploadedPostData, param);
    DEBUG_FUNCTION_LINE("Inkay/OLV: UploadPostData(param=%p) -> result=0x%08X", param, result);
    return result;
}

DECL_FUNCTION(uint32_t, UploadPostDataByPostApp, const void *param) {
    uint32_t result = real_UploadPostDataByPostApp(param);
    DEBUG_FUNCTION_LINE("Inkay/OLV: UploadPostDataByPostApp(param=%p) -> result=0x%08X", param, result);
    return result;
}

DECL_FUNCTION(uint32_t, UploadEmpathyToPostData, const void *param) {
    uint32_t result = real_UploadEmpathyToPostData(param);
    DEBUG_FUNCTION_LINE("Inkay/OLV: UploadEmpathyToPostData(param=%p) -> result=0x%08X", param, result);
    return result;
}

DECL_FUNCTION(uint32_t, GetResultWithUploadedPostDataByPostApp, void *pOutUploadedPostData) {
    uint32_t result = real_GetResultWithUploadedPostDataByPostApp(pOutUploadedPostData);
    DEBUG_FUNCTION_LINE("Inkay/OLV: GetResultWithUploadedPostDataByPostApp() -> result=0x%08X", result);
    return result;
}

// int32_t GetErrorCode(const nn::Result &result) - this is very likely what
// decodes a raw Result into the "errorCode:1152006"-style number WSC prints,
// so logging both the raw input and the decoded output ties them together.
DECL_FUNCTION(int32_t, GetErrorCode, const uint32_t *result) {
    int32_t code = real_GetErrorCode(result);
    DEBUG_FUNCTION_LINE("Inkay/OLV: GetErrorCode(raw=0x%08X) -> %d", result ? *result : 0, code);
    return code;
}

void patchOlvCalls() {
    olv_call_patches.reserve(14);

    DEBUG_FUNCTION_LINE("Inkay/OLV: patchOlvCalls() starting");

    int failCount = 0;
    auto add_patch = [&failCount](function_replacement_data_t repl, const char *name) {
        PatchedFunctionHandle handle = 0;
        if (FunctionPatcher_AddFunctionPatch(&repl, &handle, nullptr) != FUNCTION_PATCHER_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE("Inkay/OLV: Failed to patch %s!", name);
            failCount++;
        }
        olv_call_patches.push_back(handle);
    };

    add_patch(REPLACE_FUNCTION_FOR_PROCESS(StartPortalApp, LIBRARY_NN_OLV, StartPortalApp, FP_TARGET_PROCESS_GAME), "StartPortalApp");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(PreloadPostApp, LIBRARY_NN_OLV, PreloadPostApp, FP_TARGET_PROCESS_GAME), "PreloadPostApp");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(DownloadCommunityDataList, LIBRARY_NN_OLV, DownloadCommunityDataList, FP_TARGET_PROCESS_GAME), "DownloadCommunityDataList");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(Initialize, LIBRARY_NN_OLV, Initialize, FP_TARGET_PROCESS_GAME), "Initialize");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(Cancel, LIBRARY_NN_OLV, Cancel, FP_TARGET_PROCESS_GAME), "Cancel");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(Finalize, LIBRARY_NN_OLV, Finalize, FP_TARGET_PROCESS_GAME), "Finalize");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(SwitchToOnlineMode, LIBRARY_NN_OLV, SwitchToOnlineMode, FP_TARGET_PROCESS_GAME), "SwitchToOnlineMode");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(DownloadPostDataList, LIBRARY_NN_OLV, DownloadPostDataList, FP_TARGET_PROCESS_GAME), "DownloadPostDataList");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(DownloadCommentDataList, LIBRARY_NN_OLV, DownloadCommentDataList, FP_TARGET_PROCESS_GAME), "DownloadCommentDataList");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(UploadPostData, LIBRARY_NN_OLV, UploadPostData, FP_TARGET_PROCESS_GAME), "UploadPostData");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(UploadPostDataByPostApp, LIBRARY_NN_OLV, UploadPostDataByPostApp, FP_TARGET_PROCESS_GAME), "UploadPostDataByPostApp");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(UploadEmpathyToPostData, LIBRARY_NN_OLV, UploadEmpathyToPostData, FP_TARGET_PROCESS_GAME), "UploadEmpathyToPostData");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(GetResultWithUploadedPostDataByPostApp, LIBRARY_NN_OLV, GetResultWithUploadedPostDataByPostApp, FP_TARGET_PROCESS_GAME), "GetResultWithUploadedPostDataByPostApp");
    add_patch(REPLACE_FUNCTION_FOR_PROCESS(GetErrorCode, LIBRARY_NN_OLV, GetErrorCode, FP_TARGET_PROCESS_GAME), "GetErrorCode");

    DEBUG_FUNCTION_LINE("Inkay/OLV: patchOlvCalls() done - %d/%d patches installed successfully",
                         (int) olv_call_patches.size() - failCount, (int) olv_call_patches.size());
}

void unpatchOlvCalls() {
    for (const auto handle: olv_call_patches) {
        FunctionPatcher_RemoveFunctionPatch(handle);
    }
    olv_call_patches.clear();
}
