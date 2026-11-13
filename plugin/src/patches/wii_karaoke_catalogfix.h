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

#pragma once

// Wii Karaoke U (EU) only needs one patch now: the real online-update flow
// (GD0001->GL0001->GF0001) never actually works for us (confirmed
// repeatedly - the game's own reply-verification check always fails
// against our server, and it has no way to persist anything useful even
// when forced to "succeed", since wiikaraoke-installer already writes the
// real catalog directly, outside the game's own process entirely). This
// one patch (forcing that reply-verification check to pass) exists purely
// so the game believes its own update already succeeded and stops
// prompting the player to restart/retry it - a cosmetic fix, not a
// functional one. The earlier race-fix (patch 1) and disc-reload no-op
// (patch 4) are NOT needed for this and are NOT applied - patch 4 in
// particular was confirmed to hang the game's boot against a catalog
// written by wiikaraoke-installer, see the project memory for the full
// isolation history.
//
// Call from ON_APPLICATION_START (not INITIALIZE_PLUGIN): this is a direct
// instruction patch into the running title's own RPX, which only exists
// once that title's process has actually started loading it. Applies once
// (bounded retries only while the title's RPL info isn't ready yet) - no
// perpetual watch/reassert loop, since the game never reverts this byte
// once applied.
void karaokeApplyCatalogFix();

// Call from DEINITIALIZE_PLUGIN: blocks (bounded) until the one-shot patch
// thread has exited, so the plugin's own code/memory isn't unloaded out
// from under it. Safe to call even if the thread was never started this
// launch.
void karaokeCatalogFixShutdown();
