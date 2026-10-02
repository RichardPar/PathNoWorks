// pnw-x11 -- X11 over DECnet, both ways.
//
//     pnw-x11 serve --allow VAXXY          # VMS DECwindows -> this screen
//     pnw-x11 connect VAXXY -d 20          # DISPLAY=:20 -> VAXXY::0
//
// What DEC's eXcursion did for Pathworks PCs.  DECwindows clients reach a
// display NODE::n through DECnet object "X$Xn", and the link carries the
// X protocol unchanged.  serve takes that object and joins each link to
// the local X server; connect is the other way round, a local display
// whose clients go out to a DECnet node's X server.
//
// The local X server wants a login: serve replaces whatever the far client
// sends with this user's MIT-MAGIC-COOKIE-1 from .Xauthority, as ssh's X11
// forwarding does.  Anyone it lets in can see and type into the whole
// screen, so serve lets in only the nodes it is told to.
//
// On Windows the local X server (VcXsrv, Xming) is reached over TCP, port
// 6000 + n on this machine, and connect listens there too: its clients use
// DISPLAY=localhost:d.

#include "pnw/api.h"

#include "decnet/common/socket.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#define SOCK_CLOEXEC 0
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

using decnet::sock_close;

// A pollfd for input on fd.  On Windows the fd member is a SOCKET, so a
// braced initialiser from an int will not do.
pollfd poll_in (int fd)
{
    pollfd p {};
    p.fd = fd;
    p.events = POLLIN;
    return p;
}

using pnw::Bytes;
using pnw::ByteView;

volatile std::sig_atomic_t stop = 0;
void on_signal (int) { stop = 1; }

std::string upper (std::string s)
{
    for (char &c : s) c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    return s;
}

std::vector<std::string> split (const std::string &s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { if (!cur.empty ()) out.push_back (cur); cur.clear (); }
        else cur += c;
    }
    if (!cur.empty ()) out.push_back (cur);
    return out;
}

void usage ()
{
    std::cerr <<
        "usage: pnw-x11 [options] serve --allow NODE[,NODE...] [-n display]\n"
        "       pnw-x11 [options] connect NODE[::n] [-d display]\n"
        "  serve     take DECnet X connections for display n (object X$Xn,\n"
        "            default 0) and show them on the local X server\n"
        "  connect   make local display :d (default 20) whose clients go to\n"
        "            NODE's X server, display n (default 0)\n"
        "  --allow NODE,...  nodes serve accepts, by name or address\n"
        "  --allow-any       accept any node (anyone on the network can then\n"
        "                    read your keyboard and screen)\n"
        "  --display D       the local X server for serve (default $DISPLAY):\n"
#ifdef _WIN32
        "                    :0 (TCP port 6000 on this machine), or the path\n"
        "                    of its socket\n"
        "  --listen PATH     for connect, a socket to make instead of\n"
        "                    listening on localhost:d\n"
#else
        "                    :0, or the path of its socket\n"
        "  --listen PATH     for connect, the socket to make instead of\n"
        "                    /tmp/.X11-unix/Xd\n"
#endif
        "  --socket s        decnetd API socket (default $DECNETAPI or "
        "/tmp/decnetapi.sock)\n"
        "  --trace           log connections and byte counts\n";
}

// ------------------------------------------------------------- local X

// ":0", ":0.0", "unix:0" -> 0; anything else, nothing.
std::optional<int> display_number (const std::string &d)
{
    auto colon = d.rfind (':');
    if (colon == std::string::npos) return std::nullopt;
    std::string host = d.substr (0, colon);
    if (!host.empty () && host != "unix" && host != "localhost") return std::nullopt;
    std::string n = d.substr (colon + 1);
    n = n.substr (0, n.find ('.'));
    if (n.empty () || n.find_first_not_of ("0123456789") != std::string::npos)
        return std::nullopt;
    return std::stoi (n);
}

