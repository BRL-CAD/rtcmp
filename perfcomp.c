/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "rtcmp.h"
#include "perfcomp.h"

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
struct retpack_s *
perfcomp(char *prefix, int argc, char **argv, int nthreads, int nproc,
	 void *(*constructor) (char *, int, char **),
	 int (*getbox) (void *, point_t *, point_t *),
	 double (*getsize) (void *),
	 struct part * (*shoot) (void *, struct xray * ray),
	 int (*destructor) (void *))
{
	int i, j;
	clock_t cstart, cend;
	struct part **p;
	struct retpack_s *r;
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

	prefix = prefix;
	nthreads = nthreads; 
	nproc = nproc; 

	r = (struct retpack_s *)malloc(sizeof(struct retpack_s));
	if(r == NULL) return NULL;

	p = (struct part **)bu_malloc(sizeof(struct part *)*NUMTRAYS, "allocating partition space");
	ray = (struct xray *)bu_malloc(sizeof(struct xray)*(NUMTRAYS+1), "allocating ray space");

	inst = constructor(*argv, argc-1, argv+1);
	if(inst == NULL) { free(r); return NULL; }

	/* first with a legit radius gets to define the bb and sph */
	/* XXX: should this lock? */
	if(radius < 0.0) {
		radius = getsize(inst);
		getbox(inst, bb, bb+1);
		VADD2SCALE(bb[2], *bb, bb[1], 0.5);	/* (bb[0]+bb[1])/2 */
		for(i=0;i<NUMVIEWS;i++) VUNITIZE(dir[i]); /* normalize the dirs */
	}
	/* XXX: if locking, we can unlock here */

	/* build the views with pre-defined rays, yo */
	for(j=0;j<NUMVIEWS;++j) {
		vect_t avec,bvec;

		VMOVE(ray->r_dir,dir[j]);
		VJOIN1(ray->r_pt,bb[2],-radius,dir[j]);

		r->p[j] = shoot(inst,ray);	/* shoot the accuracy ray while we're here */

		/* set up an othographic grid */
		bn_vec_ortho( avec, ray->r_dir );
		VCROSS( bvec, ray->r_dir, avec );
		VUNITIZE( bvec );
		rt_raybundle_maker(ray+j*NUMRAYS,radius,avec,bvec,100,NUMRAYS/100);
	}

/* performance run */
	gettimeofday(&start,NULL); cstart = clock();

	/* actually shoot all the pre-defined rays */
	for(i=0;i<NUMRAYS;++i) p[i] = shoot(inst,&ray[i]);

	cend = clock(); gettimeofday(&end,NULL);
/* end of performance run */

	/* return the partitions to the pool */
	for(i=0;i<NUMRAYS;++i) free_part_r(p[i]);

	/* clean up */
	bu_free(ray, "ray space");
	bu_free(p, "partition space");
	destructor(inst);

	/* fill in the perfomrance data for the bundle */
#define SEC(tv) ((double)tv.tv_sec + (double)(tv.tv_usec)/(double)1e6)
	r->t = SEC(end) - SEC(start);
	r->c = (double)(cend-cstart)/(double)CLOCKS_PER_SEC;

	return r;
}
