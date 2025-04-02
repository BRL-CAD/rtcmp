#ifndef SHOTSET_H
#define SHOTSET_H

#include <string>
#include <utility>  // std::pair
#include <unordered_map>
#include <vector>

#include "Shot.h"
#include "compare_config.h"

/*
 * organize shotfile in memory
 */ 
class ShotSet {
    public:
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