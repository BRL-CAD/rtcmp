#include <fstream>
#include <iostream>
#include "json.hpp"

void
parse_pt(const nlohmann::json &sdata)
{
    if (sdata.contains("X")) {
	std::cout << "	X:" << sdata["X"] << "\n";
    }
    if (sdata.contains("Y")) {
	std::cout << "	Y:" << sdata["Y"] << "\n";
    }
    if (sdata.contains("Z")) {
	std::cout << "	Z:" << sdata["Z"] << "\n";
    }
}

void
parse_partition_data_entry(const nlohmann::json &sdata)
{
    if (sdata.contains("region")) {
	std::cout << "partition region: " << sdata["region"] <<  "\n";
    }

    if (sdata.contains("in_dist")) {
	std::cout << "partition in_dist: " << sdata["in_dist"] <<  "\n";
    }

    if (sdata.contains("out_dist")) {
	std::cout << "partition out_dist: " << sdata["out_dist"] <<  "\n";
    }

    if (sdata.contains("in_pt")) {
	std::cout << "partition in_pt:\n";
	const nlohmann::json &ssdata = sdata["in_pt"];
	parse_pt(ssdata);
    }

    if (sdata.contains("out_pt")) {
	std::cout << "partition out_pt:\n";
	const nlohmann::json &ssdata = sdata["out_pt"];
	parse_pt(ssdata);
    }

    if (sdata.contains("in_norm")) {
	std::cout << "partition in_norm:\n";
	const nlohmann::json &ssdata = sdata["in_norm"];
	parse_pt(ssdata);
    }

    if (sdata.contains("out_norm")) {
	std::cout << "partition out_norm:\n";
	const nlohmann::json &ssdata = sdata["out_norm"];
	parse_pt(ssdata);
    }

}

void
parse_partitions(const nlohmann::json &sdata)
{
    for(nlohmann::json::const_iterator it = sdata.begin(); it != sdata.end(); ++it) {
	const nlohmann::json &ssdata = *it;
	parse_partition_data_entry(ssdata);
    }	
}

void
parse_shots(const nlohmann::json &sdata)
{
    if (sdata.contains("ray_pt")) {
	std::cout << "ray_pt:\n";
	const nlohmann::json &ssdata = sdata["ray_pt"];
	parse_pt(ssdata);
    }

    if (sdata.contains("ray_dir")) {
	std::cout << "ray_dir:\n";
	const nlohmann::json &ssdata = sdata["ray_dir"];
	parse_pt(ssdata);
    }

    if (sdata.contains("partitions")) {
	const nlohmann::json &ssdata = sdata["partitions"];
	parse_partitions(ssdata);
    }
}

void
parse_shots_file(const char *fname)
{
    std::ifstream f(fname);
    nlohmann::json fdata = nlohmann::json::parse(f);
    const std::string data_version = fdata["data_version"];
    std::cout << "data version:" << data_version << "\n";
    const std::string etype = fdata["engine"];
    std::cout << "engine type:" << etype << "\n";

    nlohmann::json &fshots = fdata["shots"];

    for(nlohmann::json::const_iterator it = fshots.begin(); it != fshots.end(); ++it) {
	const nlohmann::json &sdata = *it;
	parse_shots(sdata);
    }
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

