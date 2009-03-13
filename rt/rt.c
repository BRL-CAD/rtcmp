
/*
 * $Id$
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
	struct part *f, *c, *l;	/* first, current, last */l = c;
	struct partition *pp;
	f = c = l = NULL;
	s = NULL;

	for (pp = PartHeadp->pt_forw; pp != PartHeadp; pp = pp->pt_forw) {
		if(f==NULL)
			f = c = l = get_part();
		else {
			l->next = c = get_part();
			l = c;
		}
		c->next = NULL;
		c->in_dist = pp->pt_inhit->hit_dist;
		c->out_dist = pp->pt_outhit->hit_dist;
		strncpy(c->region, pp->pt_regionp->reg_name, NAMELEN-1);
		RT_HIT_NORM(pp->pt_inhit, pp->pt_inseg->seg_stp, a->a_ray);
		RT_HIT_NORM(pp->pt_outhit, pp->pt_outseg->seg_stp, a->a_ray);
		VMOVE(c->in, pp->pt_inhit->hit_point);
		VMOVE(c->out, pp->pt_outhit->hit_point);
		VMOVE(c->innorm, pp->pt_inhit->hit_normal);
		VMOVE(c->outnorm, pp->pt_outhit->hit_normal);
		c->depth = c->out_dist - c->in_dist;
	}
	a->a_uptr = (genptr_t)f;
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
	RT_APPLICATION_INIT(a);
	a->a_magic = RT_AP_MAGIC;	/* just in case we want to throw
					 * debugging on */

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
