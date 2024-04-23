/*                      P E R F C O M P . H
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
/** @file perfcomp.h
 *
 * Brief description
 *
 */

#ifndef PERFCOMP_H
#define PERFCOMP_H

#include "rtcmp.h"

struct retpack_s *perfcomp(char *prefix, int argc, char **argv, int nthreads, int nproc, 
		void*(*constructor)(char *, int, char**),
		int(*getbox)(void *, point_t *, point_t *),
		double(*getsize)(void*),
		struct part *(*shoot)(void*, struct xray *),
		int(*destructor)(void *));

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
