#ifndef SHOT_H
#define SHOT_H

#include <array>
#include <string>
#include <vector>

#include <brlcad/vmath.h>

/*** An rt 'Shot'. Composed of a ray, and a vector of hit partitions ***/
class Shot {
public:
    class Ray {
    public:
        point_t pt{0, 0, 0};
        vect_t dir{0, 0, 0};
    };

    class Partition {
    public:
        std::string region;
        point_t in{0, 0, 0};
        vect_t in_norm{0, 0, 0};
        double in_dist{0};
        point_t out{0, 0, 0};
        vect_t out_norm{0, 0, 0};
        double out_dist{0};
    };

    struct Segment {
        std::string primitive;
        std::array<double, ELEMENTS_PER_MAT> transform{};
        point_t in{}, out{};
        vect_t in_norm{}, out_norm{};
        double in_dist = 0.0, out_dist = 0.0;
        int in_surfno = 0, out_surfno = 0;
        bool geometry_valid = true;
    };

    // Shot members
    Ray ray;
    std::vector<Partition> parts;
    std::vector<Segment> segments;

    bool operator==(const Shot& other) const;

    // Compare the ray and evaluated partitions. Primitive segments are
    // checked separately so callers can report the two result levels.
    bool equalWithin(const Shot& other, double tolerance) const;
};

#endif // SHOT_H
