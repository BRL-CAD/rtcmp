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
#include <brlcad/bn.h>
#include <brlcad/nmg.h>
#include <brlcad/rtgeom.h>
#include <brlcad/raytrace.h>

#include <tie/struct.h>
#include <tie/define.h>
#include <tie/tie.h>

#include "adrt.h"

/* no magic to doublecheck... */
#define RESOLVE(x) tie_t *t = (tie_t *)(x)



/*** internal functions, should all be static ***/

union tree *
region_end(struct db_tree_state *tsp, struct db_full_path *pathp, union tree *curtree, genptr_t client_data)
{
	union tree *ret_tree;
        struct shell *s;

	if(curtree->tr_op == OP_NOP) return curtree;
	ret_tree = nmg_booltree_evaluate(curtree, tsp->ts_tol, &rt_uniresource); 
	if(ret_tree == NULL) { printf("Emptry region\n"); return NULL; }

	/* some sanity checking... */
	NMG_CK_REGION(ret_tree->tr_d.td_r);
	NMG_CK_MODEL(ret_tree->tr_d.td_r->m_p);
	BN_CK_TOL(tsp->ts_tol);

	nmg_triangulate_model(ret_tree->tr_d.td_r->m_p, tsp->ts_tol);

	/* for each shell */
        for(BU_LIST_FOR(s, shell, &ret_tree->tr_d.td_r->s_hd))
        {
                struct faceuse *fu;

                NMG_CK_SHELL(s);

		/* for each face */
                for(BU_LIST_FOR(fu, faceuse, &s->fu_hd))
                {
                        struct loopuse *lu;
                        vect_t facet_normal;

                        NMG_CK_FACEUSE(fu);

                        if(fu->orientation != OT_SAME)
                                continue;

                        /* Grab the face normal if needed */
#if 0
                        NMG_GET_FU_NORMAL(facet_normal, fu);
			printf("\tN: %.2f %.2f %.2f\t", V3ARGS(facet_normal));
#endif

			/* for each triangle */
                        for(BU_LIST_FOR(lu, loopuse, &fu->lu_hd))
                        {
                                struct edgeuse *eu;
				TIE_3 t[3];
				int i = 0;

                                NMG_CK_LOOPUSE(lu);
                                if(BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
                                        continue;

				/* for each vertex */
                                for(BU_LIST_FOR(eu, edgeuse, &lu->down_hd))
                                {
                                        NMG_CK_EDGEUSE(eu);
                                        NMG_CK_VERTEX(eu->vu_p->v_p);
					VMOVE(t[i].v, eu->vu_p->v_p->vg_p->coord);
					++i;
                                }
				tie_push((tie_t *)client_data, t, 1, pathp, 0);
                        }
                }
        }

	return NULL;
}

/*
 * tie internal hit function, gets called once for every single ray/tri
 * intersection
 */
static void *
hitfunc(tie_ray_t *ray, tie_id_t *id, tie_tri_t *trie, void *ptr)
{
	return NULL;
}



/*** interface functions ***/
struct part    *
adrt_shoot(void *geom, struct xray * ray)
{
	RESOLVE(geom);
	tie_ray_t r;
	tie_id_t id;
	int ptr;

	tie_work(t, &r, &id, hitfunc, (void *)&ptr);

	return NULL;
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
	char descr[BUFSIZ];
	struct db_i *dbip;
	tie_t *t;
	struct db_tree_state tree_state;

	struct rt_tess_tol       ttol;
	struct bn_tol            tol;
	struct model		*model;

	model = nmg_mm();

        ttol.magic = RT_TESS_TOL_MAGIC;
        ttol.abs = 0.0;
        ttol.rel = 0.01;
        ttol.norm = 0.0;

        tol.magic = BN_TOL_MAGIC;
        tol.dist = 0.005;
        tol.dist_sq = tol.dist * tol.dist;
        tol.perp = 1e-5;
        tol.para = 1 - tol.perp;

	tree_state = rt_initial_tree_state;     /* struct copy */
        tree_state.ts_tol = &tol;
        tree_state.ts_ttol = &ttol;
	tree_state.ts_m = &model;

	t = (tie_t *)bu_malloc(sizeof(tie_t),"TIE constructor");
	tie_init(t,0);	/* prep memory */
	/* load the .g file */

        if ((dbip = db_open(file, "r")) == DBI_NULL) {
                perror(file);
                return NULL;
        }
        db_dirbuild(dbip);


        BN_CK_TOL(tree_state.ts_tol);
        RT_CK_TESS_TOL(tree_state.ts_ttol);

	db_walk_tree (	dbip,		/* the DB instance ptr */
			numreg,		/* argc */
			regs,		/* argv */
			1, 		/* ncpu */
			&tree_state, 	/* initial tree state */
			NULL,		/* region start function */
			region_end,	/* region end function */
			nmg_booltree_leaf_tess,	/* leave function */
			t);		/* client data */
	tie_prep(t);	/* generate the K-D tree */
	return (void *)t;
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
