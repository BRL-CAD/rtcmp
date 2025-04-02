#ifndef SHOT_H
#define SHOT_H

#include <string>
#include <vector>

#include <brlcad/vmath.h>

/*** An rt 'Shot'. Composed of a ray, and a vector of hit partitions ***/
class Shot {
public:
    class Ray {
    public:
        explicit Ray(double tol) : tol(tol), pt{0, 0, 0}, dir{0, 0, 0} {}

        point_t pt;
        vect_t dir;
        const double tol; // comparison tolerance

        inline bool operator==(const Ray& other) const {
            return VNEAR_EQUAL(pt, other.pt, tol) && VNEAR_EQUAL(dir, other.dir, tol);
        }
    };

    class Partition {
    public:
        explicit Partition(double tol)
            : tol(tol), region(""), in{0, 0, 0}, in_norm{0, 0, 0}, in_dist(0),
              out{0, 0, 0}, out_norm{0, 0, 0}, out_dist(0) {}

        std::string region;
        point_t in;
        vect_t in_norm;
        double in_dist;
        point_t out;
        vect_t out_norm;
        double out_dist;
        const double tol; // comparison tolerance

        inline bool operator==(const Partition& other) const {
            return region == other.region &&
                   VNEAR_EQUAL(in, other.in, tol) &&
                   VNEAR_EQUAL(in_norm, other.in_norm, tol) &&
                   NEAR_EQUAL(in_dist, other.in_dist, tol) &&
                   VNEAR_EQUAL(out, other.out, tol) &&
                   VNEAR_EQUAL(out_norm, other.out_norm, tol) &&
                   NEAR_EQUAL(out_dist, other.out_dist, tol);
        }
    };

    // Shot members
    Ray ray;
    std::vector<Partition> parts;

    inline bool operator==(const Shot& other) const {
        return ray == other.ray && parts == other.parts;
    }
};

#endif // SHOT_H