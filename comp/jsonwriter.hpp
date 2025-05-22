#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdio>

#include <brlcad/bu.h>  // bu_semaphore

static std::string
d2s(double d)
{
    size_t prec = std::numeric_limits<double>::max_digits10;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(prec) << d;
    std::string sd = ss.str();
    return sd;
}

namespace tsj {

/* Per-thread JSON writer; buffers shot data in-memory */
class Writer {
public:
    /* Global collector for thread buffers */
    struct Collector {
        /* Internal storage for all thread-local buffers */
        static std::vector<std::string>& buffers() {
            static std::vector<std::string> instance;
            return instance;
        }

        /* flush all collected buffers to disk (ONLY CALL ONCE after parallel run) */
        static void flushToFile(const std::string &path) {
            bu_semaphore_acquire(BU_SEM_SYSCALL);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            for (auto &b : buffers()) {
                out.write(b.data(), b.size());
            }
            out.close();
            bu_semaphore_release(BU_SEM_SYSCALL);
        }
    };


    // ---- Writer methods ---- //
    /* get the thread-local instance */
    static Writer &instance() {
        thread_local Writer w;
        return w;
    }

    /* Reserve an initial buffer capacity (bytes) */
    void reserve(size_t bytes) {
        buf.reserve(bytes);
    }

    /* Begin a new shot: open partition array, stash ray origin/dir for later
     * TODO: json.hpp .dump() writes partitions first and then ray info
     *       which is the pattern we're copying. Need to see if we can just
     *       write ray info first so we don't have to stash
     */
    inline void beginShot(const struct xray &ray) {
        buf.append("{\"partitions\":[");

        // stash the ray for endShot()
        ray_pt[0]  = ray.r_pt[0];
        ray_pt[1]  = ray.r_pt[1];
        ray_pt[2]  = ray.r_pt[2];
        ray_dir[0] = ray.r_dir[0];
        ray_dir[1] = ray.r_dir[1];
        ray_dir[2] = ray.r_dir[2];
    }

    /* append a partition */
    inline void addPartition(struct partition* pp) {
        char tmp[1024];
        int n = std::snprintf(tmp, sizeof(tmp),
            "{\"in_dist\":\"%s\","
            "\"in_norm\":{\"X\":\"%s\",\"Y\":\"%s\",\"Z\":\"%s\"},"
            "\"in_pt\":{\"X\":\"%s\",\"Y\":\"%s\",\"Z\":\"%s\"},"
            "\"out_dist\":\"%s\","
            "\"out_norm\":{\"X\":\"%s\",\"Y\":\"%s\",\"Z\":\"%s\"},"
            "\"out_pt\":{\"X\":\"%s\",\"Y\":\"%s\",\"Z\":\"%s\"},"
            "\"region\":\"%s\"},",
            d2s(pp->pt_inhit->hit_dist).c_str(), //in_dist,
            d2s(pp->pt_inhit->hit_normal[X]).c_str(), d2s(pp->pt_inhit->hit_normal[Y]).c_str(), d2s(pp->pt_inhit->hit_normal[Z]).c_str(), //in_norm[0], in_norm[1], in_norm[2],
            d2s(pp->pt_inhit->hit_point[X]).c_str(), d2s(pp->pt_inhit->hit_point[Y]).c_str(), d2s(pp->pt_inhit->hit_point[Z]).c_str(), //in_pt[0],    in_pt[1],    in_pt[2],
            d2s(pp->pt_outhit->hit_dist).c_str(), //out_dist,
            d2s(pp->pt_outhit->hit_normal[X]).c_str(), d2s(pp->pt_outhit->hit_normal[Y]).c_str(), d2s(pp->pt_outhit->hit_normal[Z]).c_str(), //out_norm[0], out_norm[1], out_norm[2],
            d2s(pp->pt_outhit->hit_point[X]).c_str(), d2s(pp->pt_outhit->hit_point[Y]).c_str(), d2s(pp->pt_outhit->hit_point[Z]).c_str(), //out_pt[0],   out_pt[1],   out_pt[2],
            pp->pt_regionp->reg_name //region
        );
        buf.append(tmp, n);
    }

    /* End the shot: close partitions array, append ray fields,
     *               then hand off buffer to global collector */
    inline void endShot() {
        // replace trailing comma with closing bracket
        if (!buf.empty() && buf.back() == ',') 
            buf.back() = ']';
        else 
            buf.append("]");

        char tmp[512];
        int n = std::snprintf(tmp, sizeof(tmp),
            ",\"ray_dir\":{\"X\":\"%s\",\"Y\":\"%s\",\"Z\":\"%s\"},"
            "\"ray_pt\":{\"X\":\"%s\",\"Y\":\"%s\",\"Z\":\"%s\"}}\n",
            d2s(ray_dir[0]).c_str(), d2s(ray_dir[1]).c_str(), d2s(ray_dir[2]).c_str(),
            d2s(ray_pt[0]).c_str(),  d2s(ray_pt[1]).c_str(),  d2s(ray_pt[2]).c_str()
        );
        buf.append(tmp, n);

        // hand off to global if we're getting close to our max size
        if (buf.capacity() - buf.size() < 2048) {   // check if we've almost maxed out our str
            syncToGlobal();
        }
    }

    inline void syncToGlobal() {
        bu_semaphore_acquire(BU_SEM_SYSCALL);
        Collector::buffers().push_back(std::move(buf));
        bu_semaphore_release(BU_SEM_SYSCALL);

        buf.clear();
    }
private:
    Writer() = default;
    ~Writer() = default;

    std::string buf;
    double ray_pt[3];
    double ray_dir[3];

};

} // namespace tsj