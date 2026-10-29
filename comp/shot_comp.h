#ifndef RTCMP_SHOT_COMP_H
#define RTCMP_SHOT_COMP_H

#include <cstddef>
#include <fstream>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

#include "Shot.h"
#include "util/util.h"

namespace shot_utils {

bool parse_json_shot(const std::string& line, Shot& shot, std::string& error);

} // namespace shot_utils

struct ShotCoverage {
    std::size_t total_records = 0;
    std::size_t observed_count = 0;
    std::size_t malformed_count = 0;
    std::size_t duplicate_count = 0;
    std::size_t unknown_count = 0;
    std::size_t missing_count = 0;
    bool io_error = false;

    bool malformed() const noexcept
    {
	return malformed_count != 0 || duplicate_count != 0 || unknown_count != 0;
    }
};

class ShotIndex {
public:
    ShotIndex(const std::string& filename, const std::vector<rtutil::RayKey>& expected_keys);

    const ShotCoverage& coverage() const noexcept { return m_coverage; }
    const std::string& error_message() const noexcept { return m_error_message; }
    const std::string& filename() const noexcept { return m_filename; }
    bool has_segments() const noexcept { return m_has_segments; }
    bool read_shot(const rtutil::RayKey& key, std::ifstream& input, Shot& shot) const;

private:
    std::string m_filename;
    std::string m_error_message;
    ShotCoverage m_coverage;
    bool m_has_segments = false;
    bool m_schema_seen = false;
    std::unordered_map<rtutil::RayKey, std::streamoff, rtutil::RayKeyHash> m_offsets;
};

enum class CorrectnessState {
    Equivalent,
    Different,
    Malformed,
    Incomplete,
    IoError
};

const char *correctness_state_name(CorrectnessState state) noexcept;

struct CorrectnessMismatches {
    std::size_t different_rays = 0;
    std::size_t partition_count = 0;
    std::size_t region = 0;
    std::size_t numeric = 0;
    std::size_t segment_count = 0;
    std::size_t primitive = 0;
    std::size_t segment_numeric = 0;
    std::size_t filtered_grazing = 0;
    std::size_t read_errors = 0;
};

struct CorrectnessResult {
    CorrectnessState state = CorrectnessState::IoError;
    std::size_t expected_count = 0;
    double tolerance = 0.0;
    ShotCoverage baseline;
    ShotCoverage candidate;
    CorrectnessMismatches mismatches;
    std::string detail;
};

class CorrectnessComparison {
public:
    using GrazingPredicate = std::function<bool(
	const Shot::Ray&, const std::vector<std::string>&)>;

    CorrectnessComparison(const rtutil::RaySet& rayset,
			  const std::string& baseline_file,
			  const std::string& candidate_file,
			  double tolerance,
			  int thread_count = 0);

    const CorrectnessResult& result() const noexcept { return m_result; }
    void filter_grazing(const GrazingPredicate& is_grazing);
    bool write_differences(const std::string& filename) const;
    void summary(std::ostream& output, const std::string& differences_file = {}) const;

private:
    const rtutil::RaySet& m_rayset;
    std::vector<rtutil::RayKey> m_expected_keys;
    ShotIndex m_baseline;
    ShotIndex m_candidate;
    CorrectnessResult m_result;
    std::vector<std::size_t> m_different_indices;
    std::vector<std::uint8_t> m_mismatch_flags;

    void compare(int thread_count);
    void summarize_mismatches();
};

std::string correctness_status_json(const CorrectnessResult& result,
				    const std::string& rayset_hash,
				    int exit_code);

#endif // RTCMP_SHOT_COMP_H
