#ifndef RTCMP_UTIL_H
#define RTCMP_UTIL_H

#include <cstddef>
#include <string>
#include <vector>

#include <brlcad/raytrace.h>

namespace rtutil {

// Load, validate, and prepare the requested objects. The caller retains
// ownership of rtip on both success and failure.
bool prepare_scene(struct rt_i *rtip, int object_count, const char **objects);

// Build ray array based on requested number of rays per view.
// TODO: paramaterize num view(s)
std::vector<xray> build_view_rays(double radius, const point_t center, std::size_t rays_per_view);

// Build approximately target_rays total.  Return empty if a nonzero memory cap
// cannot hold the minimum ray set.
std::vector<xray> build_view_rays_for_target(double radius, const point_t center,
				     std::size_t target_rays, std::size_t max_bytes);

// Read or write a complete .rays file (a header followed by xyz/dir pairs).
bool read_rays(const std::string& filename, std::vector<xray>& rays, std::string& error);
bool write_rays(const std::string& filename, const std::vector<xray>& rays, std::string& error);

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
