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

bool shots_differ(const char *file1, const char *file2, const CompareConfig& config) {
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
    struct part **p;
    struct xray *ray;
    void *inst;

    /* retained between runs. */
    static double radius = -1.0;	/* bounding sphere and flag */
    static point_t bb[3];	/* bounding box, third is center */

    static vect_t dir[NUMVIEWS] = {
	{0,0,1}, {0,1,0}, {1,0,0},	/* axis */
	{1,1,1}, {1,4,-1}, {-1,-2,4}	/* non-axis */
    };
    for(int i=0;i<NUMVIEWS;i++) VUNITIZE(dir[i]); /* normalize the dirs */

    nlohmann::json jshots;

    ray = (struct xray *)bu_malloc(sizeof(struct xray)*(rays_per_view * NUMVIEWS+1), "allocating ray space");

    inst = constructor(*argv, argc-1, argv+1, dinfo.json_ofile);
    if (inst == NULL) {
	return;
    }

    if (!dinfo.in_ray_file.empty()) {
	// TODO: ray_file could have diff amount of rays than expected in the malloc a few lines up
	// open file
	std::ifstream rayfile(dinfo.in_ray_file, std::ios::binary);
	if (!rayfile.is_open()) {
	    std::cerr << "failed to open ray_file: " << dinfo.in_ray_file << std::endl;
	    return;
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
		iss >> prefix >> x >> y >> z;

		if (prefix == "xyz") {
		    ray[ray_idx].r_pt[0] = x;
		    ray[ray_idx].r_pt[1] = y;
		    ray[ray_idx].r_pt[2] = z;
		} else {
		    // assume this is 'dir' line
		    ray[ray_idx].r_dir[0] = x;
		    ray[ray_idx].r_dir[1] = y;
		    ray[ray_idx].r_dir[2] = z;

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
		    /* NOTE: hijack rays_per_view so our shoot() for-loop fires all the rays in here.
			     we should end up with +NUMVIEWS rays as the 'accuracy rays' are shot and
			     logged in the building of views
		       TODO / FIXME: the bu_malloc of ray[] looks iffy
		    */
		    rays_per_view = stoi(line.substr(startIdx + 1, endIdx - startIdx - 1));
		}
		in_section = true;
	    }
	}
    } else {
	/* first with a legit radius gets to define the bb and sph */
	/* XXX: should this lock? */
	if (radius < 0.0) {
	    radius = getsize(inst);
	    getbox(inst, bb, bb+1);
	    VADD2SCALE(bb[2], *bb, bb[1], 0.5);	/* (bb[0]+bb[1])/2 */
	}
	/* XXX: if locking, we can unlock here */

	/* build the views with pre-defined rays, yo */
	std::string accuracy_rays = "";
	char buff[200];
	for(int j=0; j < NUMVIEWS; ++j) {
	    vect_t avec,bvec;

	    VMOVE(ray->r_dir,dir[j]);
	    VJOIN1(ray->r_pt,bb[2],-radius,dir[j]);

	    shoot(inst,ray);	/* shoot the accuracy ray while we're here */
	    sprintf(buff, "xyz %0.17f %0.17f %0.17f\ndir %0.17f %0.17f %0.17f\n", V3ARGS(ray->r_pt), V3ARGS(ray->r_dir));
	    accuracy_rays += buff;

	    /* set up an othographic grid */
	    bn_vec_ortho( avec, ray->r_dir );
	    VCROSS( bvec, ray->r_dir, avec );
	    VUNITIZE( bvec );
	    rt_raybundle_maker(ray+j*rays_per_view,radius,avec,bvec,100,rays_per_view/100);
	}

	// write all the ray info
	// TODO: do this in dry-run?
	if (true) {
	    FILE* ray_file = fopen(dinfo.ray_file.c_str(), "w");	// intentionally erase contents if file already exists
	    fprintf(ray_file, "**rays fired for %s [%d]**\n", dinfo.json_ofile.c_str(), rays_per_view + NUMVIEWS);	// bundle rays + accuracy rays (0-index)
	    fprintf(ray_file, "%s", accuracy_rays.c_str());
	    for (int i = 0; i < rays_per_view; i++) {
		fprintf(ray_file, "xyz %0.17f %0.17f %0.17f\ndir %0.17f %0.17f %0.17f\n", V3ARGS(ray[i].r_pt), V3ARGS(ray[i].r_dir));
	    }
	    fclose(ray_file);
	}
    }

    /* actually shoot all the pre-defined rays */
    for(int i=0;i<rays_per_view;++i)
	shoot(inst,&ray[i]);

    /* clean up */
    bu_free(ray, "ray space");
    destructor(inst);
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