#ifndef _WIN32
std::string x_socket_path (int n) { return "/tmp/.X11-unix/X" + std::to_string (n); }
#endif

int connect_unix (const std::string &path, bool abstract)
{
    int fd = decnet::sock_open (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC);
    if (fd < 0) return -1;
    sockaddr_un a {};
    a.sun_family = AF_UNIX;
    std::size_t off = abstract ? 1 : 0;
    if (path.size () + off >= sizeof a.sun_path) { sock_close (fd); return -1; }
    std::memcpy (a.sun_path + off, path.data (), path.size ());
    socklen_t len = static_cast<socklen_t> (offsetof (sockaddr_un, sun_path) + off + path.size ()
                                            + (abstract ? 0 : 1));
    if (::connect (fd, reinterpret_cast<sockaddr *> (&a), len) < 0) {
        sock_close (fd);
        return -1;
    }
    return fd;
}

#ifdef _WIN32

// X over TCP, the only way on Windows: display n is port 6000 + n here.
constexpr int x_tcp_base = 6000;

sockaddr_in x_tcp_address (int n)
{
    sockaddr_in a {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    a.sin_port = htons (static_cast<std::uint16_t> (x_tcp_base + n));
    return a;
}

int connect_tcp_x (int n)
{
    sockaddr_in a = x_tcp_address (n);
    int fd = decnet::sock_open (AF_INET, SOCK_STREAM);
    if (fd < 0) return -1;
    if (::connect (fd, reinterpret_cast<sockaddr *> (&a), sizeof a) < 0) {
        sock_close (fd);
        return -1;
    }
    decnet::Socket s (fd);
    s.set_nodelay ();               // X requests are small and many
    return s.release ();
}

// connect's display d, for local clients: TCP on this machine only.
int listen_tcp_x (int d)
{
    sockaddr_in a = x_tcp_address (d);
    int fd = decnet::sock_open (AF_INET, SOCK_STREAM);
    if (fd < 0) return -1;
    if (::bind (fd, reinterpret_cast<sockaddr *> (&a), sizeof a) < 0
        || ::listen (fd, 16) < 0) {
        sock_close (fd);
        return -1;
    }
    return fd;
}

#endif

// The local X server: a display, or a socket path.
int connect_local_x (const std::string &display)
{
    if (!display.empty () && display[0] == '/') return connect_unix (display, false);
    auto n = display_number (display);
    if (!n) return -1;
#ifdef _WIN32
    return connect_tcp_x (*n);
#else
    std::string path = x_socket_path (*n);
    // Xorg listens on both; the abstract one works inside sandboxes too.
    int fd = connect_unix (path, true);
    return fd >= 0 ? fd : connect_unix (path, false);
#endif
}

// This user's MIT-MAGIC-COOKIE-1 for local display n, from $XAUTHORITY or
// ~/.Xauthority.  The file is a list of entries: family, address, display
// number, name, data, each but the family a 16-bit big-endian length and
// the bytes.
std::optional<Bytes> local_cookie (int n)
{
    std::string path;
#ifdef _WIN32
    const char *home = std::getenv ("USERPROFILE");
#else
    const char *home = std::getenv ("HOME");
#endif
    if (const char *x = std::getenv ("XAUTHORITY")) path = x;
    else if (home) path = std::string (home) + "/.Xauthority";
    std::ifstream in (path, std::ios::binary);
    if (!in) return std::nullopt;
    Bytes all ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char> ());

    char host[256] = {};
    decnet::net_init ();
    ::gethostname (host, sizeof host - 1);
    std::size_t at = 0;
    auto u16 = [&] () -> std::optional<unsigned> {
        if (at + 2 > all.size ()) return std::nullopt;
        unsigned v = (all[at] << 8) | all[at + 1];
        at += 2;
        return v;
    };
    auto field = [&] () -> std::optional<std::string> {
        auto len = u16 ();
        if (!len || at + *len > all.size ()) return std::nullopt;
        std::string s (all.begin () + static_cast<std::ptrdiff_t> (at),
                       all.begin () + static_cast<std::ptrdiff_t> (at + *len));
        at += *len;
        return s;
    };
    std::optional<Bytes> wild;
    while (at < all.size ()) {
        auto family = u16 ();
        auto addr = field (), number = field (), name = field (), data = field ();
        if (!family || !addr || !number || !name || !data) break;
        if (*name != "MIT-MAGIC-COOKIE-1") continue;
        bool display_ok = number->empty () || *number == std::to_string (n);
        if (!display_ok) continue;
        Bytes cookie (data->begin (), data->end ());
        // 256 is FamilyLocal, by host name; 65535 FamilyWild.
        if (*family == 256 && *addr == host) return cookie;
        if (*family == 65535 && !wild) wild = cookie;
    }
    return wild;
}

