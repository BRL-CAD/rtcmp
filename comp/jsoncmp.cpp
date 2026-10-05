#include <fstream>
#include <brlcad/bu/env.h>
#include <iostream>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "rtcmp.h"
#include "shot_comp.h"
#include "jsonwriter.hpp"
#include "grazing.h"
#include "util/util.h"

int do_comp(const char *file1, const char *file2, const CompareConfig& config) {
    if (config.grazing_geometry.empty() != config.grazing_object.empty()) {
        std::cerr << "Both --grazing-geometry and --grazing-object are required" << std::endl;
        return -1;
    }

    if (config.grazing_geometry.empty() &&
        (config.grazing_radius != 0.0 || config.grazing_samples != 8)) {
        std::cerr << "--grazing-radius and --grazing-samples require grazing geometry" << std::endl;
        return -1;
    }

    std::unique_ptr<GrazingDetector> detector;
    if (!config.grazing_geometry.empty()) {
        if (!std::isfinite(config.grazing_radius) || config.grazing_radius < 0.0 ||
            config.grazing_samples < 3 || config.grazing_samples > 1024) {
            std::cerr << "Grazing radius must be nonnegative and samples must be between 3 and 1024" << std::endl;
            return -1;
        }
        detector = std::make_unique<GrazingDetector>(config.grazing_geometry,
            config.grazing_object, config.grazing_radius, config.grazing_samples);
        if (!detector->valid()) {
            std::cerr << detector->error() << std::endl;
            return -1;
        }
    }

    // build indexes for both shot files
    ShotIndex s1(file1);
    if (!s1.isValid()) {
        std::cerr << "Failed to index " << file1 << std::endl;
        return -1;
    }

    ShotIndex s2(file2);
    if (!s2.isValid()) {
        std::cerr << "Failed to index " << file2 << std::endl;
        return -1;
    }

    if (s1.hasSegments() != s2.hasSegments()) {
        std::cerr << "Primitive data is present in only one input file" << std::endl;
        return -1;
    }
    ComparisonResult results(s1, s2, config.tol, config.report_missing_rays);
    if (detector) {
        results.filterGrazing(*detector);
        std::cout << "Grazing probe radius: " << detector->radius() << " mm ("
                  << detector->sampleCount() << " nearby rays)\n";
    }
    results.summary(config.nirt_file);
    return 0;
}

/*
 *  Helper Function to load or create array of rays to fire
 */
static std::vector<xray>
load_or_build_rays(int rays_per_view,
		   const std::string& in_ray_file, const std::string& out_ray_file,
		   const point_t center, double radius)
{
    std::vector<xray> rays;
    std::string error;

    if (!in_ray_file.empty()) {
	if (!rtutil::read_rays(in_ray_file, rays, error)) {
	    std::cerr << error << '\n';
	    return {};
	}
    } else {
	rays = rtutil::build_view_rays(radius, center, rays_per_view);
	if (rays.empty()) {
	    std::cerr << "Unable to create the requested ray pool\n";
	    return {};
	}

	if (!rtutil::write_rays(out_ray_file, rays, error)) {
	    std::cerr << error << '\n';
	    return {};
	}
    }

    return rays;
}

/*
 * TODO:
 *	* Shoot on a grid set instead of a single ray.
 */
