// pnwclient/reasons.cc -- session control reason codes as text.
//
// The texts are PyDECnet's reject_text, with the DNA Session Control
// numbers it does not name.

#include "pnw/api.h"

namespace pnw {

std::string reason_text (unsigned reason)
{
    switch (reason) {
    case 0:  return "Rejected by object";
    case 1:  return "Network resources";
    case 2:  return "Unrecognized node name";
    case 3:  return "Remote node shut down";
    case 4:  return "Unrecognized object";
    case 5:  return "Invalid object name format";
    case 6:  return "Object too busy";
    case 8:  return "Abort by management";
    case 9:  return "Abort by object";
    case 10: return "Invalid node name format";
    case 11: return "Local node shut down";
    case 34: return "Access control rejected";
    case 36: return "Account not valid";
    case 38: return "Node or object failed";
    case 39: return "Node unreachable";
    case 41: return "No link";
    case 43: return "Access control data too long";
    default: return "Reason " + std::to_string (reason);
    }
}

}   // namespace pnw
