#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <time.h>
#include <sys/time.h>

#include "rtcmp.h"

double
s2d(std::string s)
{
    double d;
    std::stringstream ss(s);
    size_t prec = std::numeric_limits<double>::max_digits10;
    ss >> std::setprecision(prec) >> std::fixed >> d;
    return d;
}

void
parse_pt(point_t *p, const nlohmann::json &sdata)
{
    if (sdata.contains("X")) {
	std::string s(sdata["X"]);
	(*p)[X] = s2d(s);
    }
    if (sdata.contains("Y")) {
	std::string s(sdata["Y"]);
	(*p)[Y] = s2d(s);
    }
    if (sdata.contains("Z")) {
	std::string s(sdata["Z"]);
	(*p)[Z] = s2d(s);
    }
}

void
parse_partition_data_entry(run_part &rp, const nlohmann::json &sdata)
{
    if (sdata.contains("region")) {
	rp.region = sdata["region"];
    }

    if (sdata.contains("in_dist")) {
	std::string s(sdata["in_dist"]);
	rp.in_dist = s2d(s);
    }

    if (sdata.contains("out_dist")) {
	std::string s(sdata["out_dist"]);
	rp.out_dist = s2d(s);
    }

    if (sdata.contains("in_pt")) {
	const nlohmann::json &ssdata = sdata["in_pt"];
	parse_pt(&rp.in, ssdata);
    }

    if (sdata.contains("out_pt")) {
	const nlohmann::json &ssdata = sdata["out_pt"];
	parse_pt(&rp.out, ssdata);
    }

    if (sdata.contains("in_norm")) {
	const nlohmann::json &ssdata = sdata["in_norm"];
	parse_pt(&rp.innorm, ssdata);
    }

    if (sdata.contains("out_norm")) {
	const nlohmann::json &ssdata = sdata["out_norm"];
	parse_pt(&rp.outnorm, ssdata);
    }

}

void
parse_partitions(run_shot &rs, const nlohmann::json &sdata)
{
    for(nlohmann::json::const_iterator it = sdata.begin(); it != sdata.end(); ++it) {
	const nlohmann::json &ssdata = *it;
	run_part rp;
	parse_partition_data_entry(rp, ssdata);
	rs.partitions.push_back(rp);
    }
}

void
parse_shot(run_shot &rs, const nlohmann::json &sdata)
{
    point_t p;
    if (sdata.contains("ray_pt")) {
	const nlohmann::json &ssdata = sdata["ray_pt"];
	parse_pt(&rs.ray_pt, ssdata);
    }

    if (sdata.contains("ray_dir")) {
	const nlohmann::json &ssdata = sdata["ray_dir"];
	parse_pt(&rs.ray_dir, ssdata);
    }

    if (sdata.contains("partitions")) {
	const nlohmann::json &ssdata = sdata["partitions"];
	parse_partitions(rs, ssdata);
    }
}

run_shotset *
parse_shots_file(const char *fname)
{
    std::ifstream f(fname);
    if (!f.is_open())
	return NULL;

    run_shotset *ss = new run_shotset;

    nlohmann::json fdata = nlohmann::json::parse(f);
    const std::string data_version = fdata["data_version"];
    ss->data_version = fdata["data_version"];
    ss->engine = fdata["engine"];

    nlohmann::json &fshots = fdata["shots"];

    for(nlohmann::json::const_iterator it = fshots.begin(); it != fshots.end(); ++it) {
	const nlohmann::json &sdata = *it;
	run_shot rs;
	parse_shot(rs, sdata);
	ss->shots.push_back(rs);
    }

    return ss;
}

void
compare_shots(const char *file1, const char *file2)
{
    run_shotset *s1 = parse_shots_file(file1);
    run_shotset *s2 = parse_shots_file(file2);

    if (!s1 || !s2)
	return;
}

/*
 * TODO:
 *	* Shoot on a grid set instead of a single ray.
 */
void
do_diff_run(const char *prefix, int argc, const char **argv, int nthreads,
	void *(*constructor) (const char *, int, const char **, nlohmann::json *),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *),
	std::string &json_ofile)
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

    /* actually shoot all the pre-defined rays */
    for(int i=0;i<NUMRAYS;++i)
	shoot(inst,&ray[i]);

    /* clean up */
    bu_free(ray, "ray space");
    destructor(inst);

    std::ofstream jfile(json_ofile);
    jfile << std::setw(2) << jshots << "\n";
    jfile.close();
}

bool
run_part::cmp(class run_part &o)
{
    if (o.region != region)
	return false;

    // TODO - numerical comparisons - probably want to allow for a
    // tolerance specification.  Theoretically, if we do the number
    // reading and writing correctly, we can compare the full floating
    // point numbers here (see the work we did for NIRT about that -
    // will probably want to pre-convert data using those techniques
    // to strings before packing it off to json.hpp).  However, depending
    // on the use case, we may be wanting to spot larger differences
    // rather than tiny changes.
    return false;
}

void
run_part::print()
{
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

