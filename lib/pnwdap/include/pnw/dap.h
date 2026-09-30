// pnw/dap.h -- file access over DECnet (the NFT side of DAP).
//
// A Session is one logical link to a node's FAL (object 17).  It lists
// directories and reads files, turning DEC record formats into local
// bytes: text records get a newline each, binary files are cut to their
// real length.

#ifndef PNW_DAP_H
#define PNW_DAP_H

#include "pnw/api.h"

#include "decnet/dap/messages.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pnw {

namespace dapm = decnet::dap;

// NODE"user password account"::filespec, as VMS writes a remote file.
struct RemoteSpec {
    std::string node;
    std::string user, password, account;
    std::string path;                   // everything after the ::

    // Throws ApiError if there is no "::".
    static RemoteSpec parse (const std::string &s);

    // Is this a remote spec at all?
    static bool is_remote (const std::string &s);
};

// The remote end said no, with a DAP status.
class DapError : public ApiError {
public:
    explicit DapError (const dapm::Status &s)
        : ApiError (s.str ()), status_ (s) {}
    const dapm::Status &status () const noexcept { return status_; }

private:
    dapm::Status status_;
};

// One file in a directory listing.
struct DirEntry {
    std::string volume, directory, name;
    std::optional<dapm::Attributes> attributes;
    std::optional<dapm::DateTime>   dates;
    std::optional<dapm::Protection> protection;

    // Blocks in use, from the end of file block, if known.
    std::optional<std::uint64_t> blocks () const;
};

enum class Transfer {
    automatic,      // text if the file's records imply line ends
    text,           // ask for text records, one line each
    binary          // bytes as stored
};

class DapSession {
public:
    // Connect to FAL on spec's node and exchange configurations.
    DapSession (Api &api, const RemoteSpec &spec);

    const dapm::Config &remote_config () const noexcept { return remote_; }

    // List files matching spec.path.  Directories are reported as
    // returned: VMS gives device and directory in Name messages.
    std::vector<DirEntry> directory (const std::string &path);

    // Read one file, handing its converted contents to sink in pieces.
    // Returns the attributes the server gave.
    dapm::Attributes get (const std::string &path, Transfer mode,
                          const std::function<void (decnet::ByteView)> &sink);

    // Trace every message to stderr.
    void set_trace (bool on) noexcept { trace_ = on; }

private:
    void send (const dapm::Message &m);
    dapm::Message recv ();

    std::unique_ptr<Link>       link_;
    dapm::Config                remote_;
    std::vector<dapm::Message>  pending_;
    bool                        trace_ = false;
    bool                        configured_ = false;
};

}   // namespace pnw

#endif  // PNW_DAP_H
