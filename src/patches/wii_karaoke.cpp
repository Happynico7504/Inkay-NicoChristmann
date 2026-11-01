#include "wii_karaoke.h"
#include "config.h"
#include "utils/logger.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <function_patcher/function_patching.h>

// Wii Karaoke U by JOYSOUND asks its own server, not a Nintendo Network service, so the IOSU
// URL patches don't cover it. Plain http: the game only sets up its pinned NSSL context for https.
// Rewritten as the game hands it to libcurl, instead of in .rodata: the game builds every request
// URL from this prefix, and this way nothing depends on when the title's memory can be changed.
constexpr char karaoke_original[] = "https://ssl.wahp.wah.wup.app.nintendo.net/";
constexpr char karaoke_new[] =      "http://wahp.nicoch.net/";

constexpr int CURLOPT_URL_OPTION = 10002;

std::vector<PatchedFunctionHandle> karaoke_patches;

// curl_easy_setopt is variadic, but on PowerPC its one pointer/long argument still arrives in r5.
DECL_FUNCTION(int, curl_easy_setopt_karaoke, void *curl, int option, void *param) {
    if (option == CURLOPT_URL_OPTION && param && Config::connect_to_network) {
        const auto *url = (const char *) param;
        if (strncmp(url, karaoke_original, sizeof(karaoke_original) - 1) == 0) {
            // libcurl copies the string, so a stack buffer is fine (and safe across threads).
            char rewritten[2048];
            int len = snprintf(rewritten, sizeof(rewritten), "%s%s", karaoke_new, url + sizeof(karaoke_original) - 1);
            if (len > 0 && (size_t) len < sizeof(rewritten)) {
                return real_curl_easy_setopt_karaoke(curl, option, rewritten);
            }
            DEBUG_FUNCTION_LINE("Inkay: Wii Karaoke URL too long");
        }
    }

    return real_curl_easy_setopt_karaoke(curl, option, param);
}

void patchWiiKaraoke() {
    karaoke_patches.reserve(1);

    auto add_patch = [](function_replacement_data_t repl, const char *name) {
        PatchedFunctionHandle handle = 0;
        if (FunctionPatcher_AddFunctionPatch(&repl, &handle, nullptr) != FUNCTION_PATCHER_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE("Inkay/Karaoke: Failed to patch %s!", name);
        }
        karaoke_patches.push_back(handle);
    };

    add_patch(REPLACE_FUNCTION_FOR_PROCESS(curl_easy_setopt_karaoke, LIBRARY_NLIBCURL, curl_easy_setopt, FP_TARGET_PROCESS_GAME), "curl_easy_setopt");
}

void unpatchWiiKaraoke() {
    for (auto handle: karaoke_patches) {
        FunctionPatcher_RemoveFunctionPatch(handle);
    }
    karaoke_patches.clear();
}
