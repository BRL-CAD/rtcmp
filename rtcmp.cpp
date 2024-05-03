/*                         R T C M P . C
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
/** @file rtcmp.c
 *
 *
 *
 */

#include <iostream>

#include "cxxopts.hpp"

#include "dry/dry.h"
#include "rt/rt_diff.h"
#include "rt/rt_perf.h"
#include "tie/tie_diff.h"
#include "tie/tie_perf.h"

#include "rtcmp.h"

int
main(int argc, char **argv)
{
    int ncpus = 0;
    bool use_tie = false;
    bool dry_run = false;
    bool enable_tie = false;
    bool performance_test = false;
    bool diff_test = false;
    bool compare_json = false;
    int rays_per_view = 1e5;
    double diff_tol = SMALL_FASTF;
    diff_output_info dinfo;
    std::vector<std::string> nonopts;

    cxxopts::Options options(argv[0], "rtcmp - a program to evaluate raytracer performance and correctness\n");

    try
    {
	options
	    .set_width(70)
	    .custom_help("[OPTIONS...] file.g geom | [-c results1.json results2.json]")
	    .add_options()
	    ("n,num-cpus",         "Number of CPUs to use for performance runs - >1 means a parallel run, 1 is a serial run, 0 (default) means use maximize CPU usage", cxxopts::value<int>(ncpus))
	    ("enable-tie",         "Use the Triangle Intersection Engine in librt", cxxopts::value<bool>(use_tie))
	    ("dry-run",            "Test overhead costs by doing a run that doesn't calculate intersections", cxxopts::value<bool>(dry_run))
	    ("p,performance-test", "Run tests for raytracing speed (doesn't store and write results)", cxxopts::value<bool>(performance_test))
	    ("d,difference-test",  "Run tests to generate input files for difference comparisons", cxxopts::value<bool>(diff_test))
	    ("t,tolerance",        "Numerical tolerance to use when comparing numbers", cxxopts::value<double>(diff_tol))
	    ("c,compare",          "Compare two JSON results files", cxxopts::value<bool>(compare_json))
	    ("rays-per-view",      "Number of rays to fire per view (default is 1e5)", cxxopts::value<int>(rays_per_view))
	    ("output-json",        "Provide a name for the JSON output file (default is shots.json)", cxxopts::value<std::string>(dinfo.json_ofile))
	    ("output-nirt",        "Provide a name for the NIRT output file (default is diff.nrt)", cxxopts::value<std::string>(dinfo.nirt_file))
	    ("output-plot3",       "Provide a name for the PLOT3 output file (default is diff.plot3)", cxxopts::value<std::string>(dinfo.plot3_file))
	    ("h,help",             "Print help")
	    ;
	auto result = options.parse(argc, argv);

	nonopts = result.unmatched();

	if (result.count("help")) {
	    std::cout << options.help({""}) << "\n";
	    std::cout << "\n";
	    return 0;
	}
    }

    catch (const cxxopts::exceptions::exception& e)
    {
	std::cerr << "error parsing options: " << e.what() << "\n";
	return -1;
    }

    if (nonopts.size() != 2) {
	if (compare_json) {
	    std::cerr << "Error:  need to specify two JSON results files\n\n";
	    std::cout << options.help({""}) << "\n";
	} else {
	    std::cerr << "Error:  need to specify a geometry file and object\n\n";
	    std::cout << options.help({""}) << "\n";
	}
	return -1;
    }

    if (compare_json) {

	// Clear any old output files, to avoid any confusion about what results
	// are associated with what run.
	bu_file_delete(dinfo.nirt_file.c_str());
	bu_file_delete(dinfo.plot3_file.c_str());

	std::cerr << "Using diff tolerance: " << diff_tol << "\n";
	bool is_different = shots_differ(nonopts[0].c_str(), nonopts[1].c_str(), diff_tol, dinfo);
	if (is_different) {
	    std::cerr << "Differences found\n";
	} else {
	    std::cerr << "No differences found\n";
	}
	return 0;
    }

    /* Dry run (no shotlining, establishes overhead costs - diff run is a no-op) */
    const char *av[3] = {NULL};
    av[0] = nonopts[0].c_str();
    av[1] = nonopts[1].c_str();
    if (dry_run) {
	if (diff_test) {
	    std::cerr << "Dry-run method does not support generating JSON output for diff comparisons\n";
	    return -1;
	}
	do_perf_run("dry", 2, (const char **)av, ncpus, rays_per_view, dry_constructor, dry_getbox, dry_getsize, dry_shoot, dry_destructor);
    }

    /* librt */
    rt_init_resource(&rt_uniresource, 0, NULL);
    if (!enable_tie) {
	if (diff_test) {
	    do_diff_run("rt", 2, (const char **)av, ncpus, rays_per_view, rt_diff_constructor, rt_diff_getbox, rt_diff_getsize, rt_diff_shoot, rt_diff_destructor, dinfo);
	}
	if (performance_test) {
	    do_perf_run("rt", 2, (const char **)av, ncpus, rays_per_view, rt_perf_constructor, rt_perf_getbox, rt_perf_getsize, rt_perf_shoot, rt_perf_destructor);
	}
    }


    /* TIE */
    if (enable_tie) {
	if (diff_test) {
	    do_diff_run("tie", 2, (const char **)av, ncpus, rays_per_view, tie_diff_constructor, tie_diff_getbox, tie_diff_getsize, tie_diff_shoot, tie_diff_destructor, dinfo);
	}
	if (performance_test) {
	    do_perf_run("tie", 2, (const char **)av, ncpus, rays_per_view, tie_perf_constructor, tie_perf_getbox, tie_perf_getsize, tie_perf_shoot, tie_perf_destructor);
	}
    }

    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

