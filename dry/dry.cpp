/*                           D R Y . C
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
/** @file dry.c
 *
 * "Functions for the "dry run" rtcmp methodology
 *
 */

#include <string.h>

#include "rtcmp.h"

#include "dry/dry.h"

void
dry_shoot(void *UNUSED(g), struct xray *UNUSED(ray))
{
    ShotSet::Ray ray(0.0);
    VSET(ray.pt,0,0,0);
    VSET(ray.dir,0,0,0);

    std::vector<ShotSet::Partition> parts;
    for (int i = 0; i < 4; i++) {
	ShotSet::Partition part(0.0);
	part.region = std::string("/some/nifty/little/part.r");
	VSET(part.in,0,0,0);
	VSET(part.out,0,0,0);
	VSET(part.in_norm,0,0,0);
	VSET(part.out_norm,0,0,0);
	part.in_dist = 0;
	part.out_dist = 0;

	parts.push_back(part);
    }

    ShotSet::Shot shot{ray, parts};
}

double
dry_getsize(void *UNUSED(g))
{
    return -1.0;	/* MUST be negative for perfcomp! */
}

int
dry_getbox(void *UNUSED(g), point_t *UNUSED(min), point_t *UNUSED(max))
{
    return 0;
}

void           *
dry_constructor(const char *UNUSED(file), int UNUSED(numreg), const char **UNUSED(regs))
{
    return (void *) 1;
}

int
dry_destructor(void *UNUSED(g))
{
    return 0;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
