#include "shot_comp.h"

#include <charconv>
#include <iostream>
#include <sstream>
#include <brlcad/bu.h>
#include <brlcad/vmath.h>

/***************************/
/***** ShotIndex class *****/
/***************************/
ShotIndex::ShotIndex(const std::string& filename): p_filename(filename) {
    // TODO: check filename exists

    // open ifstream; mark valid
    p_file.open(p_filename, std::ios::binary);
    p_valid = p_file.is_open();

    if (!p_valid) {
        std::cerr << "ERROR opening " << filename << "\n";
        return;
    }

    // get a very rough guess on number of lines
    auto file_size = std::filesystem::file_size(p_filename);
    size_t avg_bytes_per_line = 650;	// arbitrary-ish value
    size_t estimated_lines = file_size / avg_bytes_per_line;
    // upfront ballpark reserve before indexing
    p_ordered_keys.reserve(estimated_lines);
    p_offset_map.reserve(estimated_lines * 1.3);    // load factor ~0.75
    
    // index
    p_buildIndex();
}

ShotIndex::~ShotIndex() {
    if (p_file.is_open())
	p_file.close();
}

// basic getters
bool ShotIndex::isValid() const noexcept { return p_valid; }
const std::vector<std::pair<uint64_t,uint64_t>>& ShotIndex::orderedKeys() const noexcept { return p_ordered_keys; }
std::string ShotIndex::filename() const noexcept { return p_filename; }

void ShotIndex::p_buildIndex() {
    if (!p_valid)
	return;

    // zero
    p_file.seekg(0, std::ios::beg);
    p_ordered_keys.clear();
    p_offset_map.clear();

    std::string line;
    uint64_t offset = 0;
    while (true) {
	offset = p_file.tellg();
	if (!std::getline(p_file, line)) 
	    break;
	
	// skip empty lines
	if (line.empty()) 
	    continue;

	try {
	    Shot::Ray ray = shot_utils::parse_json_ray(line);
	    uint64_t key = shot_utils::hash_ray(ray);

	    // collision check
	    auto collision_check = lookup(key);
	    if (collision_check.has_value()) {
		// we have an identical key. Do we have a duplicate shot or is something wrong
		std::string cmp_line;

                // update where we need to return to
                offset = p_file.tellg();

                // jump to line that generated identical key
                p_file.seekg(collision_check.value(), std::ios::beg);
                std::getline(p_file, cmp_line);
                if (line != cmp_line) {
                    std::cerr << "KEY COLLISION FOR " << key << ". Got differing: '" << line << "' and '" << cmp_line << "'. Check file is valid" << std::endl;
                    p_valid = false;
                    return;
                }

                // skip duplicate - pick up where we left off
                p_file.seekg(offset, std::ios::beg);
                continue;
	    }
	    // add
	    p_offset_map[key] = offset;
	    p_ordered_keys.emplace_back(offset, key);
	} catch (const std::exception& e) {
	    std::cerr << "ShotIndex file parse error at offset " << offset << ": " << e.what() << std::endl;
            // lazy - keep going if we can
            continue;
	}
    }

    // sanity
    if (p_offset_map.size() != p_ordered_keys.size()) {
	std::cerr << "indexing alignment error" << std::endl;
        p_valid = false;
    }

    std::cout << "  DEBUG: indexed " << p_offset_map.size() << " shots in " << p_filename << "\n";
}

std::optional<uint64_t> ShotIndex::lookup(uint64_t rayHash) const noexcept {
    auto it = p_offset_map.find(rayHash);
    if (it == p_offset_map.end()) 
	return std::nullopt;

    return it->second;
}

std::optional<Shot> ShotIndex::getShot(uint64_t rayHash) const {
    // find the offset
    auto maybe_offset = lookup(rayHash);
    if (!maybe_offset) 
        return std::nullopt;
    uint64_t offset = *maybe_offset;

    // open a short-lived ifstream so we don't clobber the main index file
    std::ifstream in(p_filename, std::ios::binary);
    if (!in.is_open()) 
        return std::nullopt;

    // seek & read
    in.seekg(offset, std::ios::beg);
    std::string line;
    if (!std::getline(in, line) || line.empty()) 
        return std::nullopt;

    try {
        Shot shot = shot_utils::parse_json_shot(line);

        return shot;
    } catch (...) {
        // parse error or missing fields
        return std::nullopt;
    }
}


