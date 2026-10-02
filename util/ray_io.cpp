#include "util/util.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr char RAY_FILE_HEADER_PREFIX[] = "**rays fired for ";
constexpr char RAY_FILE_COUNT_PREFIX[] = " [";
constexpr char RAY_FILE_HEADER_SUFFIX[] = "]**";
constexpr std::size_t RAY_FILE_HEADER_PREFIX_LENGTH = sizeof(RAY_FILE_HEADER_PREFIX) - 1;
constexpr std::size_t RAY_FILE_COUNT_PREFIX_LENGTH = sizeof(RAY_FILE_COUNT_PREFIX) - 1;
constexpr std::size_t RAY_FILE_HEADER_SUFFIX_LENGTH = sizeof(RAY_FILE_HEADER_SUFFIX) - 1;
constexpr char RAY_POINT_PREFIX[] = "xyz";
constexpr char RAY_DIRECTION_PREFIX[] = "dir";
constexpr std::size_t VECTOR_COMPONENT_COUNT = 3;

bool
fail(std::string& error, const std::string& message)
{
    error = message;
    return false;
}

bool
parse_ray_count(const std::string& header, std::size_t& count)
{
    if (header.compare(0, RAY_FILE_HEADER_PREFIX_LENGTH, RAY_FILE_HEADER_PREFIX) != 0
	|| header.size() < RAY_FILE_HEADER_SUFFIX_LENGTH
	|| header.compare(header.size() - RAY_FILE_HEADER_SUFFIX_LENGTH,
			  RAY_FILE_HEADER_SUFFIX_LENGTH, RAY_FILE_HEADER_SUFFIX) != 0)
	return false;

    const std::size_t count_prefix = header.rfind(RAY_FILE_COUNT_PREFIX);
    const std::size_t count_end = header.size() - RAY_FILE_HEADER_SUFFIX_LENGTH;
    if (count_prefix == std::string::npos || count_prefix < RAY_FILE_HEADER_PREFIX_LENGTH
	|| count_prefix + RAY_FILE_COUNT_PREFIX_LENGTH >= count_end)
	return false;

    const std::size_t count_begin = count_prefix + RAY_FILE_COUNT_PREFIX_LENGTH;
    const char *first = header.data() + count_begin;
    const char *last = header.data() + count_end;
    const std::from_chars_result parsed = std::from_chars(first, last, count);
    return parsed.ec == std::errc() && parsed.ptr == last && count != 0;
}

bool
parse_ray_record(const std::string& line, const char *expected_prefix, double values[3])
{
    std::istringstream input(line);
    input.imbue(std::locale::classic());

    std::string prefix;
    if (!(input >> prefix >> values[0] >> values[1] >> values[2]) || prefix != expected_prefix)
	return false;
    input >> std::ws;
    return input.eof();
}

bool
is_finite_vector(const double values[3])
{
    return std::all_of(values, values + VECTOR_COMPONENT_COUNT,
		       [](double value) { return std::isfinite(value); });
}

bool
validate_rays(const std::vector<xray>& rays, std::string& error)
{
    if (rays.empty())
	return fail(error, "ray file contains no rays");

    for (std::size_t index = 0; index < rays.size(); ++index) {
	const xray& ray = rays[index];
	if (!is_finite_vector(ray.r_pt) || !is_finite_vector(ray.r_dir))
	    return fail(error, "ray " + std::to_string(index + 1) + " contains a non-finite value");
    }
    return true;
}

} // namespace

namespace rtutil {

bool
read_rays(const std::string& filename, std::vector<xray>& rays, std::string& error)
{
    rays.clear();
    error.clear();

    std::ifstream input(filename, std::ios::binary);
    if (!input.is_open())
	return fail(error, "failed to open ray file: " + filename);

    std::string header;
    if (!std::getline(input, header))
	return fail(error, "failed to read ray file header: " + filename);
    if (!header.empty() && header.back() == '\r')
	header.pop_back();

    std::size_t expected_count = 0;
    if (!parse_ray_count(header, expected_count))
	return fail(error, "invalid ray file header: " + filename);

    std::vector<xray> parsed_rays;
    parsed_rays.reserve(expected_count);
    for (std::size_t index = 0; index < expected_count; ++index) {
	std::string point_line;
	std::string direction_line;
	if (!std::getline(input, point_line) || !std::getline(input, direction_line))
	    return fail(error, "ray file ended before its declared count: " + filename);

	xray ray{};
	if (!parse_ray_record(point_line, RAY_POINT_PREFIX, ray.r_pt)
	    || !parse_ray_record(direction_line, RAY_DIRECTION_PREFIX, ray.r_dir))
	    return fail(error, "invalid ray " + std::to_string(index + 1) + " in: " + filename);
	parsed_rays.push_back(ray);
    }

    std::string trailing;
    if (std::getline(input, trailing))
	return fail(error, "ray file contains data after its declared count: " + filename);
    if (input.bad())
	return fail(error, "failed while reading ray file: " + filename);

    if (!validate_rays(parsed_rays, error)) {
	error += " in: " + filename;
	return false;
    }
    rays = std::move(parsed_rays);
    return true;
}

bool
write_rays(const std::string& filename, const std::vector<xray>& rays, std::string& error)
{
    error.clear();
    if (!validate_rays(rays, error)) {
	error += " in: " + filename;
	return false;
    }

    std::ofstream output(filename, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
	return fail(error, "failed to open ray file for writing: " + filename);

    output.imbue(std::locale::classic());
    output << RAY_FILE_HEADER_PREFIX << filename << RAY_FILE_COUNT_PREFIX << rays.size()
	   << RAY_FILE_HEADER_SUFFIX << '\n'
	   << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const xray& ray : rays) {
	output << RAY_POINT_PREFIX << ' ' << ray.r_pt[0] << ' ' << ray.r_pt[1] << ' ' << ray.r_pt[2] << '\n'
	       << RAY_DIRECTION_PREFIX << ' ' << ray.r_dir[0] << ' ' << ray.r_dir[1] << ' ' << ray.r_dir[2] << '\n';
    }
    output.close();
    if (!output)
	return fail(error, "failed while writing ray file: " + filename);
    return true;
}

} // namespace rtutil

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
