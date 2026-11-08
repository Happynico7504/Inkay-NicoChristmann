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
#include <vector>
#include <array>

#include <coreinit/title.h>

#include "tvii_rootca_der.h" // generated at buildtime from data/tvii_rootca.der

// Unlike eShop/Miiverse/account settings, which read their rootca.pem through the
// applet's own FSClient (so we can hook FSOpenFile/FSReadFile), TVii reads its two
// Go Daddy root CA DER files straight off the MLC filesystem below the applet layer
// we can hook with FunctionPatcher. So instead we overlay the real files on disk for
// the duration of the TVii applet's run (storage_mlc is already mounted via Mocha in
// WUMS_INITIALIZE) and restore the originals when it closes, rather than patching
// them permanently.
constexpr const char *tvii_rootca_mlc_paths[] = {
        "storage_mlc:/sys/title/00050030/1001320a/content/rootca/Go_Daddy_Class_2_Certification_Authority.der",
        "storage_mlc:/sys/title/00050030/1001320a/content/rootca/Go_Daddy_Root_Certificate_Authority_-_G2.der",
};

constexpr uint64_t TVII_TITLE_ID = 0x000500301001320AULL;

static std::array<std::vector<uint8_t>, std::size(tvii_rootca_mlc_paths)> tvii_rootca_backup{};
static bool tvii_overlay_applied = false;

static bool isTviiApplet() {
    return OSGetTitleID() == TVII_TITLE_ID;
}

static bool readWholeFile(const char *path, std::vector<uint8_t> &out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return false;
    }

    out.resize((size_t) size);
    size_t did_read = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return did_read == out.size();
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

void tviiApplyRootCAOverlay() {
    if (!isTviiApplet() || !Config::connect_to_network || tvii_overlay_applied) {
        return;
    }

    for (size_t i = 0; i < std::size(tvii_rootca_mlc_paths); i++) {
        if (!readWholeFile(tvii_rootca_mlc_paths[i], tvii_rootca_backup[i])) {
            DEBUG_FUNCTION_LINE("Inkay/TVii: failed to back up %s, not overlaying root CAs", tvii_rootca_mlc_paths[i]);
            for (auto &backup: tvii_rootca_backup) {
                backup.clear();
            }
            return;
        }
    }

    for (const auto *path: tvii_rootca_mlc_paths) {
        if (!writeWholeFile(path, tvii_rootca_der, tvii_rootca_der_size)) {
            DEBUG_FUNCTION_LINE("Inkay/TVii: failed to overlay %s", path);
        }
    }

    tvii_overlay_applied = true;
    DEBUG_FUNCTION_LINE_VERBOSE("Inkay: TVii root CA overlay applied");
}

void tviiRemoveRootCAOverlay() {
    if (!tvii_overlay_applied) {
        return;
    }

    for (size_t i = 0; i < std::size(tvii_rootca_mlc_paths); i++) {
        if (!tvii_rootca_backup[i].empty()) {
            writeWholeFile(tvii_rootca_mlc_paths[i], tvii_rootca_backup[i].data(), tvii_rootca_backup[i].size());
        }
        tvii_rootca_backup[i].clear();
    }

    tvii_overlay_applied = false;
    DEBUG_FUNCTION_LINE_VERBOSE("Inkay: TVii root CA overlay removed");
}
