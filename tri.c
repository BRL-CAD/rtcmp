/*
 * $Id$
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <common.h>

#include <stdio.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

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

char *GLOBAL_filename = NULL;
struct tri_region_s *GLOBAL_trireg = NULL;

int havecache(char *filename) {
	struct stat sb;
	return stat(filename,&sb) == 0 && sb.st_mode&S_IFREG && sb.st_size>0;
}

int readint(int fd, int *i) {
	int len;
	len = read(fd,i,sizeof(int));
	*i = ntohl(*i);
	return len;
}

int readtri(int fd, tri_t *t) {
	int i,j;
	union {
		fastf_t f;
		int i;
	} v;
	for(i=0;i<3;++i) for(j=0;j<3;++j) {
		if(readint(fd, &v.i) == 0) 
			return 0;
		(*t)[i][j] = v.f;
	}
	return 1;
}

struct tri_region_s *readregion(int fd) {
	int len, i;
	struct tri_region_s *r;

	if(readint(fd,&len) != sizeof(int)) return NULL;
	r = (struct tri_region_s *)malloc(sizeof(struct tri_region_s));
	r->name = (char *)malloc(len+1);
	if(read(fd,r->name,len) != len) return NULL;
	r->name[len] = 0;	/* verify zero termination */
	if(readint(fd,&r->ntri) != sizeof(int)) return NULL;
	r->t = (tri_t *)malloc(sizeof(tri_t) * len);
	for(i=0;i<r->ntri;i++) readtri(fd,&r->t[i]);
	return r;
}

struct tri_region_s *loadcache(char *filename) {
	struct tri_region_s *r;
	int fd;

	GLOBAL_trireg = r = NULL;
	fd = open(filename,O_RDONLY);
	if(fd<0) { perror(filename); return NULL; }
	while( ( r = readregion(fd) ) != NULL) {
		r->next = GLOBAL_trireg;
		GLOBAL_trireg = r;
	}
	return GLOBAL_trireg;
}

int writeint(int fd, int *val){ int v = htonl(*val); return write(fd,&v,sizeof(int)); }
int writetri(int fd, tri_t t) { int r=0,i,j; for(i=0;i<3;++i) for(j=0;j<3;++j) r+=writeint(fd,(int *)&t[i][j]); return r; }
int savecache(char *filename, struct tri_region_s *regs)
{
	int fd, i, len;
	if(regs == NULL) return -1;
	fd = open(filename,O_WRONLY|O_CREAT,0644);
	if(fd<0) { perror(filename); return -1; }
	while(regs) {
		len = strlen(regs->name);
		writeint(fd,&len);
		write(fd,regs->name,len);
		writeint(fd,&regs->ntri);
		for(i=0;i<regs->ntri;i++) writetri(fd,regs->t[i]);
		regs = regs->next;
	}
	close(fd);
	return 0;
}

