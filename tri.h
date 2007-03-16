#ifndef TRI_H
#define TRI_H

typedef point_t tri_t[3];

struct tri_region_s {
	int magic;
	struct tri_region_s *next;	/* singly linked list */
	char *name;
	int ntri;
	tri_t *t;
};

/* 
 * loads a linked list of regions with triangles. Triangles are CCW. Will use a
 * cache if available.
 */
struct tri_region_s *load_tris(char *filename, int nreg, char **regs);
int close_tris(struct tri_region_s *);

#endif
