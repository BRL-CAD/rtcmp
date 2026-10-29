#include "util/util.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <locale>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_set>

#include <brlcad/bu/process.h>

#include "util/numeric.h"

namespace {

constexpr char HEADER_PREFIX[] = "**rays fired for ";
constexpr char HEADER_COUNT_SEPARATOR[] = " [";
constexpr char HEADER_SUFFIX[] = "]**";
constexpr char HEADER_LABEL[] = "canonical rayset";
constexpr char POINT_PREFIX[] = "xyz";
constexpr char DIRECTION_PREFIX[] = "dir";
constexpr char HASH_PREFIX[] = "rtcmp-rayset-v1-sha256:";
constexpr char HASH_DOMAIN[] = "rtcmp-rayset-ieee754-be-v1";
constexpr std::uintmax_t MINIMUM_RAY_RECORD_BYTES = 20;
constexpr std::size_t SHA256_BLOCK_BYTES = 64;
constexpr std::size_t SHA256_DIGEST_WORDS = 8;
constexpr std::size_t SHA256_ROUNDS = 64;
constexpr std::size_t SHA256_LENGTH_BYTES = 8;
constexpr std::uint32_t SHA256_INITIAL_STATE[SHA256_DIGEST_WORDS] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
};
constexpr std::uint32_t SHA256_ROUND_CONSTANTS[SHA256_ROUNDS] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};
constexpr std::size_t HASH_COMBINE_SHIFT_LEFT = 6;
constexpr std::size_t HASH_COMBINE_SHIFT_RIGHT = 2;
constexpr std::size_t HASH_COMBINE_CONSTANT =
    sizeof(std::size_t) == 8 ? static_cast<std::size_t>(0x9e3779b97f4a7c15ULL)
			     : static_cast<std::size_t>(0x9e3779b9UL);

class Sha256 {
public:
    Sha256()
    {
	std::copy(std::begin(SHA256_INITIAL_STATE), std::end(SHA256_INITIAL_STATE), m_state.begin());
    }

    void update(const char *data, std::size_t size)
    {
	for (std::size_t offset = 0; offset < size; ++offset) {
	    m_block[m_block_size++] = static_cast<std::uint8_t>(data[offset]);
	    if (m_block_size == SHA256_BLOCK_BYTES) {
		transform();
		m_bit_count += SHA256_BLOCK_BYTES * 8;
		m_block_size = 0;
	    }
	}
    }

    std::string finish()
    {
	m_bit_count += m_block_size * 8;
	m_block[m_block_size++] = 0x80U;
	if (m_block_size > SHA256_BLOCK_BYTES - SHA256_LENGTH_BYTES) {
	    while (m_block_size < SHA256_BLOCK_BYTES)
		m_block[m_block_size++] = 0;
	    transform();
	    m_block_size = 0;
	}
	while (m_block_size < SHA256_BLOCK_BYTES - SHA256_LENGTH_BYTES)
	    m_block[m_block_size++] = 0;
	for (std::size_t index = 0; index < SHA256_LENGTH_BYTES; ++index) {
	    const std::size_t shift = (SHA256_LENGTH_BYTES - 1 - index) * 8;
	    m_block[SHA256_BLOCK_BYTES - SHA256_LENGTH_BYTES + index] =
		static_cast<std::uint8_t>(m_bit_count >> shift);
	}
	transform();

	static constexpr char HEX_DIGITS[] = "0123456789abcdef";
	std::string digest;
	digest.reserve(SHA256_DIGEST_WORDS * sizeof(std::uint32_t) * 2);
	for (const std::uint32_t word : m_state) {
	    for (int shift = 28; shift >= 0; shift -= 4)
		digest.push_back(HEX_DIGITS[(word >> shift) & 0x0fU]);
	}
	return digest;
    }

private:
    static std::uint32_t rotate_right(std::uint32_t value, unsigned shift) noexcept
    {
	return (value >> shift) | (value << (32U - shift));
    }

