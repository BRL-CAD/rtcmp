/*                      A C C U C H E C K . H
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

#ifndef ACCUCHECK_H
#define ACCUCHECK_H

#include "json.hpp"
#include "rtcmp.h"

void do_accu_run(const char *prefix, int argc, char **argv, int ncpus,
	void*(*constructor)(char *, int, char**, nlohmann::json *),
	int(*getbox)(void *, point_t *, point_t *),
	double(*getsize)(void*),
	void (*shoot)(void*, struct xray *),
	int(*destructor)(void *));


void compare_shots(const char *r1, const char *r2);

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
