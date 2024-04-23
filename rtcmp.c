/*                         R T C M P . C
 * RtCmp
 *
 * Copyright (c) 2007-2024 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @file rtcmp.c
 *
 * Brief description
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Meh. */
#ifdef __linux__
# include <getopt.h>
#endif

#include "perfcomp.h"

#ifdef HAVE_TIE
# include "adrt/adrt.h"
#endif

#ifdef HAVE_RAYFORCE
# include "rayforce/rayforce.h"
#endif

#include "dry/dry.h"
#include "rt/rt.h"

#undef PARALLEL		/* brlcad defines this, but I want my own */

/* mode bitfield ... hurrrrr, I'm special. :D */
#define PARALLEL	0x01
#define DISTRIBUTED	0x02
#define ADRT		0x04
#define BRLCAD		0x08
#define RAYFORCE	0x10
#define DRY		0x20	/* oh the horror */

void
doversion(char *name)
{
	printf("rtcmp (1.0) (C) 2007 US Army Research Lab - Erik Greenwald <erikg@arl.army.mil>\n");
	return;
}
void
dohelp(char *name)
{
	doversion(name);
	printf("Usage:\n\
\t%s [options] <geom.g> <component 1> [component 2] ...\n\
\n\
\t-s		Serial mode (not threaded)\n\
\t-p<threads>	Parallel mode (threaded)\n\
\t-d<procs>	Distributed mode (not implemented)\n\
\n\
\t-a		Use ADRT\n\
\t-b		Use BRL-CAD librt\n\
\t-r		Use Ray-Force\n\
\n\
\t-h		Help\n\
\t-v		Version\n\
\n", name);
}

int 
main(int argc, char **argv)
{
	int c, mode = DRY, nthreads = 0, nproc = 0;
	char *pname = *argv;
	struct retpack_s *dry_retpack = NULL, *rt_retpack = NULL, *adrt_retpack = NULL, *rayforce_retpack = NULL;

	while( (c = getopt( argc, argv, "abd:hp:rsv")) != -1 ){ 
		switch(c) 
		{
			/* serial/parallel share a bit, distributed is seperate.
			 * This lets us do things like declare serial but
			 * distributed (if that makes sense... serial/parallel
			 * talks about threads, not seperate processes) 
			 */ 
			case 's': mode &= ~PARALLEL; break;	/* serial (not parallel) */
			case 'p': mode |= PARALLEL; if(optarg)nthreads=atoi(optarg); break;	/* parallel */
			case 'd': mode |= DISTRIBUTED; if(optarg)nproc=atoi(optarg); break;
			case 'a': mode |= ADRT; break;
			case 'b': mode |= BRLCAD; break;
			case 'r': mode |= RAYFORCE; break;
			case 'h': dohelp(pname); return EXIT_SUCCESS;
			case 'v': doversion(pname); return EXIT_SUCCESS;
			case '?':
			default: dohelp(*argv); return EXIT_FAILURE;
		}
	}
	argc -= optind;
	argv += optind;

	if(argc <= 0) return dohelp(pname), EXIT_FAILURE;

	if(mode&PARALLEL && nthreads == 0) {
		nthreads = bu_avail_cpus() * 2 - 1;
		if(nthreads <= 1) nthreads = 2;
	}

	if(mode&DISTRIBUTED) { printf("Uh, no distributed yet\n"); return EXIT_FAILURE; }

	if(!(mode&(BRLCAD|ADRT|RAYFORCE))) {
		printf("Must select at least one raytracing engine to use\n");
		dohelp(pname);
		return EXIT_FAILURE;
	}

#define TRY(flag,prefix) if(mode&flag) prefix##_retpack = perfcomp(#prefix, argc, argv, nthreads, nproc, prefix##_constructor, prefix##_getbox, prefix##_getsize, prefix##_shoot, prefix##_destructor)

	TRY(DRY,dry);	/* prime it */
	TRY(BRLCAD,rt);	/* librt is not optional. */

#ifdef HAVE_TIE
	TRY(ADRT,adrt);
#else
	if(mode&ADRT) printf("ADRT support not compiled in\n");
#endif

#ifdef HAVE_RAYFORCE
	TRY(RAYFORCE,rayforce);
#else
	if(mode&RAYFORCE) printf("RAYFORCE support not compiled in\n");
#endif
#undef TRY

	if((mode & ADRT) && (mode & BRLCAD))
		for(c=0;c<NUMVIEWS;++c) { 
			double rms;
			printf("Shot %d ", c+1);
			if( !rt_retpack && !adrt_retpack ) {
				printf("%s retpack missing!\n", !rt_retpack?"rt_retpack":"adrt_retpack");
				exit(-1);
			}
			rms = cmppartl(rt_retpack->p[c], adrt_retpack->p[c]);
			if(rms < 0.0) {
				printf("- region list differs!!!\n");
				printf("LRT:  "); showpart(rt_retpack->p[c]); printf("\n");
				printf("ADRT: "); showpart(adrt_retpack->p[c]); printf("\n");
			} else
				printf("deviation[%d]: %f mm RMS\n", c, rms);
		}
	
#define SHOW(prefix) if(prefix##_retpack) printf(#prefix"\t: %f seconds (%f cpu) %f wrps  %f crps\n", prefix##_retpack->t, prefix##_retpack->c, (double)NUMTRAYS/prefix##_retpack->t, (double)NUMTRAYS/prefix##_retpack->c)
	SHOW(dry);
	SHOW(rt);
	SHOW(adrt);
	SHOW(rayforce);
#undef SHOW

	printf("\n");
#define SPEEDUP(a,b) if(a##_retpack && b##_retpack) printf(#b" shows %.3f times speedup over "#a"\n", (a##_retpack->c-dry_retpack->c) / (b##_retpack->c-dry_retpack->c) - 1);
	SPEEDUP(rt,adrt);
	SPEEDUP(rt,rayforce);
	SPEEDUP(adrt,rayforce);
#undef SPEEDUP

	return EXIT_SUCCESS;
}

/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
