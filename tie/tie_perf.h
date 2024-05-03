/*                     T I E _ P E R F . H
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
/** @file tie_perf.h
 *
 * The "standard" librt geometry/ray intersection engine.
 *
 * Using Triangle Intersection Engine (TIE) for BoT raytracing
 */

#ifndef _TIE_PERF_H
#define _TIE_PERF_H

#include "rtcmp.h"

void            tie_perf_shoot(void *geom, struct xray * ray);
double          tie_perf_getsize(void *g);
int             tie_perf_getbox(void *g, point_t * min, point_t * max);
void           *tie_perf_constructor(const char*, int, const char**);
int             tie_perf_destructor(void *);

#endif

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

