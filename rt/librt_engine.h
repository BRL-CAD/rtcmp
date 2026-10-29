/*                   L I B R T _ E N G I N E . H
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
/** @file librt_engine.h
 *
 * Shared "standard" librt geometry/ray intersection engine core.
 *
 * Both the plain librt engine (rt) and the TIE-enabled engine (tie)
 * are thin variants over this single core.  They differ ONLY by whether
 * LIBRT_BOT_MINTIE is toggled during prep, which is controlled by the
 * mintie flag of librt_engine_construct().
 *
 * Output is fully decoupled from the engine: the engine reads an
 * optional ShotSink* from the application's a_uptr.  A NULL sink
 * (perf mode) makes the hot loop do no output work.
 */

#ifndef _LIBRT_ENGINE_H
#define _LIBRT_ENGINE_H

#include "rtcmp.h"

/* Shared engine interface (identical for perf and diff). */
void            librt_engine_shoot(void *geom, struct xray *ray);
double          librt_engine_getsize(void *g);
int             librt_engine_getbox(void *g, point_t *min, point_t *max);
int             librt_engine_destructor(void *g);

/* Construct a librt engine instance.  When mintie is nonzero, the
 * LIBRT_BOT_MINTIE env var is saved, set to "1" around prep, then
 * restored - which turns the standard librt shotline logic into a
 * TIE-enabled run. */
void           *librt_engine_construct(const char *file, int numreg,
				       const char **regs, int mintie);

#endif

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
