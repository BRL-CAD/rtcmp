#ifndef RTCMP_NUMERIC_H
#define RTCMP_NUMERIC_H

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace rtcmp_numeric {

inline bool
equal(double left, double right, double tolerance) noexcept
{
    if (std::isnan(left) || std::isnan(right))
	return false;
    if (left == right)
	return true;
    if (!std::isfinite(left) || !std::isfinite(right) || tolerance == 0.0)
	return false;
    return std::fabs(left - right) < tolerance;
}

inline bool
vector_equal(const double left[3], const double right[3], double tolerance) noexcept
{
    return equal(left[0], right[0], tolerance)
	&& equal(left[1], right[1], tolerance)
	&& equal(left[2], right[2], tolerance);
}

inline bool
finite_vector(const double values[3]) noexcept
{
    return std::isfinite(values[0]) && std::isfinite(values[1])
	&& std::isfinite(values[2]);
}

inline bool
sentinel_vector(const double values[3], int sign) noexcept
{
    return std::isinf(values[0]) && std::isinf(values[1]) && std::isinf(values[2])
	&& (std::signbit(values[0]) ? -1 : 1) == sign
	&& (std::signbit(values[1]) ? -1 : 1) == sign
	&& (std::signbit(values[2]) ? -1 : 1) == sign;
}

inline std::string
format(double value)
{
    if (std::isnan(value))
	return "nan";
    if (std::isinf(value))
	return std::signbit(value) ? "-inf" : "inf";

    std::array<char, 64> buffer{};
    const std::to_chars_result converted = std::to_chars(
	buffer.data(), buffer.data() + buffer.size(), value,
	std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc())
	throw std::runtime_error("unable to format floating-point value");
    return std::string(buffer.data(), converted.ptr);
}

} // namespace rtcmp_numeric

#endif // RTCMP_NUMERIC_H
