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

// Call from INITIALIZE_PLUGIN (boot), same reasoning as tvii_applet.h: by the
// time Wii Karaoke U's own process starts, it has already read these files
// into memory, so writing them after launch is too late for that session.
//
// Downloads the current data_base01-04.bin catalog from a dedicated plain-
// HTTP endpoint (no TLS needed - same server, same reasoning as the existing
// curl_easy_setopt URL redirect in wii_karaoke.cpp) and writes it straight
// into the save directory with an Mocha_UnlockFSClient()-unlocked FSClient,
// same mechanism as tviiApplyRootCAOverlay(). This replaces the need for the
// game's own, fragile in-game GD0001/GF0001 online-update flow entirely:
// by the next time the game launches, the catalog is already current.
//
// Only touches the save directory if it already exists (the title has been
// launched at least once) - never creates save data for a title that
// hasn't been played. Skips the (multi-megabyte) download entirely unless
// a small version marker fetched first differs from the last one we wrote,
// so most boots do nothing beyond that one small request.
//
// This call itself returns immediately - the actual work (including the
// network request) runs on a background thread. INITIALIZE_PLUGIN fires
// before the network stack is necessarily up this early in boot, so a
// synchronous attempt here could fail every single boot for that reason
// alone; the background thread waits a few seconds first and retries the
// "couldn't reach the server at all" case with backoff before giving up
// until next boot, without blocking the rest of plugin init on it.
void karaokeApplyCatalogOverlay();
