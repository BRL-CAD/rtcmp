/*                         A D R T . C P P
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
/** @file adrt.cpp
 *
 * Brief description
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <common.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_STRING_H
#  include <string.h>
#else
#  include <strings.h>
#endif

extern "C" {
#include <brlcad/bu.h>
#include <brlcad/vmath.h>
#include <brlcad/raytrace.h>
#include <brlcad/rt/tie.h>

#include "adrt.h"
}
#include "gfile.h"

#define RESOLVE(x) GFile *gf = (GFile *)(x)

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

extern "C" struct part    *
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
    TIE_WORK(gf->tie, &r, &id, hitfunc, (void *)p);

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
extern "C" double
adrt_getsize(void *g)
{
    RESOLVE(g);
#define SQ(x) ((x)*(x))			/* square */
#define GTR(a,b) (a)>(b)?(a):(b)	/* the greater of two values */
#define F(f,i) fabs(gf->tie->f[i])		/* non-hygenic expansion. */
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

extern "C" int
adrt_getbox(void *g, point_t * min, point_t * max)
{
    RESOLVE(g);
    VMOVE(*min, gf->tie->min);
    VMOVE(*max, gf->tie->max);
    return 1;
}

extern "C" void *
adrt_constructor(char *file, int numreg, char **regs)
{
    GFile *g = new GFile;
    g->load_g(file, numreg, (const char **)regs);
    return (void *)g;
}

extern "C" int
adrt_destructor(void *g)
{
    RESOLVE(g);
    delete gf;
    return 0;
}

