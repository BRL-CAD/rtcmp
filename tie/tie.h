/*                           T I E . H
 * RtCmp
 *
 * Copyright (c) 2007-2024 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file tie.h
 *
 * The TIE-enabled librt engine.  A thin variant over the shared librt
 * engine core: identical to rt except construction toggles
 * LIBRT_BOT_MINTIE so BoT prep uses the Triangle Intersection Engine.
 */

#ifndef _TIE_H
#define _TIE_H

#include "rtcmp.h"
#include "rt/librt_engine.h"

/* tie uses the shared engine core verbatim */
#define tie_shoot	librt_engine_shoot
#define tie_getsize	librt_engine_getsize
#define tie_getbox	librt_engine_getbox
#define tie_destructor	librt_engine_destructor

/* TIE-enabled construction (mintie enabled) */
void *tie_constructor(const char *file, int numreg, const char **regs);

#endif

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
