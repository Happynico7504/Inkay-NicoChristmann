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

#include <coreinit/filesystem.h>
#include <mocha/mocha.h>

#include "tvii_rootca_der.h"          // generated at buildtime from data/tvii_rootca.der (our own CA)
#include "godaddy_class2_root_der.h"  // generated at buildtime from data/godaddy_class2_root.der (stock)
#include "godaddy_g2_root_der.h"      // generated at buildtime from data/godaddy_g2_root.der (stock)

// Native Cafe OS paths, not through a Mocha_MountFS devoptab: content partitions are
// mounted read-only even to their own owning title, so writing here needs a FSClient
// that Mocha_UnlockFSClient() has granted full permissions to.
struct tvii_rootca_file {
    const char *path;
    const uint8_t *stock_der;
    size_t stock_der_size;
};

constexpr tvii_rootca_file tvii_rootca_files[] = {
        {"/vol/storage_mlc01/sys/title/00050030/1001320a/content/rootca/Go_Daddy_Class_2_Certification_Authority.der",
         godaddy_class2_root_der, godaddy_class2_root_der_size},
        {"/vol/storage_mlc01/sys/title/00050030/1001320a/content/rootca/Go_Daddy_Root_Certificate_Authority_-_G2.der",
         godaddy_g2_root_der, godaddy_g2_root_der_size},
};

void tviiApplyRootCAOverlay() {
    if (!Config::connect_to_network) {
        return;
    }

    if (auto res = Mocha_InitLibrary(); res != MOCHA_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE("Inkay/TVii: Mocha init failed (%d), not overwriting root CAs", res);
        return;
    }

    FSInit();

    FSClient client;
    FSCmdBlock block;
    FSInitCmdBlock(&block);

    if (auto res = FSAddClient(&client, FS_ERROR_FLAG_ALL); res != FS_STATUS_OK) {
        DEBUG_FUNCTION_LINE("Inkay/TVii: FSAddClient failed (%d)", res);
        Mocha_DeInitLibrary();
        return;
    }

    if (auto res = Mocha_UnlockFSClient(&client); res != MOCHA_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE("Inkay/TVii: failed to unlock FSClient (%d), not overwriting root CAs", res);
        FSDelClient(&client, FS_ERROR_FLAG_ALL);
        Mocha_DeInitLibrary();
        return;
    }

    for (const auto &file: tvii_rootca_files) {
        const uint8_t *data = Config::tvii_use_custom_ca ? tvii_rootca_der : file.stock_der;
        size_t size = Config::tvii_use_custom_ca ? tvii_rootca_der_size : file.stock_der_size;

        FSFileHandle handle;
        auto open_res = FSOpenFile(&client, &block, file.path, "w", &handle, FS_ERROR_FLAG_ALL);
        if (open_res != FS_STATUS_OK) {
            DEBUG_FUNCTION_LINE("Inkay/TVii: failed to open %s (%d)", file.path, open_res);
            continue;
        }

        auto write_res = FSWriteFile(&client, &block, (uint8_t *) data, 1, size, handle, 0, FS_ERROR_FLAG_ALL);
        if (write_res != (FSStatus) size) {
            DEBUG_FUNCTION_LINE("Inkay/TVii: failed to write %s (%d)", file.path, write_res);
        } else {
            DEBUG_FUNCTION_LINE_VERBOSE("Inkay/TVii: overwrote %s with %s CA", file.path, Config::tvii_use_custom_ca ? "custom" : "stock");
        }

        FSCloseFile(&client, &block, handle, FS_ERROR_FLAG_ALL);
    }

    FSDelClient(&client, FS_ERROR_FLAG_ALL);
    Mocha_DeInitLibrary();
}
