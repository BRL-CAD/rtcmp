#include "shotset.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <brlcad/bu.h>
#include <brlcad/vmath.h>

#include "json.hpp"

// ***** HELPER FUNCTIONS [start] - find a better home for these ***** //
unsigned long long rhash(point_t pt, vect_t dir) {
    struct bu_vls rstr = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&rstr, "%0.15f%0.15f%0.15f%0.15f%0.15f%0.15f", V3ARGS(pt), V3ARGS(dir));
    unsigned long long hash = bu_data_hash((void *)bu_vls_cstr(&rstr), bu_vls_strlen(&rstr));
    bu_vls_free(&rstr);

    return hash;
}

double _s2d(std::string doubleStr) {
    double ret;
    std::stringstream ss(doubleStr);
    size_t prec = std::numeric_limits<double>::max_digits10;
    ss >> std::setprecision(prec) >> std::fixed >> ret;
    return ret;
}

/* helper function to extract XYZ values from json 'data'
 * xyz is stored in ret[0], ret[1], ret[2] respectively
 */
bool parse_xyz(double (*ret)[3], const nlohmann::json &data) {
    if (!data.contains("X") || !data.contains("Y") || !data.contains("Z")) {
        return false;
    }

    (*ret)[X] = _s2d(data["X"]);
    (*ret)[Y] = _s2d(data["Y"]);
    (*ret)[Z] = _s2d(data["Z"]);

    return true;
}
// ***** HELPER FUNCTIONS [end] ***** //


ShotSet::ShotSet(std::string filename, const CompareConfig& _config) : shotfile(filename), config(&_config) {
    // valid file?
    std::ifstream ndjsonFile(filename, std::ios::binary);
    if (!ndjsonFile.is_open()) {
	std::cerr << "failed to open shotset file:" << filename << std::endl;
        this->valid_set = false;
	return;
    }

    // parse json
    std::string line;
    uint64_t offset = 0;	// offset in file
    while (true) {
        offset = ndjsonFile.tellg();
        if (!std::getline(ndjsonFile, line))
            break;

	// skip empty lines
	if (line.empty())
	    continue;

        // load shot in lookup
        try {
            // make sure we have valid json
	    nlohmann::json shotJson = nlohmann::json::parse(line);
	    if (!shotJson.contains("ray_pt") || !shotJson.contains("ray_dir"))
	        continue;

            // extract ray pt and dir
	    point_t ray_pt;
	    vect_t ray_dir;
	    if (!parse_xyz(&ray_pt, shotJson["ray_pt"]) || !parse_xyz(&ray_dir, shotJson["ray_dir"]))
                continue;

	    unsigned long long key = rhash(ray_pt, ray_dir);

	    // collision check
            if (this->shot_lookup.count(key)) {
                // we have an identical key. Do we have a duplicate shot or is something wrong
                std::string cmp_line;

                // keep track of where we are
                offset = ndjsonFile.tellg();

                // jump to line that generated identical key
                ndjsonFile.seekg(shot_lookup[key], std::ios::beg);
                std::getline(ndjsonFile, cmp_line);
                if (line != cmp_line) {
                    std::cerr << "KEY COLLISION FOR " << key << ". Got differing: '" << line << "' and '" << cmp_line << "'. Check file is valid" << std::endl;
                    this->valid_set = false;
                    return;
                }

                // skip duplicate - pick up where we left off
                ndjsonFile.seekg(offset, std::ios::beg);
                continue;
            }

            // add
            // TODO: can we do something better than maintaining parallel structures?
            // TODO: explore if we can get away with loading <Shot> directly in map instead of offset
	    this->shot_lookup[key] = offset;
            this->offset_ordered_keys.emplace_back(offset, key);
        } catch (const nlohmann::json::parse_error& e) {
            std::cerr << "shotset file parse error at offset " << offset << ": " << e.what() << std::endl;
            // lazy - keep going if we can
            continue;
        }
    }

    // sanity
    if (this->shot_lookup.size() != this-> offset_ordered_keys.size())
        std::cerr << "indexing alignment error" << std::endl;

    std::cout << "  DEBUG: indexed " << this->shot_lookup.size() << " shots in " << filename << std::endl;
}