static union tree *
region_end(struct db_tree_state *tsp, struct db_full_path *pathp, union tree *curtree, genptr_t client_data)
{
	union tree *ret_tree;
        struct shell *s;
	char *path;
	struct tri_region_s *reg;

	if(curtree->tr_op == OP_NOP) return curtree;

	if( BU_SETJUMP )  {
		/* Error, bail out */
		char *sofar;
		BU_UNSETJUMP;		/* Relinquish the protection */

		sofar = db_path_to_string(pathp);
		bu_log( "FAILED in Boolean evaluation: %s\n", sofar );
		bu_free( (char *)sofar, "sofar" );

		/* Release any intersector 2d tables */
		nmg_isect2d_final_cleanup();

		/* Release the tree memory & input regions */
/*XXX*/			/* db_free_tree(curtree);*/		/* Does an nmg_kr() */

		/* Get rid of (m)any other intermediate structures */
		if( (*tsp->ts_m)->magic == NMG_MODEL_MAGIC )  {
			nmg_km(*tsp->ts_m);
		} else {
			bu_log("WARNING: tsp->ts_m pointer corrupted, ignoring it.\n");
		}

		/* Now, make a new, clean model structure for next pass. */
		*tsp->ts_m = nmg_mm();
		db_free_tree(curtree, &rt_uniresource);		/* Does an nmg_kr() */

		BU_GETUNION(curtree, tree);
		curtree->magic = RT_TREE_MAGIC;
		curtree->tr_op = OP_NOP;
		return curtree;
	}

	ret_tree = nmg_booltree_evaluate(curtree, tsp->ts_tol, &rt_uniresource); 
	BU_UNSETJUMP;		/* Relinquish the protection */

	if(ret_tree == NULL || ret_tree->tr_d.td_r == NULL) { 
		printf("Empty region\n"); 
		return NULL; 
	}

	/* some sanity checking... */
	NMG_CK_REGION(ret_tree->tr_d.td_r);
	NMG_CK_MODEL(ret_tree->tr_d.td_r->m_p);
	BN_CK_TOL(tsp->ts_tol);


	if( BU_SETJUMP )
	{
		BU_UNSETJUMP;

		nmg_isect2d_final_cleanup();

		if( (*tsp->ts_m)->magic == NMG_MODEL_MAGIC )
			nmg_km(*tsp->ts_m);
		else
			bu_log("WARNING: tsp->ts_m pointer corrupted, ignoring it.\n");

		*tsp->ts_m = nmg_mm();
		db_free_tree(curtree, &rt_uniresource);		/* Does an nmg_kr() */

		BU_GETUNION(curtree, tree);
		curtree->magic = RT_TREE_MAGIC;
		curtree->tr_op = OP_NOP;
		return curtree;
	} 
	nmg_triangulate_model(ret_tree->tr_d.td_r->m_p, tsp->ts_tol);
	BU_UNSETJUMP;

	/* extract the path name... we must free this memory! */
	reg = (struct tri_region_s *)malloc(sizeof(struct tri_region_s));
	reg->name = db_path_to_string(pathp);
	/* (set! gtr (cons reg gtr)) */
	reg->next = GLOBAL_trireg;
	reg->t = NULL;
	reg->ntri = 0;
	GLOBAL_trireg = reg;

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

                        /* 
			 * Grab the face normal if needed
			 * this might be used for orienting triangles?
			 */
                        NMG_GET_FU_NORMAL(facet_normal, fu);
			/*
			printf("\tN: %.2f %.2f %.2f\t", V3ARGS(facet_normal));
			*/

			/* for each triangle */
                        for(BU_LIST_FOR(lu, loopuse, &fu->lu_hd))
                        {
                                 struct edgeuse *eu;
				point_t t[3];
				int i = 0;

                                NMG_CK_LOOPUSE(lu);
                                if(BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
                                        continue;

				reg->ntri++;
				reg->t = (tri_t *)bu_realloc((void *)reg->t, sizeof(tri_t)*(reg->ntri+1));

				/* for each vertex */
                                for(BU_LIST_FOR(eu, edgeuse, &lu->down_hd))
                                {
					if(i>=3)
						printf("Neat, a triangle with more than 3 edges\n");
                                        NMG_CK_EDGEUSE(eu);
                                        NMG_CK_VERTEX(eu->vu_p->v_p);
					VMOVE(reg->t[reg->ntri][i] , eu->vu_p->v_p->vg_p->coord);
					++i;
                                }
                        }
                }
        }

	return NULL;
}

struct tri_region_s *
tri_load(const char *filename, int numreg, const char **regs)
{
	char descr[BUFSIZ];
	struct db_i *dbip;
	struct db_tree_state tree_state;

	struct rt_tess_tol       ttol;
	struct bn_tol            tol;
	struct model		*model;

	char cachename[BUFSIZ];

	/* if it's already in mem, just load it */
	if(GLOBAL_filename && !strncmp(GLOBAL_filename,filename,BUFSIZ))
		return GLOBAL_trireg;

	/* try to quickly load a binary cache file */
	snprintf(cachename,BUFSIZ,"%s.tricache",filename);
	if(havecache(cachename))
		return loadcache(cachename);

	/* oh crap. */
	GLOBAL_filename = strdup(filename);
	nmg_bool_eval_silent = 1;

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

	/* load the .g file */

        if ((dbip = db_open(filename, "r")) == DBI_NULL) {
                perror(filename);
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
			NULL);		/* client data */
	savecache(cachename, GLOBAL_trireg);
	return GLOBAL_trireg;
}
