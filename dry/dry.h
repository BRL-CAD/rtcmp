/*                           D R Y . H
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
/** @file dry.h
 *
 * The "dry" raytrace method does not calculate any geometry intersections.
 * Rather, it provides an "upper bound" to characterize overhead introduced by
 * logic other than the actual intersection calculations.  In effect, it
 * simulates the numbers one would expect with an infinitely fast raytracer -
 * all *real* timings should be slower than a dry run, modulo resource
 * starvation or other operating system related testing interference.
 */

#ifndef _DRY_H
#define _DRY_H

#include "rtcmp.h"

extern "C" void     dry_shoot(void *geom, struct xray * ray);
extern "C" double   dry_getsize(void *g);
extern "C" int      dry_getbox(void *g, point_t * min, point_t * max);
extern "C" void    *dry_constructor(const char *file, int numreg, const char **regs);
extern "C" int      dry_destructor(void *);

#endif

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