// The connection setup a client sends first, with its login replaced:
// byte order, protocol version, then the login's name and data, each
// padded to four bytes.  Nothing until all of it has arrived.
std::optional<Bytes> rewrite_setup (Bytes &held, const std::optional<Bytes> &cookie,
                                    std::size_t &used)
{
    if (held.size () < 12) return std::nullopt;
    bool msb = held[0] == 'B';
    auto get16 = [&] (std::size_t i) -> unsigned {
        return msb ? (held[i] << 8) | held[i + 1] : held[i] | (held[i + 1] << 8);
    };
    auto pad = [] (std::size_t n) { return (n + 3) & ~std::size_t (3); };
    std::size_t name_len = get16 (6), data_len = get16 (8);
    std::size_t total = 12 + pad (name_len) + pad (data_len);
    if (held.size () < total) return std::nullopt;
    used = total;
    if (!cookie) return Bytes (held.begin (), held.begin () + static_cast<std::ptrdiff_t> (total));

    static const std::string name = "MIT-MAGIC-COOKIE-1";
    Bytes out (held.begin (), held.begin () + 12);
    auto put16 = [&] (std::size_t i, unsigned v) {
        if (msb) { out[i] = static_cast<std::uint8_t> (v >> 8); out[i + 1] = static_cast<std::uint8_t> (v); }
        else     { out[i] = static_cast<std::uint8_t> (v); out[i + 1] = static_cast<std::uint8_t> (v >> 8); }
    };
    put16 (6, static_cast<unsigned> (name.size ()));
    put16 (8, static_cast<unsigned> (cookie->size ()));
    out.insert (out.end (), name.begin (), name.end ());
    out.resize (12 + pad (name.size ()), 0);
    out.insert (out.end (), cookie->begin (), cookie->end ());
    out.resize (12 + pad (name.size ()) + pad (cookie->size ()), 0);
    return out;
}

bool write_all (int fd, ByteView b)
{
    std::size_t off = 0;
    while (off < b.size ()) {
        ssize_t n = decnet::sock_send (fd, b.data () + off, b.size () - off);
        if (n < 0 && decnet::sock_interrupted (decnet::sock_errno ())) continue;
        if (n <= 0) return false;
        off += static_cast<std::size_t> (n);
    }
    return true;
}

// ------------------------------------------------------------- the pump

// One X connection: a DECnet link and a local socket.
struct Conn {
    std::unique_ptr<pnw::Link> link;
    int          fd = -1;
    std::string  who;
    bool         rewrite = false;       // serve: the setup is still to come
    Bytes        held;                  // setup bytes so far
    std::uint64_t in = 0, out = 0;      // bytes from DECnet, to DECnet
};

struct Options {
    std::string socket = pnw::Api::default_path ();
    std::vector<std::string> allow;
    bool allow_any = false;
    std::string display;
    std::string listen;
    int number = 0;                     // serve -n, connect's remote display
    int local = 20;                     // connect -d
    bool trace = false;
};

class Bridge {
public:
    Bridge (pnw::Api &api, const Options &o) : api_ (api), o_ (o) {}

