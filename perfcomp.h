/*
 * $Id$
 */

#ifndef PERFCOMP_H
#define PERFCOMP_H

#include "rtcmp.h"

struct retpack_s *perfcomp(char *prefix, int argc, char **argv, int nthreads, int nproc, 
		void*(*constructor)(char *, int, char**),
		int(*getbox)(void *, point_t *, point_t *),
		double(*getsize)(void*),
		struct part *(*shoot)(void*, struct xray *),
		int(*destructor)(void *));

#endif
