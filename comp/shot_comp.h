#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <fstream>
#include <mutex>                    // needed?

#include "json.hpp"                 // nlohmann::json
#include "Shot.h"                   // existing Shot, Ray, Partition definitions
#include "comp/compare_config.h"    // config struct (needed?)

/* Auxiliary helper functions. Handle method specific reading / writing */
namespace shot_utils {
    // computes hash on ray origin + direction
    uint64_t hash_ray(const Shot::Ray& ray) noexcept;

    // parse a JSON object {"X":...,"Y":...,"Z":...} into Shot::Ray
    Shot::Ray parse_json_ray(const std::string& line);

    // parse a NDJSON line into Shot
    Shot parse_json_shot(const std::string &line);

    // compares shots values within tolerance; returns true if equal
    // TODO: should probably be a Shot class function
    bool shot_equal_at_tol(const Shot* shotA, const Shot* shotB, const double tolerance);
};

/* Indexes a large NDJSON shot file into (file-offset, hash) pairs */
class ShotIndex {
public:
    explicit ShotIndex(const std::string& filename);
    ~ShotIndex();

    // returns whether shotIndex loaded indices successfully from filename
    bool isValid() const noexcept;

    // returns filename for associated shotIndex
    std::string filename() const noexcept;

    // order using file offset so we can iterate in sequential chunks
    const std::vector<std::pair<uint64_t, uint64_t>>& orderedKeys() const noexcept;  // <file_offset, ray_hash>

    // lookup rayHash; returns either map value or std::nullopt (thread safe)
    std::optional<uint64_t> lookup(uint64_t rayHash) const noexcept;
    
    // get full shot for rayHash; returns Shot or std::nullopt (NOT thread safe)
    std::optional<Shot> getShot(uint64_t rayHash) const;
private:
    std::string p_filename;
    std::ifstream p_file;
    bool p_valid{false};

    std::vector<std::pair<uint64_t, uint64_t>> p_ordered_keys;  // <file_offset, ray_hash>
    std::unordered_map<uint64_t, uint64_t> p_offset_map;        // hashed ray pt+dir -> file offset

    void p_buildIndex();                                        // main driver: iterate over file and load map
};

/* fully compare two ShotIndex at tolerance */
class ComparisonResult {
public:
    ComparisonResult(const ShotIndex& idxA,
                     const ShotIndex& idxB,
                     double tolerance,
                     int nThreads = 0);     // default (0): use all available CPU

    // returns total number of differences
    int differences() const noexcept { return p_differing.size() + p_onlyA.size() + p_onlyB.size(); }

    /* summarize differences
     * logs general info to cout, and specifics to 'filename'. */
    void summary(const std::string& filename) const;

    // individual results writers for !equal
    void writeOnlyDiffering(const std::string& filename) const;
    void writeOnlyA(const std::string& filename) const;
    void writeOnlyB(const std::string& filename) const;
private:
    // results (stored ray hash)
    std::vector<uint64_t> p_differing;  // same ray-hash but mismatched data
    std::vector<uint64_t> p_onlyA;      // in A but not in B
    std::vector<uint64_t> p_onlyB;      // in B but not in A

    // compare inputs
    const ShotIndex* p_idxA;
    const ShotIndex* p_idxB;
    const double p_tolerance;

    // protects writes into the shared vectors
    // TODO: bu_mutex
    std::mutex p_mtxResult;

    // compare one ray by hash: load from each index, then custom-compare
    void p_compareOne(uint64_t rayHash);
};
