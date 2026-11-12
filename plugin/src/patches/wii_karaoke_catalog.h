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

// Call from INITIALIZE_PLUGIN, same reasoning as tvii_applet.h: that hook
// fires once per title LAUNCH (injected into that title's own about-to-start
// process, not once at system boot). Runs unconditionally regardless of
// which title is launching (including the Home Menu's own launch) so the
// catalog has the longest possible head start before Wii Karaoke U itself
// might launch - not gated to that title specifically.
//
// Downloads the current data_base01-04.bin catalog from a dedicated plain-
// HTTP endpoint (no TLS needed - same server, same reasoning as the existing
// curl_easy_setopt URL redirect in wii_karaoke.cpp) and writes it straight
// into the save directory with an Mocha_UnlockFSClient()-unlocked FSClient,
// same mechanism as tviiApplyRootCAOverlay(). This no longer replaces the
// game's own GD0001/GF0001 online update (see wii_karaoke_catalogfix.h for
// why that's expected to succeed on its own now) - it just means the save
// data is already current if that update isn't needed, and gives whatever
// update does happen less work to do.
//
// Only touches the save directory if the real data_base01.bin file already
// exists there (the title has actually been played before, not just that
// its save directory was created) - never creates save data for a title
// that hasn't been played. Skips the (multi-megabyte) download entirely
// unless a small version marker fetched first differs from the last one we
// wrote, so most boots do nothing beyond that one small request.
//
// This call itself returns immediately - the actual work (including the
// network request) runs on a background thread. INITIALIZE_PLUGIN fires
// before the network stack is necessarily up this early in boot, so a
// synchronous attempt here could fail every single boot for that reason
// alone; the background thread waits a few seconds first and retries the
// "couldn't reach the server at all" case with backoff before giving up
// until next boot, without blocking the rest of plugin init on it.
void karaokeApplyCatalogOverlay();

// Tells the background thread's retry loop to give up immediately instead
// of running for up to its ~10 minute budget. Call unconditionally from
// DEINITIALIZE_PLUGIN (this process is exiting, regardless of which title
// it was for) - this is what lets karaokeApplyCatalogOverlay run on every
// launch without risking the thread lingering into the next one.
void karaokeCatalogRequestStop();

// Same stop signal, but only takes effect if the title that just started is
// Wii Karaoke U itself - call from ON_APPLICATION_START. Confirmed real bug:
// calling karaokeCatalogRequestStop() unconditionally there stopped the
// thread within seconds of ANY title's own launch, including the Home
// Menu's (which starts almost immediately after INITIALIZE_PLUGIN) - never
// giving the network a real chance to come up before giving up. "Further
// writes can't help this session anymore" is only true once Wii Karaoke U
// itself has started, not for every other title along the way.
void karaokeCatalogOnApplicationStart();
