#include <chrono>
#include <cfenv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <string>
#include <vector>

#include <brlcad/raytrace.h>

#include "util/util.h"

namespace {

class TestDirectory {
public:
    TestDirectory()
    {
	const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	path = std::filesystem::temp_directory_path() / ("rtcmp-rayset-test-" + std::to_string(unique));
	std::filesystem::create_directories(path);
    }

    ~TestDirectory()
    {
	std::error_code error;
	std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

int failures = 0;

class GroupingFacet : public std::numpunct<char> {
protected:
    char do_thousands_sep() const override { return '_'; }
    std::string do_grouping() const override { return "\1"; }
};

void
check(bool condition, const std::string& message)
{
    if (condition)
	return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void
write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

void
expect_error(const std::filesystem::path& directory, const std::string& name,
	     const std::string& contents, rtutil::RayFileError expected)
{
    const std::filesystem::path path = directory / (name + ".rays");
    write_text(path, contents);
    rtutil::RaySet rayset;
    const rtutil::RayFileStatus status = rtutil::read_rays(path.string(), rayset);
    check(status.error == expected,
	  name + " returned " + rtutil::ray_file_error_name(status.error)
	      + " instead of " + rtutil::ray_file_error_name(expected));
}

xray
make_ray(double px, double py, double pz, double dx, double dy, double dz)
{
    xray ray{};
    VSET(ray.r_pt, px, py, pz);
    VSET(ray.r_dir, dx, dy, dz);
    return ray;
}

} // namespace

int
main()
{
    TestDirectory temporary;
    const xray first = make_ray(1.25, -2.5, 3.75, 0.0, 1.0, 0.0);
    const xray second = make_ray(-4.5, 5.25, 6.0, 1.0, 0.0, 0.0);
    const std::vector<xray> rays = {first, second};

    const std::filesystem::path roundtrip_path = temporary.path / "roundtrip.rays";
    std::string written_hash;
    const rtutil::RayFileStatus write_status =
	rtutil::write_rays(roundtrip_path.string(), rays, written_hash);
    check(write_status.ok(), "strict rayset write succeeds");
    constexpr char HASH_PREFIX[] = "rtcmp-rayset-v1-sha256:";
    check(written_hash.rfind(HASH_PREFIX, 0) == 0
	      && written_hash.size() == sizeof(HASH_PREFIX) - 1 + 64,
	  "rayset identity is a versioned SHA-256 digest");
    check(written_hash == "rtcmp-rayset-v1-sha256:0f980ebcffeb7b94ab18525e8128846821413b744fee33d3860aa9cb7d12f95d",
	  "rayset identity matches an independently checked SHA-256 fixture");
    check(written_hash == rtutil::ray_content_hash(rays), "rayset hashing is deterministic");
    check(written_hash != rtutil::ray_content_hash({second, first}),
	  "rayset identity includes canonical order");

    const int previous_rounding = std::fegetround();
    const std::locale previous_locale = std::locale();
    std::locale::global(std::locale(previous_locale, new GroupingFacet));
    std::fesetround(FE_DOWNWARD);
    check(written_hash == rtutil::ray_content_hash(rays),
	  "rayset identity is independent of locale and floating-point rounding mode");
    const std::filesystem::path ambient_path = temporary.path / "ambient-state.rays";
    std::string ambient_hash;
    rtutil::RaySet ambient_roundtrip;
    check(rtutil::write_rays(ambient_path.string(), rays, ambient_hash).ok()
	      && rtutil::read_rays(ambient_path.string(), ambient_roundtrip).ok()
	      && ambient_hash == written_hash && ambient_roundtrip.content_hash == written_hash,
	  "rayset serialization preserves identity under changed ambient numeric state");
    std::fesetround(previous_rounding);
    std::locale::global(previous_locale);

    rtutil::RaySet roundtrip;
    const rtutil::RayFileStatus read_status = rtutil::read_rays(roundtrip_path.string(), roundtrip);
    check(read_status.ok(), "strict rayset read succeeds");
    check(roundtrip.rays.size() == rays.size(), "round trip preserves ray count");
    check(roundtrip.content_hash == written_hash, "round trip preserves rayset hash");
    const std::vector<rtutil::RayKey> keys = rtutil::ray_keys(roundtrip);
    check(keys.size() == rays.size(), "ray_keys extracts all canonical keys");
    if (roundtrip.rays.size() == rays.size() && keys.size() == rays.size()) {
	for (std::size_t index = 0; index < rays.size(); ++index) {
	    check(rtutil::ray_key(roundtrip.rays[index]) == rtutil::ray_key(rays[index]),
		  "round trip preserves canonical ray order and values");
	    check(keys[index] == rtutil::ray_key(rays[index]),
		  "ray_keys matches individual ray_key extraction");
	}
    }

    point_t center = {0.0, 0.0, 0.0};
    constexpr std::size_t REQUESTED_MINIMUM = 100000;
    constexpr std::size_t EXPECTED_PER_VIEW = 16701;
    const rtutil::BalancedRaySet balanced =
	rtutil::build_balanced_view_rays(10.0, center, REQUESTED_MINIMUM);
    check(balanced.rays_per_view == EXPECTED_PER_VIEW,
	  "balanced generation chooses the smallest complete per-view count");
    check(balanced.rays.size() == EXPECTED_PER_VIEW * rtutil::BALANCED_VIEW_COUNT,
	  "balanced generation reports the exact total count");
    check(balanced.rays.size() >= REQUESTED_MINIMUM,
	  "balanced generation meets the requested minimum");
    check(rtutil::validate_rays(balanced.rays).ok(), "generated balanced rayset is strict-valid");

    const std::string header = "**rays fired for test [1]**\n";
    const std::string point = "xyz 1 2 3\n";
    const std::string direction = "dir 0 0 1\n";
    expect_error(temporary.path, "bad-header", "rays [1]\n" + point + direction,
		 rtutil::RayFileError::InvalidHeader);
    expect_error(temporary.path, "zero-count", "**rays fired for test [0]**\n",
		 rtutil::RayFileError::InvalidCount);
    expect_error(temporary.path, "bad-order", header + direction + point,
		 rtutil::RayFileError::BadOrdering);
    expect_error(temporary.path, "unknown-record", header + "point 1 2 3\n" + direction,
		 rtutil::RayFileError::MalformedRecord);
    expect_error(temporary.path, "trailing-field", header + "xyz 1 2 3 extra\n" + direction,
		 rtutil::RayFileError::MalformedRecord);
    expect_error(temporary.path, "non-finite", header + "xyz nan 2 3\n" + direction,
		 rtutil::RayFileError::NonFiniteValue);
    expect_error(temporary.path, "zero-direction", header + point + "dir 0 0 0\n",
		 rtutil::RayFileError::NonUnitDirection);
    expect_error(temporary.path, "short-direction", header + point + "dir 0 0 0.5\n",
		 rtutil::RayFileError::NonUnitDirection);
    expect_error(temporary.path, "long-direction", header + point + "dir 0 0 2\n",
		 rtutil::RayFileError::NonUnitDirection);
    expect_error(temporary.path, "missing-direction", header + point,
		 rtutil::RayFileError::CountMismatch);
    expect_error(temporary.path, "missing-ray",
		 "**rays fired for test [2]**\n" + point + direction,
		 rtutil::RayFileError::CountMismatch);
    expect_error(temporary.path, "extra-ray", header + point + direction + point + direction,
		 rtutil::RayFileError::TrailingRecord);
    expect_error(temporary.path, "duplicate",
		 "**rays fired for test [2]**\n" + point + direction + point + direction,
		 rtutil::RayFileError::DuplicateRay);

    std::string unused_hash;
    check(rtutil::write_rays((temporary.path / "duplicate-write.rays").string(),
			      {first, first}, unused_hash).error == rtutil::RayFileError::DuplicateRay,
	  "strict writer rejects duplicate rays");
    std::vector<xray> non_finite_rays = {first};
    non_finite_rays[0].r_pt[0] = std::numeric_limits<double>::infinity();
    check(rtutil::write_rays((temporary.path / "non-finite-write.rays").string(),
			      non_finite_rays, unused_hash).error == rtutil::RayFileError::NonFiniteValue,
	  "strict writer rejects non-finite rays");
    std::vector<xray> non_unit_rays = {first};
    VSET(non_unit_rays[0].r_dir, 0.0, 0.0, 0.5);
    check(rtutil::write_rays((temporary.path / "non-unit-write.rays").string(),
			      non_unit_rays, unused_hash).error == rtutil::RayFileError::NonUnitDirection,
	  "strict writer rejects non-unit directions");

    rtutil::RaySet missing;
    check(rtutil::read_rays((temporary.path / "missing.rays").string(), missing).error
	      == rtutil::RayFileError::Io,
	  "missing ray files are I/O failures");

    if (failures != 0) {
	std::cerr << failures << " rayset test(s) failed\n";
	return 1;
    }
    std::cout << "rayset tests passed\n";
    return 0;
}
