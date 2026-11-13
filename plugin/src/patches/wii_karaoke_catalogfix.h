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

// Ported from the standalone wiikaraoke-catalogfix plugin (v8-noop3): fixes
// two confirmed Wii Karaoke U (EU) client-side bugs so the game's own
// GD0001->GL0001->GF0001 catalog-update sequence can run to completion
// against our server, instead of either being skipped or failing midway.
// The game actually needs this full sequence to succeed - it's what lets
// songs play - so these patches make it succeed rather than trying to
// avoid it. Only patches 1 (the race fix) and 2 (forced reply-verification
// success) are applied - patch 4 (the disc-reload no-op) and PATCH3A/3B
// (retracted) are both disabled/removed, see the .cpp for why.
//
// Call from ON_APPLICATION_START (not INITIALIZE_PLUGIN): these are direct
// instruction patches into the running title's own RPX, which only exists
// once that title's process has actually started loading it.
void karaokeApplyCatalogFix();

// Call from DEINITIALIZE_PLUGIN: signals the watch thread (which otherwise
// runs forever, reasserting the patches for as long as the title is open)
// to stop, and blocks until it actually has (OSJoinThread) - the plugin's
// own code/memory can be unloaded right after DEINITIALIZE_PLUGIN returns,
// which a still-running thread would then be executing out from under.
// Safe to call even if the thread was never started this launch.
void karaokeCatalogFixShutdown();
