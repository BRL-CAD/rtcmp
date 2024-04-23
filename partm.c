/*                         P A R T M . C
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
/** @file partm.c
 *
 * Brief description
 *
 */

/** @file partm.c
 * Basic memory manager, to avoid malloc() thrashing. Most of a functional
 * garbage collector at this point, with slab style OS interface. If someone
 * implemented a collect function, it'd be real, yo. Otherwise, it's just a
 * simply grow-only memory pool. With a thermonuclear destruct function.
 */

#include <stdio.h>
#include <stdlib.h>

#include "rtcmp.h"

#define DEFAULT_ALLOC_SIZE	BUFSIZ

/*** local stuff (should all be static, but alloc_part() is permitted) ***/

/* slab info */
static int nslabs = 0;
static void **slabs = NULL;

/* obviously, the free list */
static struct part *free_list = NULL;
static int nfree = 0;

/* 
 * slab allocator. Automatically called when the free list is empty, but can be
 * called any old time.
 */
static int alloc_part(int count) 
{
	struct part *n, *t;
	static int lastcount = -1;

	if(count == -1) count = lastcount==-1?DEFAULT_ALLOC_SIZE:lastcount;
	else lastcount = count;

	nfree += count;

	/* "pages" */
	nslabs ++;
	slabs = realloc(slabs, sizeof(void *) * nslabs);	

	/* initialize cells in the page */
	t = n = (void *)malloc(sizeof(struct part)*count);
	while(--count) {
		t->next = t+1;
		t = t->next;
	}

	/* attach it to the existing free list */
	t->next = free_list;
	free_list = n;
	return 0;
}



/*** exported functions ***/

/* 'allocate' a part */
struct part *get_part()
{
	struct part *n;
	if(free_list == NULL) 
		alloc_part(-1);
	n = free_list;
	free_list = free_list->next;
	n->next = NULL;
	return n;
}
		
/* 'free' part, puts back in memory pool */
int free_part (struct part *p)
{
	p->next = free_list;
	free_list = p;
	return 0;
}
		
/* recursive free (follows list) */
int free_part_r (struct part *p)
{
	struct part *tmp;

	while(p) {
		tmp = p;
		p = p->next;
		free_part(tmp);
	}
	return 0;
}
	
/* clean up part memory manager */
int end_part()
{
	while(nslabs)
		free(slabs[--nslabs]);
	free(slabs);
	slabs = NULL;
	nslabs = 0;
	return 0;
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
