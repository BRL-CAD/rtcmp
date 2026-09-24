/*                      P E R F C O M P . C
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
/** @file perfcomp.c
 *
 * Brief description
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include <brlcad/bu.h>

#include "rtcmp.h"
#include "util/util.h"

typedef struct {
    double wall_sec;
    double cpu_sec;
    size_t rays_shot;
    size_t pool_size;

    /* derivative values */
    double rays_per_sec_wall;
    double rays_per_sec_cpu;
    double pool_reuse;
} perf_results_t;

typedef struct {
    const double runtime_seconds;	/* time to run for */
    const size_t target_num_rays;	/* num rays in rays arr */
    const size_t max_memory_bytes;	/* memory limit for rays arr */

    double radius;
    const fastf_t* center;
    void* inst;
    void (*shoot) (void *, struct xray * ray);
} perf_run_bundle_t;

perf_results_t do_perf(perf_run_bundle_t params) {
    std::vector<xray> rays = rtutil::build_view_rays_for_target(
	params.radius, params.center, params.target_num_rays, params.max_memory_bytes
    );
    if (rays.empty()) {
	std::cerr << "Unable to create the requested ray pool";
	if (params.max_memory_bytes > 0)
	    std::cerr << " within the " << params.max_memory_bytes << "-byte memory limit";
	std::cerr << '\n';
	return {};
    }

    int64_t wallclock_start, wallclock_end;
    clock_t cpu_start, cpu_end;
    size_t rays_shot = 0;
    size_t ray_index = 0;
    const int64_t max_usecs = (int64_t)(params.runtime_seconds * 1000000.0);
    const double TARGET_CHECK_SECONDS = .005;	    /* approximate time poll every n-seconds */
    size_t check_interval = (rays.size() / params.runtime_seconds) * TARGET_CHECK_SECONDS;
    // arbitrary clamp [1, 100000]
    if (check_interval < 1) check_interval = 1;
    if (check_interval > 100000) check_interval = 100000;

    /* performance run */
    wallclock_start = bu_gettime(); cpu_start = clock();
    do {
	for (size_t i = 0; i < check_interval; ++i) {
	    params.shoot(params.inst, &rays[ray_index]);
	    ++rays_shot;

	    ++ray_index;
	    if (ray_index == rays.size())
		// ran out of rays - loop back
		ray_index = 0;
	}
    } while (bu_gettime() - wallclock_start < max_usecs);
    cpu_end = clock(); wallclock_end = bu_gettime();
    /* end of performance run */

    double wall_sec = (wallclock_end - wallclock_start) / 1000000.0;
    double cpu_sec = (double)(cpu_end - cpu_start) / (double)CLOCKS_PER_SEC;
    double rays_per_sec_wall = rays_shot / wall_sec;
    double rays_per_sec_cpu = rays_shot / cpu_sec;
    double pool_reuse = (double)rays_shot / (double)rays.size();

    return {wall_sec, cpu_sec, rays_shot, rays.size(), rays_per_sec_wall, rays_per_sec_cpu, pool_reuse};
}

void
do_perf_run(const char *prefix, int argc, const char **argv, int nthreads, double perf_seconds, size_t max_ray_pool_bytes,
	void *(*constructor) (const char *, int, const char **),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *))
{
    void *inst;

    /* retained between runs. */
    static double radius = -1.0;	/* bounding sphere and flag */
    static point_t bb[3];	/* bounding box, third is center */

    /* make sure we have reasonable runtime */
    if (perf_seconds <= 0.0)
	perf_seconds = 1.0;

    inst = constructor(*argv, argc-1, argv+1);
    if (inst == NULL) {
	return;
    }

    /* first with a legit radius gets to define the bb and sph */
    /* XXX: should this lock? */
    if (radius < 0.0) {
	radius = getsize(inst);
	getbox(inst, bb, bb+1);
	VADD2SCALE(bb[2], *bb, bb[1], 0.5);	/* (bb[0]+bb[1])/2 */
    }
    /* XXX: if locking, we can unlock here */

    /*
     * Small seed pool used only to estimate rough throughput
     * use this very small run to estimate our number of rays needed for the real request
     *	ie if we get 100k rays/second and want to run for 20 seconds, scale our rays array accordingly
     */
    const double SEED_SECONDS = 0.25;
    const size_t SEED_TARGET_RAYS = 100000;
    perf_run_bundle_t seed_bundle = {SEED_SECONDS, SEED_TARGET_RAYS, max_ray_pool_bytes, radius, bb[2], inst, shoot};
    perf_results_t seed_res = do_perf(seed_bundle);
    if (seed_res.pool_size == 0) {
	// nothing in the pool, something went wrong
	destructor(inst);
	return;
    }

    double estimated_rays_per_sec = seed_res.rays_per_sec_wall * perf_seconds;

    /*
     * Build the real measured ray pool.  Size it according to estimated
     * throughput, requested runtime, and the requested memory cap.
     */
    perf_run_bundle_t main_bundle = {perf_seconds, (size_t)estimated_rays_per_sec, max_ray_pool_bytes, radius, bb[2], inst, shoot};
    // TODO: since we have this in a modular call, we could do n-runs and average our times
    perf_results_t main_res = do_perf(main_bundle);
    if (main_res.pool_size == 0) {
	// nothing in the pool, something went wrong
	destructor(inst);
	return;
    }

    /* clean up */
    destructor(inst);

    /* Report times */
    // TODO: better printing for parsing in scripts
    std::cout << std::fixed << std::setprecision(2) << "Rays/sec [wall] (" << prefix << "): " << main_res.rays_per_sec_wall << "\n";
    std::cout << std::fixed << std::setprecision(2) << "Wall clock time (" << prefix << "): " << main_res.wall_sec << "\n";
    std::cout << std::fixed << std::setprecision(2) << "CPU time        (" << prefix << "): " << main_res.cpu_sec << "\n";
    std::cout << std::fixed << std::setprecision(2) << "Rays shot       (" << prefix << "): " << main_res.rays_shot << "\n";
    std::cout << std::fixed << std::setprecision(2) << "Ray pool size   (" << prefix << "): " << main_res.pool_size << "\n";
    std::cout << std::fixed << std::setprecision(2) << "Pool reuse      (" << prefix << "): " << main_res.pool_reuse << "\n";
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

