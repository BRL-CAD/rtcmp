/*                          R T _ P E R F . H
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
/** @file rt_perf.h
 *
 * The "standard" librt geometry/ray intersection engine.
 *
 */

#ifndef _RT_H
#define _RT_H

#include "rtcmp.h"

void            rt_perf_shoot(void *geom, struct xray * ray);
double          rt_perf_getsize(void *g);
int             rt_perf_getbox(void *g, point_t * min, point_t * max);
void           *rt_perf_constructor(const char*, int, const char**);
int             rt_perf_destructor(void *);

#endif

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

