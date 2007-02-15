
/*
 * $Id$
 */

#ifndef _RAYFORCE_H
#define _RAYFORCE_H

#include "rtcmp.h"		/* drags in brlcad vmath stuff, too */

struct part    *rayforce_shoot(void *geom, struct xray * ray);
double          rayforce_getsize(void *geom);
int             rayforce_getbox(void *geom, point_t * min, point_t * max);
void           *rayforce_constructor();
int             rayforce_destructor(void *);

#endif