    void transform()
    {
	std::array<std::uint32_t, SHA256_ROUNDS> words{};
	for (std::size_t index = 0; index < 16; ++index) {
	    const std::size_t byte = index * 4;
	    words[index] = (static_cast<std::uint32_t>(m_block[byte]) << 24)
		| (static_cast<std::uint32_t>(m_block[byte + 1]) << 16)
		| (static_cast<std::uint32_t>(m_block[byte + 2]) << 8)
		| static_cast<std::uint32_t>(m_block[byte + 3]);
	}
	for (std::size_t index = 16; index < SHA256_ROUNDS; ++index) {
	    const std::uint32_t sigma0 = rotate_right(words[index - 15], 7)
		^ rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3);
	    const std::uint32_t sigma1 = rotate_right(words[index - 2], 17)
		^ rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10);
	    words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
	}

	std::array<std::uint32_t, SHA256_DIGEST_WORDS> work = m_state;
	for (std::size_t index = 0; index < SHA256_ROUNDS; ++index) {
	    const std::uint32_t sum1 = rotate_right(work[4], 6) ^ rotate_right(work[4], 11)
		^ rotate_right(work[4], 25);
	    const std::uint32_t choice = (work[4] & work[5]) ^ (~work[4] & work[6]);
	    const std::uint32_t temporary1 = work[7] + sum1 + choice
		+ SHA256_ROUND_CONSTANTS[index] + words[index];
	    const std::uint32_t sum0 = rotate_right(work[0], 2) ^ rotate_right(work[0], 13)
		^ rotate_right(work[0], 22);
	    const std::uint32_t majority = (work[0] & work[1]) ^ (work[0] & work[2])
		^ (work[1] & work[2]);
	    const std::uint32_t temporary2 = sum0 + majority;

	    work[7] = work[6];
	    work[6] = work[5];
	    work[5] = work[4];
	    work[4] = work[3] + temporary1;
	    work[3] = work[2];
	    work[2] = work[1];
	    work[1] = work[0];
	    work[0] = temporary1 + temporary2;
	}
	for (std::size_t index = 0; index < m_state.size(); ++index)
	    m_state[index] += work[index];
    }

    std::array<std::uint8_t, SHA256_BLOCK_BYTES> m_block{};
    std::array<std::uint32_t, SHA256_DIGEST_WORDS> m_state{};
    std::uint64_t m_bit_count = 0;
    std::size_t m_block_size = 0;
};

rtutil::RayFileStatus
failure(rtutil::RayFileError error, std::size_t line, const std::string& message)
{
    return {error, line, message};
}

void
remove_carriage_return(std::string& line)
{
    if (!line.empty() && line.back() == '\r')
	line.pop_back();
}

bool
remove_regular_file_if_present(const std::filesystem::path& path)
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
	return false;
    if (!exists)
	return true;
    if (!std::filesystem::is_regular_file(path, error) || error)
	return false;
    return std::filesystem::remove(path, error) && !error;
}

bool
parse_count(const std::string& text, std::size_t& count)
{
    const char *begin = text.data();
    const char *end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, count);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

