#include "topology_parser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace nch {

Topology TopologyParser::parse_file(const std::string& path) const {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open topology file: " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse_string(buffer.str());
}

Topology TopologyParser::parse_string(const std::string& contents) const {
    nlohmann::json doc = nlohmann::json::parse(contents);
    Topology topology = topology_from_json(doc);
    topology.validate();
    return topology;
}

}  // namespace nch
