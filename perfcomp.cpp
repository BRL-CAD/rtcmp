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
#include <sys/time.h>

#include <fstream>

#include "rtcmp.h"
#include "perfcomp.h"

#include "json.hpp"

#define NUMRAYSPERVIEW NUMRAYS/NUMVIEWS

void
showpart(struct part *p)
{
    while(p) {
	printf("(%.2f)%s  ", p->depth, p->region);
	p = p->next;
    }
    return;
}

/*
 * compare two partition lists. Return the RMS of deviation (both indist and
 * outdist). If the regions don't match up, return a negative number
 */
double
cmppartl(struct part *p1, struct part *p2)
{
    float rms = 0.0;
    while(p1&&p2) {
	if( strncmp(p1->region,p2->region,NAMELEN) != 0 )
	    return -99999;
#define SQ(x) ((x)*(x))
	rms += SQ(p1->in_dist - p2->in_dist) + SQ(p1->out_dist - p2->out_dist);

	p1 = p1->next;
	p2 = p2->next;
    }
    if(p1||p2)
	return -1;
    return sqrt(rms);
}

/*
 * TODO:
 *	* pass in "accuracy" rays and pass back the results.
 *	* Shoot on a grid set instead of a single ray.
 */
nlohmann::json *
do_perf_run(const char *prefix, int argc, char **argv, int nthreads, int nproc,
	void *(*constructor) (char *, int, char **, nlohmann::json *),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *))
{
    clock_t cstart, cend;
    struct part **p;
    struct timeval start, end;
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

    nlohmann::json jshots;

    jshots["engine"] = prefix;
    jshots["data_version"] = "1.0";

    ray = (struct xray *)bu_malloc(sizeof(struct xray)*(NUMTRAYS+1), "allocating ray space");

    inst = constructor(*argv, argc-1, argv+1, &jshots);
    if (inst == NULL) {
	return NULL;
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
	rt_raybundle_maker(ray+j*NUMRAYS,radius,avec,bvec,100,NUMRAYS/100);
    }

    /* performance run */
    gettimeofday(&start,NULL); cstart = clock();

    /* actually shoot all the pre-defined rays */
    for(int i=0;i<NUMRAYS;++i)
	shoot(inst,&ray[i]);

    cend = clock(); gettimeofday(&end,NULL);
    /* end of performance run */

    /* clean up */
    bu_free(ray, "ray space");
    destructor(inst);

    /* fill in the perfomrance data for the bundle */
#define SEC(tv) ((double)tv.tv_sec + (double)(tv.tv_usec)/(double)1e6)
    //ret->t = SEC(end) - SEC(start);
    //ret->c = (double)(cend-cstart)/(double)CLOCKS_PER_SEC;

    std::ofstream jfile("shotlines.json");
    jfile << std::setw(2) << jshots << "\n";
    jfile.close();

    parse_shots_file("shotlines.json");

    return NULL;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

