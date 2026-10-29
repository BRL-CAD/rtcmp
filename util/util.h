#ifndef RTCMP_UTIL_H
#define RTCMP_UTIL_H

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <brlcad/raytrace.h>

namespace rtutil {

constexpr std::size_t BALANCED_VIEW_COUNT = 6;

struct BalancedRaySet {
    std::vector<xray> rays;
    std::size_t rays_per_view = 0;
};

enum class RayFileError {
    None,
    Io,
    InvalidHeader,
    InvalidCount,
    BadOrdering,
    MalformedRecord,
    NonFiniteValue,
    NonUnitDirection,
    DuplicateRay,
    CountMismatch,
    TrailingRecord
};

struct RayFileStatus {
    RayFileError error = RayFileError::None;
    std::size_t line = 0;
    std::string message;

    bool ok() const noexcept { return error == RayFileError::None; }
};

struct RayKey {
    std::array<double, 6> values{};

    bool operator==(const RayKey& other) const noexcept { return values == other.values; }
};

struct RayKeyHash {
    std::size_t operator()(const RayKey& key) const noexcept;
};

struct RaySet {
    std::vector<xray> rays;
    std::string content_hash;
};

// Load, validate, and prepare the requested objects. The caller retains
// ownership of rtip on both success and failure. When ncpus <= 0, all
// available CPUs are used for parallel scene preparation.
bool prepare_scene(struct rt_i *rtip, int object_count, const char **objects, int ncpus = 0);

// Build ray array based on requested number of rays per view.
// TODO: paramaterize num view(s)
std::vector<xray> build_view_rays(double radius, const point_t center, std::size_t rays_per_view);

// Build approximately target_rays total.  Return empty if a nonzero memory cap
// cannot hold the minimum ray set.
std::vector<xray> build_view_rays_for_target(double radius, const point_t center,
				     std::size_t target_rays, std::size_t max_bytes);

// Build the smallest complete, balanced six-view pool containing at least the
// requested total number of rays.
BalancedRaySet build_balanced_view_rays(double radius, const point_t center,
					std::size_t minimum_total_rays);

const char *ray_file_error_name(RayFileError error) noexcept;
RayKey ray_key(const point_t point, const vect_t direction) noexcept;
RayKey ray_key(const xray& ray) noexcept;
std::vector<RayKey> ray_keys(const RaySet& rayset);
RayFileStatus validate_rays(const std::vector<xray>& rays);
RayFileStatus read_rays(const std::string& filename, RaySet& rayset);
RayFileStatus write_rays(const std::string& filename, const std::vector<xray>& rays,
			 std::string& content_hash);
std::string ray_content_hash(const std::vector<xray>& rays);

std::string json_quote(const std::string& value);

} // namespace rtutil

#endif // RTCMP_UTIL_H

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
