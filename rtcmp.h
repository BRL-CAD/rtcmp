/*                         R T C M P . H
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
/** @file rtcmp.h
 *
 * Brief description
 *
 */

#ifndef RTCMP_H
#define RTCMP_H

#include <string>
#include <unordered_map>
#include <brlcad/vmath.h>
#include <brlcad/bu.h>
#include <brlcad/bn.h>
#include <brlcad/raytrace.h>
#include "comp/shotset.h"


/* Defines used when setting up shotline inputs */
#define NUMVIEWS	6			/* this refers to data in perfcomp.c */

/* Do a performance testing run - the purpose of this run is to
 * compare the relative performance of two raytracers (or the impact
 * of changes to the same raytracer) so outputs are not captured -
 * instead, run time is measured by the caller */
void do_perf_run(const char *prefix, int argc, const char **argv, int ncpus, int nvrays,
	void*(*constructor)(const char *, int, const char**),
	int(*getbox)(void *, point_t *, point_t *),
	double(*getsize)(void*),
	void (*shoot)(void*, struct xray *),
	int(*destructor)(void *));

/* Do a run to generate a file used to identify differences between
 * raytracing results.  This will produce a sizable output file, and
 * may run rather slowly since shotline intersection data is being captured
 * for output. */
void
do_diff_run(const char *prefix, int argc, const char **argv, int ncpus, int nvrays,
	void*(*constructor)(const char *, int, const char**, std::string),
	int(*getbox)(void *, point_t *, point_t *),
	double(*getsize)(void*),
	void (*shoot)(void*, struct xray *),
	int(*destructor)(void *),
	CompareConfig& dinfo);

/* Do a comparison between two generated results files (from do_diff_run()).
 * produces output file of differing rays
 */
bool do_comp(const char *file1, const char *file2, const CompareConfig& dinfo);

#endif // RTCMP_H

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

