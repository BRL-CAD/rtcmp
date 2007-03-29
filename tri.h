#ifndef TRI_H
#define TRI_H

struct tri_region_s {
	int magic;
	struct tri_region_s *next;	/* singly linked list */
	char *name;
	int ntri;
	fastf_t *t;
};

/* 
 * loads a linked list of regions with triangles. Triangles are CCW. Will use a
 * cache if available.
 */
struct tri_region_s *tri_load(const char *filename, int numreg, const char **regs);
int close_tris(struct tri_region_s *);

#endif
