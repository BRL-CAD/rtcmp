/*                      P E R F C O M P . C
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
/** @file perfcomp.c
 *
 * Brief description
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fstream>

#include <brlcad/bu.h>

#include "rtcmp.h"

/*
 * TODO:
 *	* pass in "accuracy" rays and pass back the results.
 *	* Shoot on a grid set instead of a single ray.
 */
void
do_perf_run(const char *prefix, int argc, const char **argv, int nthreads, int rays_per_view,
	void *(*constructor) (const char *, int, const char **),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *))
{
    int64_t wallclock_start, wallclock_end;
    clock_t cpu_start, cpu_end;
    struct part **p;
    struct xray *ray;
    void *inst;

    /* retained between runs. */
    static double radius = -1.0;	/* bounding sphere and flag */
    static point_t bb[3];	/* bounding box, third is center */

    static vect_t dir[NUMVIEWS] = {
	{0,0,1}, {0,1,0}, {1,0,0},	/* axis */
	{1,1,1}, {1,4,-1}, {-1,-2,4}	/* non-axis */
    };
    for(int i=0;i<NUMVIEWS;i++) VUNITIZE(dir[i]); /* normalize the dirs */


    ray = (struct xray *)bu_malloc(sizeof(struct xray)*(rays_per_view*NUMVIEWS+1), "allocating ray space");

    inst = constructor(*argv, argc-1, argv+1);
    if (inst == NULL) {
	return;
    }

    /* first with a legit radius gets to define the bb and sph */
    /* XXX: should this lock? */
    if (radius < 0.0) {
	radius = getsize(inst);
	getbox(inst, bb, bb+1);
	VADD2SCALE(bb[2], *bb, bb[1], 0.5);	/* (bb[0]+bb[1])/2 */
    }
    /* XXX: if locking, we can unlock here */

    /* build the views with pre-defined rays, yo */
    for(int j=0; j < NUMVIEWS; ++j) {
	vect_t avec,bvec;

	VMOVE(ray->r_dir,dir[j]);
	VJOIN1(ray->r_pt,bb[2],-radius,dir[j]);

	shoot(inst,ray);	/* shoot the accuracy ray while we're here */

	/* set up an othographic grid */
	bn_vec_ortho( avec, ray->r_dir );
	VCROSS( bvec, ray->r_dir, avec );
	VUNITIZE( bvec );
	rt_raybundle_maker(ray+j*rays_per_view,radius,avec,bvec,100,rays_per_view/100);
    }

    /* performance run */
    wallclock_start = bu_gettime(); cpu_start = clock();

    /* actually shoot all the pre-defined rays */
    for(int i=0;i<rays_per_view;++i)
	shoot(inst,&ray[i]);

    cpu_end = clock(); wallclock_end = bu_gettime();
    /* end of performance run */

    /* clean up */
    bu_free(ray, "ray space");
    destructor(inst);

    /* Report times */
    std::cout << "Wall clock time (" << prefix << "): " << (wallclock_end - wallclock_start)/1000000.0 << "\n";
    std::cout << "CPU time        (" << prefix << "): " << (double)(cpu_end-cpu_start)/(double)CLOCKS_PER_SEC << "\n";
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

