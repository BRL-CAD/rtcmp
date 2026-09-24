#include "grazing.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <brlcad/bn.h>
#include <brlcad/bu.h>

GrazingDetector::GrazingDetector(const std::string& geometry, const std::string& object,
                                 double requested_radius, int samples)
    : sample_count(samples)
{
    RT_APPLICATION_INIT(&app);
    std::memset(&resource, 0, sizeof(resource));

    char description[BUFSIZ];
    rtip = rt_dirbuild(geometry.c_str(), description, 0);
    if (!rtip) {
        error_message = "Failed to load geometry: " + geometry;
        return;
    }

    rt_init_resource(&resource, 0, rtip);
    if (rt_gettree(rtip, object.c_str()) < 0) {
        error_message = "Failed to load object '" + object + "' from " + geometry;
        return;
    }
    rt_prep_parallel(rtip, 1);

    ring_radius = requested_radius > 0.0 ? requested_radius :
        std::fmax(1e-12, std::fabs(rtip->rti_radius) * 1e-7);
    if (!std::isfinite(ring_radius) || ring_radius <= 0.0) {
        error_message = "Could not determine a finite grazing radius";
        return;
    }

    app.a_rt_i = rtip;
    app.a_resource = &resource;
    app.a_logoverlap = rt_silent_logoverlap;
    app.a_hit = hit;
    app.a_miss = miss;
}

GrazingDetector::~GrazingDetector()
{
    if (rtip)
        rt_free_rti(rtip);
}

int GrazingDetector::hit(struct application *a, struct partition *head, struct seg *)
{
    auto *regions = static_cast<std::map<std::string, int> *>(a->a_uptr);
    for (struct partition *pp = head->pt_forw; pp != head; pp = pp->pt_forw)
        ++(*regions)[pp->pt_regionp->reg_name];
    return 0;
}

int GrazingDetector::miss(struct application *)
{
    return 0;
}

std::map<std::string, int> GrazingDetector::shoot(const struct xray& ray)
{
    std::map<std::string, int> regions;
    app.a_uptr = &regions;
    VMOVE(app.a_ray.r_pt, ray.r_pt);
    VMOVE(app.a_ray.r_dir, ray.r_dir);
    rt_shootray(&app);
    app.a_uptr = nullptr;
    return regions;
}

bool GrazingDetector::unstable(const Shot::Ray& ray, const std::vector<std::string>& regions)
{
    if (!valid() || regions.empty())
        return false;

    vect_t direction, across, up;
    VMOVE(direction, ray.dir);
    double length = MAGNITUDE(direction);
    if (!std::isfinite(length) || length <= 0.0)
        return false;
    VUNITIZE(direction);
    bn_vec_ortho(across, direction);
    VUNITIZE(across);
    VCROSS(up, direction, across);
    VUNITIZE(up);

    std::vector<struct xray> rays(sample_count + 1);
    VMOVE(rays[0].r_pt, ray.pt);
    VMOVE(rays[0].r_dir, direction);
    if (rt_raybundle_maker(rays.data(), ring_radius, across, up, sample_count, 1) != sample_count + 1)
        return false;

    auto center = shoot(rays[0]);
    for (int i = 1; i <= sample_count; ++i) {
        auto nearby = shoot(rays[i]);
        for (const auto& region : regions) {
            if (nearby[region] != center[region])
                return true;
        }
    }
    return false;
}
