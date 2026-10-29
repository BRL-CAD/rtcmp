#include "util/util.h"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <utility>

#include <brlcad/bn.h>

namespace {

std::size_t
active_solid_count(struct rt_i *rtip)
{
    std::size_t count = 0;
    struct soltab *solid;
    RT_VISIT_ALL_SOLTABS_START(solid, rtip) {
	++count;
    } RT_VISIT_ALL_SOLTABS_END;
    return count;
}

// TODO: better and/or random dirs?
const vect_t VIEW_DIRECTIONS[] = {
    {0, 0, 1}, {0, 1, 0}, {1, 0, 0},
    {1, 1, 1}, {1, 4, -1}, {-1, -2, 4}
};
static_assert(sizeof(VIEW_DIRECTIONS) / sizeof(VIEW_DIRECTIONS[0]) == rtutil::BALANCED_VIEW_COUNT,
	      "balanced view metadata must match the generated directions");

constexpr std::size_t RAYS_PER_RING = 100;
constexpr std::size_t MIN_RAYS_PER_VIEW = RAYS_PER_RING + 1;	// min = 1ring + center
constexpr std::size_t MAX_RING_COUNT = (static_cast<std::size_t>(std::numeric_limits<int>::max()) - 1) / RAYS_PER_RING;

std::size_t complete_ray_count(std::size_t requested_rays)
{
    requested_rays = std::max(requested_rays, MIN_RAYS_PER_VIEW);
    const std::size_t additional_rays = requested_rays - 1;
    const std::size_t ring_count = additional_rays / RAYS_PER_RING + (additional_rays % RAYS_PER_RING != 0);

    if (ring_count > MAX_RING_COUNT)
	return 0;

    return ring_count * RAYS_PER_RING + 1;
}

std::size_t
ray_count_per_view(std::size_t requested_rays, std::size_t max_pool_bytes)
{
    if (max_pool_bytes == 0)
	return complete_ray_count(requested_rays);

    const std::size_t minimum_pool_bytes = rtutil::BALANCED_VIEW_COUNT * MIN_RAYS_PER_VIEW * sizeof(struct xray);
    if (max_pool_bytes < minimum_pool_bytes)
	return 0;

    const std::size_t max_rays_per_view = max_pool_bytes / sizeof(struct xray) / rtutil::BALANCED_VIEW_COUNT;
    const std::size_t max_complete_rays =
	((max_rays_per_view - 1) / RAYS_PER_RING) * RAYS_PER_RING + 1;
    if (requested_rays >= max_complete_rays)
	return max_complete_rays;

    return complete_ray_count(requested_rays);
}

void
build_view(struct xray *rays, double radius, const point_t center,
	   const vect_t direction, std::size_t rays_per_view)
{
    vect_t first_axis;
    vect_t second_axis;
    bn_vec_ortho(first_axis, direction);
    VCROSS(second_axis, direction, first_axis);
    VUNITIZE(second_axis);

    VMOVE(rays[0].r_dir, direction);
    VJOIN1(rays[0].r_pt, center, -radius, direction);

    const int ring_count = static_cast<int>((rays_per_view - 1) / RAYS_PER_RING);
    rt_raybundle_maker(rays, radius, first_axis, second_axis, static_cast<int>(RAYS_PER_RING), ring_count);
}

std::vector<xray>
build_view_rays_impl(double radius, const point_t center,
		     std::size_t requested_rays, std::size_t max_pool_bytes)
{
    const std::size_t rays_per_view = ray_count_per_view(requested_rays, max_pool_bytes);
    if (rays_per_view == 0)
	return {};

    if (rays_per_view > std::numeric_limits<std::size_t>::max() / rtutil::BALANCED_VIEW_COUNT)
	return {};

    const std::size_t total_rays = rtutil::BALANCED_VIEW_COUNT * rays_per_view;
    std::vector<xray> rays;
    try {
	rays.resize(total_rays);
    } catch (const std::bad_alloc&) {
	return {};
    }
    for (std::size_t view = 0; view < rtutil::BALANCED_VIEW_COUNT; ++view) {
	vect_t direction;
	VMOVE(direction, VIEW_DIRECTIONS[view]);
	VUNITIZE(direction);
	build_view(rays.data() + view * rays_per_view, radius, center, direction, rays_per_view);
    }

    return rays;
}

} // namespace

namespace rtutil {

bool
prepare_scene(struct rt_i *rtip, int object_count, const char **objects, int ncpus)
{
    while (object_count-- > 0) {
	const char *object = *objects++;
	if (rt_gettree(rtip, object) < 0) {
	    std::fprintf(stderr, "RT: Failed to load object: %s\n", object);
	    return false;
	}
    }

    const std::size_t active_solids = active_solid_count(rtip);
    if (active_solids != rtip->stats.nsolids) {
	std::fprintf(stderr, "RT: Object loading retained %zu of %zu solids\n",
		     active_solids, rtip->stats.nsolids);
	return false;
    }

    const int prep_cpus = ncpus > 0 ? ncpus : bu_avail_cpus();
    rt_prep_parallel(rtip, prep_cpus);
    if (rtip->stats.nregions == 0 || rtip->stats.nsolids == 0) {
	std::fprintf(stderr, "RT: Prepared scene contains no regions or solids\n");
	return false;
    }
    return true;
}

std::vector<xray>
build_view_rays(double radius, const point_t center, std::size_t rays_per_view)
{
    return build_view_rays_impl(radius, center, rays_per_view, 0);
}

std::vector<xray>
build_view_rays_for_target(double radius, const point_t center,
			   std::size_t target_rays, std::size_t max_pool_bytes)
{
    const std::size_t target_rays_per_view = target_rays / BALANCED_VIEW_COUNT
	+ (target_rays % BALANCED_VIEW_COUNT != 0);
    return build_view_rays_impl(radius, center, target_rays_per_view, max_pool_bytes);
}

BalancedRaySet
build_balanced_view_rays(double radius, const point_t center, std::size_t minimum_total_rays)
{
    const std::size_t requested_per_view = minimum_total_rays / BALANCED_VIEW_COUNT
	+ (minimum_total_rays % BALANCED_VIEW_COUNT != 0);
    std::vector<xray> rays = build_view_rays_impl(radius, center, requested_per_view, 0);
    const std::size_t actual_per_view = rays.empty() ? 0 : rays.size() / BALANCED_VIEW_COUNT;
    return {std::move(rays), actual_per_view};
}

std::string
json_quote(const std::string& value)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << '"' << std::hex << std::setfill('0');
    for (const unsigned char character : value) {
	switch (character) {
	    case '"': output << "\\\""; break;
	    case '\\': output << "\\\\"; break;
	    case '\b': output << "\\b"; break;
	    case '\f': output << "\\f"; break;
	    case '\n': output << "\\n"; break;
	    case '\r': output << "\\r"; break;
	    case '\t': output << "\\t"; break;
	    default:
		if (character < 0x20U)
		    output << "\\u" << std::setw(4) << static_cast<unsigned>(character);
		else
		    output << static_cast<char>(character);
	}
    }
    output << '"';
    return output.str();
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
