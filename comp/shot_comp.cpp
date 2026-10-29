#include "comp/shot_comp.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <thread>
#include <unordered_set>

#include <brlcad/bu.h>

#include "util/numeric.h"

std::uint8_t
Shot::mismatchClasses(const Shot& other, double tolerance) const
{
    std::uint8_t mismatches = NoMismatch;
    if (!rtcmp_numeric::vector_equal(ray.pt, other.ray.pt, tolerance)
	|| !rtcmp_numeric::vector_equal(ray.dir, other.ray.dir, tolerance))
	mismatches |= NumericMismatch;
    if (parts.size() != other.parts.size())
	mismatches |= PartitionCountMismatch;

    const std::size_t common_count = std::min(parts.size(), other.parts.size());
    for (std::size_t index = 0; index < common_count; ++index) {
	const Partition& baseline = parts[index];
	const Partition& candidate = other.parts[index];
	if (baseline.region != candidate.region)
	    mismatches |= RegionMismatch;
	if (!rtcmp_numeric::vector_equal(baseline.in, candidate.in, tolerance)
	    || !rtcmp_numeric::vector_equal(baseline.in_norm, candidate.in_norm, tolerance)
	    || !rtcmp_numeric::equal(baseline.in_dist, candidate.in_dist, tolerance)
	    || !rtcmp_numeric::vector_equal(baseline.out, candidate.out, tolerance)
	    || !rtcmp_numeric::vector_equal(baseline.out_norm, candidate.out_norm, tolerance)
	    || !rtcmp_numeric::equal(baseline.out_dist, candidate.out_dist, tolerance))
	    mismatches |= NumericMismatch;
    }

    if (segments_present != other.segments_present) {
	mismatches |= PrimitiveMismatch;
	return mismatches;
    }
    if (!segments_present)
	return mismatches;
    if (segments.size() != other.segments.size())
	mismatches |= SegmentCountMismatch;

    const auto segment_mismatches = [tolerance](const Segment& baseline,
					       const Segment& candidate) {
	// librt preserves unevaluable primitive segments with null geometry;
	// matching NaN distances describe the same captured failure condition.
	const auto segment_number_equal = [tolerance](double left, double right) {
	    return (std::isnan(left) && std::isnan(right))
		|| rtcmp_numeric::equal(left, right, tolerance);
	};
	std::uint8_t result = NoMismatch;
	if (baseline.primitive != candidate.primitive
	    || baseline.in_surfno != candidate.in_surfno
	    || baseline.out_surfno != candidate.out_surfno
	    || baseline.geometry_valid != candidate.geometry_valid)
	    result |= PrimitiveMismatch;
	for (std::size_t index = 0; index < baseline.transform.size(); ++index) {
	    if (!rtcmp_numeric::equal(baseline.transform[index], candidate.transform[index], tolerance))
		result |= SegmentNumericMismatch;
	}
	if (!segment_number_equal(baseline.in_dist, candidate.in_dist)
	    || !segment_number_equal(baseline.out_dist, candidate.out_dist))
	    result |= SegmentNumericMismatch;
	if (baseline.geometry_valid && candidate.geometry_valid
	    && (!rtcmp_numeric::vector_equal(baseline.in, candidate.in, tolerance)
		|| !rtcmp_numeric::vector_equal(baseline.out, candidate.out, tolerance)
		|| !rtcmp_numeric::vector_equal(baseline.in_norm, candidate.in_norm, tolerance)
		|| !rtcmp_numeric::vector_equal(baseline.out_norm, candidate.out_norm, tolerance)))
	    result |= SegmentNumericMismatch;
	return result;
    };

    const std::size_t segment_count = segments.size();
    if (segment_count == other.segments.size()) {
	std::vector<int> matched(segment_count, -1);
	std::function<bool(std::size_t, std::vector<bool>&)> find_match =
	    [&](std::size_t baseline_index, std::vector<bool>& seen) {
		for (std::size_t candidate_index = 0; candidate_index < segment_count;
		     ++candidate_index) {
		    if (seen[candidate_index]
			|| segment_mismatches(segments[baseline_index],
			    other.segments[candidate_index]) != NoMismatch)
			continue;
		    seen[candidate_index] = true;
		    if (matched[candidate_index] < 0
			|| find_match(static_cast<std::size_t>(matched[candidate_index]), seen)) {
			matched[candidate_index] = static_cast<int>(baseline_index);
			return true;
		    }
		}
		return false;
	    };
	for (std::size_t index = 0; index < segment_count; ++index) {
	    std::vector<bool> seen(segment_count, false);
	    if (find_match(index, seen))
		continue;
	    std::uint8_t closest = PrimitiveMismatch | SegmentNumericMismatch;
	    for (const Segment& candidate : other.segments)
		closest &= segment_mismatches(segments[index], candidate);
	    mismatches |= closest == NoMismatch
		? static_cast<std::uint8_t>(PrimitiveMismatch | SegmentNumericMismatch)
		: closest;
	    break;
	}
    }
    return mismatches;
}