/**********************************/
/***** ComparisonResult class *****/
/**********************************/
ComparisonResult::ComparisonResult(const ShotIndex& idxA,
                                   const ShotIndex& idxB,
                                   double tolerance,
                                   int nThreads) : p_idxA(&idxA), p_idxB(&idxB), p_tolerance(tolerance) {
    // prepare threading parameters
    const auto& keysA = p_idxA->orderedKeys();
    size_t total_indices = keysA.size();
    nThreads = (nThreads == 0) ? bu_avail_cpus() : nThreads;	// 0 implies maximize cpu
    size_t per = total_indices / nThreads;                      // indices per thread

    // upfront reserve max size for our arrays
    p_differing.reserve(total_indices);
    p_onlyA.reserve(total_indices);
    p_onlyB.reserve(total_indices);

    // spawn workers, each handling a slice of keysA
    std::vector<std::thread> workers;
    workers.reserve(nThreads);
    for (unsigned t = 0; t < nThreads; ++t) {
        size_t begin = t * per;
        // last thread catches non-even division
        size_t end = (t == nThreads - 1)
		   ? total_indices
		   : begin + per;

        workers.emplace_back([this, &keysA, begin, end]() {
            for (size_t i = begin; i < end; ++i) {
                uint64_t hash = keysA[i].second;
                p_compareOne(hash);
            }
        });
    }

    // join all threads
    for (auto &th : workers) {
        th.join();
    }

    // now catch any rays in B not present in A
    for (auto const& [offsetB, hash] : p_idxB->orderedKeys()) {
        if (!p_idxA->lookup(hash)) {
            std::lock_guard<std::mutex> lock(p_mtxResult);
            p_onlyB.emplace_back(hash);
        }
    }
}

void ComparisonResult::p_compareOne(uint64_t rayHash) {
    // load from A
    auto maybe_A = p_idxA->getShot(rayHash);
    if (!maybe_A) {
        // parse error. now what?
        return;
    }
    Shot shotA = std::move(*maybe_A);

    // load from B
    auto maybe_B = p_idxB->getShot(rayHash);
    if (!maybe_B) {
        // couldn't find in B
        std::lock_guard<std::mutex> lock(p_mtxResult);
        p_onlyA.emplace_back(rayHash);
        return;
    }
    Shot shotB = std::move(*maybe_B);

    if (!shot_utils::shot_equal_at_tol(&shotA, &shotB, p_tolerance)) {
        std::lock_guard<std::mutex> lock(p_mtxResult);
        p_differing.emplace_back(rayHash);
    }
}

void ComparisonResult::summary(const std::string& filename) const {
    // TODO: add verbosity levels
    std::cout << "Used diff tolerance: " << p_tolerance << "\n";

    if (this->differences()) {
        // log summary to cout
        std::cout << "Difference(s) found.\n";

        // categorize differences
        if (!p_differing.empty()) {
            std::cout << "\t(" << p_differing.size() << ") shots with unequal hit data.\n";
            this->writeOnlyDiffering(filename);
        }

        if (!p_onlyA.empty()) {
            std::cout << "\t(" << p_onlyA.size() << ") shots only in " << p_idxA->filename() << ".\n";
            this->writeOnlyA(filename);
        }

        if (!p_onlyB.empty()) {
            std::cout << "\t(" << p_onlyA.size() << ") shots only in " << p_idxB->filename() << ".\n";
            this->writeOnlyB(filename);
        }

        // 'total'
        int total_in_A = p_idxA->orderedKeys().size();  // assumes sizeA == sizeB
        double percent_diff = (double)this->differences() / (double)total_in_A * 100.0;
        std::cout << "\ttotal differences: " << this->differences() << " / " << total_in_A << " = ~" << std::fixed << std::setprecision(2) << percent_diff << "%\n";
        std::cout << "See " << filename << " for full differences.\n";
    } else {
	std::cout << "No differences found\n";
    }
}

