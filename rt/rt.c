/*                            R T . C
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
/** @file rt.c
 *
 * The "standard" librt geometry/ray intersection engine.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <brlcad/vmath.h>
#include <brlcad/bu.h>
#include <brlcad/bn.h>
#include <brlcad/raytrace.h>

#include "rt/rt.h"

#undef DEBUG
#ifdef DEBUG
#warning "DEBUG"
#define RESOLVE(x) struct application *a; (((struct application *)(x))->a_magic == RT_AP_MAGIC)?(struct application *)(x):PANIC("This is not an RT instance!\n"),NULL
#else
#define RESOLVE(x) struct application *a = ((struct application *)(x))
#endif

#define RTIP a->a_rt_i

static int
hit(struct application * a, struct partition *PartHeadp, struct seg * s)
{
    /* (set! a->a_uptr (map translate p)) */
    struct part *f, *c, *l;	/* first, current, last */
    struct partition *pp;
    f = c = l = NULL;
    s = NULL;

    /* walk the partition list */
    for (pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {

	/* get a fresh part cell from the memory manager. Append it to
	 * the end of the list.
	 * (would it be better to wrap this, or do cons style?) */
	if(f==NULL)
	    f = c = l = get_part();
	else {
	    l->next = c = get_part();
	    l = c;
	}
	c->next = NULL;

	/* generate the in/out normals */
	RT_HIT_NORMAL(pp->pt_inhit->hit_normal, pp->pt_inhit, pp->pt_inseg->seg_stp, a->a_ray, 0);
	RT_HIT_NORMAL(pp->pt_inhit->hit_normal, pp->pt_outhit, pp->pt_outseg->seg_stp, a->a_ray, 0);

	/* copy the useful factors */
	c->in_dist = pp->pt_inhit->hit_dist;
	c->out_dist = pp->pt_outhit->hit_dist;
	strncpy(c->region, pp->pt_regionp->reg_name, NAMELEN-1);
	VMOVE(c->in, pp->pt_inhit->hit_point);
	VMOVE(c->out, pp->pt_outhit->hit_point);
	VMOVE(c->innorm, pp->pt_inhit->hit_normal);
	VMOVE(c->outnorm, pp->pt_outhit->hit_normal);
	/* and compute the hit depth */
	c->depth = c->out_dist - c->in_dist;
    }
    a->a_uptr = (void *)f;
    return 0;
}

static int
miss(struct application * a)
{
    a->a_uptr = NULL;
    return 0;
}

struct part    *
rt_shoot(void *g, struct xray * ray)
{
    RESOLVE(g);
    VMOVE(a->a_ray.r_pt, (*ray).r_pt);
    VMOVE(a->a_ray.r_dir, (*ray).r_dir);
    rt_shootray(a);		/* call into librt */
    return (struct part *) a->a_uptr;
}

double
rt_getsize(void *g)
{
    RESOLVE(g);
    return RTIP->rti_radius;
}

int
rt_getbox(void *g, point_t * min, point_t * max)
{
    RESOLVE(g);
    VMOVE(*min, RTIP->mdl_min);
    VMOVE(*max, RTIP->mdl_max);
    return 0;
}

void           *
rt_constructor(char *file, int numreg, char **regs)
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

    while (numreg--)
	rt_gettree(a->a_rt_i, *regs++);	/* load up the named regions */
    rt_prep_parallel(a->a_rt_i, bu_avail_cpus());	/* and compile to in-mem
							 * versions */
    return (void *) a;
}

int
rt_destructor(void *g)
{
    RESOLVE(g);
    rt_free_rti(a->a_rt_i);
    free(a);
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
