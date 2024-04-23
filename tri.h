/*                           T R I . H
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
/** @file tri.h
 *
 * Brief description
 *
 */

#ifndef TRI_H
#define TRI_H

struct tri_region_s {
    int magic;
    struct tri_region_s *next;	/* singly linked list */
    char *name;
    int ntri;
    fastf_t *t;
};

/*
 * loads a linked list of regions with triangles. Triangles are CCW. Will use a
 * cache if available.
 */
struct tri_region_s *tri_load(const char *filename, int numreg, const char **regs);
int close_tris(struct tri_region_s *);

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
