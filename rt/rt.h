/*
 * $Id$
 */

#ifndef _RT_H
#define _RT_H

#include "rtcmp.h"

struct part    *rt_shoot(void *geom, struct xray * ray);
double          rt_getsize(void *g);
int             rt_getbox(void *g, point_t * min, point_t * max);
void           *rt_constructor();
int             rt_destructor(void *);

#endif