void ComparisonResult::writeOnlyDiffering(const std::string& filename) const {
    std::ofstream out(filename, std::ios::out);
    out << "** differing shots [" << p_differing.size() << "] **\n";
    out << std::fixed << std::setprecision(17);
    for (uint64_t hash : p_differing) {
        // TODO: is this all we want to log for differing?
        Shot shot = p_idxA->getShot(hash).value();
        out << "xyz " << shot.ray.pt[X] << " " << shot.ray.pt[Y] << " " << shot.ray.pt[Z] << "\n" <<
               "dir " << shot.ray.dir[X] << " " << shot.ray.dir[Y] << " " << shot.ray.dir[Z] << "\n";
    }
}

void ComparisonResult::writeOnlyA(const std::string& filename) const {
    std::ofstream out(filename, std::ios::out);
    out << "** shots only in " << p_idxA->filename() << " [" << p_onlyA.size() << "] **\n";
    out << std::fixed << std::setprecision(17);
    for (uint64_t hash : p_onlyA) {
        Shot shot = p_idxA->getShot(hash).value();
        out << "xyz " << shot.ray.pt[X] << " " << shot.ray.pt[Y] << " " << shot.ray.pt[Z] << "\n" <<
               "dir " << shot.ray.dir[X] << " " << shot.ray.dir[Y] << " " << shot.ray.dir[Z] << "\n";
    }
}

void ComparisonResult::writeOnlyB(const std::string& filename) const {
    std::ofstream out(filename, std::ios::out);
    out << "** shots only in " << p_idxB->filename() << " [" << p_onlyB.size() << "] **\n";
    out << std::fixed << std::setprecision(17);
    for (uint64_t hash : p_onlyB) {
        Shot shot = p_idxB->getShot(hash).value();
        out << "xyz " << shot.ray.pt[X] << " " << shot.ray.pt[Y] << " " << shot.ray.pt[Z] << "\n" <<
               "dir " << shot.ray.dir[X] << " " << shot.ray.dir[Y] << " " << shot.ray.dir[Z] << "\n";
    }
}


/*****************************************/
/***** shot helper utility functions *****/
/*****************************************/
uint64_t shot_utils::hash_ray(const Shot::Ray& ray) noexcept {
    struct bu_vls rstr = BU_VLS_INIT_ZERO;
    bu_vls_sprintf(&rstr, "%0.15f%0.15f%0.15f%0.15f%0.15f%0.15f", V3ARGS(ray.pt), V3ARGS(ray.dir));
    unsigned long long hash = bu_data_hash((void *)bu_vls_cstr(&rstr), bu_vls_strlen(&rstr));
    bu_vls_free(&rstr);

    return hash;
}

// Helper to find `"KEY":"<NUMBER>"` and parse the number into `out`.
// Returns the updated pointer (or NULL on failure).
inline const char* _parse_quoted_double(const char* p,
                                       const char* end,
                                       const char* pattern,
                                       double &out) noexcept
{
    p = std::strstr(p, pattern);
    if (!p) return NULL;
    p += std::strlen(pattern);
    auto r = std::from_chars(p, end, out);

    return (r.ec == std::errc()) ? r.ptr : NULL;
}

// Helper to parse three quoted coords X,Y,Z into arr[0..2].
// Assumes pattern `"X":"`, `"Y":"`, `"Z":"` in that order.
inline const char* _parse_xyz(const char* p,
                             const char* end,
                             double arr[3]) noexcept {
    // X
    p = _parse_quoted_double(p, end, "\"X\":\"", arr[0]);
    if (!p) return NULL;
    p++;    // skip closing "

    // Y
    p = _parse_quoted_double(p, end, "\"Y\":\"", arr[1]);
    if (!p) return NULL;
    p++;    // skip closing "

    // Z
    p = _parse_quoted_double(p, end, "\"Z\":\"", arr[2]);
    p++;    // skip closing "
    
    return p;
}