namespace {

constexpr std::uint8_t READ_ERROR = 1U << 7;
constexpr std::size_t JSON_UNICODE_DIGITS = 4;

class ShotParser {
public:
    explicit ShotParser(const std::string& input) : m_input(input) {}

    bool parse(Shot& shot)
    {
	shot = {};
	if (!consume('{') || !consume_key("partitions") || !consume('['))
	    return false;

	skip_whitespace();
	if (!peek(']')) {
	    while (true) {
		Shot::Partition partition;
		if (!parse_partition(partition))
		    return false;
		shot.parts.push_back(std::move(partition));
		skip_whitespace();
		if (!consume_if(','))
		    break;
	    }
	}
	if (!consume(']') || !consume(','))
	    return false;

	std::string next_key;
	if (!parse_string(next_key) || !consume(':'))
	    return false;
	if (next_key == "segments") {
	    shot.segments_present = true;
	    if (!parse_segments(shot.segments) || !consume(',') || !consume_key("ray_dir"))
		return false;
	} else if (next_key != "ray_dir") {
	    return fail("unexpected or out-of-order object field");
	}

	if (!parse_vector(shot.ray.dir, true) || !consume(',') || !consume_key("ray_pt")
	    || !parse_vector(shot.ray.pt, true) || !consume('}'))
	    return false;

	skip_whitespace();
	return m_position == m_input.size() || fail("trailing data after shot object");
    }

    const std::string& error() const noexcept { return m_error; }

private:
    bool parse_partition(Shot::Partition& partition)
    {
	return consume('{') && consume_key("in_dist") && parse_quoted_double(partition.in_dist)
	    && consume(',') && consume_key("in_norm") && parse_vector(partition.in_norm, true)
	    && consume(',') && consume_key("in_pt") && parse_vector(partition.in, false)
	    && consume(',') && consume_key("out_dist") && parse_quoted_double(partition.out_dist)
	    && consume(',') && consume_key("out_norm") && parse_vector(partition.out_norm, true)
	    && consume(',') && consume_key("out_pt") && parse_vector(partition.out, false)
	    && consume(',') && consume_key("region") && parse_string(partition.region)
	    && consume('}') && validate_partition(partition);
    }

    bool parse_segments(std::vector<Shot::Segment>& segments)
    {
	if (!consume('['))
	    return false;
	skip_whitespace();
	if (!peek(']')) {
	    while (true) {
		Shot::Segment segment;
		if (!parse_segment(segment))
		    return false;
		segments.push_back(std::move(segment));
		skip_whitespace();
		if (!consume_if(','))
		    break;
	    }
	}
	return consume(']');
    }

