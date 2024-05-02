/*                         R T C M P . H
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
/** @file rtcmp.h
 *
 * Brief description
 *
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

#include "json.hpp"

struct app_json {
    nlohmann::json *jshots;
    nlohmann::json *shotparts;
};


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

void parse_shots_file(const char *fname);

void do_perf_run(const char *prefix, int argc, char **argv, int ncpus,
	void*(*constructor)(char *, int, char**),
	int(*getbox)(void *, point_t *, point_t *),
	double(*getsize)(void*),
	void (*shoot)(void*, struct xray *),
	int(*destructor)(void *));

nlohmann::json *
do_accu_run(const char *prefix, int argc, char **argv, int ncpus,
	void*(*constructor)(char *, int, char**, nlohmann::json *),
	int(*getbox)(void *, point_t *, point_t *),
	double(*getsize)(void*),
	void (*shoot)(void*, struct xray *),
	int(*destructor)(void *));


void compare_shots(const char *r1, const char *r2);



#endif


/*
 * Local Variables:
 * tab-width: 8
 * mode: C
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
