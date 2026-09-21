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

    // Shot members
    Ray ray;
    std::vector<Partition> parts;

    // Compares this shot against 'other' within the given tolerance.
    // Returns true if the rays (pt/dir), the partition count, and every
    // partition (region + in/out points, normals, and dists) match at 'tol'.
    inline bool equalWithin(const Shot& other, double tol) const {
        // compare ray origin / direction
        if (!VNEAR_EQUAL(ray.pt,  other.ray.pt,  tol) ||
            !VNEAR_EQUAL(ray.dir, other.ray.dir, tol)) {
            return false;
        }

        // compare number of partitions
        if (parts.size() != other.parts.size()) {
            return false;
        }

        // compare each partition
        for (size_t i = 0; i < parts.size(); ++i) {
            const Partition& pa = parts[i];
            const Partition& pb = other.parts[i];

            if (pa.region != pb.region ||
                !VNEAR_EQUAL(pa.in,       pb.in,       tol) ||
                !VNEAR_EQUAL(pa.in_norm,  pb.in_norm,  tol) ||
                !NEAR_EQUAL( pa.in_dist,  pb.in_dist,  tol) ||
                !VNEAR_EQUAL(pa.out,      pb.out,      tol) ||
                !VNEAR_EQUAL(pa.out_norm, pb.out_norm, tol) ||
                !NEAR_EQUAL( pa.out_dist, pb.out_dist, tol)) {
                return false;
            }
        }

        return true;
    }
};

#endif // SHOT_H