    bool parse_segment(Shot::Segment& segment)
    {
	bool in_normal_null = false;
	bool in_point_null = false;
	bool out_normal_null = false;
	bool out_point_null = false;
	if (!consume('{') || !consume_key("primitive") || !parse_string(segment.primitive)
	    || !consume(',') || !consume_key("transform") || !parse_transform(segment.transform)
	    || !consume(',') || !consume_key("in_dist") || !parse_quoted_double(segment.in_dist)
	    || !consume(',') || !consume_key("in_norm")
	    || !parse_nullable_vector(segment.in_norm, in_normal_null)
	    || !consume(',') || !consume_key("in_pt")
	    || !parse_nullable_vector(segment.in, in_point_null)
	    || !consume(',') || !consume_key("in_surfno") || !parse_integer(segment.in_surfno)
	    || !consume(',') || !consume_key("out_dist") || !parse_quoted_double(segment.out_dist)
	    || !consume(',') || !consume_key("out_norm")
	    || !parse_nullable_vector(segment.out_norm, out_normal_null)
	    || !consume(',') || !consume_key("out_pt")
	    || !parse_nullable_vector(segment.out, out_point_null)
	    || !consume(',') || !consume_key("out_surfno") || !parse_integer(segment.out_surfno)
	    || !consume('}'))
	    return false;

	const bool all_null = in_normal_null && in_point_null && out_normal_null && out_point_null;
	const bool none_null = !in_normal_null && !in_point_null && !out_normal_null && !out_point_null;
	if (!all_null && !none_null)
	    return fail("inconsistent null primitive geometry");
	segment.geometry_valid = none_null;
	if (none_null && (!std::isfinite(segment.in_dist) || !std::isfinite(segment.out_dist)
		|| segment.in_dist > segment.out_dist))
	    return fail("invalid primitive segment interval");
	return true;
    }

    bool parse_transform(std::array<double, ELEMENTS_PER_MAT>& transform)
    {
	if (!consume('['))
	    return false;
	for (std::size_t index = 0; index < transform.size(); ++index) {
	    if (index != 0 && !consume(','))
		return false;
	    if (!parse_quoted_double(transform[index]) || !std::isfinite(transform[index]))
		return fail("non-finite primitive transform");
	}
	return consume(']');
    }

    bool parse_nullable_vector(double values[3], bool& was_null)
    {
	skip_whitespace();
	if (peek('n')) {
	    was_null = true;
	    return consume_literal("null");
	}
	was_null = false;
	return parse_vector(values, true);
    }

    bool parse_vector(double values[3], bool finite_only)
    {
	if (!(consume('{') && consume_key("X") && parse_quoted_double(values[0])
	    && consume(',') && consume_key("Y") && parse_quoted_double(values[1])
	    && consume(',') && consume_key("Z") && parse_quoted_double(values[2])
	    && consume('}')))
	    return false;
	return !finite_only || rtcmp_numeric::finite_vector(values)
	    || fail("non-finite vector component");
    }

    bool parse_quoted_double(double& value)
    {
	std::string text;
	if (!parse_string(text))
	    return false;
	if (text == "inf") {
	    value = std::numeric_limits<double>::infinity();
	    return true;
	}
	if (text == "-inf") {
	    value = -std::numeric_limits<double>::infinity();
	    return true;
	}
	if (text == "nan" || text == "+nan" || text == "nan(ind)" || text == "+nan(ind)") {
	    value = std::numeric_limits<double>::quiet_NaN();
	    return true;
	}
	if (text == "-nan" || text == "-nan(ind)") {
	    value = -std::numeric_limits<double>::quiet_NaN();
	    return true;
	}
	const char *begin = text.data();
	const char *end = begin + text.size();
	const std::from_chars_result parsed = std::from_chars(begin, end, value);
	if (parsed.ec != std::errc() || parsed.ptr != end)
	    return fail("invalid numeric value");
	return true;
    }

    bool parse_integer(int& value)
    {
	skip_whitespace();
	const char *begin = m_input.data() + m_position;
	const char *end = m_input.data() + m_input.size();
	const std::from_chars_result parsed = std::from_chars(begin, end, value);
	if (parsed.ec != std::errc() || parsed.ptr == begin)
	    return fail("invalid integer value");
	m_position = static_cast<std::size_t>(parsed.ptr - m_input.data());
	return true;
    }

    bool consume_literal(const char *literal)
    {
	skip_whitespace();
	const std::size_t length = std::char_traits<char>::length(literal);
	if (m_input.compare(m_position, length, literal) != 0)
	    return fail("invalid JSON literal");
	m_position += length;
	return true;
    }