bool ShotSet::is_valid() {
    // ensure we have a valid filename and lookup map was loaded with *something*
    return (valid_set && !shotfile.empty() && (shot_lookup.size() > 0));
}

bool ShotSet::shotset_different(const ShotSet& cmp_set) {
    // open files
    std::ifstream this_file(this->shotfile, std::ios::binary);
    std::ifstream cmp_file(cmp_set.shotfile, std::ios::binary);
    if (!this_file.is_open() || !cmp_file.is_open()) {
        std::cerr << "Error: Unable to open files for comparison." << std::endl;
        return true;
    }

    std::unordered_map<unsigned long long, uint64_t> cmp_lookup = cmp_set.shot_lookup;  // mutable copy
    std::vector<unsigned long long> this_unmatched;                                     // gather unmatched keys from this->shot_lookup
    std::vector<unsigned long long> diff_shots;                                         // gather non-equal rays

    //for (const auto& [key, this_offset] : this->shot_lookup) {
    for (const auto& [this_offset, key] : this->offset_ordered_keys) {
        auto it = cmp_lookup.find(key);
        if (it == cmp_lookup.end()) {
            // non-matching ray from shotset 1
            this_unmatched.push_back(key);
            continue;   // TODO: break at difference or keep going?
        }

        uint64_t cmp_offset = it->second;

        this_file.seekg(this_offset, std::ios::beg);
        cmp_file.seekg(cmp_offset, std::ios::beg);

        std::string this_line, cmp_line;
        if (!std::getline(this_file, this_line) || !std::getline(cmp_file, cmp_line)) {
            std::cerr << "Error reading lines at offsets: " << this_offset << ", " << cmp_offset << std::endl;
            continue;   //  TODO: fail?
        }

        // sanity: this shouldn't be possible if constructor is loading correctly
        if (this_line.empty() || cmp_line.empty()) continue;

        if ((this->_get_shot(key) == cmp_set._get_shot(key)) == false) {
            // TODO: shot hit data doesn't match - now what?
            diff_shots.push_back(key);
        }

        cmp_lookup.erase(it);
    }



    /* log findings */
    bool different = false; // return value
    FILE* nirt_file = fopen(config->nirt_file.c_str(), "a");
    // rays with different hit data
    if (!diff_shots.empty()) {
        fprintf(nirt_file, "**differing shots [%zd]. Use nirt -f diff to produce high-accuracy textual output**\n", diff_shots.size());
        for (auto key : diff_shots) {
            fprintf(nirt_file, "xyz %0.17f %0.17f %0.17f\n", V3ARGS(this->_get_shot(key).ray.pt));
            fprintf(nirt_file, "dir %0.17f %0.17f %0.17f\n", V3ARGS(this->_get_shot(key).ray.dir));
        }
        different = true;
        fprintf(nirt_file, "\n");  // pretty formatting
    }
    // unmatched rays in this->shotset
    if (!this_unmatched.empty()) {
        fprintf(nirt_file, "**rays not found in %s** [%zd]\n", cmp_set.shotfile.c_str(), this_unmatched.size());
        for (auto key : this_unmatched) {
            fprintf(nirt_file, "xyz %0.17f %0.17f %0.17f\n", V3ARGS(this->_get_shot(key).ray.pt));
            fprintf(nirt_file, "dir %0.17f %0.17f %0.17f\n", V3ARGS(this->_get_shot(key).ray.dir));
        }
        different = true;
        fprintf(nirt_file, "\n");  // pretty formatting
    }
    // unmatched rays in cmp->shotset
    if (!cmp_lookup.empty()) {
        fprintf(nirt_file, "**rays not found in %s** [%zd]\n", this->shotfile.c_str(), cmp_lookup.size());
        for (auto& [key, offset] : cmp_lookup) {
            fprintf(nirt_file, "xyz %0.17f %0.17f %0.17f\n", V3ARGS(cmp_set._get_shot(key).ray.pt));
            fprintf(nirt_file, "dir %0.17f %0.17f %0.17f\n", V3ARGS(cmp_set._get_shot(key).ray.dir));
        }
        different = true;
    }
    fclose(nirt_file);

    // TODO: create plot file to be able to visualize differences

    return different;
}

