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

// TVii reads its root CA DER files straight off the MLC filesystem instead of going
// through FSClient calls we could hook with FunctionPatcher, so there's nothing to
// intercept at runtime. Call these from WUMS_APPLICATION_STARTS/_ENDS instead: they
// swap the real on-disk files for ours while TVii is the running applet and restore
// the originals when it closes.
void tviiApplyRootCAOverlay();
void tviiRemoveRootCAOverlay();