    bool validate_partition(const Shot::Partition& partition)
    {
	if (std::isnan(partition.in_dist) || std::isnan(partition.out_dist))
	    return fail("NaN partition distance");
	const bool unbounded_in = std::isinf(partition.in_dist) && std::signbit(partition.in_dist);
	const bool unbounded_out = std::isinf(partition.out_dist) && !std::signbit(partition.out_dist);
	if ((std::isinf(partition.in_dist) && !unbounded_in)
	    || (std::isinf(partition.out_dist) && !unbounded_out)
	    || partition.in_dist > partition.out_dist)
	    return fail("invalid partition interval");
	if (unbounded_in != rtcmp_numeric::sentinel_vector(partition.in, -1)
	    || unbounded_out != rtcmp_numeric::sentinel_vector(partition.out, 1))
	    return fail("partition endpoint does not match its distance");
	if ((!unbounded_in && !rtcmp_numeric::finite_vector(partition.in))
	    || (!unbounded_out && !rtcmp_numeric::finite_vector(partition.out)))
	    return fail("invalid partition endpoint");
	return true;
    }

    bool parse_string(std::string& value)
    {
	skip_whitespace();
	if (m_position >= m_input.size() || m_input[m_position] != '"')
	    return fail("expected JSON string");
	++m_position;
	value.clear();
	while (m_position < m_input.size()) {
	    const unsigned char character = static_cast<unsigned char>(m_input[m_position++]);
	    if (character == '"')
		return true;
	    if (character < 0x20U)
		return fail("unescaped control character in JSON string");
	    if (character != '\\') {
		value.push_back(static_cast<char>(character));
		continue;
	    }
	    if (m_position >= m_input.size())
		return fail("incomplete JSON escape");
	    switch (m_input[m_position++]) {
		case '"': value.push_back('"'); break;
		case '\\': value.push_back('\\'); break;
		case '/': value.push_back('/'); break;
		case 'b': value.push_back('\b'); break;
		case 'f': value.push_back('\f'); break;
		case 'n': value.push_back('\n'); break;
		case 'r': value.push_back('\r'); break;
		case 't': value.push_back('\t'); break;
		case 'u': if (!parse_unicode_escape(value)) return false; break;
		default: return fail("invalid JSON escape");
	    }
	}
	return fail("unterminated JSON string");
    }

    bool parse_unicode_escape(std::string& value)
    {
	std::uint32_t codepoint = 0;
	if (!parse_hex_quad(codepoint))
	    return false;
	if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
	    if (m_position + 2 > m_input.size() || m_input[m_position] != '\\'
		|| m_input[m_position + 1] != 'u')
		return fail("missing low surrogate in JSON escape");
	    m_position += 2;
	    std::uint32_t low = 0;
	    if (!parse_hex_quad(low) || low < 0xdc00U || low > 0xdfffU)
		return fail("invalid low surrogate in JSON escape");
	    codepoint = 0x10000U + ((codepoint - 0xd800U) << 10) + (low - 0xdc00U);
	} else if (codepoint >= 0xdc00U && codepoint <= 0xdfffU) {
	    return fail("unexpected low surrogate in JSON escape");
	}
	append_utf8(value, codepoint);
	return true;
    }

    bool parse_hex_quad(std::uint32_t& value)
    {
	if (m_position + JSON_UNICODE_DIGITS > m_input.size())
	    return fail("incomplete Unicode escape");
	value = 0;
	for (std::size_t digit = 0; digit < JSON_UNICODE_DIGITS; ++digit) {
	    const char character = m_input[m_position++];
	    value <<= 4;
	    if (character >= '0' && character <= '9')
		value += static_cast<std::uint32_t>(character - '0');
	    else if (character >= 'a' && character <= 'f')
		value += static_cast<std::uint32_t>(character - 'a' + 10);
	    else if (character >= 'A' && character <= 'F')
		value += static_cast<std::uint32_t>(character - 'A' + 10);
	    else
		return fail("invalid Unicode escape");
	}
	return true;
    }

