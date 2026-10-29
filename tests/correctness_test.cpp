#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include "comp/shot_comp.h"
#include "rtcmp.h"
#include "util/numeric.h"
#include "util/util.h"

namespace {

class TestDirectory {
public:
    TestDirectory()
    {
	const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	path = std::filesystem::temp_directory_path() / ("rtcmp-correctness-test-" + std::to_string(unique));
	std::filesystem::create_directories(path);
    }

    ~TestDirectory()
    {
	std::error_code error;
	std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

struct PartitionFixture {
    std::string region;
    double in_distance = 1.0;
    double out_distance = 2.0;
};

struct SegmentFixture {
    std::string primitive = "/primitive.s";
    double in_distance = 1.0;
    double out_distance = 2.0;
    int in_surface = 1;
    int out_surface = 2;
    bool geometry_valid = true;
};

int failures = 0;

void
check(bool condition, const std::string& message)
{
    if (condition)
	return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

xray
make_ray(double px, double py, double pz, double dx, double dy, double dz)
{
    xray ray{};
    VSET(ray.r_pt, px, py, pz);
    VSET(ray.r_dir, dx, dy, dz);
    return ray;
}

std::string
number(double value)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return output.str();
}

std::string
vector_json(const double values[3])
{
    return "{\"X\":\"" + number(values[0]) + "\",\"Y\":\"" + number(values[1])
	+ "\",\"Z\":\"" + number(values[2]) + "\"}";
}

std::string
shot_json(const xray& ray, const PartitionFixture *partition = nullptr,
	  const SegmentFixture *segment = nullptr, bool include_segments = false)
{
    std::ostringstream output;
    output << "{\"partitions\":[";
    if (partition != nullptr) {
	const double in_normal[3] = {0.0, 0.0, -1.0};
	const double in_point[3] = {
	    std::isinf(partition->in_distance) ? partition->in_distance : ray.r_pt[0],
	    std::isinf(partition->in_distance) ? partition->in_distance : ray.r_pt[1],
	    std::isinf(partition->in_distance) ? partition->in_distance
						 : ray.r_pt[2] + partition->in_distance
	};
	const double out_normal[3] = {0.0, 0.0, 1.0};
	const double out_point[3] = {
	    std::isinf(partition->out_distance) ? partition->out_distance : ray.r_pt[0],
	    std::isinf(partition->out_distance) ? partition->out_distance : ray.r_pt[1],
	    std::isinf(partition->out_distance) ? partition->out_distance
						  : ray.r_pt[2] + partition->out_distance
	};
	output << "{\"in_dist\":\"" << number(partition->in_distance)
	       << "\",\"in_norm\":" << vector_json(in_normal)
	       << ",\"in_pt\":" << vector_json(in_point)
	       << ",\"out_dist\":\"" << number(partition->out_distance)
	       << "\",\"out_norm\":" << vector_json(out_normal)
	       << ",\"out_pt\":" << vector_json(out_point)
	       << ",\"region\":" << rtutil::json_quote(partition->region) << '}';
    }
    output << ']';
    if (include_segments) {
	output << ",\"segments\":[";
	if (segment != nullptr) {
	    const double identity[ELEMENTS_PER_MAT] = MAT_INIT_IDN;
	    const double in_normal[3] = {0.0, 0.0, -1.0};
	    const double out_normal[3] = {0.0, 0.0, 1.0};
	    const double in_point[3] = {ray.r_pt[0], ray.r_pt[1], ray.r_pt[2] + segment->in_distance};
	    const double out_point[3] = {ray.r_pt[0], ray.r_pt[1], ray.r_pt[2] + segment->out_distance};
	    output << "{\"primitive\":" << rtutil::json_quote(segment->primitive)
		   << ",\"transform\":[";
	    for (std::size_t index = 0; index < ELEMENTS_PER_MAT; ++index) {
		if (index != 0)
		    output << ',';
		output << '"' << number(identity[index]) << '"';
	    }
	    output << "],\"in_dist\":\"" << number(segment->in_distance) << "\",\"in_norm\":";
	    if (segment->geometry_valid)
		output << vector_json(in_normal) << ",\"in_pt\":" << vector_json(in_point);
	    else
		output << "null,\"in_pt\":null";
	    output << ",\"in_surfno\":" << segment->in_surface
		   << ",\"out_dist\":\"" << number(segment->out_distance) << "\",\"out_norm\":";
	    if (segment->geometry_valid)
		output << vector_json(out_normal) << ",\"out_pt\":" << vector_json(out_point);
	    else
		output << "null,\"out_pt\":null";
	    output << ",\"out_surfno\":" << segment->out_surface << '}';
	}
	output << ']';
    }
    output << ",\"ray_dir\":" << vector_json(ray.r_dir)
	   << ",\"ray_pt\":" << vector_json(ray.r_pt) << '}';
    return output.str();
}

void
write_lines(const std::filesystem::path& path, const std::vector<std::string>& lines)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    for (const std::string& line : lines)
	output << line << '\n';
}

std::string
read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

CorrectnessResult
compare(const rtutil::RaySet& rayset, const std::filesystem::path& baseline,
	const std::filesystem::path& candidate, double tolerance)
{
    CorrectnessComparison comparison(rayset, baseline.string(), candidate.string(), tolerance, 1);
    return comparison.result();
}

} // namespace

int
main()
{
    TestDirectory temporary;
    const xray first = make_ray(1.0, 2.0, 3.0, 0.0, 0.0, 1.0);
    const xray second = make_ray(-2.0, 4.0, 1.0, 1.0, 0.0, 0.0);
    const xray unknown = make_ray(9.0, 9.0, 9.0, 0.0, 1.0, 0.0);
    const std::vector<xray> rays = {first, second};
    const std::filesystem::path rayset_path = temporary.path / "canonical.rays";
    rtutil::RaySet rayset;
    check(rtutil::write_rays(rayset_path.string(), rays, rayset.content_hash).ok(),
	  "correctness fixture rayset writes successfully");
    rayset.rays = rays;

    const std::filesystem::path baseline_path = temporary.path / "baseline.json";
    const std::filesystem::path candidate_path = temporary.path / "candidate.json";
    const PartitionFixture baseline_partition = {"region", 1.0};
    const std::string baseline_first = shot_json(first, &baseline_partition);
    const std::string baseline_second = shot_json(second);
    write_lines(baseline_path, {baseline_first, baseline_second});
    write_lines(candidate_path, {baseline_second, baseline_first});

    const PartitionFixture escaped_region = {"quote\" slash\\ newline\n", 1.0};
    Shot parsed_shot;
    std::string parse_error;
    check(shot_utils::parse_json_shot(shot_json(first, &escaped_region), parsed_shot, parse_error)
	      && parsed_shot.parts.size() == 1
	      && parsed_shot.parts[0].region == escaped_region.region,
	  "result parser round-trips escaped region names");

    CorrectnessResult result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Equivalent,
	  "equivalent results pass regardless of global result order");
    check(result.baseline.observed_count == rays.size()
	      && result.candidate.observed_count == rays.size(),
	  "equivalent results account for every canonical ray");