    // DECnet data for conn h, to its socket.
    void from_decnet (std::int64_t h, Conn &c, Bytes data)
    {
        c.in += data.size ();
        if (c.rewrite) {
            c.held.insert (c.held.end (), data.begin (), data.end ());
            std::size_t used = 0;
            auto setup = rewrite_setup (c.held, cookie_, used);
            if (!setup) return;
            c.rewrite = false;
            data.assign (c.held.begin () + static_cast<std::ptrdiff_t> (used), c.held.end ());
            c.held.clear ();
            if (!write_all (c.fd, *setup)) { close (h, "the X server went away"); return; }
        }
        if (!data.empty () && !write_all (c.fd, data)) close (h, "the X server went away");
    }

    void from_socket (std::int64_t h, Conn &c)
    {
        std::uint8_t buf[65536];
        ssize_t n = decnet::sock_recv (c.fd, buf, sizeof buf);
        if (n < 0 && decnet::sock_interrupted (decnet::sock_errno ())) return;
        if (n <= 0) { close (h, "closed"); return; }
        c.out += static_cast<std::uint64_t> (n);
        try {
            c.link->send (ByteView (buf, static_cast<std::size_t> (n)));
        } catch (const std::exception &e) {
            close (h, e.what ());
        }
    }

    void close (std::int64_t h, const std::string &why)
    {
        auto it = conns_.find (h);
        if (it == conns_.end ()) return;
        Conn &c = it->second;
        if (c.fd >= 0) sock_close (c.fd);
        try { if (c.link->open ()) c.link->disconnect (); } catch (...) {}
        std::cerr << "%PNW-I-XCLOSED, " << c.who << ": " << why;
        if (o_.trace) std::cerr << " (" << c.in << " bytes in, " << c.out << " out)";
        std::cerr << "\n";
        conns_.erase (it);
    }

    // Everything decnetd has sent so far.
    void decnet_ready ()
    {
        while (const pnw::json::Object *m = api_.peek (pnw::Timeout (0))) {
            std::string type = m->str ("type");
            std::int64_t h = m->num ("handle", -1);
            if (type == "connect" && m->has ("listenhandle")) {
                auto in = api_.incoming (pnw::Timeout (0));
                if (in) take (*in);
                continue;
            }
            auto it = conns_.find (h);
            if (it == conns_.end ()) { api_.drop (); continue; }
            try {
                auto d = it->second.link->recv (pnw::Timeout (0));
                if (d) from_decnet (h, it->second, std::move (*d));
            } catch (const std::exception &e) {
                close (h, e.what ());
            }
        }
    }

    // serve: an X client arriving over DECnet.
    void take (const pnw::Incoming &in)
    {
        std::string who = in.node + "::" + (in.source_user.empty () ? "?" : in.source_user);
        bool ok = o_.allow_any;
        for (const std::string &a : o_.allow)
            if (upper (a) == upper (in.node) || a == in.address) ok = true;
        if (!ok) {
            std::cerr << "%PNW-W-XREFUSED, " << who << ": not in --allow\n";
            api_.reject (in);
            return;
        }
        int fd = connect_local_x (o_.display);
        if (fd < 0) {
            std::cerr << "%PNW-E-XREFUSED, " << who << ": cannot reach the X server "
                      << o_.display << "\n";
            api_.reject (in);
            return;
        }
        Conn c;
        c.link = api_.accept (in);
        c.fd = fd;
        c.who = who;
        c.rewrite = true;
        std::cerr << "%PNW-I-XCONNECT, " << who << " to " << o_.display << "\n";
        conns_.emplace (in.handle, std::move (c));
    }

    // connect: a local X client, to go out over DECnet.
    void new_client (int fd, const std::string &node)
    {
        pnw::ConnectOptions co;
        co.dest = node;
        co.object = "X$X" + std::to_string (o_.number);
        try {
            Conn c;
            c.link = api_.connect (co);
            c.fd = fd;
            c.who = "a local client to " + upper (node) + "::" + std::to_string (o_.number);
            if (o_.trace) std::cerr << "%PNW-I-XCONNECT, " << c.who << "\n";
            std::int64_t h = c.link->handle ();
            conns_.emplace (h, std::move (c));
        } catch (const std::exception &e) {
            std::cerr << "%PNW-E-XCONNECT, to " << node << "::" << o_.number << ": "
                      << e.what () << "\n";
            sock_close (fd);
        }
    }

