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

// Lives in the plugin, not the module: WUPS's ON_APPLICATION_START/_ENDS are the
// hooks actually guaranteed to run inside every launched title's own process
// (including system applets like TVii), unlike the module's WUMS lifecycle
// callbacks. Uses libcontentredirection (the same API SDCafiine uses) to merge a
// replacement "rootca" folder over /vol/content for TVii's own process, instead
// of writing over the real NAND files.
void tviiApplyRootCAOverlay();
void tviiRemoveRootCAOverlay();