inline const char* _parse_xyz_fields(const char* p,
                                    const char* end,
                                    const char* key,
                                    double arr[3]) noexcept {
    // look for quoted key (ie "KEY":{WHAT_WE_WANT})
    p = std::strstr(p, key);
    if (!p) return NULL;
    p += std::strlen(key) + 3; // skip closing quote, colon, open-paren ("KEY":{ ..);

    p = _parse_xyz(p, end, arr);

    // sanity: we should be at our closing brace
    return (!p || (*p != '}')) ? NULL : p+1;
}

Shot::Ray shot_utils::parse_json_ray(const std::string& jsonLine) {
    // NOTE: assumes the form (ordering, naming, and quoting matter):
    // {..json.. "ray_dir":"{"X":"VAL","Z":"VAL","Z":"VAL"},"ray_pt":{"X":"VAL","Z":"VAL","Z":"VAL"}}
    Shot::Ray ray{0.0};
    const char *p   = jsonLine.c_str();
    const char *end = p + jsonLine.size();

    // parse ray_dir
    p = _parse_xyz_fields(p, end, "ray_dir", ray.dir);
    if (!p) return ray;

    // parse ray_pt
    p = _parse_xyz_fields(p, end, "ray_pt", ray.pt);
    if (!p) return ray;

    return ray;
}

Shot shot_utils::parse_json_shot(const std::string &jsonLine) {
    Shot shot{ Shot::Ray(0.0) };
    const char *p   = jsonLine.c_str();
    const char *end = p + jsonLine.size();

    /* partitions */
    p = std::strstr(p, "\"partitions\":[");
    if (p) {
        p += std::strlen("\"partitions\":[");
        while (true) {
            // skip until next '{' or ']' (end of entry or array)
            while (*p && *p != '{' && *p != ']') ++p;
            if (*p != '{') break;   // no more partitions

            Shot::Partition part{0.0};

            // in_dist
            p = _parse_quoted_double(p, end, "\"in_dist\":\"", part.in_dist);
            if (!p) break;
            // in_norm
            p = _parse_xyz_fields(p, end, "in_norm", part.in_norm);
            if (!p) break;
            // in_pt
            p = _parse_xyz_fields(p, end, "in_pt", part.in);
            if (!p) break;
            // out_dist
            p = _parse_quoted_double(p, end, "\"out_dist\":\"", part.out_dist);
            // out_norm
            p = _parse_xyz_fields(p, end, "out_norm", part.out_norm);
            if (!p) break;
            // out_pt
            p = _parse_xyz_fields(p, end, "out_pt", part.out);
            if (!p) break;

            // region
            p = std::strstr(p, "\"region\":\"");
            if (p) {
                p += std::strlen("\"region\":\"");
                const char *start = p;
                const char *term  = std::strchr(p, '"');
                if (term) {
                    part.region.assign(start, term - start);
                    p = term + 1;
                }
            }

            shot.parts.push_back(std::move(part));

            // advance past the closing '}' of this partition
            p = std::strchr(p, '}');
            if (!p) break;
            ++p;
        }
    }

    /* ray pt and dir */
    Shot::Ray ray = shot_utils::parse_json_ray(jsonLine);
    VMOVE(shot.ray.dir, ray.dir);
    VMOVE(shot.ray.pt, ray.pt);

    return shot;
}

bool shot_utils::shot_equal_at_tol(const Shot* shotA, const Shot* shotB, const double tol) {
    // compare ray origin & direction
    if (!VNEAR_EQUAL(shotA->ray.pt,  shotB->ray.pt,  tol) ||
        !VNEAR_EQUAL(shotA->ray.dir, shotB->ray.dir, tol)) {
        return false;
    }

    // compare number of partitions
    if (shotA->parts.size() != shotB->parts.size()) {
        return false;
    }

    // compare each partition
    for (size_t i = 0; i < shotA->parts.size(); ++i) {
        const auto &pa = shotA->parts[i];
        const auto &pb = shotB->parts[i];

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