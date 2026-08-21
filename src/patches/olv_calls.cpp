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

void patchOlvCalls() {
    olv_call_patches.reserve(1);

    auto add_patch = [](function_replacement_data_t repl, const char *name) {
        PatchedFunctionHandle handle = 0;
        if (FunctionPatcher_AddFunctionPatch(&repl, &handle, nullptr) != FUNCTION_PATCHER_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE("Inkay/OLV: Failed to patch %s!", name);
        }
        olv_call_patches.push_back(handle);
    };

    add_patch(REPLACE_FUNCTION_FOR_PROCESS(StartPortalApp, LIBRARY_NN_OLV, StartPortalApp, FP_TARGET_PROCESS_GAME), "StartPortalApp");
}

void unpatchOlvCalls() {
    for (const auto handle: olv_call_patches) {
        FunctionPatcher_RemoveFunctionPatch(handle);
    }
    olv_call_patches.clear();
}
