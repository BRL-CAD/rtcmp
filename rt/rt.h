/*                            R T . H
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
/** @file rt.h
 *
 * The "standard" librt geometry/ray intersection engine.  A thin
 * variant over the shared librt engine core (no TIE toggle).  The same
 * engine serves both perf and diff runs; output is selected by the sink
 * wired into a_uptr by the driver.
 */

#ifndef _RT_H
#define _RT_H

#include "rtcmp.h"
#include "rt/librt_engine.h"

/* rt uses the shared engine core verbatim */
#define rt_shoot	librt_engine_shoot
#define rt_getsize	librt_engine_getsize
#define rt_getbox	librt_engine_getbox
#define rt_destructor	librt_engine_destructor

/* Plain librt construction (mintie disabled) */
void *rt_constructor(const char *file, int numreg, const char **regs);

#endif

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
