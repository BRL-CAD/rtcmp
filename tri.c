/*
 * $Id$
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <arpa/inet.h>

#ifdef HAVE_STRING_H
#  include <string.h>
#else
#  include <strings.h>
#endif

#include <brlcad/bu.h>
#include <brlcad/vmath.h>
#include <brlcad/raytrace.h>

#include "tri.h"

#define FLTPERTRI (3*3)
#define TRISIZE (sizeof(fastf_t)*FLTPERTRI)

static char *GLOBAL_filename = NULL;
static struct tri_region_s *GLOBAL_trireg = NULL;

static int 
havecache(char *filename) 
{
	struct stat sb;
	return stat(filename,&sb) == 0 && S_ISREG(sb.st_mode) && sb.st_size>0;
}

static struct tri_region_s *
readregion(int fd) 
{
	long len;
	struct tri_region_s *r;
	fastf_t *buf;

	if(read(fd,&len, sizeof(long)) != sizeof(long)) return NULL;
	len = ntohl(len);
	r = (struct tri_region_s *)malloc(sizeof(struct tri_region_s));
	r->name = (char *)malloc(len+1);
	if(read(fd,r->name,len) != len) return NULL;
	r->name[len] = 0;	/* verify zero termination */
	if(read(fd,&r->ntri, sizeof(long)) != sizeof(long)) return NULL;
	r->ntri = ntohl(r->ntri);
	r->t = (fastf_t *)malloc(TRISIZE * r->ntri);
	buf = (fastf_t *)malloc(TRISIZE * r->ntri);
	read(fd,buf,TRISIZE * r->ntri);
	bu_cv_ntohd((unsigned char *)r->t, (unsigned char *)buf, 9*r->ntri);
	free(buf);
	return r;
}

static struct tri_region_s *
loadcache(char *filename) 
{
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

static int 
savecache(char *filename, struct tri_region_s *regs)
{
	long fd, len;
	fastf_t *buf;
	if(regs == NULL) return -1;
	fd = open(filename,O_WRONLY|O_CREAT|O_TRUNC,0644);
	if(fd<0) { perror(filename); return -1; }
	while(regs) {
		len = htonl(strlen(regs->name));
		write(fd,&len,sizeof(long));
		write(fd,regs->name,ntohl(len));
		len = htonl(regs->ntri);
		write(fd,&len,sizeof(long));
		buf = (fastf_t *)malloc(TRISIZE * regs->ntri);
		bu_cv_htond((unsigned char *)buf,(unsigned char *)regs->t, FLTPERTRI * regs->ntri);
		write(fd,buf,TRISIZE*regs->ntri);
		free(buf);
		regs = regs->next;
	}
	close(fd);
	return 0;
}

static union tree *
region_end(struct db_tree_state *tsp, const struct db_full_path *pathp, union tree *curtree, genptr_t client_data)
{
	union tree *ret_tree;
        struct shell *s;
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

		BU_GET(curtree, union tree);
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

		BU_GET(curtree, union tree);
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
				int i = 0;

                                NMG_CK_LOOPUSE(lu);
                                if(BU_LIST_FIRST_MAGIC(&lu->down_hd) != NMG_EDGEUSE_MAGIC)
                                        continue;

				/* spinning a realloc gets really ugly on most
				 * malloc's (doug lea's with full mmu is the
				 * only place it's "ok" afaik */
				reg->ntri++;
				reg->t = (fastf_t *)bu_realloc((void *)reg->t, TRISIZE*reg->ntri, "region triangle area");

				/* for each vertex */
                                for(BU_LIST_FOR(eu, edgeuse, &lu->down_hd))
                                {
					fastf_t *vert;
					if(i>=3)
						printf("Neat, a triangle with more than 3 edges\n");
                                        NMG_CK_EDGEUSE(eu);
                                        NMG_CK_VERTEX(eu->vu_p->v_p);
					vert = reg->t + 3*(3*(reg->ntri-1) + i);
					VMOVE(vert, eu->vu_p->v_p->vg_p->coord);
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
	if(havecache(cachename)) {
		printf("Found triangle cache, using it\n");
		return loadcache(cachename);
	}

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
			nmg_booltree_leaf_tess,	/* leaf function */
			NULL);		/* client data */
	savecache(cachename, GLOBAL_trireg);
	return GLOBAL_trireg;
}
