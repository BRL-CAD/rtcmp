
/*
 * $Id$
 */

#ifndef RTCMP_H
#define RTCMP_H

/* brlcad shtuff */
#ifndef VMATH_H
# include <stdio.h>
# include <math.h>
# include <brlcad/vmath.h>
# include <brlcad/bu.h>
# include <brlcad/bn.h>
# include <brlcad/raytrace.h>
#endif

#define NUMRAYS		((int)1e5)		/* this is PER VIEW */
#define NUMVIEWS	6			/* this refers to data in perfcomp.c */
#define NUMTRAYS	(NUMRAYS*NUMVIEWS)	/* total rays shot */

#define PANIC(x) printf(x),exit(-1)

/* no. mine. */
#ifdef NAMELEN
# undef NAMELEN
#endif

/* maximum size of a region/part name */
#define NAMELEN 128

/* local form of partition list */
struct part {
	struct part *next;	/* singly linked list */
	char region[NAMELEN];	/* human readable region name */
	double in_dist;
	point_t in;
	vect_t innorm;
	double out_dist;
	point_t out;
	vect_t outnorm;
	double depth;
	float obliquity;	/* unused */
	float curvature;	/* unused */
};

struct part *get_part();		/* 'allocate' a part */
int free_part (struct part *);		/* 'free' part, puts back in memory pool (loses 'tail') */
int free_part_r (struct part *);	/* recursive free */
int end_part();				/* clean up part memory manager */

void showpart(struct part *p);
double cmppartl(struct part *p1, struct part *p2);

struct retpack_s {
	double t;			/* wall time */
	double c;			/* cpu clock time */
	struct part *p[NUMVIEWS];	/* ordered set of accuracy partition lists */
};

#endif