Shot ShotSet::_get_shot(std::string& line) const {
    // TODO: error checking for each setter

    // extract the JSON we need from line
    nlohmann::json shotJson = nlohmann::json::parse(line);

    // build ray
    Shot::Ray ray(this->config->tol);
    parse_xyz(&ray.pt, shotJson["ray_pt"]);
    parse_xyz(&ray.dir, shotJson["ray_dir"]);

    // build partition table
    std::vector<Shot::Partition> partitions;
    for (const auto& part : shotJson["partitions"]) {
        Shot::Partition partition(this->config->tol);

        // get members from json
        partition.region = part["region"];
        parse_xyz(&partition.in, part["in_pt"]);
        parse_xyz(&partition.in_norm, part["in_norm"]);
        partition.in_dist = _s2d(part["in_dist"]);
        parse_xyz(&partition.out, part["out_pt"]);
        parse_xyz(&partition.out_norm, part["out_norm"]);
        partition.out_dist = _s2d(part["out_dist"]);

        // add to vec
        partitions.push_back(partition);
    }

    // return the shot
    Shot shot{ray, partitions};
    return shot;
}

Shot ShotSet::_get_shot(unsigned long long ray_hash) const {
    // get out this shot's line from file
    auto it = shot_lookup.find(ray_hash);
    uint64_t offset = it->second;
    
    std::ifstream ndjsonFile(shotfile, std::ios::binary);
    ndjsonFile.seekg(offset, std::ios::beg);
    std::string line;
    std::getline(ndjsonFile, line);

    return _get_shot(line);
}

void ShotSet::_print_ray_info(std::string& line, bool linedump) const {
    FILE* outfp = stdout;

    // just want the raw line?
    if (linedump) {
        fprintf(outfp, "%s\n", line.c_str());
        return;
    }

    // print ray pt/dir
    try {
        double pt[3], dir[3];
        nlohmann::json shotJson = nlohmann::json::parse(line);
        parse_xyz(&pt, shotJson["ray_pt"]);
        parse_xyz(&dir, shotJson["ray_dir"]);

        fprintf(outfp, "xyz %0.17f %0.17f %0.17f\n", V3ARGS(pt));
        fprintf(outfp, "dir %0.17f %0.17f %0.17f\n", V3ARGS(dir));

        /*if (shotJson.contains("partitions")) {
            fprintf(outfp, "%s\n", shotJson["partitions"].dump().c_str());
        }*/
    } catch (const nlohmann::json::parse_error& e) {
        fprintf(outfp, "Error parsing JSON: %s", e.what());
    }
}

void ShotSet::_print_ray_info(unsigned long long ray_hash, bool linedump) const {
    // valid hash?
    auto it = shot_lookup.find(ray_hash);
    if (it == shot_lookup.end()) {
        std::cerr << "Ray hash [" << ray_hash << "] not found" << std::endl;
        return;
    }

    // open shot file
    uint64_t offset = it->second;
    std::ifstream file(this->shotfile, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "error opening " << this->shotfile << std::endl;
        return;
    }

    // move to offset, retrieve ray
    file.seekg(offset, std::ios::beg);
    std::string line;
    if (!std::getline(file, line)) {
        std::cerr << "Error reading line at offset: " << offset << std::endl;
        return;
    }

    this->_print_ray_info(line, linedump);
}