    result = compare(rayset, baseline_path, candidate_path, 0.0);
    check(result.state == CorrectnessState::Equivalent,
	  "zero tolerance accepts identical finite results");

    const PartitionFixture within_tolerance = {"region", 1.0005};
    write_lines(candidate_path, {shot_json(first, &within_tolerance), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Equivalent,
	  "numeric differences within the configured tolerance pass");

    const PartitionFixture numeric_difference = {"region", 1.01};
    write_lines(candidate_path, {shot_json(first, &numeric_difference), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Different && result.mismatches.numeric == 1,
	  "numeric differences outside tolerance are classified");

    const PartitionFixture region_difference = {"other-region", 1.0};
    write_lines(candidate_path, {shot_json(first, &region_difference), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Different && result.mismatches.region == 1,
	  "region differences remain exact and are classified");

    write_lines(candidate_path, {shot_json(first), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Different && result.mismatches.partition_count == 1,
	  "partition counts remain exact and are classified");

    CorrectnessComparison grazing_comparison(
	rayset, baseline_path.string(), candidate_path.string(), 0.001, 1);
    bool grazing_checked = false;
    grazing_comparison.filter_grazing([&](
	    const Shot::Ray& ray, const std::vector<std::string>& regions) {
	grazing_checked = rtcmp_numeric::vector_equal(ray.pt, first.r_pt, 0.0)
	    && regions.size() == 1 && regions.front() == baseline_partition.region;
	return true;
    });
    check(grazing_checked
	      && grazing_comparison.result().state == CorrectnessState::Equivalent
	      && grazing_comparison.result().mismatches.filtered_grazing == 1,
	  "grazing classification removes only the implicated partition-count finding");

    const PartitionFixture negative_unbounded = {
	"halfspace", -std::numeric_limits<double>::infinity(), 2.0};
    write_lines(baseline_path, {shot_json(first, &negative_unbounded), baseline_second});
    write_lines(candidate_path, {shot_json(first, &negative_unbounded), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.0);
    check(result.state == CorrectnessState::Equivalent,
	  "negative unbounded halfspace entries round-trip at zero tolerance");

    const PartitionFixture positive_unbounded = {
	"halfspace", 1.0, std::numeric_limits<double>::infinity()};
    write_lines(baseline_path, {shot_json(first, &positive_unbounded), baseline_second});
    write_lines(candidate_path, {shot_json(first, &positive_unbounded), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.0);
    check(result.state == CorrectnessState::Equivalent,
	  "positive unbounded halfspace exits round-trip at zero tolerance");

    const PartitionFixture both_unbounded = {
	"halfspace", -std::numeric_limits<double>::infinity(),
	std::numeric_limits<double>::infinity()};
    write_lines(baseline_path, {shot_json(first, &both_unbounded), baseline_second});
    write_lines(candidate_path, {shot_json(first, &both_unbounded), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.0);
    check(result.state == CorrectnessState::Equivalent,
	  "doubly unbounded halfspace partitions round-trip");

    write_lines(candidate_path, {"not-json", baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Malformed
	      && result.candidate.malformed_count == 1
	      && result.candidate.missing_count == 1,
	  "malformed records fail without hiding missing coverage");

    const PartitionFixture non_finite = {"region", std::numeric_limits<double>::infinity()};
    write_lines(candidate_path, {shot_json(first, &non_finite), baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Malformed && result.candidate.malformed_count == 1,
	  "non-finite correctness values are malformed");

    std::string invalid_partial_sentinel = shot_json(first, &negative_unbounded);
    const std::string sentinel_component = "\"X\":\"-inf\"";
    invalid_partial_sentinel.replace(invalid_partial_sentinel.find(sentinel_component),
	 sentinel_component.size(), "\"X\":\"0\"");
    write_lines(candidate_path, {invalid_partial_sentinel, baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Malformed,
	  "partially unbounded endpoint vectors are malformed");

    const SegmentFixture segment;
    const std::string primitive_first = shot_json(first, &baseline_partition, &segment, true);
    const std::string primitive_second = shot_json(second, nullptr, nullptr, true);
    write_lines(baseline_path, {primitive_first, primitive_second});
    write_lines(candidate_path, {primitive_second, primitive_first});
    result = compare(rayset, baseline_path, candidate_path, 0.0);
    check(result.state == CorrectnessState::Equivalent,
	  "primitive segment results compare independently of global ray order");

    SegmentFixture invalid_geometry_segment = segment;
    invalid_geometry_segment.geometry_valid = false;
    invalid_geometry_segment.in_distance = std::numeric_limits<double>::quiet_NaN();
    const std::string invalid_geometry_first =
	shot_json(first, &baseline_partition, &invalid_geometry_segment, true);
    write_lines(baseline_path, {invalid_geometry_first, primitive_second});
    write_lines(candidate_path, {primitive_second, invalid_geometry_first});
    result = compare(rayset, baseline_path, candidate_path, 0.0);
    check(result.state == CorrectnessState::Equivalent,
	  "identical unevaluable primitive segments compare without hiding their null geometry");

    write_lines(baseline_path, {primitive_first, primitive_second});
    SegmentFixture changed_segment = segment;
    changed_segment.in_distance = 1.25;
    write_lines(candidate_path,
	{shot_json(first, &baseline_partition, &changed_segment, true), primitive_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Different
	      && result.mismatches.segment_numeric == 1,
	  "primitive numeric differences are classified");

    CorrectnessComparison primitive_grazing_comparison(
	rayset, baseline_path.string(), candidate_path.string(), 0.001, 1);
    bool primitive_grazing_checked = false;
    primitive_grazing_comparison.filter_grazing([&](
	    const Shot::Ray&, const std::vector<std::string>&) {
	primitive_grazing_checked = true;
	return true;
    });
    check(!primitive_grazing_checked
	      && primitive_grazing_comparison.result().state == CorrectnessState::Different,
	  "grazing filtering never hides primitive-segment findings");

    changed_segment = segment;
    changed_segment.primitive = "/different.s";
    write_lines(candidate_path,
	{shot_json(first, &baseline_partition, &changed_segment, true), primitive_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Different && result.mismatches.primitive == 1,
	  "primitive identity differences are classified");

    write_lines(baseline_path, {baseline_first, baseline_second});
    write_lines(candidate_path, {primitive_first, primitive_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Malformed,
	  "mixed evaluated-only and primitive result schemas are rejected");

    write_lines(candidate_path, {baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Incomplete && result.candidate.missing_count == 1,
	  "missing canonical results are incomplete");

    write_lines(candidate_path, {baseline_first, baseline_first, baseline_second});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Malformed && result.candidate.duplicate_count == 1,
	  "duplicate canonical results are malformed");

    write_lines(candidate_path, {baseline_first, baseline_second, shot_json(unknown)});
    result = compare(rayset, baseline_path, candidate_path, 0.001);
    check(result.state == CorrectnessState::Malformed && result.candidate.unknown_count == 1,
	  "results outside the canonical rayset are malformed");

    result = compare(rayset, temporary.path / "missing.json", candidate_path, 0.001);
    check(result.state == CorrectnessState::IoError && result.baseline.io_error,
	  "unreadable result files are I/O failures");

    CompareConfig config;
    config.in_ray_file = rayset_path.string();
    config.nirt_file = (temporary.path / "differences.rays").string();
    config.status_file = (temporary.path / "status.json").string();
    config.tol = 0.001;

    write_lines(candidate_path, {baseline_second, baseline_first});
    write_lines(config.nirt_file, {"stale difference data"});
    check(do_comp(baseline_path.string().c_str(), candidate_path.string().c_str(), config)
	      == RtcmpExitCode::Success,
	  "equivalent correctness exits successfully");
    check(!std::filesystem::exists(config.nirt_file),
	  "equivalent correctness removes stale difference output");
    check(read_text(config.status_file).find("\"state\":\"equivalent\"") != std::string::npos,
	  "equivalent correctness writes structured status");

    write_lines(candidate_path, {shot_json(first, &numeric_difference), baseline_second});
    check(do_comp(baseline_path.string().c_str(), candidate_path.string().c_str(), config)
	      == RtcmpExitCode::CorrectnessMismatch,
	  "different correctness has a distinct exit");
    check(read_text(config.status_file).find("\"exit_code\":6") != std::string::npos,
	  "difference status records its exit code");

    write_lines(config.nirt_file, {"stale difference data"});
    write_lines(candidate_path, {"not-json", baseline_second});
    check(do_comp(baseline_path.string().c_str(), candidate_path.string().c_str(), config)
	      == RtcmpExitCode::InvalidCoverage,
	  "malformed correctness coverage has a distinct exit");
    check(!std::filesystem::exists(config.nirt_file),
	  "failed correctness removes stale difference output");
    check(read_text(config.status_file).find("\"state\":\"malformed\"") != std::string::npos,
	  "malformed correctness writes structured status");

    write_lines(candidate_path, {baseline_second});
    check(do_comp(baseline_path.string().c_str(), candidate_path.string().c_str(), config)
	      == RtcmpExitCode::InvalidCoverage,
	  "incomplete correctness coverage has a distinct exit");

    check(do_comp((temporary.path / "missing.json").string().c_str(),
		  candidate_path.string().c_str(), config) == RtcmpExitCode::Io,
	  "result I/O failure has a distinct exit");

    const std::filesystem::path invalid_rayset = temporary.path / "invalid.rays";
    write_lines(invalid_rayset, {"invalid header"});
    config.in_ray_file = invalid_rayset.string();
    check(do_comp(baseline_path.string().c_str(), candidate_path.string().c_str(), config)
	      == RtcmpExitCode::InvalidRayset,
	  "invalid canonical rayset has a distinct exit");

    config.in_ray_file.clear();
    write_lines(config.nirt_file, {"existing difference data"});
    check(do_comp(baseline_path.string().c_str(), candidate_path.string().c_str(), config)
	      == RtcmpExitCode::Usage,
	  "missing canonical rayset is a usage error");
    check(std::filesystem::exists(config.nirt_file),
	  "usage error does not delete an existing difference output file");

    config.in_ray_file = rayset_path.string();
    config.tol = -1.0;
    check(do_comp(baseline_path.string().c_str(), candidate_path.string().c_str(), config)
	      == RtcmpExitCode::Usage,
	  "invalid correctness tolerance is a usage error");
    check(std::filesystem::exists(config.nirt_file),
	  "tolerance usage error does not delete an existing difference output file");

    if (failures != 0) {
	std::cerr << failures << " correctness test(s) failed\n";
	return 1;
    }
    std::cout << "correctness tests passed\n";
    return 0;
}
