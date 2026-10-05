#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <brlcad/bu.h>
#include <brlcad/raytrace.h>

#include "util/util.h"

namespace {

int failures = 0;

void
check(bool condition, const char *message)
{
    if (condition)
	return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool
same_ray(const xray& first, const xray& second)
{
    for (std::size_t component = 0; component < 3; ++component) {
	if (first.r_pt[component] != second.r_pt[component]
	    || first.r_dir[component] != second.r_dir[component])
	    return false;
    }
    return true;
}

void
test_ray_file_round_trip(const std::filesystem::path& path)
{
    xray first{};
    first.r_pt[0] = 1.25;
    first.r_pt[1] = -2.5;
    first.r_pt[2] = 3.75;
    first.r_dir[0] = 0.0;
    first.r_dir[1] = 1.0;
    first.r_dir[2] = 0.0;

    xray second{};
    second.r_pt[0] = -4.5;
    second.r_pt[1] = 5.25;
    second.r_pt[2] = 6.0;
    second.r_dir[0] = 1.0;
    second.r_dir[1] = 0.0;
    second.r_dir[2] = 0.0;

    const std::vector<xray> expected{first, second};
    std::string error;
    const bool wrote_rays = rtutil::write_rays(path.string(), expected, error);
    check(wrote_rays, error.c_str());

    std::vector<xray> actual;
    const bool read_rays = rtutil::read_rays(path.string(), actual, error);
    check(read_rays, error.c_str());
    check(actual.size() == expected.size(), "ray-file round trip changed the ray count");
    if (actual.size() == expected.size()) {
	for (std::size_t index = 0; index < expected.size(); ++index)
	    check(same_ray(actual[index], expected[index]), "ray-file round trip changed a ray");
    }
}

} // namespace

int
main()
{
    const std::filesystem::path test_directory = std::filesystem::temp_directory_path()
	/ ("rtcmp-ray-io-test-" + std::to_string(bu_pid()));
    std::filesystem::create_directory(test_directory);

    test_ray_file_round_trip(test_directory / "rays.rays");

    std::error_code ignored_error;
    std::filesystem::remove_all(test_directory, ignored_error);
    return failures == 0 ? 0 : 1;
}
