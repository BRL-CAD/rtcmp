/*                      R T _ J S O N . C
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
/** @file rt_json.cpp
 *
 * Output raytrace results to a json file
 *
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

#include "json/rt_json.h"
#include "json/json.hpp"

static int
hit(struct application * a, struct partition *PartHeadp, struct seg * s)
{
    struct app_json *j = (struct app_json *)a->a_uptr;
    nlohmann::json *shotparts = j->shotparts;
    
    /* walk the partition list */
    for (struct partition *pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {

	/* generate the in/out normals */
	RT_HIT_NORMAL(pp->pt_inhit->hit_normal, pp->pt_inhit, pp->pt_inseg->seg_stp, a->a_ray, 0);
	RT_HIT_NORMAL(pp->pt_inhit->hit_normal, pp->pt_outhit, pp->pt_outseg->seg_stp, a->a_ray, 0);

	nlohmann::json jpart;
	jpart["region"] = pp->pt_regionp->reg_name;
	jpart["in_dist"] = pp->pt_inhit->hit_dist;
	jpart["out_dist"] = pp->pt_outhit->hit_dist;
	jpart["in_pt"]["X"] = pp->pt_inhit->hit_point[X];
	jpart["in_pt"]["Y"] = pp->pt_inhit->hit_point[Y];
	jpart["in_pt"]["Z"] = pp->pt_inhit->hit_point[Z];
	jpart["out_pt"]["X"] = pp->pt_outhit->hit_point[X];
	jpart["out_pt"]["Y"] = pp->pt_outhit->hit_point[Y];
	jpart["out_pt"]["Z"] = pp->pt_outhit->hit_point[Z];
	jpart["in_norm"]["X"] = pp->pt_inhit->hit_normal[X];
	jpart["in_norm"]["Y"] = pp->pt_inhit->hit_normal[Y];
	jpart["in_norm"]["Z"] = pp->pt_inhit->hit_normal[Z];
	jpart["out_norm"]["X"] = pp->pt_outhit->hit_normal[X];
	jpart["out_norm"]["Y"] = pp->pt_outhit->hit_normal[Y];
	jpart["out_norm"]["Z"] = pp->pt_outhit->hit_normal[Z];

	(*shotparts)["partitions"].push_back(jpart);
    }
    return 0;
}

static int
miss(struct application * a)
{
    return 0;
}

/* Note - output data is stored in the json container */
void
json_shoot(void *g, struct xray * ray)
{
    struct application *a = (struct application *)g;
    struct app_json *j = (struct app_json *)a->a_uptr;

    // Make a container for this particular shot
    
    nlohmann::json rayparts;
    rayparts["px"] = ray->r_pt[X];
    rayparts["py"] = ray->r_pt[Y];
    rayparts["pz"] = ray->r_pt[Z];
    rayparts["dx"] = ray->r_dir[X];
    rayparts["dy"] = ray->r_dir[Y];
    rayparts["dz"] = ray->r_dir[Z];

    // TODO - need parallel awareness in shotparts container?
    j->shotparts = &rayparts;

    VMOVE(a->a_ray.r_pt, (*ray).r_pt);
    VMOVE(a->a_ray.r_dir, (*ray).r_dir);
    rt_shootray(a);		/* call into librt */

    // TODO - lock?
    (*j->jshots)["shots"].push_back(rayparts);
}

double
json_getsize(void *g)
{
    struct application *a = (struct application *)g;
    return a->a_rt_i->rti_radius;
}

int
json_getbox(void *g, point_t * min, point_t * max)
{
    struct application *a = (struct application *)g;
    VMOVE(*min, a->a_rt_i->mdl_min);
    VMOVE(*max, a->a_rt_i->mdl_max);
    return 0;
}

extern "C" void           *
json_constructor(char *file, int numreg, char **regs, nlohmann::json *j)
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

    /* Connect the json output container to the application structure */
    struct app_json *jc;
    BU_GET(jc, struct app_json);
    jc->jshots = j;
    a->a_uptr = (void *)jc;

    return (void *) a;
}

int
json_destructor(void *g)
{
    struct application *a = (struct application *)g;
    struct app_json *jc = (struct app_json *)a->a_uptr;
    BU_PUT(jc, struct app_json);
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