int
do_diff_run(const char *prefix, int argc, const char **argv, int nthreads, int rays_per_view,
	void *(*constructor) (const char *, int, const char **),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *),
	CompareConfig& dinfo)
{
    nthreads = (nthreads == 0) ? bu_avail_cpus() : nthreads;	// 0 implies maximize cpu

    /* base instance for this run */
    void* base_inst = constructor(*argv, argc-1, argv+1);
    if (base_inst == NULL) {
	return 1;
    }

    struct application* base_app = (struct application*)base_inst;

    // get these for ray generation
    double radius = getsize(base_inst);
    point_t bbox[3];
    getbox(base_inst, bbox, bbox +1);
    VADD2SCALE(bbox[2], bbox[0], bbox[1], 0.5);
    std::vector<xray> rays = load_or_build_rays(
	rays_per_view, dinfo.in_ray_file, dinfo.ray_file, bbox[2], radius
    );
    if (rays.empty()) {
	destructor(base_inst);
	return 1;
    }

    // prep file for writing; erase any existing contents with trunc
    std::ofstream f(dinfo.json_ofile, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::cerr << "Unable to open output file " << dinfo.json_ofile << "\n";
        destructor(base_inst);
        return 1;
    }
    f.close();
    if (dinfo.primitive_hits) {
	if (bu_setenv("LIBRT_RTCMP_FILE", dinfo.json_ofile.c_str(), 1) != 0 ||
	    bu_setenv("LIBRT_RTCMP_PRIMITIVES", "1", 1) != 0 ||
	    bu_setenv("LIBRT_RTCMP_SKIP_MISSES", dinfo.skip_misses ? "1" : "0", 1) != 0) {
	    std::cerr << "Unable to configure primitive capture\n";
            destructor(base_inst);
	    return 1;
	}
    }
    const unsigned int previous_debug = rt_debug;
    if (dinfo.primitive_hits) rt_debug |= RT_DEBUG_RTCMP;
#ifdef RT_RTCMP_CAPTURE_RUNTIME_TOGGLE
    if (dinfo.primitive_hits && rt_rtcmp_capture_set_enabled(1) != 0) {
        std::cerr << "Unable to enable primitive capture\n";
        rt_debug = previous_debug;
        destructor(base_inst);
        return 1;
    }
#endif

    /* multithreading? */
    // we need one application* and resrouces per thread
    base_app->a_resource = (struct resource *)bu_calloc(1, sizeof(struct resource), "resource");
    rt_init_resource(base_app->a_resource, 0, base_app->a_rt_i);
    std::vector<application> apps;
    apps.reserve(nthreads);
    apps.emplace_back(*base_app);
    for (int i = 1; i < nthreads; i++) {
	application thread_app = *base_app;
	thread_app.a_resource = (struct resource *)bu_calloc(1, sizeof(struct resource), "resource");
	rt_init_resource(thread_app.a_resource, i, thread_app.a_rt_i);
	apps.emplace_back(thread_app);
    }
    struct ThreadArgs {
	std::vector<application>& apps;
	std::vector<xray>& rays;
	int nthreads;
	bool skip_misses;
	bool primitive_hits;
	void (*shoot)(void*, struct xray*);
    } targs { apps, rays, nthreads, dinfo.skip_misses, dinfo.primitive_hits, shoot };

    auto worker = [](int cpu, void* data) {
	cpu--;	// cpu is 1-indexed
	
	// unpack data
	ThreadArgs* ta = (ThreadArgs*) data;
	if (!ta->primitive_hits)
	    tsj::Writer::instance().reserve(100 * 1024 * 1024);  // approx 100MB per thread
	if (!ta->primitive_hits) tsj::Writer::instance().setSkipMisses(ta->skip_misses);
	
	// split in contiguous blocks
	size_t per = ta->rays.size() / ta->nthreads;
	size_t base = cpu * per;
	// lazy - let our last thread handle extras from non-even divide
	size_t end = (cpu == ta->nthreads - 1)
		   ? ta->rays.size()
		   : base + per;

	// do the shooting for this block of rays
	for (size_t i = base; i < end; i++) {
	    if (ta->primitive_hits) {
		struct application *app = &ta->apps[cpu];
		VMOVE(app->a_ray.r_pt, ta->rays[i].r_pt);
		VMOVE(app->a_ray.r_dir, ta->rays[i].r_dir);
		rt_shootray(app);
	    } else {
		ta->shoot((void*)&ta->apps[cpu], &ta->rays[i]);
	    }
	}

	// make sure thread collection buffer is synced
	if (!ta->primitive_hits) tsj::Writer::instance().syncToGlobal();
    };

    /* do the work */
    if (nthreads < 2)
	worker(1, &targs);  // serial
    else
	bu_parallel(worker, nthreads, (void*)&targs);

    // write all collected data
    int capture_status = 0;
    if (dinfo.primitive_hits) {
#ifdef RT_RTCMP_CAPTURE_RUNTIME_TOGGLE
	rt_rtcmp_capture_set_enabled(0);
#endif
	capture_status = rt_rtcmp_capture_flush();
	rt_debug = previous_debug;
        if (capture_status) std::cerr << "Primitive capture failed\n";
    } else {
	tsj::Writer::Collector::flushToFile(dinfo.json_ofile);
    }
    
    /* cleanup */
    // rt_free_rti cleans registered resources, so their storage must outlive the destructor.
    destructor(base_inst);
    for (application &app : apps) {
	bu_free(app.a_resource, "resource");
	app.a_resource = NULL;
    }
    return capture_status ? 1 : 0;
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
