#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <brlcad/bu/process.h>

#include "comp/jsonwriter.hpp"
#include "comp/writer_sink.hpp"
#include "comp/grazing.h"
#include "comp/shot_comp.h"
#include "rtcmp.h"
#include "util/util.h"

namespace {

struct PreparedRays {
    rtutil::RaySet rayset;
    rtutil::RayFileStatus status;
    std::size_t requested_count = 0;
    std::size_t rays_per_view = 0;
    std::size_t result_count = 0;
    std::string count_basis;
    std::string generator;
    std::string detail;
};

constexpr std::size_t RAYS_PER_WORK_CLAIM = 64;
// Keep aggregate in-memory serialization bounded while amortizing synchronized writes.
constexpr std::size_t TOTAL_SERIALIZATION_BUFFER_BYTES = 16 * 1024 * 1024;

std::string
temporary_output_path(const std::string& destination, const char *purpose)
{
    static std::atomic<unsigned long> sequence{0};
    return destination + ".rtcmp-" + purpose + '-'
	+ std::to_string(bu_pid()) + '-'
	+ std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
}

bool
emit_status(const std::string& filename, const std::string& json)
{
    if (!filename.empty()) {
	std::ofstream output(filename, std::ios::binary | std::ios::trunc);
	if (!output.is_open())
	    return false;
	output << json << '\n';
	output.close();
	if (!output)
	    return false;
    }
    std::cout << json << '\n';
    return true;
}

bool
remove_stale_file(const std::string& filename, std::string& error)
{
    if (filename.empty())
	return true;
    std::error_code filesystem_error;
    const bool exists = std::filesystem::exists(filename, filesystem_error);
    if (filesystem_error) {
	error = "unable to inspect stale output: " + filename;
	return false;
    }
    if (!exists)
	return true;
    if (!std::filesystem::is_regular_file(filename, filesystem_error) || filesystem_error) {
	error = "stale output is not a removable file: " + filename;
	return false;
    }
    if (!std::filesystem::remove(filename, filesystem_error) || filesystem_error) {
	error = "unable to remove stale output: " + filename;
	return false;
    }
    return true;
}

bool
publish_file(const std::string& temporary, const std::string& destination, std::string& error)
{
    std::error_code filesystem_error;
    std::filesystem::rename(temporary, destination, filesystem_error);
    if (!filesystem_error)
	return true;
    error = "unable to publish output " + destination + ": " + filesystem_error.message();
    return false;
}

bool
validate_result_file(const std::string& filename, const rtutil::RaySet& rayset,
		     bool expect_segments, std::size_t& result_count, std::string& error)
{
    ShotIndex index(filename, rtutil::ray_keys(rayset));
    const ShotCoverage& coverage = index.coverage();
    result_count = coverage.observed_count;
    if (coverage.io_error || coverage.malformed() || coverage.missing_count != 0
	|| coverage.total_records != rayset.rays.size()) {
	error = index.error_message().empty()
	    ? "generated result does not cover the canonical rayset exactly"
	    : index.error_message();
	return false;
    }
    if (index.has_segments() != expect_segments) {
	error = expect_segments ? "primitive capture did not emit segment records"
				: "evaluated capture unexpectedly emitted segment records";
	return false;
    }
    return true;
}

RtcmpExitCode
ray_file_exit(const rtutil::RayFileStatus& status)
{
    return status.error == rtutil::RayFileError::Io ? RtcmpExitCode::Io
						    : RtcmpExitCode::InvalidRayset;
}

void
begin_status_json(std::ostringstream& output, const std::string& operation,
		  const char *state, RtcmpExitCode exit_code)
{
    output.imbue(std::locale::classic());
    output << "{\"operation\":" << rtutil::json_quote(operation)
	   << ",\"state\":" << rtutil::json_quote(state)
	   << ",\"exit_code\":" << static_cast<int>(exit_code);
}

void
append_rayset_error_json(std::ostringstream& output, const rtutil::RayFileStatus& status)
{
    output << ",\"rayset_error\":{\"class\":"
	   << rtutil::json_quote(rtutil::ray_file_error_name(status.error))
	   << ",\"line\":" << status.line
	   << ",\"detail\":" << rtutil::json_quote(status.message) << '}';
}

std::string
ray_file_failure_json(const char *operation, const rtutil::RayFileStatus& status,
		      RtcmpExitCode exit_code)
{
    const char *state = exit_code == RtcmpExitCode::Io ? "io_error" : "invalid_rayset";
    std::ostringstream output;
    begin_status_json(output, operation, state, exit_code);
    append_rayset_error_json(output, status);
    output << '}';
    return output.str();
}

std::string
operation_failure_json(const std::string& operation, const char *state,
		       const std::string& detail, RtcmpExitCode exit_code)
{
    std::ostringstream output;
    begin_status_json(output, operation, state, exit_code);
    output << ",\"detail\":" << rtutil::json_quote(detail) << '}';
    return output.str();
}

std::string
configuration_failure_json(const std::string& operation, const std::string& detail)
{
    return operation_failure_json(operation, "configuration_error", detail, RtcmpExitCode::Usage);
}

std::string
generation_status_json(const PreparedRays& prepared, const char *engine, const char *geometry,
		       const char *object, const char *state, RtcmpExitCode exit_code)
{
    std::ostringstream output;
    begin_status_json(output, "ray_generation", state, exit_code);
    output << ",\"generator\":" << rtutil::json_quote(prepared.generator)
	   << ",\"engine\":" << rtutil::json_quote(engine ? engine : "")
	   << ",\"geometry\":" << rtutil::json_quote(geometry ? geometry : "")
	   << ",\"object\":" << rtutil::json_quote(object ? object : "")
	   << ",\"count_basis\":" << rtutil::json_quote(prepared.count_basis)
	   << ",\"requested_count\":" << prepared.requested_count
	   << ",\"rays_per_view\":" << prepared.rays_per_view
	   << ",\"actual_count\":" << prepared.rayset.rays.size()
	   << ",\"result_count\":" << prepared.result_count
	   << ",\"rayset_hash\":" << rtutil::json_quote(prepared.rayset.content_hash);
    if (!prepared.status.ok())
	append_rayset_error_json(output, prepared.status);
    if (!prepared.detail.empty())
	output << ",\"detail\":" << rtutil::json_quote(prepared.detail);
    output << '}';
    return output.str();
}

PreparedRays
describe_ray_request(const CompareConfig& config)
{
    PreparedRays prepared;
    prepared.generator = config.generator;
    if (!config.in_ray_file.empty()) {
	prepared.count_basis = "input";
    } else if (config.minimum_ray_count != 0) {
	prepared.count_basis = "minimum_total";
	prepared.requested_count = config.minimum_ray_count;
    } else {
	prepared.count_basis = "per_view";
	prepared.requested_count = config.rays_per_view;
    }
    return prepared;
}

PreparedRays
load_or_build_rays(const CompareConfig& config, const point_t center, double radius)
{
    PreparedRays prepared = describe_ray_request(config);
    if (!config.in_ray_file.empty()) {
	prepared.status = rtutil::read_rays(config.in_ray_file, prepared.rayset);
	if (prepared.status.ok())
	    prepared.requested_count = prepared.rayset.rays.size();
	return prepared;
    }

    if (config.minimum_ray_count != 0) {
	rtutil::BalancedRaySet generated =
	    rtutil::build_balanced_view_rays(radius, center, config.minimum_ray_count);
	prepared.rays_per_view = generated.rays_per_view;
	prepared.rayset.rays = std::move(generated.rays);
    } else {
	prepared.rayset.rays = rtutil::build_view_rays(radius, center, config.rays_per_view);
	prepared.rays_per_view = prepared.rayset.rays.empty()
	    ? 0 : prepared.rayset.rays.size() / rtutil::BALANCED_VIEW_COUNT;
    }

    if (prepared.rayset.rays.empty()) {
	prepared.status = {rtutil::RayFileError::InvalidCount, 0,
			   "unable to create the requested rayset"};
	return prepared;
    }
    prepared.status = rtutil::write_rays(config.ray_file, prepared.rayset.rays,
					 prepared.rayset.content_hash);
    return prepared;
}

RtcmpExitCode
correctness_exit(CorrectnessState state)
{
    switch (state) {
	case CorrectnessState::Equivalent: return RtcmpExitCode::Success;
	case CorrectnessState::Different: return RtcmpExitCode::CorrectnessMismatch;
	case CorrectnessState::Malformed:
	case CorrectnessState::Incomplete: return RtcmpExitCode::InvalidCoverage;
	case CorrectnessState::IoError: return RtcmpExitCode::Io;
    }
    return RtcmpExitCode::ExecutionFailure;
}

bool
emit_generation_status(const CompareConfig& config, const PreparedRays& prepared, const char *engine,
		       const char *geometry, const char *object, const char *state,
		       RtcmpExitCode exit_code)
{
    if (emit_status(config.status_file,
		    generation_status_json(prepared, engine, geometry, object, state, exit_code)))
	return true;
    std::cerr << "Unable to write structured status: " << config.status_file << '\n';
    return false;
}

} // namespace