    static void append_utf8(std::string& value, std::uint32_t codepoint)
    {
	if (codepoint <= 0x7fU) {
	    value.push_back(static_cast<char>(codepoint));
	} else if (codepoint <= 0x7ffU) {
	    value.push_back(static_cast<char>(0xc0U | (codepoint >> 6)));
	    value.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
	} else if (codepoint <= 0xffffU) {
	    value.push_back(static_cast<char>(0xe0U | (codepoint >> 12)));
	    value.push_back(static_cast<char>(0x80U | ((codepoint >> 6) & 0x3fU)));
	    value.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
	} else {
	    value.push_back(static_cast<char>(0xf0U | (codepoint >> 18)));
	    value.push_back(static_cast<char>(0x80U | ((codepoint >> 12) & 0x3fU)));
	    value.push_back(static_cast<char>(0x80U | ((codepoint >> 6) & 0x3fU)));
	    value.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
	}
    }

    bool consume_key(const char *expected)
    {
	std::string key;
	if (!parse_string(key))
	    return false;
	if (key != expected)
	    return fail("unexpected or out-of-order object field");
	return consume(':');
    }

    bool consume(char expected)
    {
	skip_whitespace();
	if (m_position >= m_input.size() || m_input[m_position] != expected)
	    return fail(std::string("expected '") + expected + "'");
	++m_position;
	return true;
    }

    bool consume_if(char expected)
    {
	skip_whitespace();
	if (m_position >= m_input.size() || m_input[m_position] != expected)
	    return false;
	++m_position;
	return true;
    }

    bool peek(char expected)
    {
	skip_whitespace();
	return m_position < m_input.size() && m_input[m_position] == expected;
    }

    void skip_whitespace()
    {
	while (m_position < m_input.size()) {
	    const char character = m_input[m_position];
	    if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
		break;
	    ++m_position;
	}
    }

    bool fail(const std::string& message)
    {
	if (m_error.empty())
	    m_error = message + " at byte " + std::to_string(m_position);
	return false;
    }

    const std::string& m_input;
    std::size_t m_position = 0;
    std::string m_error;
};

void
append_coverage_json(std::ostringstream& output, const ShotCoverage& coverage)
{
    output << "{\"total_records\":" << coverage.total_records
	   << ",\"observed_count\":" << coverage.observed_count
	   << ",\"malformed_count\":" << coverage.malformed_count
	   << ",\"duplicate_count\":" << coverage.duplicate_count
	   << ",\"unknown_count\":" << coverage.unknown_count
	   << ",\"missing_count\":" << coverage.missing_count
	   << ",\"io_error\":" << (coverage.io_error ? "true" : "false") << '}';
}

} // namespace

namespace shot_utils {

bool
parse_json_shot(const std::string& line, Shot& shot, std::string& error)
{
    ShotParser parser(line);
    if (!parser.parse(shot)) {
	error = parser.error();
	return false;
    }
    error.clear();
    return true;
}

} // namespace shot_utils

