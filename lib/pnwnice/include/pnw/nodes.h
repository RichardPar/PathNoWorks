// pnw/nodes.h -- the nodes a DECnet node knows, from its NICE listener.
//
// What NCP's SHOW KNOWN NODES STATUS gives, as data for a table: one row
// per node, with whether it can be reached and how.

#ifndef PNW_NODES_H
#define PNW_NODES_H

#include "pnw/api.h"

#include <optional>
#include <string>
#include <vector>

namespace pnw {

struct NodeRow {
    std::string address;                // "29.157"
    std::string name;                   // "VAXXY", or empty
    std::string state;                  // "reachable", "on", ... in lower
                                        // case; may be empty
    std::string circuit;                // the circuit to it, if reachable
    std::string next;                   // the next node on the way
    std::optional<unsigned> hops, cost;
    bool executor = false;              // the node that answered

    bool reachable () const
    { return executor || state.rfind ("reachable", 0) == 0 || !circuit.empty (); }
};

// Ask target's NICE listener (object 19), the local node if empty.
// Throws ApiError, or Rejected if the node will not talk.
std::vector<NodeRow> known_nodes (Api &api, const std::string &target = {});

}   // namespace pnw

#endif  // PNW_NODES_H
