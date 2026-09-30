// pnw/api.h -- client for the decnetd API socket.
//
// decnetd speaks PyDECnet's API: one JSON object per line over a Unix
// socket.  Api is one connection to it; Link is one DECnet logical link
// opened through it.  Both are synchronous and single threaded, which is
// what command line tools want.  Messages that arrive for another link
// while waiting are kept until that link asks for them.

#ifndef PNW_API_H
#define PNW_API_H

#include "decnet/common/json.h"
#include "decnet/common/types.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pnw {

using decnet::Bytes;
using decnet::ByteView;
namespace json = decnet::json;

using Timeout = std::chrono::milliseconds;
inline constexpr Timeout default_timeout { 30000 };

// Anything the API server reports, or failure to talk to it at all.
class ApiError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The far end, or session control, refused a connection.
class Rejected : public ApiError {
public:
    explicit Rejected (unsigned reason);
    unsigned reason () const noexcept { return reason_; }

private:
    unsigned reason_;
};

// The text for a session control reject or disconnect reason:
// "Unrecognized object", "Node unreachable".
std::string reason_text (unsigned reason);

// How to open a link.  remote is an object number or name.
struct ConnectOptions {
    std::string dest;                   // node name or address
    std::string object;                 // "25", or "MIRROR"
    std::string local = "PNW";          // our end user name
    Bytes       data;                   // connect data, up to 16 bytes
    std::string username, password, account;
    bool        proxy = false;
};

class Api;

// One logical link.  Disconnects when destroyed if still open.
class Link {
public:
    ~Link ();
    Link (const Link &) = delete;
    Link &operator= (const Link &) = delete;

    // Data the far end sent with its accept.
    const Bytes &accept_data () const noexcept { return accept_data_; }

    bool open () const noexcept { return open_; }

    // Why the link closed, once it has.
    unsigned reason () const noexcept { return reason_; }

    void send (ByteView data);
    void interrupt (ByteView data);

    // The next message from the far end.  Nothing on timeout; throws
    // ApiError if the link closes while waiting.
    std::optional<Bytes> recv (Timeout t = default_timeout);

    void disconnect (ByteView data = {});

private:
    friend class Api;
    Link (Api &api, std::int64_t handle) : api_ (api), handle_ (handle) {}

    void request (const char *type, ByteView data);

    Api          &api_;
    std::int64_t  handle_;
    Bytes         accept_data_;
    bool          open_ = true;
    unsigned      reason_ = 0;
};

class Api {
public:
    // $DECNETAPI, or PyDECnet's default /tmp/decnetapi.sock.
    static std::string default_path ();

    // Connect to the server.  Throws ApiError if nothing is listening.
    explicit Api (std::string path = default_path ());
    ~Api ();
    Api (const Api &) = delete;
    Api &operator= (const Api &) = delete;

    const std::string &path () const noexcept { return path_; }

    // The node the server runs, from the system list.
    const std::string &system () const noexcept { return system_; }

    // Send a request and wait for its reply, matched by tag.  Throws
    // ApiError for an error reply or a timeout.
    json::Object request (json::Object req, Timeout t = default_timeout);

    // Open a logical link and wait for the far end to accept.  Throws
    // Rejected if it does not.
    std::unique_ptr<Link> connect (const ConnectOptions &o,
                                   Timeout t = default_timeout);

private:
    friend class Link;

    void send (const json::Object &o);

    // The next message from the server, or nothing on timeout.
    std::optional<json::Object> read (Timeout t);

    // The next message about this handle, taking queued ones first.
    std::optional<json::Object> next_for (std::int64_t handle, Timeout t);

    std::string                path_;
    int                        fd_ = -1;
    std::string                pending_;
    std::deque<json::Object>   queued_;
    std::int64_t               tag_ = 0;
    std::string                system_;
};

}   // namespace pnw

#endif  // PNW_API_H