ShotIndex::ShotIndex(const std::string& filename, const std::vector<rtutil::RayKey>& expected_keys)
    : m_filename(filename)
{
    std::ifstream input(filename, std::ios::binary);
    if (!input.is_open()) {
	m_coverage.io_error = true;
	m_error_message = "unable to open result file: " + filename;
	m_coverage.missing_count = expected_keys.size();
	return;
    }

    const std::unordered_set<rtutil::RayKey, rtutil::RayKeyHash> expected(
	expected_keys.begin(), expected_keys.end());
    std::unordered_set<rtutil::RayKey, rtutil::RayKeyHash> seen;
    seen.reserve(expected_keys.size());
    m_offsets.reserve(expected_keys.size());

    std::string line;
    while (true) {
	const std::streamoff offset = static_cast<std::streamoff>(input.tellg());
	if (!std::getline(input, line))
	    break;
	++m_coverage.total_records;

	Shot shot;
	std::string parse_error;
	if (!shot_utils::parse_json_shot(line, shot, parse_error)) {
	    ++m_coverage.malformed_count;
	    if (m_error_message.empty())
		m_error_message = "malformed result record "
		    + std::to_string(m_coverage.total_records) + ": " + parse_error;
	    continue;
	}
	if (m_schema_seen && shot.segments_present != m_has_segments) {
	    ++m_coverage.malformed_count;
	    if (m_error_message.empty())
		m_error_message = "result file mixes primitive and evaluated-only records";
	    continue;
	}
	m_has_segments = shot.segments_present;
	m_schema_seen = true;

	const rtutil::RayKey key = rtutil::ray_key(shot.ray.pt, shot.ray.dir);
	if (!seen.insert(key).second) {
	    ++m_coverage.duplicate_count;
	    if (m_error_message.empty())
		m_error_message = "duplicate result for one ray";
	    continue;
	}
	if (expected.find(key) == expected.end()) {
	    ++m_coverage.unknown_count;
	    if (m_error_message.empty())
		m_error_message = "result contains a ray outside the canonical rayset; "
		    "lossy legacy ray coordinates are unsupported";
	    continue;
	}

	m_offsets.emplace(key, offset);
	++m_coverage.observed_count;
    }

    if (input.bad()) {
	m_coverage.io_error = true;
	if (m_error_message.empty())
	    m_error_message = "error while reading result file: " + filename;
    }
    m_coverage.missing_count = expected_keys.size() - m_coverage.observed_count;
}

bool
ShotIndex::read_shot(const rtutil::RayKey& key, std::ifstream& input, Shot& shot) const
{
    const auto found = m_offsets.find(key);
    if (found == m_offsets.end())
	return false;
    input.clear();
    input.seekg(found->second, std::ios::beg);
    std::string line;
    if (!std::getline(input, line))
	return false;
    std::string error;
    return shot_utils::parse_json_shot(line, shot, error);
}

const char *
correctness_state_name(CorrectnessState state) noexcept
{
    switch (state) {
	case CorrectnessState::Equivalent: return "equivalent";
	case CorrectnessState::Different: return "different";
	case CorrectnessState::Malformed: return "malformed";
	case CorrectnessState::Incomplete: return "incomplete";
	case CorrectnessState::IoError: return "io_error";
    }
    return "unknown";
}

CorrectnessComparison::CorrectnessComparison(const rtutil::RaySet& rayset,
					     const std::string& baseline_file,
					     const std::string& candidate_file,
					     double tolerance,
					     int thread_count)
    : m_rayset(rayset),
      m_expected_keys(rtutil::ray_keys(rayset)),
      m_baseline(baseline_file, m_expected_keys),
      m_candidate(candidate_file, m_expected_keys)
{
    m_result.expected_count = rayset.rays.size();
    m_result.tolerance = tolerance;
    m_result.baseline = m_baseline.coverage();
    m_result.candidate = m_candidate.coverage();

    if (m_result.baseline.io_error || m_result.candidate.io_error) {
	m_result.state = CorrectnessState::IoError;
	m_result.detail = m_result.baseline.io_error ? m_baseline.error_message()
					    : m_candidate.error_message();
    } else if (m_result.baseline.malformed() || m_result.candidate.malformed()) {
	m_result.state = CorrectnessState::Malformed;
	m_result.detail = m_result.baseline.malformed() ? m_baseline.error_message()
					      : m_candidate.error_message();
    } else if (m_result.baseline.missing_count != 0 || m_result.candidate.missing_count != 0) {
	m_result.state = CorrectnessState::Incomplete;
	m_result.detail = "one or both result files do not cover every canonical ray";
    } else if (m_baseline.has_segments() != m_candidate.has_segments()) {
	m_result.state = CorrectnessState::Malformed;
	m_result.detail = "primitive segments are present in only one result file";
    } else {
	compare(thread_count);
    }
}