RtcmpExitCode
report_configuration_failure(const CompareConfig& config, const std::string& operation,
			     const std::string& detail)
{
    if (!emit_status(config.status_file, configuration_failure_json(operation, detail)))
	return RtcmpExitCode::Io;
    return RtcmpExitCode::Usage;
}

RtcmpExitCode
do_comp(const char *baseline_file, const char *candidate_file, const CompareConfig& config)
{
    if (!config.nirt_file.empty()
	&& (config.nirt_file == baseline_file || config.nirt_file == candidate_file
	    || config.nirt_file == config.in_ray_file)) {
	return report_configuration_failure(config, "correctness",
	    "difference output must not replace a comparison input");
    }
    if (config.in_ray_file.empty()) {
	return report_configuration_failure(config, "correctness",
	    "correctness comparison requires --input-rays");
    }
    if (!std::isfinite(config.tol) || config.tol < 0.0) {
	return report_configuration_failure(config, "correctness",
	    "correctness tolerance must be finite and nonnegative");
    }
    if (config.grazing_geometry.empty() != config.grazing_object.empty()) {
	return report_configuration_failure(config, "correctness",
	    "--grazing-geometry and --grazing-object must be supplied together");
    }
    if (config.grazing_geometry.empty()
	&& (config.grazing_radius != 0.0
	    || config.grazing_samples != DEFAULT_GRAZING_SAMPLES)) {
	return report_configuration_failure(config, "correctness",
	    "grazing radius and sample options require grazing geometry");
    }
    if (!std::isfinite(config.grazing_radius) || config.grazing_radius < 0.0
	|| config.grazing_samples < MINIMUM_GRAZING_SAMPLES
	|| config.grazing_samples > MAXIMUM_GRAZING_SAMPLES) {
	return report_configuration_failure(config, "correctness",
	    "grazing radius must be nonnegative and samples must be between 3 and 1024");
    }

    std::string output_error;
    if (!remove_stale_file(config.nirt_file, output_error)) {
	if (!emit_status(config.status_file, operation_failure_json(
		"correctness", "io_error", output_error, RtcmpExitCode::Io)))
	    return RtcmpExitCode::Io;
	return RtcmpExitCode::Io;
    }

    rtutil::RaySet rayset;
    const rtutil::RayFileStatus ray_status = rtutil::read_rays(config.in_ray_file, rayset);
    if (!ray_status.ok()) {
	const RtcmpExitCode exit_code = ray_file_exit(ray_status);
	if (!emit_status(config.status_file,
		 ray_file_failure_json("correctness", ray_status, exit_code)))
	    return RtcmpExitCode::Io;
	return exit_code;
    }

    std::unique_ptr<GrazingDetector> grazing_detector;
    if (!config.grazing_geometry.empty()) {
	grazing_detector = std::make_unique<GrazingDetector>(config.grazing_geometry,
	    config.grazing_object, config.grazing_radius, config.grazing_samples);
	if (!grazing_detector->valid()) {
	    if (!emit_status(config.status_file, operation_failure_json(
		    "correctness", "scene_setup_error", grazing_detector->error(),
		    RtcmpExitCode::SceneSetup)))
		return RtcmpExitCode::Io;
	    return RtcmpExitCode::SceneSetup;
	}
    }

    CorrectnessComparison comparison(rayset, baseline_file, candidate_file, config.tol);
    if (grazing_detector) {
	comparison.filter_grazing([&grazing_detector](
		const Shot::Ray& ray, const std::vector<std::string>& regions) {
	    return grazing_detector->unstable(ray, regions);
	});
	std::cout << "Grazing probe radius: " << grazing_detector->radius() << " mm ("
		  << grazing_detector->sampleCount() << " nearby rays)\n";
    }
    CorrectnessResult result = comparison.result();
    RtcmpExitCode exit_code = correctness_exit(result.state);
    if (result.state == CorrectnessState::Different
	&& !comparison.write_differences(config.nirt_file)) {
	result.state = CorrectnessState::IoError;
	result.detail = "unable to write difference rays: " + config.nirt_file;
	exit_code = RtcmpExitCode::Io;
    }
    comparison.summary(std::cout, config.nirt_file);

    const std::string status = correctness_status_json(
	result, rayset.content_hash, static_cast<int>(exit_code));
    if (!emit_status(config.status_file, status)) {
	std::cerr << "Unable to write structured status: " << config.status_file << '\n';
	return RtcmpExitCode::Io;
    }
    return exit_code;
}

