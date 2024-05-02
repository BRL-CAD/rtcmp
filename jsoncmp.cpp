#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <queue>
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

bool
compare_shots(const char *file1, const char *file2)
{
    run_shotset *s1 = parse_shots_file(file1);
    run_shotset *s2 = parse_shots_file(file2);

    if (!s1 || !s2)
	return false;

    bool ret = s1->cmp(*s2);
    delete s1;
    delete s2;
    return ret;
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
run_part::cmp(class run_part &o, double tol)
{
    // Partition comparisons start with the region name - if
    // that doesn't match, we're done.
    if (o.region != region)
	return false;

    // If the region is OK, then we do a numerical check of the distances and
    // points, number by number.  Any deltas larger than the specified
    // tolerance are grounds for failure.
    if (!NEAR_EQUAL(o.in_dist, in_dist, tol))
	return false;
    if (!NEAR_EQUAL(o.out_dist, out_dist, tol))
	return false;
    if (!VNEAR_EQUAL(o.in, in, tol))
	return false;
    if (!VNEAR_EQUAL(o.out, out, tol))
	return false;
    if (!VNEAR_EQUAL(o.in_norm, in_norm, tol))
	return false;
    if (!VNEAR_EQUAL(o.out_norm, out_norm, tol))
	return false;

    // Looks good
    return true;
}

void
run_part::print()
{
}

bool
run_shot::cmp(class run_shot &o, double tol)
{
    bool ret = true;

    // What do we need to do to compare a shot?
    //
    // 1.  partition count - if the partition counts differ, return is false.
    // May want to proceed anyway to try to characterize differences
    if (o.partitions.size() != partitions.size())
	ret = false;

    // 2.  For all partitions along the shot, compare them to see if there
    // are any discrepancies.  This is where it gets interesting - to provide
    // the most useful reporting on differences, we may want to use two queues
    // and pop partitions off of each one - if we get an unmatched partition in
    // one of them, we can take the one with the smallest in_hit distance and
    // store it then pop the next one from that queue to see if it matches the
    // unmatched segment from the other queue that failed the test.  This would
    // (for example) allow reporting of one segment being inserted into a
    // shotline partition list due to a change in grazing hit behavior, rather
    // than cascading the "failed" status down the rest of the shotline.  The
    // condition for stopping the popping process is when the new candidate
    // partition has an in_dist that is further out than the in_dist of the
    // one we are trying to match - at that point, the original target is popped
    // from the other queue and we try to match the new partition from there.
    std::queue<size_t> oq, q;
    for (size_t i = o.partitions.size() - 1; i >= 0; i--)
	oq.push(i);
    for (size_t i = partitions.size() - 1; i >= 0; i--)
	q.push(i);

    std::vector<size_t> o_unmatched;
    std::vector<size_t> c_unmatched;
    size_t opind = oq.front();
    size_t pind = q.front();
    bool do_opop = true;
    bool do_cpop = true;

    while (!oq.empty() && !q.empty(), do_opop, do_cpop) {
	if (do_opop) {
	    opind = oq.front();
	    oq.pop();
	}
	if (do_cpop) {
	    pind = q.front();
	    q.pop();
	}
	do_opop = true;
	do_cpop = true;
	run_part &opart = o.partitions[opind];
	run_part &cpart = partitions[pind];
	if (!opart.cmp(cpart)) {
	    // We have a difference.  See if we can decide based on distances
	    // which one to add to its unmatched vector.
	    if (!NEAR_ZERO(opart.in_dist, cpart.in_dist, SMALL_FASTF)) {
		if (opart.in_dist < cpart.in_dist) {
		    o_unmatched.push_back(opind);
		    do_cpop = false;
		    continue;
		} else {
		    unmatched.push_back(pind);
		    do_opop = false;
		    continue;
		}
	    } else if (!NEAR_ZERO(opart.out_dist, cpart.out_dist, SMALL_FASTF)) {
		if (opart.out_dist < cpart.out_dist) {
		    o_unmatched.push_back(opind);
		    do_cpop = false;
		    continue;
		} else {
		    unmatched.push_back(pind);
		    do_opop = false;
		    continue;
		}
	    } else {
		// If the difference is in the region or the normals rather
		// than the partition length itself, those won't have
		// implications for getting us "out of sync" in subsequent
		// diffing.  Record both as unmatched and proceed.
		unmatched.push_back(pind);
		o_unmatched.push_back(opind);
	    }
	}
    }

    // At this point, if either queue has anything left, it is unmatched
    while (!oq.empty()) {
	o_unmatched.push_back(oq.front());
	oq.pop();
    }
    while (!q.empty()) {
	unmatched.push_back(oq.front());
	q.pop();
    }

    // TODO - we need to generate a plot file with the unmatched partitions for
    // debugging.  Need to think about how to do that - one file per ray?  That
    // could be a lot of files, but all-in-one wouldn't let us inspect the
    // individual ray graphically.  However, an all-in-one file would let us
    // see patters visually (i.e. BVH aligned, they're all grazing rays, only
    // diffs on spheres, etc.) that we can't see from the data itself.  Probably
    // will end up having to generate both.  Should put individual ray plot files
    // in their own directory to make it easier to delete them all later.  May
    // want to use the ray hash ids for bot the plot files and nirt reproduction
    // files, and lead the names with some naming key based on a metric of
    // difference size.
    //
    // Another possibility would be to mod plot (if it can't already do this) to
    // support name specifications, and then allow overlay to optionally use those
    // names to generate multiple bv scene objects.  That would be ideal, particularly
    // if we could implement (maybe as a qged plugin) a way to graphically interrogate
    // the scene objects to select one object.  That would be ideal, in that we could
    // graphically "pick" a scene object corresponding to a problematic ray and get
    // its exact shotline info via name lookup.

}

void
run_shot::print()
{
}


bool
run_shotset::cmp(class run_shotset &o, double tol)
{

    // What do we need to do to compare a shot set?
    //
    // 1.  (Sanity) find out which rays are present in both sets, and which
    // rays are unique to one of them.  Initial thought is to hash rays (i.e.
    // points and directions) into sets for easy lookups. If there are any
    // unmatched rays, the ultimate result will technically be a failure, but
    // probably want to proceed anyway to get any additional insights from the
    // data we can.
    //
    // 2.  For each common shot, compare its results.  The final
    // equal/not-equal decision is based on the individual shot comparisons -
    // if they all match within tolerance, and all shots are in both sets, then
    // the sets are the same (return true).  Otherwise return false.

}

void
run_shotset::print()
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

