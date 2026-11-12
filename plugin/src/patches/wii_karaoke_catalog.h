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
// network request) runs on a single background thread that's only ever
// started once per boot (INITIALIZE_PLUGIN fires once per title launch,
// but the thread persists across those rather than being respawned each
// time). INITIALIZE_PLUGIN fires before the network stack is necessarily
// up this early in boot, so a synchronous attempt here could fail every
// single boot for that reason alone; the background thread waits a few
// seconds first and retries the "couldn't reach the server at all" case
// with backoff before giving up until next boot, without blocking the
// rest of plugin init on it.
//
// Confirmed real bug on hardware: letting this thread's Mocha/FS/curl work
// run through an application-start transition (any title's, not just Wii
// Karaoke U's) made the UI hang right at that transition - contention on
// Mocha's shared FSClient/kernel access between this thread and whatever
// the newly-starting title's own process needs right then. A one-shot
// "stop requested" flag alone wasn't enough, since signalling it couldn't
// interrupt a curl_easy_perform() already in progress (which can run for
// its own full timeout, up to 180s for a big file, regardless) - the
// thread now also installs a curl progress callback that aborts an
// in-progress transfer the moment an application-start pause is set, via
// karaokeCatalogOnApplicationStart below.
void karaokeApplyCatalogOverlay();

// Tells the background thread's retry loop to give up for good instead of
// running for up to its ~10 minute budget. Call unconditionally from
// DEINITIALIZE_PLUGIN (this process is exiting, regardless of which title
// it was for).
void karaokeCatalogRequestStop();

// Call from ON_APPLICATION_START, for every title launch unconditionally.
// Pauses the background thread (aborting any curl transfer already in
// flight, and holding off starting a new one) for a few seconds around
// every single application start - confirmed necessary on hardware, see
// karaokeApplyCatalogOverlay's own comment above. Only also calls
// karaokeCatalogRequestStop() (give up for good, not just pause) if the
// title that just started is Wii Karaoke U itself, since that's the only
// case where further writes genuinely can't help this session anymore.
void karaokeCatalogOnApplicationStart();
