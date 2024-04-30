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

#define SET()  \
    strncpy(p.region,"/some/nifty/little/part.r",NAMELEN); \
    VSET(p.in,0,0,0); \
    VSET(p.out,0,0,0); \
    VSET(p.innorm,0,0,0); \
    VSET(p.outnorm,0,0,0); \
    p.in_dist = 0; \
    p.out_dist = 0; \
    bu_free(p.region, "region");

void
dry_shoot(void *UNUSED(g), struct xray *UNUSED(ray))
{
    for (int i = 0; i < 4; i++) {
    struct part p;
    SET();
    }
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
dry_constructor(char *UNUSED(file), int UNUSED(numreg), char **UNUSED(regs))
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
