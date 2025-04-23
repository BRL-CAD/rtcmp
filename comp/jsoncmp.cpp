#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <set>
#include <time.h>

#include "rtcmp.h"
#include "shotset.h"
#include "jsonwriter.hpp"

bool do_comp(const char *file1, const char *file2, const CompareConfig& config) {
    // Clear any old output files, to avoid any confusion about what results
    // are associated with what run.
    bu_file_delete(config.nirt_file.c_str());
    bu_file_delete(config.plot3_file.c_str());

    std::cerr << "Using diff tolerance: " << config.tol << "\n";

    ShotSet s1(file1, config);
    ShotSet s2(file2, config);

    if (!s1.is_valid() || !s2.is_valid()) {
	std::cerr << "Invalid shot files" << std::endl;
	return true;
    }

    return s1 == s2;
}

/*
 *  Helper Function to load or create array of rays to fire
 */
struct xray* create_ray_array(int* total_rays, int rays_per_view, 
			      std::string in_ray_file, std::string out_ray_file,
			      point_t* bbox, double radius) {
    struct xray* rays = (struct xray *)bu_malloc(sizeof(struct xray) * *total_rays, "allocating ray space");

    if (!in_ray_file.empty()) {
	// open file
	std::ifstream rayfile(in_ray_file, std::ios::binary);
	if (!rayfile.is_open()) {
	    std::cerr << "failed to open ray_file: " << in_ray_file << std::endl;
	    return NULL;
	}

	// parse, load into array
	int ray_idx = 0;
	std::string line;
	bool in_section = false;
	while (std::getline(rayfile, line)) {
	    // trim leading/trailing whitespace
	    line.erase(0, line.find_first_not_of(" \t"));
	    line.erase(line.find_last_not_of(" \t") + 1);

	    if (in_section) {
		std::istringstream iss(line);
		std::string prefix;
		double x, y, z;
		// assumes line is point: xyz 123.4 234.5 345.6
		//	    or direction: dir 123.4 234.5 345.6
		iss >> prefix >> x >> y >> z;

		if (prefix == "xyz") {
		    rays[ray_idx].r_pt[0] = x;
		    rays[ray_idx].r_pt[1] = y;
		    rays[ray_idx].r_pt[2] = z;
		} else {
		    // assume this is 'dir' line
		    rays[ray_idx].r_dir[0] = x;
		    rays[ray_idx].r_dir[1] = y;
		    rays[ray_idx].r_dir[2] = z;

		    // got dir; next ray's up
		    ray_idx++;
		}
	    } else if (line.rfind("**", 0) == 0) {  // check for section start
		if (in_section)
		    break;

		// extract amount of rays from section header
		size_t startIdx = line.find('[');
		size_t endIdx = line.find(']');
		if (startIdx != std::string::npos && endIdx != std::string::npos) {
		    // assume number in bracket is number of rays we need to read
		    int num_rays = stoi(line.substr(startIdx + 1, endIdx - startIdx - 1));

		    // ray file has more rays than we were expecting
		    if (num_rays > *total_rays) {
			rays = (struct xray*)bu_realloc(rays, sizeof(struct xray) * num_rays, "realloc ray space");
			*total_rays = num_rays;
		    }
		}
		in_section = true;
	    }
	}
    } else {
	// TODO: better and/or random dirs?
	static vect_t dir[NUMVIEWS] = {
	    {0,0,1}, {0,1,0}, {1,0,0},			/* axis */
	    {1,1,1}, {1,4,-1}, {-1,-2,4}		/* non-axis */
	};
	for(int i = 0; i < NUMVIEWS; ++i) VUNITIZE(dir[i]);	/* normalize the dirs */

	/* build the views with pre-defined rays, yo */
	int rays_per_ring = 100;
	for(int j=0; j < NUMVIEWS; ++j) {
	    vect_t avec, bvec;

	    // add accuracy ray in before bundle
	    int acc_idx = j * (rays_per_view +1);
	    VMOVE(rays[acc_idx].r_dir,dir[j]);
	    VJOIN1(rays[acc_idx].r_pt, bbox[2], -radius, dir[j]);

	    /* set up an othographic grid */
	    bn_vec_ortho( avec, rays[acc_idx].r_dir );
	    VCROSS( bvec, rays[acc_idx].r_dir, avec );
	    VUNITIZE( bvec );
	    rt_raybundle_maker(rays + acc_idx, radius, avec, bvec, rays_per_ring, rays_per_view/rays_per_ring);
	}

	// write all the ray info
	// TODO: do this in dry-run?
	// TODO: write in chunks
	FILE* ray_file = fopen(out_ray_file.c_str(), "w");	// intentionally erase contents if file already exists
	fprintf(ray_file, "**rays fired for %s [%d]**\n", out_ray_file.c_str(), *total_rays);
	for (int i = 0; i < *total_rays; i++) {
	    fprintf(ray_file, "xyz %0.17f %0.17f %0.17f\ndir %0.17f %0.17f %0.17f\n", V3ARGS(rays[i].r_pt), V3ARGS(rays[i].r_dir));
	}
	fclose(ray_file);
    }

    return rays;
}

