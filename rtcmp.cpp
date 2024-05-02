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
#include "rt/rt_acc.h"
#include "rt/rt_perf.h"
#include "tie/tie.h"

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
    std::string json_ofile;
    std::vector<std::string> nonopts;

    cxxopts::Options options(argv[0], "A program to evaluate raytracer performance and correctness\n");

    try
    {
	options
	    .set_width(70)
	    .custom_help("[OPTIONS...] file.g geom | [-c results1.json results2.json]")
	    .add_options()
	    ("n,num-cpus",         "Number of CPUs to use for performance runs - >1 means a parallel run, 1 is a serial run, 0 (default) means use maximize CPU usage", cxxopts::value<int>(ncpus))
	    ("enable-tie",         "Use the Triangle Intersection Engine in librt", cxxopts::value<bool>(use_tie))
	    ("dry-run",            "Test overhead costs by doing a run that doesn't calculate intersections", cxxopts::value<bool>(dry_run))
	    ("t,test-performance", "Run tests for raytracing speed (doesn't store and write results)", cxxopts::value<bool>(performance_test))
	    ("a,test-differences", "Run tests to generate input files for difference comparisons", cxxopts::value<bool>(diff_test))
	    ("c,compare",          "Compare two JSON results files", cxxopts::value<bool>(compare_json))
	    ("output-json",        "Compare two JSON results files", cxxopts::value<std::string>(json_ofile))
	    ("h,help",             "Print help")
	    ;
	auto result = options.parse(argc, argv);

	nonopts = result.unmatched();

	if (result.count("help")) {
	    std::cout << options.help({""}) << std::endl;
	    std::cout << "\n";
	    return 0;
	}
    }

    catch (const cxxopts::exceptions::exception& e)
    {
	std::cerr << "error parsing options: " << e.what() << std::endl;
	return -1;
    }

    if (nonopts.size() != 2) {
	if (compare_json) {
	    std::cerr << "Error:  need to specify two JSON results files\n";
	} else {
	    std::cerr << "Error:  need to specify a geometry file and object\n";
	}
	return -1;
    }

    /* Dry run (no shotlining, establishes overhead costs - diff run is a no-op) */
    if (dry_run) {
	if (diff_test) {
	    std::cerr << "Dry-run method does not support generating JSON output for diff comparisons\n";
	    return -1;
	}
	do_perf_run("dry", argc, argv, ncpus, dry_constructor, dry_getbox, dry_getsize, dry_shoot, dry_destructor);
    }

    /* librt */
    if (!enable_tie) {
	if (diff_test) {
	    do_diff_run("rt", argc, argv, ncpus, rt_acc_constructor, rt_acc_getbox, rt_acc_getsize, rt_acc_shoot, rt_acc_destructor);
	}
	if (performance_test) {
	    do_perf_run("rt", argc, argv, ncpus, rt_perf_constructor, rt_perf_getbox, rt_perf_getsize, rt_perf_shoot, rt_perf_destructor);
	}
    }

#if 0
    /* TIE */
    if (enable_tie) {
	if (diff_test) {
	    do_diff_run("tie", argc, argv, ncpus, tie_constructor, tie_getbox, tie_getsize, tie_shoot, tie_destructor);
	}
	if (performance_test) {
	    do_perf_run("tie", argc, argv, ncpus, tie_constructor, tie_getbox, tie_getsize, tie_shoot, tie_destructor);
	}
    }
#endif

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

