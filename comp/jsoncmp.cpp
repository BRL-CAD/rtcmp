#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "rtcmp.h"
#include "shot_comp.h"
#include "jsonwriter.hpp"
#include "util/util.h"

void do_comp(const char *file1, const char *file2, const CompareConfig& config) {
    // build indexes for both shot files
    ShotIndex s1(file1);
    if (!s1.isValid()) {
        std::cerr << "Failed to index " << file1 << std::endl;
        return; // or handle error appropriately?
    }

    ShotIndex s2(file2);
    if (!s2.isValid()) {
        std::cerr << "Failed to index " << file2 << std::endl;
        return;
    }

    ComparisonResult results(s1, s2, config.tol);
    results.summary(config.nirt_file);
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
void
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
	return;
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
	return;
    }

    base_app->a_resource = (struct resource *)bu_calloc(1, sizeof(struct resource), "resource");
    rt_init_resource(base_app->a_resource, 0, base_app->a_rt_i);

    // prep file for writing; erase any existing contents with trunc
    std::ofstream f(dinfo.json_ofile, std::ios::binary | std::ios::trunc);

    /* multithreading? */
    // we need one application* and resrouces per thread
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
	void (*shoot)(void*, struct xray*);
    } targs { apps, rays, nthreads, shoot };

    auto worker = [](int cpu, void* data) {
	cpu--;	// cpu is 1-indexed
	
	// reserve buffer for this thread
	tsj::Writer::instance().reserve(100 * 1024 * 1024);  // approx 100MB per thread

	// unpack data
	ThreadArgs* ta = (ThreadArgs*) data;
	
	// split in contiguous blocks
	size_t per = ta->rays.size() / ta->nthreads;
	size_t base = cpu * per;
	// lazy - let our last thread handle extras from non-even divide
	size_t end = (cpu == ta->nthreads - 1)
		   ? ta->rays.size()
		   : base + per;

	// do the shooting for this block of rays
	for (size_t i = base; i < end; i++) {
	    ta->shoot((void*)&ta->apps[cpu], &ta->rays[i]);
	}

	// make sure thread collection buffer is synced
	tsj::Writer::instance().syncToGlobal();
    };

    /* do the work */
    if (nthreads < 2)
	worker(1, &targs);  // serial
    else
	bu_parallel(worker, nthreads, (void*)&targs);

    // write all collected data
    tsj::Writer::Collector::flushToFile(dinfo.json_ofile);
    
    /* cleanup */
    // rt_free_rti cleans registered resources, so their storage must outlive the destructor.
    destructor(base_inst);
    for (application &app : apps) {
	bu_free(app.a_resource, "resource");
	app.a_resource = NULL;
    }
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
