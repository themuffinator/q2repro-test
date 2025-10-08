/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "shared/shared.h"
#include "common/pmove.h"
#include "shared/game3_shared.h"
#include "common/game3_pmove.h"

#define PMOVE_OLD 1
#define PMOVE_TYPE game3_pmove_old_t
#define PMOVE_FUNC game3_PmoveOld
#define PMOVE_TIME_SHIFT 3
#define PMOVE_C2S(x) COORD2SHORT(x)
#define PMOVE_TRACE(start, mins, maxs, end) pm->trace(start, mins, maxs, end)
#define PMOVE_TRACE_MASK(start, mins, maxs, end, mask) pm->trace(start, mins, maxs, end)
#include "template.c"