void
CorrectnessComparison::compare(int thread_count)
{
    if (m_expected_keys.empty()) {
	m_result.state = CorrectnessState::Incomplete;
	m_result.detail = "canonical rayset is empty";
	return;
    }

    const int requested_threads = thread_count == 0 ? bu_avail_cpus() : std::max(thread_count, 1);
    const std::size_t worker_count = std::min(m_expected_keys.size(),
	static_cast<std::size_t>(std::max(requested_threads, 1)));
    m_mismatch_flags.assign(m_expected_keys.size(), Shot::NoMismatch);
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
	const std::size_t begin = worker_index * m_expected_keys.size() / worker_count;
	const std::size_t end = (worker_index + 1) * m_expected_keys.size() / worker_count;
	workers.emplace_back([this, begin, end]() {
	    std::ifstream baseline_input(m_baseline.filename(), std::ios::binary);
	    std::ifstream candidate_input(m_candidate.filename(), std::ios::binary);
	    if (!baseline_input.is_open() || !candidate_input.is_open()) {
		std::fill(m_mismatch_flags.begin() + begin, m_mismatch_flags.begin() + end, READ_ERROR);
		return;
	    }
	    for (std::size_t index = begin; index < end; ++index) {
		Shot baseline_shot;
		Shot candidate_shot;
		if (!m_baseline.read_shot(m_expected_keys[index], baseline_input, baseline_shot)
		    || !m_candidate.read_shot(m_expected_keys[index], candidate_input, candidate_shot)) {
		    m_mismatch_flags[index] = READ_ERROR;
		    continue;
		}
		m_mismatch_flags[index] = baseline_shot.mismatchClasses(candidate_shot, m_result.tolerance);
	    }
	});
    }
    for (std::thread& worker : workers)
	worker.join();

    summarize_mismatches();

    if (m_result.mismatches.read_errors != 0) {
	m_result.state = CorrectnessState::IoError;
	m_result.detail = "result files changed or became unreadable during comparison";
    } else if (m_result.mismatches.different_rays != 0) {
	m_result.state = CorrectnessState::Different;
    } else {
	m_result.state = CorrectnessState::Equivalent;
    }
}

void
CorrectnessComparison::summarize_mismatches()
{
    const std::size_t filtered_grazing = m_result.mismatches.filtered_grazing;
    m_result.mismatches = {};
    m_result.mismatches.filtered_grazing = filtered_grazing;
    m_different_indices.clear();
    for (std::size_t index = 0; index < m_mismatch_flags.size(); ++index) {
	const std::uint8_t flags = m_mismatch_flags[index];
	if ((flags & READ_ERROR) != 0) {
	    ++m_result.mismatches.read_errors;
	    continue;
	}
	if (flags == Shot::NoMismatch)
	    continue;
	++m_result.mismatches.different_rays;
	m_different_indices.push_back(index);
	if ((flags & Shot::PartitionCountMismatch) != 0)
	    ++m_result.mismatches.partition_count;
	if ((flags & Shot::RegionMismatch) != 0)
	    ++m_result.mismatches.region;
	if ((flags & Shot::NumericMismatch) != 0)
	    ++m_result.mismatches.numeric;
	if ((flags & Shot::SegmentCountMismatch) != 0)
	    ++m_result.mismatches.segment_count;
	if ((flags & Shot::PrimitiveMismatch) != 0)
	    ++m_result.mismatches.primitive;
	if ((flags & Shot::SegmentNumericMismatch) != 0)
	    ++m_result.mismatches.segment_numeric;
    }
}

