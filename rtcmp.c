/*
 * raytrace comparision util.
 *
 * Erik Greenwald <erikg@arl.army.mil>
 *
 * $Id$
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
	printf("%s (%s) - %s (C) 2007 US Army Research Lab - Erik Greenwald <erikg@arl.army.mil>\n", PACKAGE, name, VERSION);
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

	TRY(DRY,dry);
	TRY(BRLCAD,rt);

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
	
#define SHOW(prefix) if(prefix##_retpack) printf(#prefix"\t: %f seconds (%f cpu) %f wrps  %f crps\n", prefix##_retpack->t, prefix##_retpack->c, (double)NUMRAYS/prefix##_retpack->t, (double)NUMRAYS/prefix##_retpack->c)
	SHOW(dry);
	SHOW(rt);
	SHOW(adrt);
	SHOW(rayforce);
#undef SHOW

	printf("\n");
#define SPEEDUP(a,b) if(a##_retpack && b##_retpack) printf(#b" shows %.3f times speedup over "#a"\n", a##_retpack->c / b##_retpack->c - 1);
	SPEEDUP(rt,adrt);
	SPEEDUP(rt,rayforce);
	SPEEDUP(adrt,rayforce);
#undef SPEEDUP

	return EXIT_SUCCESS;
}
