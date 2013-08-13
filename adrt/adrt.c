/*
 * $Id$
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_TIE

#include <common.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_STRING_H
#  include <string.h>
#else
#  include <strings.h>
#endif

#include <brlcad/bu.h>
#include <brlcad/vmath.h>
#include <brlcad/raytrace.h>

#include "tri.h"

#include <brlcad/tie.h>

#include "adrt.h"

/* no magic to doublecheck... */
#define RESOLVE(x) struct tie_s *t = (struct tie_s *)(x)



/*** internal functions, should all be static ***/

/*
 * tie internal hit function, gets called once for every single ray/tri
 * intersection
 */
static void *
hitfunc(struct tie_ray_s *UNUSED(ray), struct tie_id_s *id, struct tie_tri_s *trie, void *ptr)
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
		VMOVE((*p)->out, id->pos);
		VMOVE((*p)->outnorm, id->norm);
		(*p)->out_dist = id->dist;
		(*p)->depth = id->dist - (*p)->in_dist;
	} else {
		if(!*p)
			p[1] = (*p) = get_part();
		else {
			(*p)->next = get_part();
			(*p) = (*p)->next;
		}
		strncpy((*p)->region,(char *)trie->ptr,NAMELEN-1);	/* may be a big cost? punt in dry hopefully fixes this */
		(*p)->depth = -1.0;	/* signal for the next hit to be out */
		VMOVE((*p)->in, id->pos);
		VMOVE((*p)->innorm, id->norm);
		(*p)->in_dist = id->dist;
	}
	return NULL;
}



/*** interface functions ***/

struct part    *
adrt_shoot(void *geom, struct xray * ray)
{
	RESOLVE(geom);
	struct tie_ray_s r;
	struct tie_id_s id;
	struct part *p[2];

	VMOVE(r.pos,ray->r_pt);
	VMOVE(r.dir,ray->r_dir);
	r.depth = 0;
	p[0] = p[1] = NULL;

	/* multithread this for parallel */
	tie_work0(t, &r, &id, hitfunc, (void *)p);

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
#define F(f,i) fabs(t->f[i])		/* non-hygenic expansion. */
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
	VMOVE(*min, t->min);
	VMOVE(*max, t->max);
	return 1;
}

void           *
adrt_constructor(const char *file, int numreg, const char **regs)
{
	
	struct tie_s *te;
	struct tri_region_s *reg;

	te = (struct tie_s *)bu_malloc(sizeof(struct tie_s),"TIE constructor");
	tie_init0(te,0, TIE_KDTREE_FAST);	/* prep memory */
	reg = tri_load(file,numreg,regs);
	while(reg) {
		int i;
		float *buf;
		buf = (float *)bu_malloc(sizeof(float) * 3 * 3 * reg->ntri, "buf");
		for(i=0;i< 3 * 3 * reg->ntri; ++i) buf[i] = (float)(reg->t[i]);
		tie_push0(te,(TIE_3 **)&buf,reg->ntri,reg->name,0);
		reg = reg->next;
	}
	tie_prep(te);	/* generate the K-D tree */
	return (void *)te;
}

int
adrt_destructor(void *g)
{
	RESOLVE(g);
	tie_free0(t);
	bu_free(t,"TIE destructor");
	return 0;
}

#endif
