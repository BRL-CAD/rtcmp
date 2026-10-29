/*                    W R I T E R _ S I N K . H P P
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
/** @file writer_sink.hpp
 *
 * Concrete ShotSink that forwards to the thread-local tsj::Writer.
 * This preserves the NDJSON output format, the thread-local buffering,
 * and the Collector::flushToFile machinery unchanged.
 */

#pragma once

#include "shot_sink.h"
#include "comp/jsonwriter.hpp"

/* ShotSink implementation backed by the thread-local NDJSON Writer.
 * A single instance may be shared by every per-thread application: each
 * call resolves tsj::Writer::instance() which is itself thread_local, so
 * the buffering stays correctly partitioned per thread. */
struct WriterSink : public ShotSink {
    void beginShot(const struct xray &ray) override {
        tsj::Writer::instance().beginShot(ray);
    }
    void addPartition(struct partition *pp) override {
        tsj::Writer::instance().addPartition(pp);
    }
    void endShot() override {
        tsj::Writer::instance().endShot();
    }
};
