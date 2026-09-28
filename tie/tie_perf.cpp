/*                    T I E _ P E R F . C P P
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
/** @file tie_perf.cpp
 *
 * The "standard" librt geometry/ray intersection engine - performance
 * centric functions.
 *
 * Uses Triangle Intersection Engine (TIE) for BoT raytracing
 */

extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <brlcad/vmath.h>
#include <brlcad/bu.h>
#include <brlcad/bn.h>
#include <brlcad/raytrace.h>
}

#include "tie/tie_perf.h"
#include "rt/rt_perf.h"

void
tie_perf_shoot(void *g, struct xray * ray)
{
    rt_perf_shoot(g, ray);
}

double
tie_perf_getsize(void *g)
{
    return rt_perf_getsize(g);
}

int
tie_perf_getbox(void *g, point_t * min, point_t * max)
{
    return rt_perf_getbox(g, min, max);
}

void           *
tie_perf_constructor(const char *file, int numreg, const char **regs)
{
    /* LIBRT_BOT_MINTIE is used only when rt_bot_prep is called, so if we
     * override the setting here to always enable, we turn the "standard" librt
     * shotline logic into a TIE enabled test. We just restore the prior
     * setting once we're done in order to localize the impact to just this
     * test. */
    const char *current_mintie = getenv("LIBRT_BOT_MINTIE");
    char *previous_mintie = bu_strdup(current_mintie ? current_mintie : "");
    bu_setenv("LIBRT_BOT_MINTIE", "1", 1);

    void *instance = rt_perf_constructor(file, numreg, regs);

    /* Prep is complete, restore the env LIBRT_BOT_MINTIE value */
    bu_setenv("LIBRT_BOT_MINTIE", previous_mintie, 1);
    bu_free(previous_mintie, "previous LIBRT_BOT_MINTIE value");

    return instance;
}

int
tie_perf_destructor(void *g)
{
    return rt_perf_destructor(g);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

