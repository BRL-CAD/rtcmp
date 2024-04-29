#include <string>
#include <iostream>
#include "./json.hpp"

int
main()
{
    nlohmann::json j;

    for (size_t i = 0; i < 10; i++) {
	nlohmann::json shot;
	shot["px"] = 1.0 * i;
	shot["py"] = 3.0 * i;
	shot["pz"] = 2.0 * i;
	shot["dx"] = 1.0;
	shot["dy"] = -1.0;
	shot["dz"] = 1.0;
	for (size_t j = 0; j < 2*i; j++) {
	    nlohmann::json partition;
	    partition["ix"] = 0.1;
	    partition["iy"] = 0.1;
	    partition["iz"] = 0.1;
	    partition["ox"] = 0.2;
	    partition["oy"] = 0.2;
	    partition["oz"] = 0.2;
	    shot["partitions"].push_back(partition);
	}
	j["shotlines"].push_back(shot);
    }

    std::cout << std::setw(4) << j << "\n";

    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
