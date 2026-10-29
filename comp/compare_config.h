#ifndef COMPARE_CONFIG_H
#define COMPARE_CONFIG_H

#include <cstddef>
#include <string>
#include <brlcad/vmath.h>

constexpr std::size_t DEFAULT_RAYS_PER_VIEW = 100000;
constexpr int DEFAULT_GRAZING_SAMPLES = 8;
constexpr int MINIMUM_GRAZING_SAMPLES = 3;
constexpr int MAXIMUM_GRAZING_SAMPLES = 1024;

/* Configuration for comparing captured shot results. */
struct CompareConfig {
    double tol = SMALL_FASTF;					    // comparison tolerance
    bool primitive_hits = false;				    // include pre-Boolean primitive segments in diff output
    std::string grazing_geometry;			    // compare: geometry file used for nearby-ray checks
    std::string grazing_object;			    // compare: object to raytrace
    double grazing_radius = 0.0;			    // mm; 0 uses 1e-7 of the prepared model radius
    int grazing_samples = DEFAULT_GRAZING_SAMPLES;		    // rays on the circular ring

    // A minimum total selects balanced generation; otherwise rays_per_view
    // preserves the legacy command-line behavior.
    std::size_t rays_per_view = DEFAULT_RAYS_PER_VIEW;
    std::size_t minimum_ray_count = 0;
    std::string generator;					    // invoking executable identity

    // input file names
    std::string in_ray_file;					    // if supplied: use .rays file for results generation

    // output file names
    std::string json_ofile = "shots.json";				    // JSON shots result file
    std::string ray_file = "shots.rays";				    // rays shot in diff_run (not generated if in_ray_file supplied)
    std::string plot3_file = "diff.plot3";				    // graphically view differences
    std::string nirt_file = "diff.nrt";				    // xyz and dir of problem rays in compare_run (useful for feeding into individual nirt shots)
    std::string status_file;					    // optional structured operation status
};

#endif /* COMPARE_CONFIG_H */
