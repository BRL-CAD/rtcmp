/*
 * $Id$
 */

#ifndef _DRY_H
#define _DRY_H

#include "rtcmp.h"

struct part    *dry_shoot(void *geom, struct xray * ray);
double          dry_getsize(void *g);
int             dry_getbox(void *g, point_t * min, point_t * max);
void           *dry_constructor();
int             dry_destructor(void *);

#endif
