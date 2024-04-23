/*                      R A Y F O R C E . C
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
/** @file rayforce.c
 *
 * Brief description
 *
 */

#include <brlcad/common.h>

#include "rtcmp.h"		/* drags in brlcad vmath stuff, too */
#include "rayforce.h"

struct part    *
rayforce_shoot(void *UNUSED(geom), struct xray *UNUSED(ray))
{
    return NULL;
}

double
rayforce_getsize(void *UNUSED(geom))
{
    return 0.0;
}

int
rayforce_getbox(void *UNUSED(geom), point_t *UNUSED(min), point_t *UNUSED(max))
{
    return 0.0;
}

void           *
rayforce_constructor()
{
    return NULL;
}

int
rayforce_destructor(void *UNUSED(i))
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
