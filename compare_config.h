#ifndef COMPARE_CONFIG_H
#define COMPARE_CONFIG_H

#include <string>
#include <brlcad/vmath.h>

/* useful information when comparing shotsets */
struct CompareConfig {
    // comparison tolerance
    double tol = SMALL_FASTF;

    // output file names
    std::string json_ofile = std::string("shots.json");
    std::string ray_file = std::string("shots.rays");
    std::string plot3_file = std::string("diff.plot3");
    std::string nirt_file = std::string("diff.nrt");

    // input file names
    std::string in_ray_file = std::string("");

    // ?
    std::string dbfile;
    std::string obj_name;
};

#endif COMPARE_CONFIG_H