void
CorrectnessComparison::filter_grazing(const GrazingPredicate& is_grazing)
{
    if (m_result.state != CorrectnessState::Different || !is_grazing)
	return;

    std::ifstream baseline_input(m_baseline.filename(), std::ios::binary);
    std::ifstream candidate_input(m_candidate.filename(), std::ios::binary);
    if (!baseline_input.is_open() || !candidate_input.is_open()) {
	m_result.state = CorrectnessState::IoError;
	m_result.detail = "unable to reopen result files for grazing analysis";
	return;
    }

    for (const std::size_t index : m_different_indices) {
	const std::uint8_t flags = m_mismatch_flags[index];
	if ((flags & (Shot::SegmentCountMismatch | Shot::PrimitiveMismatch
		      | Shot::SegmentNumericMismatch)) != 0)
	    continue;

	Shot baseline_shot;
	Shot candidate_shot;
	if (!m_baseline.read_shot(m_expected_keys[index], baseline_input, baseline_shot)
	    || !m_candidate.read_shot(m_expected_keys[index], candidate_input, candidate_shot)) {
	    m_result.state = CorrectnessState::IoError;
	    m_result.detail = "result files changed or became unreadable during grazing analysis";
	    return;
	}

	std::unordered_map<std::string, int> region_counts;
	for (const Shot::Partition& partition : baseline_shot.parts)
	    ++region_counts[partition.region];
	for (const Shot::Partition& partition : candidate_shot.parts)
	    --region_counts[partition.region];
	std::vector<std::string> changed_regions;
	for (const auto& count : region_counts) {
	    if (count.second != 0)
		changed_regions.push_back(count.first);
	}
	if (!changed_regions.empty() && is_grazing(baseline_shot.ray, changed_regions)) {
	    m_mismatch_flags[index] = Shot::NoMismatch;
	    ++m_result.mismatches.filtered_grazing;
	}
    }

    summarize_mismatches();
    m_result.state = m_result.mismatches.different_rays == 0
	? CorrectnessState::Equivalent : CorrectnessState::Different;
}

bool
CorrectnessComparison::write_differences(const std::string& filename) const
{
    std::vector<xray> rays;
    rays.reserve(m_different_indices.size());
    for (const std::size_t index : m_different_indices)
	rays.push_back(m_rayset.rays[index]);
    std::string content_hash;
    return rtutil::write_rays(filename, rays, content_hash).ok();
}

void
CorrectnessComparison::summary(std::ostream& output, const std::string& differences_file) const
{
    output << "Used diff tolerance: " << m_result.tolerance << '\n';
    if (m_result.mismatches.filtered_grazing != 0)
	output << "Filtered " << m_result.mismatches.filtered_grazing
	       << " likely grazing shot differences.\n";
    switch (m_result.state) {
	case CorrectnessState::Equivalent:
	    output << (m_result.mismatches.filtered_grazing == 0
		? "No differences found\n" : "No remaining differences found\n");
	    break;
	case CorrectnessState::Different:
	    output << "Difference(s) found.\n\t" << m_result.mismatches.different_rays
		   << " canonical rays differ.\n";
	    if (!differences_file.empty())
		output << "\n\tSee '" << differences_file << "' for all differing rays.\n";
	    break;
	case CorrectnessState::Malformed:
	case CorrectnessState::Incomplete:
	case CorrectnessState::IoError:
	    output << "Correctness comparison failed: " << m_result.detail << '\n';
	    break;
    }
}

std::string
correctness_status_json(const CorrectnessResult& result, const std::string& rayset_hash, int exit_code)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
	   << "{\"operation\":\"correctness\",\"state\":\""
	   << correctness_state_name(result.state) << "\",\"exit_code\":" << exit_code
	   << ",\"expected_count\":" << result.expected_count
	   << ",\"rayset_hash\":" << rtutil::json_quote(rayset_hash)
	   << ",\"tolerance\":" << result.tolerance << ",\"baseline\":";
    append_coverage_json(output, result.baseline);
    output << ",\"candidate\":";
    append_coverage_json(output, result.candidate);
    output << ",\"mismatches\":{\"different_rays\":" << result.mismatches.different_rays
	   << ",\"partition_count\":" << result.mismatches.partition_count
	   << ",\"region\":" << result.mismatches.region
	   << ",\"numeric\":" << result.mismatches.numeric
	   << ",\"segment_count\":" << result.mismatches.segment_count
	   << ",\"primitive\":" << result.mismatches.primitive
	   << ",\"segment_numeric\":" << result.mismatches.segment_numeric
	   << ",\"filtered_grazing\":" << result.mismatches.filtered_grazing
	   << ",\"read_errors\":" << result.mismatches.read_errors << '}';
    if (!result.detail.empty())
	output << ",\"detail\":" << rtutil::json_quote(result.detail);
    output << '}';
    return output.str();
}
