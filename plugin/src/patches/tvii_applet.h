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

// Call from INITIALIZE_PLUGIN (boot), not ON_APPLICATION_START for TVii itself: by
// the time TVii's own process starts, it has already read/cached these files, so
// writing them there is too late to affect that launch. Confirmed (by hand, over
// FTP) that overwriting the two real rootca DER files on the MLC filesystem is what
// actually makes TVii trust our CA; the system just reverts those files after a few
// reboots, so we reapply this every boot instead of trying to make it stick
// permanently.
//
// Does the write with an Mocha_UnlockFSClient()-unlocked FSClient talking to the
// native "/vol/storage_mlc01/..." path directly, not Mocha_MountFS + stdio: content
// partitions are mounted read-only even to their own owning title, and the devoptab
// mount's internal FSClient is never unlocked, so fopen()/fwrite() through it never
// actually had permission to write - it was probably failing silently.
void tviiApplyRootCAOverlay();
