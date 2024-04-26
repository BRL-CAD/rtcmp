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

#define SET(x)  \
    p[x] = get_part(); \
    strncpy(p[x]->region,"/some/nifty/little/part.r",NAMELEN); \
    VSET(p[x]->in,0,0,0); \
    VSET(p[x]->out,0,0,0); \
    VSET(p[x]->innorm,0,0,0); \
    VSET(p[x]->outnorm,0,0,0); \
    p[x]->in_dist = 0; \
    p[x]->out_dist = 0;

struct part    *
dry_shoot(void *UNUSED(g), struct xray *UNUSED(ray))
{
    struct part *p[4];
    SET(0);
    SET(1);
    SET(2);
    SET(3);
    p[0]->next = p[1];
    p[1]->next = p[2];
    p[2]->next = p[3];
    p[3]->next = NULL;
    return p[0];
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
