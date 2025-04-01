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
#include "compare_config.h"

#include "rtcmp.h"

/* Command-line supplied options */
struct ProgramOptions {
    /*** Global Options ***/
    int ncpus = 0;						    // >1: parallel | 1: serial | 0: maximize CPU
    int rays_per_view = 1e5;					    // rays fired per view
    std::vector<std::string> non_opts;				    // unmatched options

    /*** Which run are we doing ***/
    bool use_tie = false;					    // use Triangle Intersection Engine in librt
    bool dry_run = false;					    // tests overhead - no actual calculations
    bool performance_run = false;				    // run tests, track performance - don't write results
    bool diff_run = false;					    // generate input file for difference tests
    bool compare_run = false;					    // compare JSON results files

    /*** Options needed for diff / comparison ***/
    CompareConfig compare_opts;
};

int
main(int argc, char **argv)
{
    ProgramOptions opts;

    cxxopts::Options options(argv[0], "rtcmp - a program to evaluate raytracer performance and correctness\n");

    try {
	options
	    .set_width(70)
	    .custom_help("[OPTIONS...] file.g geom | [-c results1.json results2.json]")
	    .add_options()
	    ("n,num-cpus",         "Number of CPUs to use for performance runs - >1 means a parallel run, 1 is a serial run, 0 (default) means use maximize CPU usage", cxxopts::value<int>(opts.ncpus))
	    ("enable-tie",         "Use the Triangle Intersection Engine in librt", cxxopts::value<bool>(opts.use_tie))
	    ("dry-run",            "Test overhead costs by doing a run that doesn't calculate intersections", cxxopts::value<bool>(opts.dry_run))
	    ("p,performance-test", "Run tests for raytracing speed (doesn't store and write results)", cxxopts::value<bool>(opts.performance_run))
	    ("d,difference-test",  "Run tests to generate input files for difference comparisons", cxxopts::value<bool>(opts.diff_run))
	    ("t,tolerance",        "Numerical tolerance to use when comparing numbers", cxxopts::value<double>(opts.compare_opts.tol))
	    ("c,compare",          "Compare two JSON results files", cxxopts::value<bool>(opts.compare_run))
	    ("rays-per-view",      "Number of rays to fire per view (default is 1e5)", cxxopts::value<int>(opts.rays_per_view))
	    ("input-rays",         "(difference run)Provide a name for the input ray file to generate shot data from", cxxopts::value<std::string>(opts.compare_opts.in_ray_file))
	    ("output-rays",        "(compare run)Provide a name for the output file (default is shots.rays)", cxxopts::value<std::string>(opts.compare_opts.ray_file))
	    ("output-json",        "(compare run)Provide a name for the JSON output file (default is shots.json)", cxxopts::value<std::string>(opts.compare_opts.json_ofile))
	    ("output-nirt",        "(compare run)Provide a name for the NIRT output file (default is diff.nrt)", cxxopts::value<std::string>(opts.compare_opts.nirt_file))
	    ("output-plot3",       "(compare run)Provide a name for the PLOT3 output file (default is diff.plot3)", cxxopts::value<std::string>(opts.compare_opts.plot3_file))
	    ("h,help",             "Print help")
	    ;
	auto result = options.parse(argc, argv);

	// unmatched supplied options
	opts.non_opts = result.unmatched();

	// looking for help?
	if (result.count("help")) {
	    std::cout << options.help({""}) << "\n";
	    std::cout << "\n";
	    return 0;
	}
    } catch (const cxxopts::exceptions::exception& e) {
	std::cerr << "error parsing options: " << e.what() << "\n";
	return -1;
    }

    // we always expect two trailing options.
    // either 'file.g component_name' for regular runs
    // or 'file1.json file2.json' for comparison runs
    if (opts.non_opts.size() != 2) {
	if (opts.compare_run) {
	    std::cerr << "Error:  need to specify two JSON results files\n\n";
	    std::cout << options.help({""}) << "\n";
	} else {
	    std::cerr << "Error:  need to specify a geometry file and object\n\n";
	    std::cout << options.help({""}) << "\n";
	}
	return -1;
    }
    const char *av[3] = {NULL};
    av[0] = opts.non_opts[0].c_str();
    av[1] = opts.non_opts[1].c_str();

    /* Compare run (compare supplied result files) */
    if (opts.compare_run) {
	if (shots_differ_new(opts.non_opts[0].c_str(), opts.non_opts[1].c_str(), opts.compare_opts)) {
	    std::cerr << "Differences found\n";
	} else {
	    std::cerr << "No differences found\n";
	}

	// this is a special run - we're done
	return 0;
    }

    /* Dry run (no shotlining, establishes overhead costs - diff run is a no-op) */
    if (opts.dry_run) {
	if (opts.diff_run) {
	    // TODO:
	    std::cerr << "Dry-run method does not support generating JSON output for diff comparisons\n";
	    return -1;
	}
	do_perf_run("dry", 2, (const char **)av, opts.ncpus, opts.rays_per_view, dry_constructor, dry_getbox, dry_getsize, dry_shoot, dry_destructor);
    }

    // librt setup
    rt_init_resource(&rt_uniresource, 0, NULL);

    /* Diff and/or Performance run */
    if (opts.use_tie) {
	/* TIE */
	if (opts.diff_run) {
	    // TODO: edited constructor signature
	    //do_diff_run("tie", 2, (const char **)av, ncpus, rays_per_view, tie_diff_constructor, tie_diff_getbox, tie_diff_getsize, tie_diff_shoot, tie_diff_destructor, dinfo);
	}
	if (opts.performance_run) {
	    do_perf_run("tie", 2, (const char **)av, opts.ncpus, opts.rays_per_view, tie_perf_constructor, tie_perf_getbox, tie_perf_getsize, tie_perf_shoot, tie_perf_destructor);
	}
    } else {
	/* Regular rt */
	if (opts.diff_run) {
	    diff_output_info dinfo;	// FIXME: migrate to new CompareConfig
	    dinfo.json_ofile = opts.compare_opts.json_ofile;
	    dinfo.in_ray_file = opts.compare_opts.in_ray_file;
	    dinfo.ray_file = opts.compare_opts.ray_file;
	    do_diff_run("rt", 2, (const char **)av, opts.ncpus, opts.rays_per_view, rt_diff_constructor, rt_diff_getbox, rt_diff_getsize, rt_diff_shoot, rt_diff_destructor, dinfo);
	}
	if (opts.performance_run) {
	    do_perf_run("rt", 2, (const char **)av, opts.ncpus, opts.rays_per_view, rt_perf_constructor, rt_perf_getbox, rt_perf_getsize, rt_perf_shoot, rt_perf_destructor);
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

