#include "util/util.h"

#include <algorithm>
#include <limits>

#include <brlcad/bn.h>

namespace {

// TODO: better and/or random dirs?
const vect_t VIEW_DIRECTIONS[] = {
    {0, 0, 1}, {0, 1, 0}, {1, 0, 0},
    {1, 1, 1}, {1, 4, -1}, {-1, -2, 4}
};
constexpr std::size_t VIEW_COUNT = sizeof(VIEW_DIRECTIONS) / sizeof(VIEW_DIRECTIONS[0]);

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

    const std::size_t minimum_pool_bytes = VIEW_COUNT * MIN_RAYS_PER_VIEW * sizeof(struct xray);
    if (max_pool_bytes < minimum_pool_bytes)
	return 0;

    const std::size_t max_rays_per_view = max_pool_bytes / sizeof(struct xray) / VIEW_COUNT;
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

    if (rays_per_view > std::numeric_limits<std::size_t>::max() / VIEW_COUNT)
	return {};

    const std::size_t total_rays = VIEW_COUNT * rays_per_view;
    std::vector<xray> rays(total_rays);
    for (std::size_t view = 0; view < VIEW_COUNT; ++view) {
	vect_t direction;
	VMOVE(direction, VIEW_DIRECTIONS[view]);
	VUNITIZE(direction);
	build_view(rays.data() + view * rays_per_view, radius, center, direction, rays_per_view);
    }

    return rays;
}

} // namespace

namespace rtutil {

std::vector<xray>
build_view_rays(double radius, const point_t center, std::size_t rays_per_view)
{
    return build_view_rays_impl(radius, center, rays_per_view, 0);
}

std::vector<xray>
build_view_rays_for_target(double radius, const point_t center,
			   std::size_t target_rays, std::size_t max_pool_bytes)
{
    const std::size_t target_rays_per_view = target_rays / VIEW_COUNT
	+ (target_rays % VIEW_COUNT != 0);
    return build_view_rays_impl(radius, center, target_rays_per_view, max_pool_bytes);
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
