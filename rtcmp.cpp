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
#include <cmath>

#include "cxxopts.hpp"

#include "dry/dry.h"
#include "rt/rt.h"
#include "tie/tie.h"
#include "comp/compare_config.h"

#include "rtcmp.h"

namespace {

constexpr double DEFAULT_PERF_SECONDS = 20.0;

} // namespace

/* Command-line supplied options */
struct ProgramOptions {
    /*** Global Options ***/
    int ncpus = 0;						    // >1: parallel | 1: serial | 0: maximize CPU
    std::vector<std::string> non_opts;				    // unmatched options

    /*** Which run are we doing ***/
    bool use_tie = false;					    // use Triangle Intersection Engine in librt
    bool dry_run = false;					    // tests overhead - no actual calculations
    bool performance_run = false;				    // run tests, track performance - don't write results
    bool diff_run = false;					    // generate input file for difference tests
    bool compare_run = false;					    // compare JSON results files

    /*** Options needed for diff / comparison ***/
    CompareConfig compare_opts;

    /*** Options needed for perf ***/
    double perf_seconds = DEFAULT_PERF_SECONDS;			    // number of seconds to run perf
    size_t perf_max_memory = 0;					    // limit memory usage for long perf runs
};

int
main(int argc, char **argv)
{
    bu_setprogname(argv[0]);
    ProgramOptions opts;
    opts.compare_opts.generator = argv[0];
    bool minimum_rays_set = false;
    bool rays_per_view_set = false;
    bool input_rays_set = false;

    const auto usage_failure = [&opts](const std::string& detail) {
	std::cerr << "Error: " << detail << '\n';
	const std::string operation = opts.compare_run ? "correctness"
	    : opts.diff_run ? "ray_generation"
	    : opts.performance_run ? "performance"
	    : opts.dry_run ? "dry_run"
	    : "configuration";
	return static_cast<int>(report_configuration_failure(
	    opts.compare_opts, operation, detail));
    };

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
	    ("primitive-hits",     "(RT difference run) Include primitive segments before Boolean evaluation", cxxopts::value<bool>(opts.compare_opts.primitive_hits))
	    ("t,tolerance",        "Numerical tolerance to use when comparing numbers", cxxopts::value<double>(opts.compare_opts.tol))
	    ("c,compare",          "Compare two JSON results files", cxxopts::value<bool>(opts.compare_run))
	    ("grazing-geometry",   "(compare run) Geometry file for optional grazing-ray filtering", cxxopts::value<std::string>(opts.compare_opts.grazing_geometry))
	    ("grazing-object",     "(compare run) Object to trace for grazing-ray filtering", cxxopts::value<std::string>(opts.compare_opts.grazing_object))
	    ("grazing-radius",     "(compare run) Probe ring radius in mm (default: 1e-7 of model radius)", cxxopts::value<double>(opts.compare_opts.grazing_radius))
	    ("grazing-samples",    "(compare run) Number of nearby rays on the ring (default: 8)", cxxopts::value<int>(opts.compare_opts.grazing_samples))
	    ("rays-per-view",      "Number of rays to fire per view (default is 100000)", cxxopts::value<size_t>(opts.compare_opts.rays_per_view))
	    ("minimum-rays",       "Generate the smallest balanced six-view rayset with at least this many total rays", cxxopts::value<size_t>(opts.compare_opts.minimum_ray_count))
	    ("perf-seconds",       "(perf run)Number of seconds to run (default is 20s)", cxxopts::value<double>(opts.perf_seconds))
	    ("perf-max_memory",    "(perf run)Limit memory in a perf run (default '0' does not limit memory)", cxxopts::value<size_t>(opts.perf_max_memory))
	    ("input-rays",         "Use this canonical rayset for difference generation or correctness comparison", cxxopts::value<std::string>(opts.compare_opts.in_ray_file))
	    ("output-rays",        "(difference run)Write generated canonical rays here (default is shots.rays)", cxxopts::value<std::string>(opts.compare_opts.ray_file))
	    ("output-json",        "(difference run)Write shot results here (default is shots.json)", cxxopts::value<std::string>(opts.compare_opts.json_ofile))
	    ("output-nirt",        "(compare run)Provide a name for the NIRT output file (default is diff.nrt)", cxxopts::value<std::string>(opts.compare_opts.nirt_file))
	    ("output-plot3",       "(compare run)Provide a name for the PLOT3 output file (default is diff.plot3)", cxxopts::value<std::string>(opts.compare_opts.plot3_file))
	    ("output-status",      "Write compact structured operation status to this file", cxxopts::value<std::string>(opts.compare_opts.status_file))
	    ("h,help",             "Print help")
	    ;
	auto result = options.parse(argc, argv);
	minimum_rays_set = result.count("minimum-rays") != 0;
	rays_per_view_set = result.count("rays-per-view") != 0;
	input_rays_set = result.count("input-rays") != 0;

	// unmatched supplied options
	opts.non_opts = result.unmatched();

	// looking for help?
	if (result.count("help")) {
	    std::cout << options.help({""}) << "\n";
	    std::cout << "\n";
	    return 0;
	}
    } catch (const cxxopts::exceptions::exception& e) {
	return usage_failure(std::string("error parsing options: ") + e.what());
    }

    if (opts.ncpus < 0 || !std::isfinite(opts.compare_opts.tol) || opts.compare_opts.tol < 0.0
	|| opts.compare_opts.rays_per_view == 0) {
	return usage_failure("CPU count and tolerance must be nonnegative, and rays per view must be positive");
    }
    if (minimum_rays_set && opts.compare_opts.minimum_ray_count == 0) {
	return usage_failure("--minimum-rays must be greater than zero");
    }
    if (minimum_rays_set && rays_per_view_set) {
	return usage_failure("--minimum-rays and --rays-per-view are mutually exclusive");
    }
    if (input_rays_set && (minimum_rays_set || rays_per_view_set)) {
	return usage_failure("explicit ray generation counts cannot be combined with --input-rays");
    }
    if (!opts.compare_run && !opts.diff_run && !opts.performance_run && !opts.dry_run) {
	return usage_failure("select a comparison, difference, or performance operation");
    }
    if (opts.compare_run && (opts.diff_run || opts.performance_run || opts.dry_run)) {
	return usage_failure("--compare cannot be combined with another operation");
    }
    if (opts.compare_run && opts.compare_opts.in_ray_file.empty()) {
	return usage_failure("correctness comparison requires --input-rays");
    }
    if (opts.compare_opts.primitive_hits
	&& (opts.compare_run || !opts.diff_run || opts.use_tie)) {
	return usage_failure("--primitive-hits requires a regular RT difference run");
    }
    const bool grazing_requested = !opts.compare_opts.grazing_geometry.empty()
	|| !opts.compare_opts.grazing_object.empty() || opts.compare_opts.grazing_radius != 0.0
	|| opts.compare_opts.grazing_samples != DEFAULT_GRAZING_SAMPLES;
    if (grazing_requested && !opts.compare_run) {
	return usage_failure("grazing options require comparison mode");
    }

    // we always expect two trailing options.
    // either 'file.g component_name' for regular runs
    // or 'file1.json file2.json' for comparison runs
    if (opts.non_opts.size() != 2) {
	std::cout << options.help({""}) << "\n";
	return usage_failure(opts.compare_run
	    ? "need to specify two JSON results files"
	    : "need to specify a geometry file and object");
    }
    const char *av[3] = {NULL};
    av[0] = opts.non_opts[0].c_str();
    av[1] = opts.non_opts[1].c_str();

    /* Compare run (compare supplied result files) */
    if (opts.compare_run) {
	return static_cast<int>(do_comp(
	    opts.non_opts[0].c_str(), opts.non_opts[1].c_str(), opts.compare_opts));
    }

    /* Dry run (no shotlining, establishes overhead costs - diff run is a no-op) */
    if (opts.dry_run) {
	if (opts.diff_run) {
	    // TODO:
	    return usage_failure("Dry-run method does not support generating JSON output for diff comparisons");
	}
	// FIXME: dry still fires all rays for perf run
	if (!do_perf_run("dry", 2, (const char **)av, opts.ncpus, opts.perf_seconds, opts.perf_max_memory, dry_constructor, dry_getbox, dry_getsize, dry_shoot, dry_destructor))
	    return 1;
    }

    /* Diff and/or Performance run */
    if (opts.use_tie) {
	/* TIE */
	if (opts.diff_run) {
	    const RtcmpExitCode status = do_diff_run("tie", 2, (const char **)av, opts.ncpus, tie_constructor, tie_getbox, tie_getsize, tie_shoot, tie_destructor, opts.compare_opts);
	    if (status != RtcmpExitCode::Success)
		return static_cast<int>(status);
	}
	if (opts.performance_run) {
	    if (!do_perf_run("tie", 2, (const char **)av, opts.ncpus, opts.perf_seconds, opts.perf_max_memory, tie_constructor, tie_getbox, tie_getsize, tie_shoot, tie_destructor))
		return 1;
	}
    } else {
	/* Regular rt */
	if (opts.diff_run) {
	    const RtcmpExitCode status = do_diff_run("rt", 2, (const char **)av, opts.ncpus, rt_constructor, rt_getbox, rt_getsize, rt_shoot, rt_destructor, opts.compare_opts);
	    if (status != RtcmpExitCode::Success)
		return static_cast<int>(status);
	}
	if (opts.performance_run) {
	    if (!do_perf_run("rt", 2, (const char **)av, opts.ncpus, opts.perf_seconds, opts.perf_max_memory, rt_constructor, rt_getbox, rt_getsize, rt_shoot, rt_destructor))
		return 1;
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