    void set_cookie (std::optional<Bytes> c) { cookie_ = std::move (c); }
    std::map<std::int64_t, Conn> &conns () { return conns_; }

private:
    pnw::Api      &api_;
    const Options &o_;
    std::optional<Bytes>          cookie_;
    std::map<std::int64_t, Conn>  conns_;
};

// One loop for both: wait on decnetd, the local sockets, and for connect
// the listening socket.
int run (pnw::Api &api, Bridge &b, int listen_fd, const std::string &node)
{
    while (!stop) {
        std::vector<pollfd> fds;
        fds.push_back (poll_in (api.fd ()));
        if (listen_fd >= 0) fds.push_back (poll_in (listen_fd));
        std::vector<std::int64_t> order;
        for (auto &[h, c] : b.conns ()) {
            fds.push_back (poll_in (c.fd));
            order.push_back (h);
        }
        int r = decnet::sock_poll (fds.data (), fds.size (), api.buffered () ? 0 : 1000);
        if (r < 0 && !decnet::sock_interrupted (decnet::sock_errno ())) {
            std::cerr << "pnw-x11: poll: "
                      << decnet::sock_strerror (decnet::sock_errno ()) << "\n";
            return 1;
        }
        try {
            b.decnet_ready ();
        } catch (const std::exception &e) {
            std::cerr << "pnw-x11: " << e.what () << "\n";
            return 1;
        }
        std::size_t at = 1;
        if (listen_fd >= 0) {
            if (fds[at].revents & POLLIN) {
#ifdef _WIN32
                int fd = decnet::sock_accept (listen_fd);
#else
                int fd = ::accept4 (listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
#endif
                if (fd >= 0) b.new_client (fd, node);
            }
            ++at;
        }
        for (std::size_t i = 0; i < order.size (); ++i, ++at) {
            if (!(fds[at].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            auto it = b.conns ().find (order[i]);
            if (it != b.conns ().end ()) b.from_socket (order[i], it->second);
        }
    }
    for (auto &[h, c] : b.conns ()) {
        if (c.fd >= 0) sock_close (c.fd);
        try { c.link->disconnect (); } catch (...) {}
    }
    return 0;
}

int serve (const Options &o)
{
    if (o.allow.empty () && !o.allow_any) {
        std::cerr << "pnw-x11: serve needs --allow NODE (or --allow-any): whoever "
                     "connects can read your screen and keyboard\n";
        return 2;
    }
    if (o.display.empty ()) {
        std::cerr << "pnw-x11: no X display: set DISPLAY or use --display\n";
        return 2;
    }
    int probe = connect_local_x (o.display);
    if (probe < 0) {
        std::cerr << "pnw-x11: cannot reach the X server at " << o.display << "\n";
        return 1;
    }
    sock_close (probe);

    pnw::Api api (o.socket);
    Bridge b (api, o);
    auto n = display_number (o.display);
    auto cookie = local_cookie (n ? *n : 0);
    b.set_cookie (cookie);
    std::string object = "X$X" + std::to_string (o.number);
    api.bind (0, object);
    std::cerr << "%PNW-I-XSERVING, " << api.system () << "::" << o.number
              << " (object " << object << ") on " << o.display
              << (cookie ? "" : ", without an X login (no cookie in .Xauthority)") << "\n";
    return run (api, b, -1, {});
}

int connect_out (const Options &o, std::string node)
{
    if (auto sep = node.find ("::"); sep != std::string::npos) node = node.substr (0, sep);
#ifdef _WIN32
    if (o.listen.empty ()) {
        // Windows X clients use TCP: a port in use is someone's display.
        int lfd = listen_tcp_x (o.local);
        if (lfd < 0) {
            std::cerr << "pnw-x11: display localhost:" << o.local
                      << " is in use: pick another display with -d\n";
            return 1;
        }
        pnw::Api api (o.socket);
        Bridge b (api, o);
        std::cerr << "%PNW-I-XLISTENING, DISPLAY=localhost:" << o.local
                  << " goes to " << upper (node) << "::" << o.number << "\n";
        int rc = run (api, b, lfd, node);
        sock_close (lfd);
        return rc;
    }
    std::string path = o.listen;
#else
    std::string path = o.listen.empty () ? x_socket_path (o.local) : o.listen;
    if (o.listen.empty ()) ::mkdir ("/tmp/.X11-unix", 01777);
#endif
    // A socket left by a bridge that died can go; one still answering
    // belongs to a real display.
    if (int fd = connect_unix (path, false); fd >= 0) {
        sock_close (fd);
        std::cerr << "pnw-x11: " << path << " is in use: pick another display with -d\n";
        return 1;
    }
    ::unlink (path.c_str ());
    int lfd = decnet::sock_open (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC);
    sockaddr_un a {};
    a.sun_family = AF_UNIX;
    std::strncpy (a.sun_path, path.c_str (), sizeof a.sun_path - 1);
    if (::bind (lfd, reinterpret_cast<sockaddr *> (&a), sizeof a) < 0 || ::listen (lfd, 16) < 0) {
        std::cerr << "pnw-x11: cannot listen on " << path << ": "
                  << decnet::sock_strerror (decnet::sock_errno ()) << "\n";
        return 1;
    }
    pnw::Api api (o.socket);
    Bridge b (api, o);
    std::cerr << "%PNW-I-XLISTENING, ";
    if (o.listen.empty ()) std::cerr << "DISPLAY=:" << o.local;
    else std::cerr << path;
    std::cerr << " goes to " << upper (node) << "::" << o.number << "\n";
    int rc = run (api, b, lfd, node);
    sock_close (lfd);
    ::unlink (path.c_str ());
    return rc;
}

}   // namespace

int main (int argc, char **argv)
{
    Options o;
    if (const char *d = std::getenv ("DISPLAY")) o.display = d;
#ifdef _WIN32
    // Windows X servers (VcXsrv, Xming) seldom set DISPLAY; :0 is where
    // they start.
    if (o.display.empty ()) o.display = ":0";
#endif
    std::string cmd;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&] () -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--socket")           o.socket = value ();
        else if (a == "--allow")       { for (auto &n : split (value (), ',')) o.allow.push_back (n); }
        else if (a == "--allow-any")   o.allow_any = true;
        else if (a == "--display")     o.display = value ();
        else if (a == "--listen")      o.listen = value ();
        else if (a == "-n")            o.number = std::atoi (value ().c_str ());
        else if (a == "-d")            o.local = std::atoi (value ().c_str ());
        else if (a == "--trace")       o.trace = true;
        else if (a == "-h" || a == "--help") { usage (); return 0; }
        else if (cmd.empty ())         cmd = a;
        else                           args.push_back (a);
    }
    std::signal (SIGINT, on_signal);
    std::signal (SIGTERM, on_signal);
#ifndef _WIN32
    std::signal (SIGPIPE, SIG_IGN);
#endif
    try {
        if (cmd == "serve" && args.empty ()) return serve (o);
        if (cmd == "connect" && args.size () == 1) {
            Options c = o;
            // NODE::n names the far display.
            if (auto sep = args[0].find ("::"); sep != std::string::npos
                && sep + 2 < args[0].size ())
                c.number = std::atoi (args[0].substr (sep + 2).c_str ());
            return connect_out (c, args[0]);
        }
    } catch (const std::exception &e) {
        std::cerr << "pnw-x11: " << e.what () << "\n";
        return 1;
    }
    usage ();
    return 2;
}
