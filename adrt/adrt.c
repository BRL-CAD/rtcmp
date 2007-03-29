/*
 * $Id$
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_TIE

#include <common.h>

#include <stdio.h>
#include <math.h>

#ifdef HAVE_STRING_H
#  include <string.h>
#else
#  include <strings.h>
#endif

#include <brlcad/machine.h>
#include <brlcad/bu.h>
#include <brlcad/vmath.h>
#include <brlcad/raytrace.h>

#include "tri.h"

#include <tie/struct.h>
#include <tie/define.h>
#include <tie/tie.h>

#include "adrt.h"

/* no magic to doublecheck... */
#define RESOLVE(x) tie_t *t = (tie_t *)(x)



/*** internal functions, should all be static ***/

/*
 * tie internal hit function, gets called once for every single ray/tri
 * intersection
 */
static void *
hitfunc(tie_ray_t *ray, tie_id_t *id, tie_tri_t *trie, void *ptr)
{
	/* Ugh. Three possible conditions
	 *  1) p and pl are NULL (first hit this shot
	 *  2) part outinfo is not set (in was done, now we do out)
	 *  3) outinfo is set (entering the second region)
	 *
	 * Note that p is a pair of part pointers packed a peck of pickled...
	 * p[0] is the *LAST* part on the list. p[1] is the FIRST.
	 */
	struct part **p = (struct part **)ptr;
	if((*p) && (*p)->depth < 0.0) {
		VMOVE((*p)->out, id->pos.v);
		VMOVE((*p)->outnorm, id->norm.v);
		(*p)->out_dist = id->dist;
		(*p)->depth = id->dist - (*p)->in_dist;
	} else {
		if(!*p)
			p[1] = (*p) = get_part();
		else {
			(*p)->next = get_part();
			(*p) = (*p)->next;
		}
		strncpy((*p)->region,(char *)trie->ptr,NAMELEN-1);
		(*p)->depth = -1.0;	/* signal for the next hit to be out */
		VMOVE((*p)->in, id->pos.v);
		VMOVE((*p)->innorm, id->norm.v);
		(*p)->in_dist = id->dist;
	}
	return NULL;
}



/*** interface functions ***/

struct part    *
adrt_shoot(void *geom, struct xray * ray)
{
	RESOLVE(geom);
	tie_ray_t r;
	tie_id_t id;
	struct part *p[2];

	VMOVE(r.pos.v,ray->r_pt);
	VMOVE(r.dir.v,ray->r_dir);
	r.depth = 0;
	p[0] = p[1] = NULL;

	tie_work(t, &r, &id, hitfunc, (void *)p);

	return p[1];
}

/*
 * I had to one-up the ugly in that adrt.c file. Only my ugly isn't 55 lines.
 *
 * It's just as stupid, though... the notion being that the "bounding sphere" is
 * defined with a center of 0,0,0 (always) and a radius equal to the distance
 * from the origin to the furthest corner of the bounding box. At least, that's
 * how I read that pile of steamin{{~[{{{{+++ATH0
 */
double
adrt_getsize(void *g)
{
	RESOLVE(g);
#define SQ(x) ((x)*(x))			/* square */
#define GTR(a,b) (a)>(b)?(a):(b)	/* the greater of two values */
#define F(f,i) fabs(t->f.v[i])		/* non-hygenic expansion. */
#define S(i) SQ(GTR(F(max,i),F(min,i)))	/* distance to the further plane of axis i, or something. */
	/* given that we know the scalar distance to the further of each plane
	 * pair, this should yeild the scalar distance to the intersection
	 * point. */
	return sqrt(S(0) + S(1) + S(2));
#undef SQ
#undef GTR
#undef F
#undef S
}

int
adrt_getbox(void *g, point_t * min, point_t * max)
{
	RESOLVE(g);
	VMOVE(*min, t->min.v);
	VMOVE(*max, t->max.v);
	return 1;
}

void           *
adrt_constructor(const char *file, int numreg, const char **regs)
{
	
	tie_t *te;
	TIE_3 t[3];
	struct tri_region_s *reg;

	te = (tie_t *)bu_malloc(sizeof(tie_t),"TIE constructor");
	tie_init(te,0);	/* prep memory */
	reg = tri_load(file,numreg,regs);
	while(reg) {
		int i;
		float *buf;
		buf = (float *)bu_malloc(sizeof(float) * 3 * 3 * reg->ntri);
		for(i=0;i< 3 * 3 * reg->ntri; ++i) buf[i] = (float)(reg->t[i]);
		tie_push(te,(TIE_3 *)buf,reg->ntri,reg->name,0);
		reg = reg->next;
	}
	tie_prep(te);	/* generate the K-D tree */
	return (void *)te;
}

int
adrt_destructor(void *g)
{
	RESOLVE(g);
	tie_free(t);
	bu_free(t,"TIE destructor");
	return 0;
}

#endif