/*
 * TODO:
 *	* Shoot on a grid set instead of a single ray.
 */
void
do_diff_run(const char *prefix, int argc, const char **argv, int nthreads, int rays_per_view,
	void *(*constructor) (const char *, int, const char **, std::string),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *),
	CompareConfig& dinfo)
{
    int total_rays = NUMVIEWS * (rays_per_view + 1);		// rays per view + 1 accuracy ray per view
    nthreads = (nthreads == 0) ? bu_avail_cpus() : nthreads;	// 0 implies maximize cpu

    /* base instance for this run */
    void* base_inst = constructor(*argv, argc-1, argv+1, dinfo.json_ofile);
    if (base_inst == NULL) {
	return;
    }
    // common prep rti for all threads
    char desc[BUFSIZ];
    int reg_to_prep = argc-1;
    const char** regs = argv+1;
    struct rt_i* rtip = rt_dirbuild(*argv, desc, 0);
    if (!rtip) {
	destructor(base_inst);
	return;
    }
    while (reg_to_prep--)
	rt_gettree(rtip, *regs++);	/* load up the named regions */
    rt_prep_parallel(rtip, nthreads);	/* and compile to in-mem
					 * versions */

    struct application* base_app = (struct application*)base_inst;
    base_app->a_rt_i = rtip;
    // get these for ray generation
    double radius = getsize(base_inst);
    point_t bbox[3];
    getbox(base_inst, bbox, bbox +1);
    VADD2SCALE(bbox[2], bbox[0], bbox[1], 0.5);
    // allocs expeceted rays; MUST FREE */
    struct xray* rays = create_ray_array(&total_rays, rays_per_view, dinfo.in_ray_file, dinfo.ray_file, bbox, radius);

    // prep file for writing; erase any existing contents with trunc
    std::ofstream f(dinfo.json_ofile, std::ios::binary | std::ios::trunc);

    /* multithreading? */
    // TODO: upfront rt_init_resource() one per thread
    struct ThreadArgs {
	const application* base;
	const std::string* json_name;
	struct xray* rays;
	int total_rays;
	int nthreads;
	void *(*constructor) (const char *, int, const char **, std::string);
	void (*shoot)(void*, struct xray*);
	int (*destructor) (void *);
    } targs { base_app, &dinfo.json_ofile, rays, total_rays, nthreads, constructor, shoot, destructor };

    auto worker = [](int cpu, void* data) {
	cpu--;	// cpu is 1-indexed
	
	// reserve buffer for this thread
	tsj::Writer::instance().reserve(100 * 1024 * 1024);  // approx 100MB per thread

	// unpack data
	ThreadArgs* ta = (ThreadArgs*) data;
	struct rt_i* rtip = ((struct application*)ta->base)->a_rt_i;

	// NOTE: we dont need file / numreg, regs since we know we have a rti already
	struct application* thread_app = (struct application*)ta->constructor(NULL, 1, NULL, *ta->json_name);

	// attach common rti
	thread_app->a_rt_i = rtip;

	// create resource for this thread
	struct resource* resp;
	BU_GET(resp, struct resource);
	rt_init_resource(resp, cpu, rtip);
	thread_app->a_resource = resp;
	
	// split in contiguous blocks
	int per = ta->total_rays / ta->nthreads;
	int base = cpu * per;
	// lazy - let our last thread handle extras from non-even divide
	int end = (cpu == ta->nthreads - 1)
		   ? ta->total_rays
		   : base + per;

	// do the shooting for this block of rays
	for (int i = base; i < end; i++)
	    ta->shoot(thread_app, &ta->rays[i]);

	// cleanup
	rt_clean_resource(rtip, resp);
	ta->destructor(thread_app);
    };

    /* do the work */
    if (nthreads < 2)
	worker(1, &targs);  // serial
    else
	bu_parallel(worker, nthreads, (void*)&targs);

    // write all collected data
    tsj::Writer::Collector::flushToFile(dinfo.json_ofile);
    
    /* cleanup */
    rt_free_rti(rtip);
    destructor(base_inst);
    bu_free(rays, "ray buffer");
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

