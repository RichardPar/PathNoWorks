// pnw/names.h -- between local paths and remote file specifications.
//
// A mount, or any tool that walks a remote tree, needs to turn a local
// path under the mount point into the remote node's syntax, and turn the
// names and dates FAL sends back into local ones.  Two syntaxes:
//
//   VMS (and RSX):  [USER.SUB]FILE.TXT;3    directories end ".DIR"
//   Unix:           user/sub/file.txt       directories end "/", as
//                                           PyDECnet's FAL and dnfal send
//
// The mount's base spec decides which: brackets or a device mean VMS.

#ifndef PNW_NAMES_H
#define PNW_NAMES_H

#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace pnw {

class RemoteTree {
public:
    // base is the remote directory the mount starts at, as written after
    // the "::": "[USER]", "DUA0:[USER]", "sub/", or empty.
    explicit RemoteTree (std::string base);

    bool vms () const noexcept { return vms_; }

    // The remote spec for a local path under the mount ("/", "/a/b.txt").
    std::string spec (const std::string &local) const;

    // The spec that lists a local directory.
    std::string listing (const std::string &local_dir) const;

private:
    std::string base_dir (const std::vector<std::string> &dirs) const;

    std::string base_;
    bool        vms_;
    std::string device_;                    // "DUA0:", VMS only
    std::vector<std::string> base_dirs_;    // VMS directory components
};

// A name from a listing as it should appear locally, and whether it is a
// directory.  Versions go; "SUB.DIR" and "sub/" are directories.  Nothing
// for names that should not appear, such as "." or empty.
struct LocalName {
    std::string name;
    bool        directory = false;
};
std::optional<LocalName> local_name (const std::string &remote, bool vms);

// A DAP date, "30-SEP-26 13:24:26", as local time; nothing if unreadable.
std::optional<std::time_t> parse_dap_date (const std::string &s);

}   // namespace pnw

#endif  // PNW_NAMES_H
