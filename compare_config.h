#ifndef COMPARE_CONFIG_H
#define COMPARE_CONFIG_H

#include <string>
#include <brlcad/vmath.h>

/* useful information when comparing shotsets */
struct CompareConfig {
    double tol = SMALL_FASTF;					    // comparison tolerance

    // input file names
    std::string in_ray_file = std::string("");			    // if supplied: use .rays file for results generation

    // output file names
    std::string json_ofile = std::string("shots.json");		    // JSON shots result file
    std::string ray_file = std::string("shots.rays");		    // rays shot in diff_run (not generated if in_ray_file supplied)
    std::string plot3_file = std::string("diff.plot3");		    // graphically view differences
    std::string nirt_file = std::string("diff.nrt");		    // xyz and dir of problem rays in compare_run (useful for feeding into individual nirt shots)
};

#endif COMPARE_CONFIG_H