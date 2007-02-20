/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "rtcmp.h"
#include "perfcomp.h"

void
showpart(struct part *p) 
{
	while(p) {
		printf("HIT: (%f) %s\n", p->depth, p->region);
		p = p->next;
	}
	return;
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
	int i;
	clock_t cstart, cend;
	struct part *p;
	struct retpack_s *r;
	struct timeval start, end;
	struct xray ray;
	void *inst;

	prefix = prefix;
	nthreads = nthreads; 
	nproc = nproc; 
	getbox = getbox; 

	r = (struct retpack_s *)malloc(sizeof(struct retpack_s));
	if(r == NULL) return NULL;

	inst = constructor(*argv, argc-1, argv+1);
	if(inst == NULL) { free(r); return NULL; }

/* shoot the accuracy rays */
/* end accuracy ray shooting */

/* performance run */
	gettimeofday(&start,NULL);
	cstart = clock();

	VSET(ray.r_pt, 0, 0, -4000);
	VSET(ray.r_dir, 0, 0, 1);

	/* this needs to be changed to a grid based on geometry size */
	for(i=0;i<NUMRAYS;++i) {
		p = shoot(inst,&ray);
		free_part_r(p);
	}

	cend = clock();
	gettimeofday(&end,NULL);
/* end of performance run */

	destructor(inst);

#define SEC(tv) ((double)tv.tv_sec + (double)(tv.tv_usec)/(double)1e6)
	r->t = SEC(end) - SEC(start);
	r->c = (double)(cend-cstart)/(double)CLOCKS_PER_SEC;

	return r;
}
