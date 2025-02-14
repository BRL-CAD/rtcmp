#ifndef SHOTSET_H
#define SHOTSET_H

#include <string>
#include <utility>  // std::pair
#include <unordered_map>
#include <vector>
#include <chrono>       // measure_time
#include <functional>   // measure_time

#include "compare_config.h"

// TODO: this should live somewhere else
template <typename Func, typename... Args>
auto measure_time(const std::string& label, Func&& func, Args&&... args) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // call original function
    auto result = std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << label << " took " << duration << " ms" << std::endl;

    return result; // return the result of the function
}

/*
 * organize shotfile in memory
 */ 
class ShotSet {
    public:
	class Ray {
	public:
	    Ray(const double tol) : tol(tol) {};
	    point_t pt;
	    vect_t dir;

	    const double tol;	// comparison tol
	    bool operator==(const Ray& other) const;
	};

	class Partition {
	public:
	    Partition(const double tol) : tol(tol) {};
	    std::string region;
	    point_t in;
	    vect_t in_norm;
	    double in_dist;
	    point_t out;
	    vect_t out_norm;
	    double out_dist;

	    const double tol;	// comparison tol
	    bool operator==(const Partition& other) const;
	};

	class Shot {
	public:
	    Ray ray;
	    std::vector<Partition> parts;

	    bool operator==(const Shot& other) const;
	};

	// ShotSet public members
	ShotSet(std::string filename, const CompareConfig& config);
	bool is_valid();
	bool shotset_different(const ShotSet& cmp_set);	    // TODO: this could use operator ==
    private:
	void _print_ray_info(unsigned long long ray_hash, bool linedump = false) const;
	void _print_ray_info(std::string& ray_json, bool linedump = false) const;
	Shot _get_shot(unsigned long long ray_hash) const;
	Shot _get_shot(std::string& shotline) const;

	const CompareConfig* config;
	bool valid_set = true;								// ensure valid shotset is loaded
	std::string shotfile;								// orignal shotfile
	std::unordered_map<unsigned long long, uint64_t> shot_lookup;			// hashed ray pt+dir -> file offset position
	std::vector<std::pair<uint64_t, unsigned long long>> offset_ordered_keys;	// order using file offset so we can iterate in sequential chunks
};

#endif SHOTSET_H