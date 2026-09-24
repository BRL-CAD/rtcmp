#include "shot_comp.h"
#include "grazing.h"

#include <thread>
#include <algorithm>
#include <functional>
#include <map>
#include <charconv>
#include <cmath>
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
bool ShotIndex::hasSegments() const noexcept { return p_has_segments; }
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
    bool schema_seen = false;
    uint64_t offset = 0;
    while (true) {
	offset = p_file.tellg();
	if (!std::getline(p_file, line)) 
	    break;
	
	// skip empty lines
	if (line.empty()) 
	    continue;

	try {
            const bool has_segments = line.find("\"segments\":[") != std::string::npos;
            if (schema_seen && has_segments != p_has_segments) {
                std::cerr << "Mixed primitive capture formats in " << p_filename << std::endl;
                p_valid = false;
                return;
            }
            p_has_segments = has_segments;
            schema_seen = true;
            if (has_segments) shot_utils::parse_json_shot(line);
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
            p_valid = false;
            return;
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

static std::optional<Shot> read_shot_at(std::ifstream& in, uint64_t offset) {
    in.clear();
    in.seekg(offset, std::ios::beg);
    std::string line;
    if (!std::getline(in, line) || line.empty())
        return std::nullopt;

    try {
        return shot_utils::parse_json_shot(line);
    } catch (...) {
        // parse error or missing fields
        return std::nullopt;
    }
}

std::optional<Shot> ShotIndex::getShot(uint64_t rayHash) const {
    auto maybe_offset = lookup(rayHash);
    if (!maybe_offset)
        return std::nullopt;

    // Use a separate stream so parallel lookups do not disturb the index.
    std::ifstream in(p_filename, std::ios::binary);
    if (!in.is_open())
        return std::nullopt;
    return read_shot_at(in, *maybe_offset);
}


/**********************************/
/***** ComparisonResult class *****/
/**********************************/
ComparisonResult::ComparisonResult(const ShotIndex& idxA,
                                   const ShotIndex& idxB,
                                   double tolerance,
                                   bool reportMissingRays,
                                   int nThreads) : p_idxA(&idxA), p_idxB(&idxB), p_tolerance(tolerance), p_reportMissingRays(reportMissingRays), p_totalRays(idxA.orderedKeys().size()) {
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

    // Now catch rays in B not present in A. Reuse one stream when checking
    // whether these records are misses, since there may be many of them.
    std::ifstream onlyBFile;
    if (!p_reportMissingRays)
        onlyBFile.open(p_idxB->filename(), std::ios::binary);
    for (auto const& [offsetB, hash] : p_idxB->orderedKeys()) {
        if (!p_idxA->lookup(hash)) {
            ++p_totalRays;
            if (p_reportMissingRays) {
                p_onlyB.emplace_back(hash);
            } else if (onlyBFile.is_open()) {
                auto shotB = read_shot_at(onlyBFile, offsetB);
                if (shotB && (!shotB->parts.empty() || !shotB->segments.empty()))
                    p_onlyB.emplace_back(hash);
            }
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
        // A recorded a miss may correspond to a skipped miss in B.
        if (p_reportMissingRays || !shotA.parts.empty() || !shotA.segments.empty()) {
            std::lock_guard<std::mutex> lock(p_mtxResult);
            p_onlyA.emplace_back(rayHash);
        }
        return;
    }
    Shot shotB = std::move(*maybe_B);

    unsigned kind = !shot_utils::shot_equal_at_tol(&shotA, &shotB, p_tolerance) ? PARTITIONS_DIFFER : 0U;
    if (p_idxA->hasSegments() &&
        !shot_utils::segments_equal_at_tol(&shotA, &shotB, p_tolerance)) kind |= SEGMENTS_DIFFER;
    if (kind) {
        std::lock_guard<std::mutex> lock(p_mtxResult);
        p_differing.emplace_back(rayHash);
        p_diff_kind.emplace(rayHash, kind);
    }
}

static std::vector<std::string> changed_regions(const Shot* a, const Shot* b)
{
    std::map<std::string, int> counts;
    if (a) {
        for (const auto& part : a->parts)
            ++counts[part.region];
    }
    if (b) {
        for (const auto& part : b->parts)
            --counts[part.region];
    }

    std::vector<std::string> changed;
    for (const auto& [region, count] : counts) {
        if (count != 0)
            changed.push_back(region);
    }
    return changed;
}

void ComparisonResult::filterGrazing(GrazingDetector& detector)
{
    auto filter = [&](std::vector<uint64_t>& keys, bool inA, bool inB) {
        keys.erase(std::remove_if(keys.begin(), keys.end(), [&](uint64_t key) {
            auto kind = p_diff_kind.find(key);
            if (kind != p_diff_kind.end() && (kind->second & SEGMENTS_DIFFER)) return false;
            auto shotA = inA ? p_idxA->getShot(key) : std::optional<Shot>{};
            auto shotB = inB ? p_idxB->getShot(key) : std::optional<Shot>{};
            if ((inA && !shotA) || (inB && !shotB))
                return false;

            const Shot* a = shotA ? &*shotA : nullptr;
            const Shot* b = shotB ? &*shotB : nullptr;
            auto regions = changed_regions(a, b);
            if (regions.empty())
                return false;

            const Shot::Ray& ray = a ? a->ray : b->ray;
            if (!detector.unstable(ray, regions))
                return false;
            ++p_filteredGrazing;
            return true;
        }), keys.end());
    };

    filter(p_differing, true, true);
    filter(p_onlyA, true, false);
    filter(p_onlyB, false, true);
}

void ComparisonResult::summary(const std::string& filename) const {
    // TODO: add verbosity levels
    std::cout << "Used diff tolerance: " << p_tolerance << "\n";
    if (p_filteredGrazing)
        std::cout << "Filtered " << p_filteredGrazing << " likely grazing shot differences.\n";
    std::ofstream(filename, std::ios::trunc).close();

    if (this->differences()) {
        // log summary to cout
        std::cout << "Difference(s) found.\n";

        // categorize differences
        if (!p_differing.empty()) {
            size_t evaluated = 0, primitive = 0, both = 0;
            for (uint64_t key : p_differing) {
                switch (p_diff_kind.at(key)) {
                    case PARTITIONS_DIFFER: ++evaluated; break;
                    case SEGMENTS_DIFFER: ++primitive; break;
                    case PARTITIONS_DIFFER | SEGMENTS_DIFFER: ++both; break;
                }
            }
            std::cout << "\t(" << evaluated << ") evaluated-only, (" << primitive
                      << ") primitive-only, (" << both << ") both-level differences.\n";
            this->writeOnlyDiffering(filename);
        }

        if (!p_onlyA.empty()) {
            std::cout << "\t(" << p_onlyA.size() << ") shots only in " << p_idxA->filename() << ".\n";
            this->writeOnlyA(filename);
        }

        if (!p_onlyB.empty()) {
            std::cout << "\t(" << p_onlyB.size() << ") shots only in " << p_idxB->filename() << ".\n";
            this->writeOnlyB(filename);
        }

        // 'total'
        double percent_diff = (double)this->differences() / (double)p_totalRays * 100.0;
        std::cout << "\ttotal differences: " << this->differences() << " / " << p_totalRays << " = ~" << std::fixed << std::setprecision(2) << percent_diff << "%\n";
        std::cout << "See " << filename << " for full differences.\n";
    } else {
        std::cout << (p_filteredGrazing ? "No remaining differences found\n" : "No differences found\n");
    }
}

void ComparisonResult::writeOnlyDiffering(const std::string& filename) const {
    std::ofstream out(filename, std::ios::app);
    out << "** differing shots [" << p_differing.size() << "] **\n";
    out << std::fixed << std::setprecision(17);
    for (uint64_t hash : p_differing) {
        Shot shot = p_idxA->getShot(hash).value();
        out << "xyz " << shot.ray.pt[X] << " " << shot.ray.pt[Y] << " " << shot.ray.pt[Z] << "\n" <<
               "dir " << shot.ray.dir[X] << " " << shot.ray.dir[Y] << " " << shot.ray.dir[Z]
               << " # " << (p_diff_kind.at(hash) == PARTITIONS_DIFFER ? "evaluated" :
                      p_diff_kind.at(hash) == SEGMENTS_DIFFER ? "primitive" : "both") << "\n";
    }
}

void ComparisonResult::writeOnlyA(const std::string& filename) const {
    std::ofstream out(filename, std::ios::app);
    out << "** shots only in " << p_idxA->filename() << " [" << p_onlyA.size() << "] **\n";
    out << std::fixed << std::setprecision(17);
    for (uint64_t hash : p_onlyA) {
        Shot shot = p_idxA->getShot(hash).value();
        out << "xyz " << shot.ray.pt[X] << " " << shot.ray.pt[Y] << " " << shot.ray.pt[Z] << "\n" <<
               "dir " << shot.ray.dir[X] << " " << shot.ray.dir[Y] << " " << shot.ray.dir[Z] << "\n";
    }
}

void ComparisonResult::writeOnlyB(const std::string& filename) const {
    std::ofstream out(filename, std::ios::app);
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

    if (jsonLine.find("\"segments\":[") != std::string::npos) {
        auto json = nlohmann::json::parse(jsonLine);
        auto number = [](const nlohmann::json &value) {
            const std::string s = value.get<std::string>();
            size_t end = 0;
            double result = std::stod(s, &end);
            if (end != s.size()) throw std::invalid_argument("invalid segment number");
            return result;
        };
        auto xyz = [&](const nlohmann::json &value, double *v) {
            v[X] = number(value.at("X"));
            v[Y] = number(value.at("Y"));
            v[Z] = number(value.at("Z"));
        };
        for (const auto &item : json.at("segments")) {
            Shot::Segment seg;
            seg.primitive = item.at("primitive").get<std::string>();
            const auto &matrix = item.at("transform");
            if (matrix.size() != seg.transform.size()) throw std::invalid_argument("invalid segment transform");
            for (size_t i = 0; i < seg.transform.size(); ++i) seg.transform[i] = number(matrix.at(i));
            seg.in_dist = number(item.at("in_dist"));
            seg.out_dist = number(item.at("out_dist"));
            seg.geometry_valid = !item.at("in_norm").is_null();
            if (seg.geometry_valid) {
                xyz(item.at("in_norm"), seg.in_norm);
                xyz(item.at("out_norm"), seg.out_norm);
                xyz(item.at("in_pt"), seg.in);
                xyz(item.at("out_pt"), seg.out);
            } else if (!item.at("out_norm").is_null() || !item.at("in_pt").is_null() ||
                       !item.at("out_pt").is_null()) {
                throw std::invalid_argument("inconsistent segment geometry");
            }
            seg.in_surfno = item.at("in_surfno").get<int>();
            seg.out_surfno = item.at("out_surfno").get<int>();
            shot.segments.push_back(std::move(seg));
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

bool shot_utils::segments_equal_at_tol(const Shot* a, const Shot* b, double tol)
{
    if (a->segments.size() != b->segments.size()) return false;
    auto near_number = [tol](double x, double y) {
        if (std::isnan(x) || std::isnan(y)) return std::isnan(x) && std::isnan(y);
        if (std::isinf(x) || std::isinf(y))
            return std::isinf(x) && std::isinf(y) && (std::signbit(x) == std::signbit(y));
        return NEAR_EQUAL(x, y, tol);
    };
    auto near_xyz = [&](const double *x, const double *y) {
        return near_number(x[X], y[X]) && near_number(x[Y], y[Y]) &&
               near_number(x[Z], y[Z]);
    };
    auto equal = [&](const Shot::Segment &x, const Shot::Segment &y) {
        if (x.primitive != y.primitive || x.in_surfno != y.in_surfno ||
            x.out_surfno != y.out_surfno || x.geometry_valid != y.geometry_valid) return false;
        for (size_t i = 0; i < x.transform.size(); ++i)
            if (!near_number(x.transform[i], y.transform[i])) return false;
        if (!near_number(x.in_dist, y.in_dist) || !near_number(x.out_dist, y.out_dist)) return false;
        return !x.geometry_valid || (near_xyz(x.in, y.in) && near_xyz(x.out, y.out) &&
               near_xyz(x.in_norm, y.in_norm) && near_xyz(x.out_norm, y.out_norm));
    };
    const size_t count = a->segments.size();
    std::vector<int> matched(count, -1);
    std::function<bool(size_t, std::vector<bool>&)> find_match = [&](size_t i, std::vector<bool> &seen) {
        for (size_t j = 0; j < count; ++j) {
            if (seen[j] || !equal(a->segments[i], b->segments[j])) continue;
            seen[j] = true;
            if (matched[j] < 0 || find_match(static_cast<size_t>(matched[j]), seen)) {
                matched[j] = static_cast<int>(i);
                return true;
            }
        }
        return false;
    };
    for (size_t i = 0; i < count; ++i) {
        std::vector<bool> seen(count, false);
        if (!find_match(i, seen)) return false;
    }
    return true;
}


bool Shot::operator==(const Shot& other) const
{
    return shot_utils::shot_equal_at_tol(this, &other, SMALL_FASTF) &&
        shot_utils::segments_equal_at_tol(this, &other, SMALL_FASTF);
}
