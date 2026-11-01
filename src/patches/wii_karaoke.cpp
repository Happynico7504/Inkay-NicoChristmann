#include "wii_karaoke.h"
#include "config.h"
#include "utils/logger.h"
#include "utils/replace_mem.h"

#include <coreinit/title.h>

// Wii Karaoke U by JOYSOUND asks its own server, not a Nintendo Network service, so the IOSU
// URL patches don't cover it. Plain http: the game only sets up its pinned NSSL context for https.
constexpr char karaoke_original[] = "https://ssl.wahp.wah.wup.app.nintendo.net/";
constexpr char karaoke_new[] =      "http://wahp.nicoch.net/";
static_assert(sizeof(karaoke_new) <= sizeof(karaoke_original), "Karaoke URL must fit over the original");

static bool isWiiKaraokeTitle() {
    const uint64_t tid = OSGetTitleID();
    return tid == 0x0005000010100D00 || tid == 0x000500001010B800 || tid == 0x0005000010149600;
}

bool hotpatchWiiKaraoke() {
    if (!Config::connect_to_network || !isWiiKaraokeTitle()) {
        return false;
    }

    // Only the EU build is known to use this URL; the others just won't match.
    if (!replace(0x10000000, 0x100000, karaoke_original, sizeof(karaoke_original), karaoke_new, sizeof(karaoke_new))) {
        DEBUG_FUNCTION_LINE("Inkay: Wii Karaoke URL not found");
        return false;
    }

    return true;
}
