/*                 L I B R T _ E N G I N E . C P P
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
/** @file librt_engine.cpp
 *
 * Shared "standard" librt geometry/ray intersection engine core, used
 * by both the plain librt engine (rt) and the TIE-enabled engine (tie).
 *
 * Output is decoupled via an injected ShotSink read from a_uptr:
 *   - perf mode leaves a_uptr NULL (null sink) -> no output work
 *   - diff mode wires a WriterSink -> NDJSON output
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

#include "shot_sink.h"
#include "rt/librt_engine.h"
#include "util/util.h"

static int
hit(struct application *a, struct partition *PartHeadp, struct seg *UNUSED(s))
{
    ShotSink *sink = (ShotSink *)a->a_uptr;

    /* walk the partition list */
    for (struct partition *pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {

	/* generate the in/out normals */
	RT_HIT_NORMAL(pp->pt_inhit->hit_normal, pp->pt_inhit, pp->pt_inseg->seg_stp, a->a_ray, 0);
	RT_HIT_NORMAL(pp->pt_outhit->hit_normal, pp->pt_outhit, pp->pt_outseg->seg_stp, a->a_ray, 0);

	/* only capture output when a sink is present (diff mode) */
	if (sink)
	    sink->addPartition(pp);
    }
    return 0;
}

static int
miss(struct application *UNUSED(a))
{
    return 0;
}

void
librt_engine_shoot(void *g, struct xray *ray)
{
    struct application *a = (struct application *)g;
    ShotSink *sink = (ShotSink *)a->a_uptr;

    VMOVE(a->a_ray.r_pt, (*ray).r_pt);
    VMOVE(a->a_ray.r_dir, (*ray).r_dir);

    if (sink)
	sink->beginShot(*ray);
    rt_shootray(a);		/* call into librt */
    if (sink)
	sink->endShot();
}

double
librt_engine_getsize(void *g)
{
    struct application *a = (struct application *)g;
    return a->a_rt_i->rti_radius;
}

int
librt_engine_getbox(void *g, point_t *min, point_t *max)
{
    struct application *a = (struct application *)g;
    VMOVE(*min, a->a_rt_i->mdl_min);
    VMOVE(*max, a->a_rt_i->mdl_max);
    return 0;
}

void *
librt_engine_construct(const char *file, int numreg, const char **regs, int mintie)
{
    struct application *a;
    char            descr[BUFSIZ];

    if (numreg < 1) {
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
     * shotline logic into a TIE enabled test.  We save the prior setting and
     * restore it once we're done in order to localize the impact to just this
     * test. */
    char *tval = NULL;
    if (mintie) {
	const char *cur = getenv("LIBRT_BOT_MINTIE");
	tval = bu_strdup(cur ? cur : "");
	bu_setenv("LIBRT_BOT_MINTIE", "1", 1);
    }

    const bool scene_ready = rtutil::prepare_scene(a->a_rt_i, numreg, regs);

    /* Prep is complete, restore the env LIBRT_BOT_MINTIE value */
    if (mintie) {
	bu_setenv("LIBRT_BOT_MINTIE", tval, 1);
	bu_free((char *)tval, "tval");
    }

    if (!scene_ready) {
	rt_free_rti(a->a_rt_i);
	bu_free(a, "RT application");
	return NULL;
    }

    return (void *) a;
}

int
librt_engine_destructor(void *g)
{
    struct application *a = (struct application *)g;

    /* Tear down the rt_i.  rt_free_rti() walks rtip->rti_resources and
     * cleans (but does NOT free) every registered resource struct.  We
     * deliberately do NOT free a->a_resource here: the engine does not
     * own it.  Perf never allocates one (librt substitutes rt_uniresource,
     * a static that must not be freed).  Diff allocates its resources in
     * do_diff_run, which frees them there after this destructor runs. */
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
