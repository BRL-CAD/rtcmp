#include <fstream>
#include <iostream>
#include <time.h>
#include <sys/time.h>


#include "bu.h"
#include "raytrace.h"
#include "accucheck.h"

void
parse_pt(const nlohmann::json &sdata)
{
    if (sdata.contains("X")) {
	std::cout << "	X:" << sdata["X"] << "\n";
    }
    if (sdata.contains("Y")) {
	std::cout << "	Y:" << sdata["Y"] << "\n";
    }
    if (sdata.contains("Z")) {
	std::cout << "	Z:" << sdata["Z"] << "\n";
    }
}

void
parse_partition_data_entry(const nlohmann::json &sdata)
{
    if (sdata.contains("region")) {
	std::cout << "partition region: " << sdata["region"] <<  "\n";
    }

    if (sdata.contains("in_dist")) {
	std::cout << "partition in_dist: " << sdata["in_dist"] <<  "\n";
    }

    if (sdata.contains("out_dist")) {
	std::cout << "partition out_dist: " << sdata["out_dist"] <<  "\n";
    }

    if (sdata.contains("in_pt")) {
	std::cout << "partition in_pt:\n";
	const nlohmann::json &ssdata = sdata["in_pt"];
	parse_pt(ssdata);
    }

    if (sdata.contains("out_pt")) {
	std::cout << "partition out_pt:\n";
	const nlohmann::json &ssdata = sdata["out_pt"];
	parse_pt(ssdata);
    }

    if (sdata.contains("in_norm")) {
	std::cout << "partition in_norm:\n";
	const nlohmann::json &ssdata = sdata["in_norm"];
	parse_pt(ssdata);
    }

    if (sdata.contains("out_norm")) {
	std::cout << "partition out_norm:\n";
	const nlohmann::json &ssdata = sdata["out_norm"];
	parse_pt(ssdata);
    }

}

void
parse_partitions(const nlohmann::json &sdata)
{
    for(nlohmann::json::const_iterator it = sdata.begin(); it != sdata.end(); ++it) {
	const nlohmann::json &ssdata = *it;
	parse_partition_data_entry(ssdata);
    }	
}

void
parse_shots(const nlohmann::json &sdata)
{
    if (sdata.contains("ray_pt")) {
	std::cout << "ray_pt:\n";
	const nlohmann::json &ssdata = sdata["ray_pt"];
	parse_pt(ssdata);
    }

    if (sdata.contains("ray_dir")) {
	std::cout << "ray_dir:\n";
	const nlohmann::json &ssdata = sdata["ray_dir"];
	parse_pt(ssdata);
    }

    if (sdata.contains("partitions")) {
	const nlohmann::json &ssdata = sdata["partitions"];
	parse_partitions(ssdata);
    }
}

void
parse_shots_file(const char *fname)
{
    std::ifstream f(fname);
    nlohmann::json fdata = nlohmann::json::parse(f);
    const std::string data_version = fdata["data_version"];
    std::cout << "data version:" << data_version << "\n";
    const std::string etype = fdata["engine"];
    std::cout << "engine type:" << etype << "\n";

    nlohmann::json &fshots = fdata["shots"];

    for(nlohmann::json::const_iterator it = fshots.begin(); it != fshots.end(); ++it) {
	const nlohmann::json &sdata = *it;
	parse_shots(sdata);
    }
}

/*
 * TODO:
 *	* pass in "accuracy" rays and pass back the results.
 *	* Shoot on a grid set instead of a single ray.
 */
nlohmann::json *
do_accu_run(const char *prefix, int argc, char **argv, int nthreads,
	void *(*constructor) (char *, int, char **, nlohmann::json *),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *))
{
    clock_t cstart, cend;
    struct part **p;
    struct timeval start, end;
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

    jshots["engine"] = prefix;
    jshots["data_version"] = "1.0";

    ray = (struct xray *)bu_malloc(sizeof(struct xray)*(NUMTRAYS+1), "allocating ray space");

    inst = constructor(*argv, argc-1, argv+1, &jshots);
    if (inst == NULL) {
	return NULL;
    }

    /* first with a legit radius gets to define the bb and sph */
    /* XXX: should this lock? */
    if (radius < 0.0) {
	radius = getsize(inst);
	getbox(inst, bb, bb+1);
	VADD2SCALE(bb[2], *bb, bb[1], 0.5);	/* (bb[0]+bb[1])/2 */
    }
    /* XXX: if locking, we can unlock here */

    /* build the views with pre-defined rays, yo */
    for(int j=0; j < NUMVIEWS; ++j) {
	vect_t avec,bvec;

	VMOVE(ray->r_dir,dir[j]);
	VJOIN1(ray->r_pt,bb[2],-radius,dir[j]);

	shoot(inst,ray);	/* shoot the accuracy ray while we're here */

	/* set up an othographic grid */
	bn_vec_ortho( avec, ray->r_dir );
	VCROSS( bvec, ray->r_dir, avec );
	VUNITIZE( bvec );
	rt_raybundle_maker(ray+j*NUMRAYS,radius,avec,bvec,100,NUMRAYS/100);
    }

    /* performance run */
    gettimeofday(&start,NULL); cstart = clock();

    /* actually shoot all the pre-defined rays */
    for(int i=0;i<NUMRAYS;++i)
	shoot(inst,&ray[i]);

    cend = clock(); gettimeofday(&end,NULL);
    /* end of performance run */

    /* clean up */
    bu_free(ray, "ray space");
    destructor(inst);

    /* fill in the perfomrance data for the bundle */
#define SEC(tv) ((double)tv.tv_sec + (double)(tv.tv_usec)/(double)1e6)
    //ret->t = SEC(end) - SEC(start);
    //ret->c = (double)(cend-cstart)/(double)CLOCKS_PER_SEC;

    std::ofstream jfile("shotlines.json");
    jfile << std::setw(2) << jshots << "\n";
    jfile.close();

    parse_shots_file("shotlines.json");

    return NULL;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

