// pnwclient/api.cc -- client for the decnetd API socket.

#include "pnw/api.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace pnw {

namespace {

using Clock = std::chrono::steady_clock;

bool all_digits (const std::string &s)
{
    return !s.empty ()
        && std::all_of (s.begin (), s.end (),
                        [] (unsigned char c) { return std::isdigit (c); });
}

// Milliseconds left until deadline, never negative.
int remaining (Clock::time_point deadline)
{
    auto left = std::chrono::duration_cast<std::chrono::milliseconds> (
        deadline - Clock::now ()).count ();
    return left > 0 ? static_cast<int> (left) : 0;
}

}   // namespace

// ----------------------------------------------------------------- errors

Rejected::Rejected (unsigned reason)
    : ApiError (reason_text (reason)), reason_ (reason)
{
}

// -------------------------------------------------------------------- Api

std::string Api::default_path ()
{
    const char *env = std::getenv ("DECNETAPI");
    return env && *env ? env : "/tmp/decnetapi.sock";
}

Api::Api (std::string path) : path_ (std::move (path))
{
    sockaddr_un a {};
    a.sun_family = AF_UNIX;
    if (path_.size () >= sizeof a.sun_path)
        throw ApiError ("API socket path too long: " + path_);
    std::memcpy (a.sun_path, path_.c_str (), path_.size () + 1);

    fd_ = ::socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0 || ::connect (fd_, reinterpret_cast<sockaddr *> (&a),
                              sizeof a) < 0) {
        std::string why = std::strerror (errno);
        if (fd_ >= 0) ::close (fd_);
        fd_ = -1;
        throw ApiError ("cannot reach decnetd at " + path_ + ": " + why
                        + " (is it running with an \"api\" line?)");
    }

    // The empty request lists the systems; decnetd runs exactly one.
    json::Object list = request ({});
    for (const std::string &k : list.keys ())
        if (k != "tag") { system_ = k; break; }
}

Api::~Api ()
{
    if (fd_ >= 0) ::close (fd_);
}

void Api::send (const json::Object &o)
{
    std::string text = o.encode () + "\n";
    std::size_t off = 0;
    while (off < text.size ()) {
        ssize_t n = ::send (fd_, text.data () + off, text.size () - off,
                            MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0)
            throw ApiError (std::string ("lost the API connection: ")
                            + std::strerror (errno));
        off += static_cast<std::size_t> (n);
    }
}

