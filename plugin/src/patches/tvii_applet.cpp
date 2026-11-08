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

#include <cstdio>
#include <sys/stat.h>

#include <coreinit/title.h>
#include <content_redirection/redirection.h>

#include "tvii_rootca_der.h" // generated at buildtime from data/tvii_rootca.der

// TVii reads its two Go Daddy root CA DER files straight off the MLC filesystem, below
// anything FunctionPatcher can hook, so there's no function call to intercept. Rather
// than writing over the real NAND files (which SDCafiine's own ContentRedirection API
// exists specifically to avoid), we merge an SD-card "content" folder containing our
// replacement DER files over /vol/content for TVii's own process only - same approach
// SDCafiine uses to swap a game's content, just with the "rootca" subfolder instead of
// a modpack's own content tree.
constexpr const char *tvii_content_replacement_dir = "fs:/vol/external01/wiiu/environments/aroma/plugins/.inkay_tvii_rootca/content";

constexpr struct {
    const char *relative_path; // relative to tvii_content_replacement_dir
} tvii_rootca_files[] = {
        {"rootca/Go_Daddy_Class_2_Certification_Authority.der"},
        {"rootca/Go_Daddy_Root_Certificate_Authority_-_G2.der"},
};

constexpr uint64_t TVII_TITLE_ID = 0x000500301001320AULL;

static CRLayerHandle tvii_content_layer = 0;

static bool isTviiApplet() {
    return OSGetTitleID() == TVII_TITLE_ID;
}

static bool writeWholeFile(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }

    size_t did_write = fwrite(data, 1, size, f);
    fclose(f);
    return did_write == size;
}

static bool ensureReplacementFilesExist() {
    char dir_path[256];

    snprintf(dir_path, sizeof(dir_path), "%s", tvii_content_replacement_dir);
    mkdir(dir_path, 0666);

    snprintf(dir_path, sizeof(dir_path), "%s/rootca", tvii_content_replacement_dir);
    mkdir(dir_path, 0666);

    bool ok = true;
    for (const auto &file: tvii_rootca_files) {
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", tvii_content_replacement_dir, file.relative_path);

        if (!writeWholeFile(full_path, tvii_rootca_der, tvii_rootca_der_size)) {
            DEBUG_FUNCTION_LINE("Inkay/TVii: failed to write %s", full_path);
            ok = false;
        }
    }

    return ok;
}

void tviiApplyRootCAOverlay() {
    if (!isTviiApplet() || !Config::connect_to_network || tvii_content_layer != 0) {
        return;
    }

    if (!ensureReplacementFilesExist()) {
        DEBUG_FUNCTION_LINE("Inkay/TVii: not adding content layer, replacement files missing");
        return;
    }

    auto res = ContentRedirection_AddFSLayer(&tvii_content_layer, "Inkay TVii root CA", tvii_content_replacement_dir, FS_LAYER_TYPE_CONTENT_MERGE);
    if (res != CONTENT_REDIRECTION_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE("Inkay/TVii: ContentRedirection_AddFSLayer failed: %s", ContentRedirection_GetStatusStr(res));
        tvii_content_layer = 0;
        return;
    }

    DEBUG_FUNCTION_LINE_VERBOSE("Inkay: TVii root CA content layer added");
}

void tviiRemoveRootCAOverlay() {
    if (tvii_content_layer == 0) {
        return;
    }

    ContentRedirection_RemoveFSLayer(tvii_content_layer);
    tvii_content_layer = 0;
    DEBUG_FUNCTION_LINE_VERBOSE("Inkay: TVii root CA content layer removed");
}
