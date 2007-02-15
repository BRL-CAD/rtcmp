/*
 * $Id$
 */

#include "rtcmp.h"		/* drags in brlcad vmath stuff, too */
#include "rayforce.h"

struct part    *
rayforce_shoot(void *geom, struct xray * ray)
{
	return NULL;
}

double 
rayforce_getsize(void *geom)
{
	return 0.0;
}

int 
rayforce_getbox(void *geom, point_t * min, point_t * max)
{
	return 0.0;
}

void           *
rayforce_constructor()
{
	return NULL;
}

int 
rayforce_destructor(void *i)
{
	return 0;
}
