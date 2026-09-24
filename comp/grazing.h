#pragma once

#include <map>
#include <string>
#include <vector>

#include <brlcad/raytrace.h>

#include "Shot.h"

/* Re-shoot a center ray and a small circular ring around it. */
class GrazingDetector {
public:
    GrazingDetector(const std::string& geometry, const std::string& object,
                    double requested_radius, int samples);
    ~GrazingDetector();

    GrazingDetector(const GrazingDetector&) = delete;
    GrazingDetector& operator=(const GrazingDetector&) = delete;

    bool valid() const { return rtip != nullptr && error_message.empty(); }
    const std::string& error() const { return error_message; }
    double radius() const { return ring_radius; }
    int sampleCount() const { return sample_count; }

    // True when a candidate region's partition count changes on the ring.
    bool unstable(const Shot::Ray& ray, const std::vector<std::string>& regions);

private:
    static int hit(struct application *a, struct partition *head, struct seg *s);
    static int miss(struct application *a);
    std::map<std::string, int> shoot(const struct xray& ray);

    struct rt_i *rtip = nullptr;
    struct application app;
    struct resource resource;
    double ring_radius = 0.0;
    int sample_count = 0;
    std::string error_message;
};
