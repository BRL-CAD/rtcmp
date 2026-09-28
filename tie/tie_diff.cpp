/*                    T I E _ D I F F . C P P
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
/** @file tie_diff.cpp
 *
 * Output raytrace results to a json file
 *
 * Use Triangle Intersection Engine (TIE) for BoT raytracing
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

#include "comp/jsonwriter.hpp"
#include "tie/tie_diff.h"

static int
hit(struct application * a, struct partition *PartHeadp, struct seg * s)
{
    auto &writer = tsj::Writer::instance();

    /* walk the partition list */
    for (struct partition *pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {

	/* generate the in/out normals */
	RT_HIT_NORMAL(pp->pt_inhit->hit_normal, pp->pt_inhit, pp->pt_inseg->seg_stp, a->a_ray, 0);
	RT_HIT_NORMAL(pp->pt_inhit->hit_normal, pp->pt_outhit, pp->pt_outseg->seg_stp, a->a_ray, 0);

	writer.addPartition(pp);
    }
    return 0;
}

static int
miss(struct application * a)
{
    return 0;
}

/* Write each shot through the thread-local JSON writer. */
void
tie_diff_shoot(void *g, struct xray * ray)
{
    struct application *a = (struct application *)g;
    auto &writer = tsj::Writer::instance();

    VMOVE(a->a_ray.r_pt, (*ray).r_pt);
    VMOVE(a->a_ray.r_dir, (*ray).r_dir);

    writer.beginShot(*ray);
    rt_shootray(a);		/* call into librt */
    writer.endShot();
}

double
tie_diff_getsize(void *g)
{
    struct application *a = (struct application *)g;
    return a->a_rt_i->rti_radius;
}

int
tie_diff_getbox(void *g, point_t * min, point_t * max)
{
    struct application *a = (struct application *)g;
    VMOVE(*min, a->a_rt_i->mdl_min);
    VMOVE(*max, a->a_rt_i->mdl_max);
    return 0;
}

extern "C" void           *
tie_diff_constructor(const char *file, int numreg, const char **regs)
{
    struct application *a;
    char            descr[BUFSIZ];

    if(numreg < 1) {
	fprintf(stderr, "RT: Need at least one region\n");
	return NULL;
    }

    a = (struct application *) bu_malloc(sizeof(struct application), "RT application");
    RT_APPLICATION_INIT(a);	/* just does a memset to 0 and then sets the magic */

    /* assign the callback functions */
    a->a_logoverlap = rt_silent_logoverlap;
    a->a_hit = hit;
    a->a_miss = miss;

    a->a_rt_i = rt_dirbuild(file, descr, 0);	/* attach the db file */
    if (a->a_rt_i == NULL) {
	fprintf(stderr, "RT: Failed to load database: %s\n", file);
	bu_free(a, "RT application");
	return NULL;
    }

    /* LIBRT_BOT_MINTIE is used only when rt_bot_prep is called, so if we
     * override the setting here to always enable, we turn the "standard" librt
     * shotline logic into a TIE enabled test. We just restore the prior
     * setting once we're done in order to localize the impact to just this
     * test. */
    const char *current_mintie = getenv("LIBRT_BOT_MINTIE");
    char *previous_mintie = bu_strdup(current_mintie ? current_mintie : "");
    bu_setenv("LIBRT_BOT_MINTIE", "1", 1);

    while (numreg--)
	rt_gettree(a->a_rt_i, *regs++);	/* load up the named regions */
    rt_prep_parallel(a->a_rt_i, bu_avail_cpus());	/* and compile to in-mem
							 * versions */
    /* Prep is complete, restore the env LIBRT_BOT_MINTIE value */
    bu_setenv("LIBRT_BOT_MINTIE", previous_mintie, 1);
    bu_free(previous_mintie, "previous LIBRT_BOT_MINTIE value");

    return (void *) a;
}

int
tie_diff_destructor(void *g)
{
    struct application *a = (struct application *)g;
    rt_free_rti(a->a_rt_i);
    bu_free(a, "free RT application");
    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

