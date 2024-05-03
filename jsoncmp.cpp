#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <set>
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
	ss->shot_lookup[rs.ray_hash()] = ss->shots.size();
	ss->shots.push_back(rs);
    }

    return ss;
}

bool
shots_differ(const char *file1, const char *file2, double tol, diff_output_info &dinfo)
{
    run_shotset *s1 = parse_shots_file(file1);
    run_shotset *s2 = parse_shots_file(file2);

    if (!s1 || !s2)
	return false;

    bool ret = s1->different(*s2, tol, dinfo);
    delete s1;
    delete s2;
    return ret;
}

/*
 * TODO:
 *	* Shoot on a grid set instead of a single ray.
 */
void
do_diff_run(const char *prefix, int argc, const char **argv, int nthreads, int rays_per_view,
	void *(*constructor) (const char *, int, const char **, nlohmann::json *),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray * ray),
	int (*destructor) (void *),
	diff_output_info &dinfo)
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

    ray = (struct xray *)bu_malloc(sizeof(struct xray)*(rays_per_view * NUMVIEWS+1), "allocating ray space");

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
	rt_raybundle_maker(ray+j*rays_per_view,radius,avec,bvec,100,rays_per_view/100);
    }

    /* actually shoot all the pre-defined rays */
    for(int i=0;i<rays_per_view;++i)
	shoot(inst,&ray[i]);

    /* clean up */
    bu_free(ray, "ray space");
    destructor(inst);

    std::ofstream jfile(dinfo.json_ofile);
    jfile << std::setw(2) << jshots << "\n";
    jfile.close();
}

// TODO - probably want to return maximum numerical delta value, so we can do some
// kind of importance ordering to find the biggest differences
bool
run_part::different(class run_part &o, double tol, diff_output_info &dinfo)
{
    // Partition comparisons start with the region name - if
    // that doesn't match, we're done.
    if (o.region != region)
	return true;

    // If the region is OK, then we do a numerical check of the distances and
    // points, number by number.  Any deltas larger than the specified
    // tolerance are grounds for failure.
    if (!NEAR_EQUAL(o.in_dist, in_dist, tol))
	return true;
    if (!NEAR_EQUAL(o.out_dist, out_dist, tol))
	return true;
    if (!VNEAR_EQUAL(o.in, in, tol))
	return true;
    if (!VNEAR_EQUAL(o.out, out, tol))
	return true;
    if (!VNEAR_EQUAL(o.innorm, innorm, tol))
	return true;
    if (!VNEAR_EQUAL(o.outnorm, outnorm, tol))
	return true;

    // Looks good
    return false;
}

void
run_part::plot(FILE *pf, const class run_part &o)
{
    if (!pf)
	return;

    if (!NEAR_EQUAL(in_dist, o.in_dist, SMALL_FASTF)) {
	pdv_3move(pf, in);
	pdv_3cont(pf, o.in);
    }
    if (!NEAR_EQUAL(out_dist, o.out_dist, SMALL_FASTF)) {
	pdv_3move(pf, out);
	pdv_3cont(pf, o.out);
    }
}

void
run_part::plot(FILE *pf)
{
    if (!pf)
	return;

    pdv_3move(pf, in);
    pdv_3cont(pf, out);
}

void
run_part::print()
{
}

