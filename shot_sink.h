/*                      S H O T _ S I N K . H
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
/** @file shot_sink.h
 *
 * Abstract output sink for shotline results.  The raytrace engines
 * (rt/tie) are output-agnostic: they hold a ShotSink* in the librt
 * application's a_uptr and forward shot/partition data to it.
 *
 * PERF runs leave a_uptr == NULL so the hot loop performs no output
 * work.  DIFF runs wire a concrete sink (WriterSink) into each
 * per-thread application, producing the shared NDJSON format.
 */

#ifndef _SHOT_SINK_H
#define _SHOT_SINK_H

#include <brlcad/vmath.h>
#include <brlcad/raytrace.h>

/* Output sink interface.  Engines call these hooks when (and only when)
 * a sink is present; a NULL sink means "no output" (perf mode). */
struct ShotSink {
    virtual void beginShot(const struct xray &ray) = 0;
    virtual void addPartition(struct partition *pp) = 0;
    virtual void endShot() = 0;
    virtual ~ShotSink() = default;
};

#endif

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
