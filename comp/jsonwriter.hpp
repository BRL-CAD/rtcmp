#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>

#include <brlcad/raytrace.h>

#include "util/numeric.h"
#include "util/util.h"

static std::string
format_number(double value)
{
    return rtcmp_numeric::format(value);
}

namespace tsj {

/* Thread-local record assembly backed by one bounded, synchronized output sink. */
class Writer {
public:
    static constexpr std::size_t MINIMUM_FLUSH_BYTES = 1024;

    struct Collector {
	static bool open(const std::string& path)
	{
	    State& output = state();
	    std::lock_guard<std::mutex> guard(output.mutex);
	    if (output.stream.is_open())
		output.stream.close();
	    output.failed = false;
	    output.stream.clear();
	    output.stream.open(path, std::ios::binary | std::ios::trunc);
	    output.failed = !output.stream.is_open();
	    return !output.failed;
	}

	static bool append(const std::string& contents)
	{
	    State& output = state();
	    std::lock_guard<std::mutex> guard(output.mutex);
	    if (output.failed || !output.stream.is_open())
		return false;
	    output.stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	    if (!output.stream)
		output.failed = true;
	    return !output.failed;
	}

	static bool close()
	{
	    State& output = state();
	    std::lock_guard<std::mutex> guard(output.mutex);
	    if (output.stream.is_open()) {
		output.stream.flush();
		if (!output.stream)
		    output.failed = true;
		output.stream.close();
		if (output.stream.fail())
		    output.failed = true;
	    }
	    return !output.failed;
	}

	static bool healthy()
	{
	    State& output = state();
	    std::lock_guard<std::mutex> guard(output.mutex);
	    return !output.failed;
	}

    private:
	struct State {
	    std::mutex mutex;
	    std::ofstream stream;
	    bool failed = false;
	};

	static State& state()
	{
	    static State output;
	    return output;
	}
    };

    static Writer& instance()
    {
	thread_local Writer writer;
	return writer;
    }

    void reset(std::size_t requested_flush_bytes)
    {
	buffer.clear();
	error_message.clear();
	shot_start = 0;
	flush_bytes = std::max(requested_flush_bytes, MINIMUM_FLUSH_BYTES);
	has_failed = false;
    }

    bool failed() const noexcept { return has_failed; }
    const std::string& error() const noexcept { return error_message; }

    void beginShot(const struct xray& ray)
    {
	if (has_failed)
	    return;
	shot_start = buffer.size();
	buffer.append("{\"partitions\":[");
	VMOVE(ray_point, ray.r_pt);
	VMOVE(ray_direction, ray.r_dir);
    }

    void addPartition(struct partition *partition)
    {
	if (has_failed)
	    return;
	if (!valid_endpoint(partition->pt_inhit->hit_dist,
			    partition->pt_inhit->hit_point, -1)
	    || !valid_endpoint(partition->pt_outhit->hit_dist,
			       partition->pt_outhit->hit_point, 1)
	    || !rtcmp_numeric::finite_vector(partition->pt_inhit->hit_normal)
	    || !rtcmp_numeric::finite_vector(partition->pt_outhit->hit_normal)
	    || partition->pt_inhit->hit_dist > partition->pt_outhit->hit_dist) {
	    fail("raytrace produced an invalid partition");
	    return;
	}

	buffer.append("{\"in_dist\":\"").append(format_number(partition->pt_inhit->hit_dist)).append("\",");
	appendVector("in_norm", partition->pt_inhit->hit_normal);
	buffer.push_back(',');
	appendVector("in_pt", partition->pt_inhit->hit_point);
	buffer.append(",\"out_dist\":\"").append(format_number(partition->pt_outhit->hit_dist)).append("\",");
	appendVector("out_norm", partition->pt_outhit->hit_normal);
	buffer.push_back(',');
	appendVector("out_pt", partition->pt_outhit->hit_point);
	buffer.append(",\"region\":").append(rtutil::json_quote(partition->pt_regionp->reg_name)).append("},");
    }

    void endShot()
    {
	if (has_failed)
	    return;
	if (!buffer.empty() && buffer.back() == ',')
	    buffer.back() = ']';
	else
	    buffer.push_back(']');
	buffer.push_back(',');
	appendVector("ray_dir", ray_direction);
	buffer.push_back(',');
	appendVector("ray_pt", ray_point);
	buffer.append("}\n");
	if (buffer.size() >= flush_bytes && !syncToGlobal())
	    fail("writing raytrace results failed");
    }

    bool syncToGlobal()
    {
	if (has_failed)
	    return false;
	if (buffer.empty())
	    return Collector::healthy();
	const bool success = Collector::append(buffer);
	if (buffer.capacity() > flush_bytes) {
	    std::string empty;
	    buffer.swap(empty);
	} else {
	    buffer.clear();
	}
	shot_start = 0;
	if (!success)
	    fail("writing raytrace results failed");
	return success;
    }

    void abandonShot() noexcept
    {
	if (shot_start <= buffer.size())
	    buffer.resize(shot_start);
    }

private:
    static bool valid_endpoint(double distance, const double point[3], int unbounded_sign) noexcept
    {
	if (std::isnan(distance))
	    return false;
	if (std::isinf(distance)) {
	    const int sign = std::signbit(distance) ? -1 : 1;
	    return sign == unbounded_sign && rtcmp_numeric::sentinel_vector(point, sign);
	}
	return rtcmp_numeric::finite_vector(point);
    }

    void fail(const char *message) noexcept
    {
	abandonShot();
	has_failed = true;
	if (error_message.empty())
	    error_message = message;
    }

    void appendVector(const char *name, const double values[3])
    {
	buffer.append("\"").append(name).append("\":{\"X\":\"").append(format_number(values[0]))
	    .append("\",\"Y\":\"").append(format_number(values[1]))
	    .append("\",\"Z\":\"").append(format_number(values[2])).append("\"}");
    }

    std::string buffer;
    std::string error_message;
    std::size_t shot_start = 0;
    std::size_t flush_bytes = MINIMUM_FLUSH_BYTES;
    double ray_point[3]{};
    double ray_direction[3]{};
    bool has_failed = false;
};

} // namespace tsj
