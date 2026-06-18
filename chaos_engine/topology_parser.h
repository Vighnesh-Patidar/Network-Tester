#pragma once

#include <string>

#include "common/topology.h"

namespace nch {

// Thin wrapper around the shared data contract: reads topology.json from disk,
// parses it through the common model, and runs structural validation so the rest
// of the engine can assume a well-formed Topology.
class TopologyParser {
public:
    Topology parse_file(const std::string& path) const;
    Topology parse_string(const std::string& contents) const;
};

}  // namespace nch
