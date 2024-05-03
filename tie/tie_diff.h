/*                      T I E _ D I F F . H
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
/** @file tie_diff.h
 *
 * Output librt TIE raytrace results to a json file.
 *
 */

#ifndef _TIE_DIFF_H
#define _TIE_DIFF_H

#include "json.hpp"
#include "rtcmp.h"

extern "C" void    tie_diff_shoot(void *geom, struct xray * ray);
extern "C" double  tie_diff_getsize(void *g);
extern "C" int     tie_diff_getbox(void *g, point_t * min, point_t * max);
extern "C" void   *tie_diff_constructor(const char *, int, const char **, nlohmann::json *);
extern "C" int     tie_diff_destructor(void *);

#endif

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