bool
is_non_finite_token(std::string token)
{
    std::transform(token.begin(), token.end(), token.begin(),
		   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (!token.empty() && (token.front() == '+' || token.front() == '-'))
	token.erase(token.begin());
    return token == "inf" || token == "infinity" || token == "nan"
	|| (token.size() > 5 && token.compare(0, 4, "nan(") == 0 && token.back() == ')');
}

rtutil::RayFileStatus
parse_header(const std::string& line, std::size_t& count)
{
    constexpr std::size_t prefix_length = sizeof(HEADER_PREFIX) - 1;
    constexpr std::size_t separator_length = sizeof(HEADER_COUNT_SEPARATOR) - 1;
    constexpr std::size_t suffix_length = sizeof(HEADER_SUFFIX) - 1;
    if (line.compare(0, prefix_length, HEADER_PREFIX) != 0
	|| line.size() < prefix_length + suffix_length)
	return failure(rtutil::RayFileError::InvalidHeader, 1, "unrecognized ray file header");

    const std::size_t count_start = line.rfind(HEADER_COUNT_SEPARATOR);
    const std::size_t suffix_start = line.size() - suffix_length;
    if (count_start == std::string::npos || count_start <= prefix_length
	|| count_start + separator_length >= suffix_start
	|| line.compare(suffix_start, suffix_length, HEADER_SUFFIX) != 0)
	return failure(rtutil::RayFileError::InvalidHeader, 1, "malformed ray file header");

    const std::size_t number_start = count_start + separator_length;
    if (!parse_count(line.substr(number_start, suffix_start - number_start), count) || count == 0)
	return failure(rtutil::RayFileError::InvalidCount, 1, "invalid declared ray count");
    return {};
}

rtutil::RayFileStatus
parse_record(const std::string& line, const char *expected_prefix, std::size_t line_number,
	     double values[3])
{
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    std::string prefix;
    std::string value_text[3];
    if (!(input >> prefix >> value_text[0] >> value_text[1] >> value_text[2]))
	return failure(rtutil::RayFileError::MalformedRecord, line_number, "malformed ray record");
    input >> std::ws;
    if (!input.eof())
	return failure(rtutil::RayFileError::MalformedRecord, line_number, "ray record has trailing fields");
    if (prefix != expected_prefix) {
	if (prefix == POINT_PREFIX || prefix == DIRECTION_PREFIX)
	    return failure(rtutil::RayFileError::BadOrdering, line_number,
			   "ray origin and direction are out of order");
	return failure(rtutil::RayFileError::MalformedRecord, line_number, "unknown ray record type");
    }

    for (std::size_t coordinate = 0; coordinate < 3; ++coordinate) {
	const char *begin = value_text[coordinate].data();
	const char *end = begin + value_text[coordinate].size();
	const std::from_chars_result parsed = std::from_chars(begin, end, values[coordinate]);
	if (parsed.ec != std::errc() || parsed.ptr != end) {
	    if (is_non_finite_token(value_text[coordinate]))
		return failure(rtutil::RayFileError::NonFiniteValue, line_number,
			       "ray record contains a non-finite value");
	    return failure(rtutil::RayFileError::MalformedRecord, line_number,
			   "ray record contains an invalid numeric value");
	}
	if (!std::isfinite(values[coordinate]))
	    return failure(rtutil::RayFileError::NonFiniteValue, line_number,
			   "ray record contains a non-finite value");
    }
    return {};
}

std::string
canonical_ray_text(const xray& ray)
{
    const auto canonical_zero = [](double value) { return value == 0.0 ? 0.0 : value; };
    return std::string(POINT_PREFIX) + ' ' + rtcmp_numeric::format(canonical_zero(ray.r_pt[0]))
	+ ' ' + rtcmp_numeric::format(canonical_zero(ray.r_pt[1]))
	+ ' ' + rtcmp_numeric::format(canonical_zero(ray.r_pt[2])) + '\n'
	+ DIRECTION_PREFIX + ' ' + rtcmp_numeric::format(canonical_zero(ray.r_dir[0]))
	+ ' ' + rtcmp_numeric::format(canonical_zero(ray.r_dir[1]))
	+ ' ' + rtcmp_numeric::format(canonical_zero(ray.r_dir[2])) + '\n';
}

bool
unit_direction(const double direction[3]) noexcept
{
    const double magnitude = std::hypot(direction[0], direction[1], direction[2]);
    return magnitude == 1.0 || (std::isfinite(magnitude)
	&& std::fabs(magnitude - 1.0) < VUNITIZE_TOL);
}

void
hash_double(Sha256& hash, double value)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t),
	"canonical rayset identity requires 64-bit doubles");
    static_assert(std::numeric_limits<double>::is_iec559,
	"canonical rayset identity requires IEEE-754 doubles");
    if (value == 0.0)
	value = 0.0;
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    std::array<char, sizeof(bits)> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
	const std::size_t shift = (bytes.size() - index - 1) * 8;
	bytes[index] = static_cast<char>((bits >> shift) & 0xffU);
    }
    hash.update(bytes.data(), bytes.size());
}

} // namespace

