/*
 * $Id$
 */

#include <brlcad/common.h>

#include "rtcmp.h"		/* drags in brlcad vmath stuff, too */
#include "rayforce.h"

struct part    *
rayforce_shoot(void *UNUSED(geom), struct xray *UNUSED(ray))
{
	return NULL;
}

double 
rayforce_getsize(void *UNUSED(geom))
{
	return 0.0;
}

int 
rayforce_getbox(void *UNUSED(geom), point_t *UNUSED(min), point_t *UNUSED(max))
{
	return 0.0;
}

void           *
rayforce_constructor()
{
	return NULL;
}

int 
rayforce_destructor(void *UNUSED(i))
{
	return 0;
}
