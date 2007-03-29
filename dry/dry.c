
/*
 * $Id$
 */

#include <string.h>

#include "rtcmp.h"

#include "dry/dry.h"

struct part    *
dry_shoot(void *g, struct xray * ray)
{
#define SET(x)  \
 p[x] = get_part(); \
 strncpy(p[x]->region,"/some/nifty/little/part.r",NAMELEN); \
 VSET(p[x]->in,0,0,0); \
 VSET(p[x]->out,0,0,0); \
 VSET(p[x]->innorm,0,0,0); \
 VSET(p[x]->outnorm,0,0,0); \
 p[x]->in_dist = 0; \
 p[x]->out_dist = 0;
	struct part *p[4];
	SET(0); 
	SET(1); 
	SET(2); 
	SET(3); 
	p[0]->next = p[1];
	p[1]->next = p[2];
	p[2]->next = p[3];
	p[3]->next = NULL;
	return p[0];
}

double
dry_getsize(void *g)
{
	return -1.0;	/* MUST be negative for perfcomp! */
}

int
dry_getbox(void *g, point_t * min, point_t * max)
{
	return 0;
}

void           *
dry_constructor(char *file, int numreg, char **regs)
{
	return (void *) 1;
}

int
dry_destructor(void *g)
{
	return 0;
}