RtcmpExitCode
do_diff_run(const char *prefix, int argc, const char **argv, int nthreads,
	void *(*constructor) (const char *, int, const char **),
	int (*getbox) (void *, point_t *, point_t *),
	double (*getsize) (void *),
	void (*shoot) (void *, struct xray *ray),
	int (*destructor) (void *),
	CompareConfig& config)
{
    PreparedRays prepared = describe_ray_request(config);
    if (config.json_ofile.empty()
	|| (!config.in_ray_file.empty() && config.json_ofile == config.in_ray_file)
	|| (config.in_ray_file.empty() && config.json_ofile == config.ray_file)) {
	return report_configuration_failure(config, "ray_generation",
	    "result, input-ray, and generated-ray paths must be nonempty and distinct");
    }

    std::string output_error;
    if (!remove_stale_file(config.json_ofile, output_error)
	|| (config.in_ray_file.empty() && !remove_stale_file(config.ray_file, output_error))) {
	prepared.detail = output_error;
	if (!emit_generation_status(config, prepared, prefix, argv[0], argv[1],
		"io_error", RtcmpExitCode::Io))
	    return RtcmpExitCode::Io;
	return RtcmpExitCode::Io;
    }

    nthreads = nthreads == 0 ? bu_avail_cpus() : nthreads;
    if (nthreads < 1)
	nthreads = 1;

    void *base_instance = constructor(*argv, argc - 1, argv + 1);
    if (base_instance == nullptr) {
	if (!emit_generation_status(config, prepared, prefix, argv[0], argv[1],
				    "scene_setup_error", RtcmpExitCode::SceneSetup))
	    return RtcmpExitCode::Io;
	return RtcmpExitCode::SceneSetup;
    }

    const double radius = getsize(base_instance);
    point_t bounding_box[3];
    if (getbox(base_instance, bounding_box, bounding_box + 1) != 0
	|| !std::isfinite(radius) || radius <= 0.0) {
	destructor(base_instance);
	if (!emit_generation_status(config, prepared, prefix, argv[0], argv[1],
				    "scene_setup_error", RtcmpExitCode::SceneSetup))
	    return RtcmpExitCode::Io;
	return RtcmpExitCode::SceneSetup;
    }
    VADD2SCALE(bounding_box[2], bounding_box[0], bounding_box[1], 0.5);

    prepared = load_or_build_rays(config, bounding_box[2], radius);
    if (!prepared.status.ok()) {
	destructor(base_instance);
	const RtcmpExitCode exit_code = ray_file_exit(prepared.status);
	const char *state = exit_code == RtcmpExitCode::Io ? "io_error" : "invalid_rayset";
	if (!emit_generation_status(config, prepared, prefix, argv[0], argv[1], state, exit_code))
	    return RtcmpExitCode::Io;
	return exit_code;
    }
    const std::size_t maximum_threads = std::min(
	prepared.rayset.rays.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    nthreads = std::min(nthreads, static_cast<int>(maximum_threads));
    nthreads = std::min(nthreads, MAX_PSW);
    nthreads = std::min(nthreads, static_cast<int>(
	TOTAL_SERIALIZATION_BUFFER_BYTES / tsj::Writer::MINIMUM_FLUSH_BYTES));

    struct application *base_application = static_cast<struct application *>(base_instance);
    WriterSink sink;
    base_application->a_uptr = &sink;
    base_application->a_resource = static_cast<struct resource *>(
	bu_calloc(1, sizeof(struct resource), "resource"));
    rt_init_resource(base_application->a_resource, 0, base_application->a_rt_i);

    std::vector<application> applications;
    applications.reserve(nthreads);
    applications.emplace_back(*base_application);
    for (int thread = 1; thread < nthreads; ++thread) {
	application thread_application = *base_application;
	thread_application.a_resource = static_cast<struct resource *>(
	    bu_calloc(1, sizeof(struct resource), "resource"));
	rt_init_resource(thread_application.a_resource, thread, thread_application.a_rt_i);
	applications.emplace_back(thread_application);
    }

    // librt's CaptureWriter singleton holds LIBRT_RTCMP_FILE open via std::fopen
    // until process exit, which prevents renaming an open temporary file on Windows.
    const std::string working_output = config.primitive_hits
	? config.json_ofile : temporary_output_path(config.json_ofile, "results");
    RtcmpExitCode run_status = RtcmpExitCode::Success;
    const char *run_state = "complete";
    bool primitive_capture_enabled = false;
    unsigned int previous_debug = rt_debug;

    if (config.primitive_hits) {
#ifdef RT_RTCMP_CAPTURE_RUNTIME_TOGGLE
	std::ofstream capture_output(working_output, std::ios::binary | std::ios::trunc);
	if (!capture_output.is_open()) {
	    prepared.detail = "unable to create primitive capture output: " + working_output;
	    run_status = RtcmpExitCode::Io;
	    run_state = "io_error";
	} else {
	    capture_output.close();
	    if (!capture_output) {
		prepared.detail = "unable to initialize primitive capture output: " + working_output;
		run_status = RtcmpExitCode::Io;
		run_state = "io_error";
	    }
	}
	if (run_status == RtcmpExitCode::Success
	    && (bu_setenv("LIBRT_RTCMP_FILE", working_output.c_str(), 1) != 0
		|| bu_setenv("LIBRT_RTCMP_PRIMITIVES", "1", 1) != 0
		|| bu_setenv("LIBRT_RTCMP_SKIP_MISSES", "0", 1) != 0)) {
	    prepared.detail = "unable to configure primitive capture";
	    run_status = RtcmpExitCode::ExecutionFailure;
	    run_state = "execution_error";
	}
	if (run_status == RtcmpExitCode::Success) {
	    rt_debug |= RT_DEBUG_RTCMP;
	    if (rt_rtcmp_capture_set_enabled(1) != 0) {
		prepared.detail = "unable to enable primitive capture";
		run_status = RtcmpExitCode::ExecutionFailure;
		run_state = "execution_error";
		rt_debug = previous_debug;
	    } else {
		primitive_capture_enabled = true;
		for (application& app : applications) {
		    app.a_hit = [](struct application *, struct partition *, struct seg *) {
			return 0;
		    };
		    app.a_miss = [](struct application *) { return 0; };
		}
	    }
	}
#else
	prepared.detail = "the linked BRL-CAD build does not support primitive capture";
	run_status = RtcmpExitCode::ExecutionFailure;
	run_state = "execution_error";
#endif
    } else if (!tsj::Writer::Collector::open(working_output)) {
	prepared.detail = "unable to create result output: " + working_output;
	run_status = RtcmpExitCode::Io;
	run_state = "io_error";
    }

    struct ThreadArgs {
	std::vector<application>& applications;
	std::vector<xray>& rays;
	bool primitive_hits;
	std::size_t writer_flush_bytes;
	void (*shoot)(void *, struct xray *);
	std::atomic<std::size_t> next_application{0};
	std::atomic<std::size_t> next_ray{0};
	std::atomic<std::size_t> completed{0};
	std::atomic<bool> failed{false};
	std::mutex error_mutex;
	std::string error;

	void fail(const std::string& detail)
	{
	    {
		std::lock_guard<std::mutex> guard(error_mutex);
		if (error.empty())
		    error = detail;
	    }
	    failed.store(true, std::memory_order_release);
	}
    } thread_args {applications, prepared.rayset.rays, config.primitive_hits,
		   std::max<std::size_t>(1, TOTAL_SERIALIZATION_BUFFER_BYTES
		       / static_cast<std::size_t>(nthreads)), shoot};

    auto worker = [](int, void *data) {
	ThreadArgs *args = static_cast<ThreadArgs *>(data);
	const std::size_t application_index = args->next_application.fetch_add(
	    1, std::memory_order_relaxed);
	if (application_index >= args->applications.size()) {
	    args->fail("parallel backend invoked more workers than requested");
	    return;
	}

	tsj::Writer& writer = tsj::Writer::instance();
	if (!args->primitive_hits)
	    writer.reset(args->writer_flush_bytes);
	try {
	    while (!args->failed.load(std::memory_order_acquire)) {
		const std::size_t begin = args->next_ray.fetch_add(
		    RAYS_PER_WORK_CLAIM, std::memory_order_relaxed);
		if (begin >= args->rays.size())
		    break;
		const std::size_t end = std::min(begin + RAYS_PER_WORK_CLAIM, args->rays.size());
		for (std::size_t index = begin; index < end; ++index) {
		    if (args->primitive_hits) {
			application& app = args->applications[application_index];
			VMOVE(app.a_ray.r_pt, args->rays[index].r_pt);
			VMOVE(app.a_ray.r_dir, args->rays[index].r_dir);
			rt_shootray(&app);
		    } else {
			args->shoot(&args->applications[application_index], &args->rays[index]);
			if (writer.failed()) {
			    args->fail(writer.error().empty()
				? "writing raytrace results failed" : writer.error());
			    break;
			}
		    }
		    args->completed.fetch_add(1, std::memory_order_relaxed);
		}
	    }
	    if (!args->primitive_hits && !args->failed.load(std::memory_order_acquire)
		&& !writer.syncToGlobal()) {
		args->fail(writer.error().empty()
		    ? "writing raytrace results failed" : writer.error());
	    }
	} catch (const std::exception& exception) {
	    if (!args->primitive_hits)
		writer.abandonShot();
	    args->fail(exception.what());
	} catch (...) {
	    if (!args->primitive_hits)
		writer.abandonShot();
	    args->fail("unknown failure while shooting rays");
	}
    };

    if (run_status == RtcmpExitCode::Success) {
	if (nthreads == 1)
	    worker(0, &thread_args);
	else
	    bu_parallel(worker, nthreads, &thread_args);
	if (thread_args.failed.load(std::memory_order_acquire)
	    || thread_args.completed.load(std::memory_order_relaxed) != prepared.rayset.rays.size()) {
	    prepared.detail = thread_args.error.empty()
		? "parallel workers did not process every canonical ray exactly once"
		: thread_args.error;
	    run_status = RtcmpExitCode::ExecutionFailure;
	    run_state = "execution_error";
	}
    }

    if (config.primitive_hits) {
#ifdef RT_RTCMP_CAPTURE_RUNTIME_TOGGLE
	if (primitive_capture_enabled) {
	    const int disable_status = rt_rtcmp_capture_set_enabled(0);
	    const int flush_status = rt_rtcmp_capture_flush();
	    if (disable_status != 0 || flush_status != 0) {
		prepared.detail = "primitive capture did not flush successfully";
		run_status = RtcmpExitCode::ExecutionFailure;
		run_state = "execution_error";
	    }
	    rt_debug = previous_debug;
	}
#endif
    } else if (!tsj::Writer::Collector::close()) {
	prepared.detail = "unable to flush result output";
	run_status = RtcmpExitCode::Io;
	run_state = "io_error";
    }

    const int destructor_status = destructor(base_instance);
    for (application& app : applications) {
	bu_free(app.a_resource, "resource");
	app.a_resource = nullptr;
    }
    if (destructor_status != 0) {
	prepared.detail = "raytrace teardown failed";
	run_status = RtcmpExitCode::ExecutionFailure;
	run_state = "execution_error";
    }

    if (run_status == RtcmpExitCode::Success
	&& !validate_result_file(working_output, prepared.rayset, config.primitive_hits,
				 prepared.result_count, prepared.detail)) {
	run_status = RtcmpExitCode::ExecutionFailure;
	run_state = "execution_error";
    }

    if (run_status == RtcmpExitCode::Success && !config.primitive_hits
	&& !publish_file(working_output, config.json_ofile, prepared.detail)) {
	run_status = RtcmpExitCode::Io;
	run_state = "io_error";
    }

    if (run_status != RtcmpExitCode::Success) {
	std::string ignored;
	if (!remove_stale_file(config.json_ofile, ignored)) {
	    std::ofstream truncate_stale(config.json_ofile, std::ios::binary | std::ios::trunc);
	}
    }
    if (!config.primitive_hits) {
	std::string ignored;
	remove_stale_file(working_output, ignored);
    }

    if (!emit_generation_status(config, prepared, prefix, argv[0], argv[1],
			      run_state, run_status))
	return RtcmpExitCode::Io;
    return run_status;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