namespace rtutil {

const char *
ray_file_error_name(RayFileError error) noexcept
{
    switch (error) {
	case RayFileError::None: return "none";
	case RayFileError::Io: return "io_error";
	case RayFileError::InvalidHeader: return "invalid_header";
	case RayFileError::InvalidCount: return "invalid_count";
	case RayFileError::BadOrdering: return "bad_ordering";
	case RayFileError::MalformedRecord: return "malformed_record";
	case RayFileError::NonFiniteValue: return "non_finite_value";
	case RayFileError::NonUnitDirection: return "non_unit_direction";
	case RayFileError::DuplicateRay: return "duplicate_ray";
	case RayFileError::CountMismatch: return "count_mismatch";
	case RayFileError::TrailingRecord: return "trailing_record";
    }
    return "unknown";
}

std::size_t
RayKeyHash::operator()(const RayKey& key) const noexcept
{
    std::size_t result = 0;
    for (const double value : key.values) {
	const std::size_t value_hash = std::hash<double>{}(value);
	result ^= value_hash + HASH_COMBINE_CONSTANT + (result << HASH_COMBINE_SHIFT_LEFT)
	    + (result >> HASH_COMBINE_SHIFT_RIGHT);
    }
    return result;
}

RayKey
ray_key(const point_t point, const vect_t direction) noexcept
{
    const auto canonical_zero = [](double value) { return value == 0.0 ? 0.0 : value; };
    return {{{canonical_zero(point[0]), canonical_zero(point[1]), canonical_zero(point[2]),
	      canonical_zero(direction[0]), canonical_zero(direction[1]), canonical_zero(direction[2])}}};
}

RayKey
ray_key(const xray& ray) noexcept
{
    return ray_key(ray.r_pt, ray.r_dir);
}

std::vector<RayKey>
ray_keys(const RaySet& rayset)
{
    std::vector<RayKey> keys;
    keys.reserve(rayset.rays.size());
    for (const xray& ray : rayset.rays)
	keys.push_back(ray_key(ray));
    return keys;
}

RayFileStatus
validate_rays(const std::vector<xray>& rays)
{
    if (rays.empty())
	return failure(RayFileError::InvalidCount, 0, "rayset is empty");

    std::unordered_set<RayKey, RayKeyHash> unique_rays;
    try {
	unique_rays.reserve(rays.size());
    } catch (const std::exception&) {
	return failure(RayFileError::InvalidCount, 0, "rayset is too large to validate");
    }
    for (std::size_t index = 0; index < rays.size(); ++index) {
	const xray& ray = rays[index];
	for (std::size_t coordinate = 0; coordinate < 3; ++coordinate) {
	    if (!std::isfinite(ray.r_pt[coordinate]) || !std::isfinite(ray.r_dir[coordinate]))
		return failure(RayFileError::NonFiniteValue, index * 2 + 2,
			       "rayset contains a non-finite value");
	}
	if (!unit_direction(ray.r_dir))
	    return failure(RayFileError::NonUnitDirection, index * 2 + 3,
			   "ray direction is not unit length");
	if (!unique_rays.insert(ray_key(ray)).second)
	    return failure(RayFileError::DuplicateRay, index * 2 + 2,
			   "rayset contains an exact duplicate ray");
    }
    return {};
}

std::string
ray_content_hash(const std::vector<xray>& rays)
{
    Sha256 hash;
    // Hash canonical IEEE-754 words, not formatted text, so identity is
    // independent of locale, rounding mode, and host byte order.
    hash.update(HASH_DOMAIN, sizeof(HASH_DOMAIN) - 1);
    for (const xray& ray : rays) {
	for (const double value : ray_key(ray).values)
	    hash_double(hash, value);
    }
    return std::string(HASH_PREFIX) + hash.finish();
}

RayFileStatus
read_rays(const std::string& filename, RaySet& rayset)
{
    rayset = {};
    std::ifstream input(filename, std::ios::binary);
    if (!input.is_open())
	return failure(RayFileError::Io, 0, "unable to open ray file: " + filename);

    std::string line;
    if (!std::getline(input, line))
	return failure(input.bad() ? RayFileError::Io : RayFileError::InvalidHeader, 1,
		       "unable to read ray file header");
    remove_carriage_return(line);

    std::size_t declared_count = 0;
    RayFileStatus status = parse_header(line, declared_count);
    if (!status.ok())
	return status;
    if (declared_count > rayset.rays.max_size())
	return failure(RayFileError::InvalidCount, 1, "declared ray count is too large");

    std::error_code size_error;
    const std::uintmax_t file_bytes = std::filesystem::file_size(filename, size_error);
    if (size_error)
	return failure(RayFileError::Io, 0, "unable to inspect ray file: " + filename);
    if (declared_count > file_bytes / MINIMUM_RAY_RECORD_BYTES)
	return failure(RayFileError::CountMismatch, 1,
		       "declared ray count cannot fit in the ray file");

    try {
	rayset.rays.reserve(declared_count);
    } catch (const std::exception&) {
	return failure(RayFileError::InvalidCount, 1, "declared ray count is too large");
    }
    std::unordered_set<RayKey, RayKeyHash> unique_rays;
    try {
	unique_rays.reserve(declared_count);
    } catch (const std::exception&) {
	return failure(RayFileError::InvalidCount, 1, "declared ray count is too large");
    }
    for (std::size_t index = 0; index < declared_count; ++index) {
	xray ray{};
	const std::size_t point_line = index * 2 + 2;
	if (!std::getline(input, line))
	    return failure(input.bad() ? RayFileError::Io : RayFileError::CountMismatch,
			   point_line, "ray file ended before its declared count");
	remove_carriage_return(line);
	status = parse_record(line, POINT_PREFIX, point_line, ray.r_pt);
	if (!status.ok())
	    return status;

	const std::size_t direction_line = point_line + 1;
	if (!std::getline(input, line))
	    return failure(input.bad() ? RayFileError::Io : RayFileError::CountMismatch,
			   direction_line, "ray file ended with an incomplete ray");
	remove_carriage_return(line);
	status = parse_record(line, DIRECTION_PREFIX, direction_line, ray.r_dir);
	if (!status.ok())
	    return status;
	if (!unit_direction(ray.r_dir))
	    return failure(RayFileError::NonUnitDirection, direction_line,
			   "ray direction is not unit length");

	if (!unique_rays.insert(ray_key(ray)).second)
	    return failure(RayFileError::DuplicateRay, point_line,
			   "ray file contains an exact duplicate ray");
	rayset.rays.push_back(ray);
    }

    if (std::getline(input, line))
	return failure(RayFileError::TrailingRecord, declared_count * 2 + 2,
		       "ray file contains records after its declared count");
    if (input.bad())
	return failure(RayFileError::Io, declared_count * 2 + 2, "error reading ray file");

    rayset.content_hash = ray_content_hash(rayset.rays);
    return {};
}

RayFileStatus
write_rays(const std::string& filename, const std::vector<xray>& rays, std::string& content_hash)
{
    content_hash.clear();
    const RayFileStatus status = validate_rays(rays);
    if (!status.ok())
	return status;

    const std::filesystem::path destination(filename);
    std::filesystem::path temporary(filename);
    temporary += ".rtcmp-" + std::to_string(bu_pid()) + ".tmp";
    std::error_code filesystem_error;
    if (!remove_regular_file_if_present(temporary))
	return failure(RayFileError::Io, 0,
	    "unable to clear temporary ray file: " + temporary.string());

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
	return failure(RayFileError::Io, 0, "unable to open ray file for writing: " + temporary.string());
    output.imbue(std::locale::classic());

    output << HEADER_PREFIX << HEADER_LABEL << HEADER_COUNT_SEPARATOR << rays.size()
	   << HEADER_SUFFIX << '\n';
    for (const xray& ray : rays)
	output << canonical_ray_text(ray);
    output.close();
    if (!output) {
	std::filesystem::remove(temporary, filesystem_error);
	return failure(RayFileError::Io, 0, "unable to write complete ray file: " + filename);
    }

    if (!remove_regular_file_if_present(destination)) {
	std::filesystem::remove(temporary, filesystem_error);
	return failure(RayFileError::Io, 0, "unable to replace ray file: " + filename);
    }
    std::filesystem::rename(temporary, destination, filesystem_error);
    if (filesystem_error) {
	std::filesystem::remove(temporary, filesystem_error);
	return failure(RayFileError::Io, 0, "unable to publish ray file: " + filename);
    }

    content_hash = ray_content_hash(rays);
    return {};
}

} // namespace rtutil

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