std::optional<json::Object> Api::read (Timeout t)
{
    auto deadline = Clock::now () + t;
    for (;;) {
        // Lines are taken from start_ on, and only new data is searched, so a
        // large burst costs time in proportion to its size.
        std::size_t nl = pending_.find ('\n', scanned_);
        if (nl == std::string::npos) scanned_ = pending_.size ();
        if (nl != std::string::npos) {
            std::string line = pending_.substr (start_, nl - start_);
            start_ = scanned_ = nl + 1;
            if (start_ > 65536 && start_ * 2 > pending_.size ()) {
                pending_.erase (0, start_);
                scanned_ -= start_;
                start_ = 0;
            }
            try {
                return json::Object::parse (line);
            } catch (const std::exception &e) {
                throw ApiError (std::string ("bad message from decnetd: ")
                                + e.what ());
            }
        }
        pollfd p { fd_, POLLIN, 0 };
        int r = ::poll (&p, 1, remaining (deadline));
        if (r < 0 && errno == EINTR) continue;
        if (r == 0) return std::nullopt;
        char buf[65536];
        ssize_t n = ::recv (fd_, buf, sizeof buf, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw ApiError ("decnetd closed the API connection");
        pending_.append (buf, static_cast<std::size_t> (n));
    }
}

json::Object Api::request (json::Object req, Timeout t)
{
    std::int64_t tag = ++tag_;
    req.set ("tag", tag);
    send (req);

    auto deadline = Clock::now () + t;
    for (;;) {
        auto m = read (Timeout (remaining (deadline)));
        if (!m) throw ApiError ("no reply from decnetd");
        const json::Value *mt = m->get ("tag");
        if (mt && mt->is_int () && mt->as_int () == tag) {
            if (m->has ("error"))
                throw ApiError ("decnetd: " + m->str ("error"));
            return *m;
        }
        // Something else: an event for a link.  Keep it for later.
        queued_.push_back (std::move (*m));
    }
}

std::optional<json::Object> Api::next_for (std::int64_t handle, Timeout t)
{
    auto mine = [handle] (const json::Object &m) {
        return m.num ("handle", -1) == handle;
    };
    if (auto it = std::find_if (queued_.begin (), queued_.end (), mine);
        it != queued_.end ()) {
        json::Object m = std::move (*it);
        queued_.erase (it);
        return m;
    }
    auto deadline = Clock::now () + t;
    for (;;) {
        auto m = read (Timeout (remaining (deadline)));
        if (!m) return std::nullopt;
        if (mine (*m)) return m;
        queued_.push_back (std::move (*m));
    }
}

std::unique_ptr<Link> Api::connect (const ConnectOptions &o, Timeout t)
{
    json::Object req;
    req.set ("api", "session");
    req.set ("type", "connect");
    req.set ("dest", o.dest);
    if (all_digits (o.object))
        req.set ("remuser", static_cast<std::int64_t> (std::stoi (o.object)));
    else
        req.set ("remuser", o.object);
    req.set ("localuser", o.local);
    if (!o.data.empty ())    req.set_bytes ("data", o.data);
    if (!o.username.empty ()) req.set ("username", o.username);
    if (!o.password.empty ()) req.set ("password", o.password);
    if (!o.account.empty ())  req.set ("account", o.account);
    if (o.proxy)              req.set ("proxy", true);

    auto deadline = Clock::now () + t;
    json::Object r = request (std::move (req), t);
    if (r.str ("type") == "reject")
        throw Rejected (static_cast<unsigned> (r.num ("reason")));

    std::int64_t h = r.num ("handle");
    std::unique_ptr<Link> link (new Link (*this, h));
    auto m = next_for (h, Timeout (remaining (deadline)));
    if (!m) {
        // Still connecting.  Abandon it: Link's destructor disconnects.
        throw ApiError ("no answer from " + o.dest);
    }
    std::string type = m->str ("type");
    if (type != "accept") {
        link->open_ = false;
        throw Rejected (static_cast<unsigned> (m->num ("reason")));
    }
    link->accept_data_ = m->bytes ("data");
    return link;
}

// ----------------------------------------------------------- listening

std::int64_t Api::bind (std::uint8_t number, const std::string &name)
{
    json::Object req;
    req.set ("api", "session");
    req.set ("type", "bind");
    if (number) req.set ("num", static_cast<std::int64_t> (number));
    if (!name.empty ()) req.set ("name", name);
    return request (std::move (req)).num ("handle");
}

std::optional<Incoming> Api::incoming (Timeout t)
{
    auto is_connect = [] (const json::Object &m) {
        return m.str ("type") == "connect" && m.has ("listenhandle");
    };
    std::optional<json::Object> m;
    if (auto it = std::find_if (queued_.begin (), queued_.end (), is_connect);
        it != queued_.end ()) {
        m = std::move (*it);
        queued_.erase (it);
    } else {
        auto deadline = Clock::now () + t;
        for (;;) {
            auto r = read (Timeout (remaining (deadline)));
            if (!r) return std::nullopt;
            if (is_connect (*r)) { m = std::move (r); break; }
            queued_.push_back (std::move (*r));
        }
    }
    Incoming in;
    in.handle = m->num ("handle");
    in.listen = m->num ("listenhandle");
    in.address = m->str ("destination");
    in.node = m->str ("nodename", in.address);
    in.source_user = m->str ("srcuser");
    in.destination = m->str ("dstuser");
    in.username = m->str ("username");
    in.password = m->str ("password");
    in.account = m->str ("account");
    const json::Value *px = m->get ("proxy");
    in.proxy = px && px->is_bool () && px->as_bool ();
    in.data = m->bytes ("data");
    return in;
}

std::unique_ptr<Link> Api::accept (const Incoming &in, ByteView data)
{
    json::Object o;
    o.set ("api", "session");
    o.set ("type", "accept");
    o.set ("handle", in.handle);
    o.set_bytes ("data", data);
    send (o);
    return std::unique_ptr<Link> (new Link (*this, in.handle));
}

void Api::reject (const Incoming &in, ByteView data)
{
    json::Object o;
    o.set ("api", "session");
    o.set ("type", "reject");
    o.set ("handle", in.handle);
    o.set_bytes ("data", data);
    send (o);
}

// ------------------------------------------------------------------- Link

Link::~Link ()
{
    if (open_) {
        try { disconnect (); } catch (...) {}
    }
}

void Link::request (const char *type, ByteView data)
{
    if (!open_) throw ApiError ("link is closed");
    json::Object o;
    o.set ("api", "session");
    o.set ("type", type);
    o.set ("handle", handle_);
    o.set_bytes ("data", data);
    // These have no reply unless something is wrong, and an error reply
    // carries no tag, so it is noticed on the next read.
    api_.send (o);
}

void Link::send (ByteView data) { request ("data", data); }

void Link::interrupt (ByteView data) { request ("interrupt", data); }

void Link::disconnect (ByteView data)
{
    if (!open_) return;
    request ("disconnect", data);
    open_ = false;
}

std::optional<Bytes> Link::recv (Timeout t)
{
    auto deadline = Clock::now () + t;
    for (;;) {
        if (!open_) throw ApiError ("link is closed");
        auto m = api_.next_for (handle_, Timeout (remaining (deadline)));
        if (!m) return std::nullopt;
        std::string type = m->str ("type");
        if (type == "data") return m->bytes ("data");
        if (type == "disconnect" || type == "reject" || type == "abort") {
            open_ = false;
            reason_ = static_cast<unsigned> (m->num ("reason"));
            // Reason 0 on a link that was running is a plain disconnect,
            // not "rejected by object".
            throw ApiError (reason_ == 0
                            ? std::string ("link closed by the far end")
                            : "link closed by the far end: " + reason_text (reason_));
        }
        if (m->has ("error"))
            throw ApiError ("decnetd: " + m->str ("error"));
        // Interrupts and run state reports are not data; a caller that
        // wants interrupts will need its own accessor.
    }
}

}   // namespace pnw
