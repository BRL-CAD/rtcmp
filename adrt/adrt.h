/*
 * $Id$
 */

#ifndef _ADRT_H
#define _ADRT_H

#include "rtcmp.h"		/* drags in brlcad vmath stuff, too */

struct part    *adrt_shoot(void *geom, struct xray * ray);
double          adrt_getsize(void *geom);
int             adrt_getbox(void *geom, point_t * min, point_t * max);
void           *adrt_constructor();
int             adrt_destructor(void *);

#endif