bool
run_shot::different(class run_shot &o, double tol, diff_output_info &dinfo)
{
    bool ret = false;

    // What do we need to do to compare a shot?
    //
    // 1.  partition count - if the partition counts differ, return is false.
    // May want to proceed anyway to try to characterize differences
    if (o.partitions.size() != partitions.size())
	ret = true;

    // 2.  For all partitions along the shot, compare them to see if there are
    // any discrepancies.  This is were things get interesting.  We use two
    // queues and pop partitions off of them to be able to "recover" if we
    // encounter a localized difference along the partition - ideally, when we
    // generate any sort of report on what about the partitions is different,
    // we want that report to be minimalist.
    //
    // For example, if one segment has been inserted into a shotline partition
    // list due to a change in grazing hit behavior, rather than cascading the
    // "failed" status of mismatched partitions down the rest of the shotline
    // we want to report just the added partition as the difference.
    std::queue<size_t> oq, q;
    for (size_t i = 0; i < o.partitions.size(); i++)
	oq.push(i);
    for (size_t i = 0; i < partitions.size(); i++)
	q.push(i);

    std::vector<size_t> o_unmatched_length;
    std::vector<size_t> c_unmatched_length;
    std::vector<size_t> o_unmatched_props;
    std::vector<size_t> c_unmatched_props;

    size_t opind = oq.front();
    size_t pind = q.front();

    while (!oq.empty() && !q.empty()) {
	opind = oq.front();
	pind = q.front();
	run_part &opart = o.partitions[opind];
	run_part &cpart = partitions[pind];
	if (opart.different(cpart, tol, dinfo)) {
	    // We have a difference.  See if we can decide based on distances
	    // which one to add to its unmatched vector.
	    if (!NEAR_EQUAL(opart.in_dist, cpart.in_dist, SMALL_FASTF)) {
		if (opart.in_dist < cpart.in_dist) {
		    o_unmatched_length.push_back(opind);
		    oq.pop();
		    continue;
		} else {
		    c_unmatched_length.push_back(pind);
		    q.pop();
		    continue;
		}
	    } else if (!NEAR_EQUAL(opart.out_dist, cpart.out_dist, SMALL_FASTF)) {
		if (opart.out_dist < cpart.out_dist) {
		    o_unmatched_length.push_back(opind);
		    oq.pop();
		    continue;
		} else {
		    c_unmatched_length.push_back(pind);
		    q.pop();
		    continue;
		}
	    } else {
		// If the difference is in the region or the normals rather
		// than the partition length itself, those won't have
		// implications for getting us "out of sync" in subsequent
		// diffing.  Record both as unmatched and proceed.
		c_unmatched_props.push_back(pind);
		q.pop();
		o_unmatched_props.push_back(opind);
		oq.pop();
	    }
	}

	// We're good, pop both of them
	q.pop();
	oq.pop();
    }

    // At this point, if either queue has anything left, it is an unmatched length
    while (!oq.empty()) {
	o_unmatched_length.push_back(oq.front());
	run_part &opart = o.partitions[oq.front()];
	oq.pop();
    }
    while (!q.empty()) {
	c_unmatched_length.push_back(q.front());
	run_part &cpart = partitions[q.front()];
	q.pop();
    }

    // We generate a plot file with full_length representations of the
    // partitions for debugging, to show context.  Note that these are NOT the
    // actual differences, but rather the partition segments that reported as
    // different between the two runs.  The differences themselves are often too
    // tiny to be visible when plotting.
    FILE *pf = fopen(dinfo.plot3_file.c_str(), "ab");
    if (pf) {
	pl_color(pf, 255, 0, 0);
	for (size_t i = 0; i < o_unmatched_length.size(); i++) {
	    run_part &part = o.partitions[o_unmatched_length[i]];
	    part.plot(pf);
	}
	pl_color(pf, 0, 0, 255);
	for (size_t i = 0; i < c_unmatched_props.size(); i++) {
	    run_part &part = partitions[c_unmatched_length[i]];
	    part.plot(pf);
	}

	pl_color(pf, 255, 0, 255);
	// Since there's no length difference for property differences,
	// just plot one copy
	for (size_t i = 0; i < c_unmatched_props.size(); i++) {
	    run_part &part = o.partitions[o_unmatched_length[i]];
	    part.plot(pf);
	}

	fclose(pf);
    }

    // Need to think about how to do that - one file per ray?  That
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

    // If we're not already different, see if we saw any differences
    if (!ret)
	ret = (o_unmatched_length.size() || c_unmatched_length.size() || o_unmatched_props.size() || c_unmatched_props.size());

    // TODO - return more than just yes/no, so we can report length vs prop
    // difference counts at higher levels
#if 0
    if (ret) {
	if (o_unmatched_length.size() || c_unmatched_length.size())
	    std::cerr << "Partition length difference observed\n";
	if (o_unmatched_props.size() || c_unmatched_props.size())
	    std::cerr << "Partition properties difference observed\n";
    }
#endif

    return ret;
}

void
run_shot::print()
{
}

unsigned long long
run_shot::ray_hash()
{
    if (!rhash) {
	struct bu_vls rstr = BU_VLS_INIT_ZERO;
	bu_vls_sprintf(&rstr, "%0.15f%0.15f%0.15f%0.15f%0.15f%0.15f", V3ARGS(ray_pt), V3ARGS(ray_dir));
	rhash = bu_data_hash((void *)bu_vls_cstr(&rstr), bu_vls_strlen(&rstr));
	bu_vls_free(&rstr);
    }
    return rhash;
}

bool
run_shotset::different(class run_shotset &o, double tol, diff_output_info &dinfo)
{
    bool ret = false;

    // What do we need to do to compare a shot set?
    //
    // 1.  (Sanity) find out which rays are present in both sets, and which
    // rays are unique to one of them.  Initial thought is to hash rays (i.e.
    // points and directions) into sets for easy lookups. If there are any
    // unmatched rays, the ultimate result will technically be a failure, but
    // probably want to proceed anyway to get any additional insights from the
    // data we can.
    std::unordered_map<unsigned long long, size_t>::iterator m_it;
    for (m_it = shot_lookup.begin(); m_it != shot_lookup.end(); m_it++) {
	if (o.shot_lookup.find(m_it->first) == o.shot_lookup.end()) {
	    // Unmatched shot
	    ret = true;
	}
    }
    for (m_it = o.shot_lookup.begin(); m_it != o.shot_lookup.end(); m_it++) {
	if (shot_lookup.find(m_it->first) == shot_lookup.end()) {
	    // Unmatched shot
	    ret = true;
	}
    }

    if (ret) {
	std::cerr << "Warning - unmatched shots found.  Doing comparison only of common shots.\n";
    }

    //
    // 2.  For each common shot, compare its results.  The final
    // equal/not-equal decision is based on the individual shot comparisons -
    // if they all match within tolerance, and all shots are in both sets, then
    // the sets are the same (return true).  Otherwise return false.
    size_t same_cnt = 0;
    size_t diff_cnt = 0;
    std::set<size_t> diff_shots;
    for (size_t i = 0; i < shots.size(); i++) {
	m_it = o.shot_lookup.find(shots[i].ray_hash());
	if (m_it == o.shot_lookup.end())
	    continue;
	bool sdiff= shots[i].different(o.shots[m_it->second], tol, dinfo);
	if (sdiff) {
	    ret = true;
	    diff_cnt++;
	    diff_shots.insert(i);
	} else {
	    same_cnt++;
	}
    }

    std::cerr << "Identical shotline count: " << same_cnt << "\n";
    std::cerr << "Differing shotline count: " << diff_cnt << "\n";

    std::set<size_t>::iterator d_it;
    for (d_it = diff_shots.begin(); d_it != diff_shots.end(); d_it++) {
	bu_log("s %0.17f %0.17f %0.17f %0.17f %0.17f %0.17f\n", V3ARGS(shots[*d_it].ray_pt), V3ARGS(shots[*d_it].ray_dir));
    }

    return ret;
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

