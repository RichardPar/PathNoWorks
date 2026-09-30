// pnw-lat -- connect to a LAT service, as a terminal server does.
//
//     pnw-lat -l                 services announced on this LAN
//     pnw-lat BAJI               connect to service BAJI
//     xterm -ti vt340 -e pnw-lat RAXDA
//
// LAT runs straight on Ethernet (EtherType 0x6004), so this needs to send
// raw frames: give the binary CAP_NET_RAW once,
//
//     sudo setcap cap_net_raw+ep pnw-lat
//
// It uses the interface with the default route unless told otherwise.
// Wi-Fi works if the access point bridges to the wired LAN, as most do:
// LAT, unlike DECnet, uses the station's own address.
//
// Ctrl-] q leaves, Ctrl-] b sends a break, Ctrl-] Ctrl-] a Ctrl-].

#include "pnw/lat.h"
#include "pnw/term.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using pnw::Mac;
using Clock = pnw::Lat::Clock;

constexpr std::uint8_t ESCAPE_KEY = 0x1d;           // Ctrl-]

std::FILE *trace_file = nullptr;
void trace (const char *dir, decnet::ByteView b)
{
    if (!trace_file) return;
    std::fprintf (trace_file, "%s", dir);
    for (std::uint8_t c : b) std::fprintf (trace_file, " %02x", c);
    std::fprintf (trace_file, "\n");
    std::fflush (trace_file);
}

// The interface the default route goes through.
std::string default_interface ()
{
    std::ifstream f ("/proc/net/route");
    std::string line;
    std::getline (f, line);                         // heading
    while (std::getline (f, line)) {
        std::istringstream in (line);
        std::string name, dest;
        in >> name >> dest;
        if (dest == "00000000") return name;
    }
    return "eth0";
}

std::string mac_text (const Mac &m)
{
    char b[24];
    std::snprintf (b, sizeof b, "%02x-%02x-%02x-%02x-%02x-%02x",
                   m[0], m[1], m[2], m[3], m[4], m[5]);
    return b;
}

class LatSocket {
public:
    explicit LatSocket (const std::string &ifname)
    {
        // Opened for us by pnw-latsock, or by us if we may.
        if (const char *given = std::getenv ("PNW_LAT_FD"))
            fd_ = std::atoi (given);
        else
            fd_ = ::socket (AF_PACKET, SOCK_DGRAM, htons (pnw::LAT_ETHERTYPE));
        if (fd_ < 0) {
            std::string why = std::strerror (errno);
            if (errno == EPERM)
                why += "; LAT needs raw Ethernet: sudo setcap cap_net_raw+ep "
                       "on this program";
            throw std::runtime_error (why);
        }
        index_ = ::if_nametoindex (ifname.c_str ());
        if (!index_) throw std::runtime_error ("no interface " + ifname);
        sockaddr_ll a {};
        a.sll_family = AF_PACKET;
        a.sll_protocol = htons (pnw::LAT_ETHERTYPE);
        a.sll_ifindex = static_cast<int> (index_);
        if (::bind (fd_, reinterpret_cast<sockaddr *> (&a), sizeof a) < 0)
            throw std::runtime_error (std::string ("bind: ") + std::strerror (errno));
        packet_mreq mr {};
        mr.mr_ifindex = static_cast<int> (index_);
        mr.mr_type = PACKET_MR_MULTICAST;
        mr.mr_alen = 6;
        std::memcpy (mr.mr_address, pnw::LAT_MULTICAST.data (), 6);
        ::setsockopt (fd_, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof mr);
    }
    ~LatSocket () { if (fd_ >= 0) ::close (fd_); }

    int fd () const { return fd_; }

    void send (const Mac &to, decnet::ByteView msg)
    {
        // Ethernet's minimum, and LAT wants an even length.
        decnet::Bytes b (msg.begin (), msg.end ());
        if (b.size () < 46) b.resize (46, 0);
        if (b.size () % 2) b.push_back (0);
        sockaddr_ll a {};
        a.sll_family = AF_PACKET;
        a.sll_protocol = htons (pnw::LAT_ETHERTYPE);
        a.sll_ifindex = static_cast<int> (index_);
        a.sll_halen = 6;
        std::memcpy (a.sll_addr, to.data (), 6);
        trace (">", b);
        ::sendto (fd_, b.data (), b.size (), 0, reinterpret_cast<sockaddr *> (&a), sizeof a);
    }

    // One frame, with who sent it.
    std::optional<std::pair<Mac, decnet::Bytes>> recv ()
    {
        std::uint8_t buf[1600];
        sockaddr_ll a {};
        socklen_t al = sizeof a;
        ssize_t n = ::recvfrom (fd_, buf, sizeof buf, 0,
                                reinterpret_cast<sockaddr *> (&a), &al);
        if (n <= 0) return std::nullopt;
        Mac from {};
        std::memcpy (from.data (), a.sll_addr, 6);
        decnet::Bytes b (buf, buf + n);
        trace ("<", b);
        return std::make_pair (from, b);
    }

private:
    int      fd_ = -1;
    unsigned index_ = 0;
};

struct Heard {
    Mac                  mac {};
    pnw::LatAnnouncement a;
};

// Services heard before, so that a connect need not wait for the next
// announcement: on Wi-Fi, multicast is easily lost, and announcements are
// 20 seconds apart.  One line each: service node address mtu.
struct Remembered {
    std::string node;
    Mac         mac {};
    unsigned    mtu = 0;
};

std::string cache_path ()
{
    const char *home = std::getenv ("HOME");
    const char *xdg = std::getenv ("XDG_CACHE_HOME");
    std::string dir = xdg && *xdg ? xdg : std::string (home ? home : ".") + "/.cache";
    return dir + "/pnw-lat/services";
}

std::map<std::string, Remembered> load_cache ()
{
    std::map<std::string, Remembered> r;
    std::ifstream f (cache_path ());
    std::string service, node, mac;
    unsigned mtu;
    while (f >> service >> node >> mac >> mtu) {
        Remembered e { node, {}, mtu };
        unsigned b[6];
        if (std::sscanf (mac.c_str (), "%x-%x-%x-%x-%x-%x",
                         &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) continue;
        for (int i = 0; i < 6; ++i) e.mac[i] = static_cast<std::uint8_t> (b[i]);
        r[service] = e;
    }
    return r;
}

void save_cache (const std::map<std::string, Heard> &heard)
{
    if (heard.empty ()) return;
    auto r = load_cache ();
    for (const auto &[node, h] : heard)
        for (const auto &s : h.a.services)
            r[s.name] = Remembered { node, h.mac, h.a.mtu };
    std::string path = cache_path ();
    std::string dir = path.substr (0, path.rfind ('/'));
    std::string parent = dir.substr (0, dir.rfind ('/'));
    ::mkdir (parent.c_str (), 0755);
    ::mkdir (dir.c_str (), 0755);
    std::ofstream f (path, std::ios::trunc);
    for (const auto &[service, e] : r)
        f << service << ' ' << e.node << ' ' << mac_text (e.mac) << ' ' << e.mtu << '\n';
}

// Listen for announcements until want says stop, or for secs.
std::map<std::string, Heard> listen (LatSocket &s, int secs,
                                     const std::function<bool (const Heard &)> &want)
{
    std::map<std::string, Heard> nodes;
    auto end = Clock::now () + std::chrono::seconds (secs);
    while (Clock::now () < end) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds> (
            end - Clock::now ()).count ();
        pollfd p { s.fd (), POLLIN, 0 };
        if (::poll (&p, 1, static_cast<int> (left)) <= 0) continue;
        auto f = s.recv ();
        if (!f) continue;
        auto a = pnw::parse_announcement (f->second);
        if (!a) continue;
        Heard h { f->first, *a };
        nodes[a->node] = h;
        if (want && want (h)) break;
    }
    return nodes;
}

void usage ()
{
    std::cerr <<
        "usage: pnw-lat [options] -l\n"
        "       pnw-lat [options] service [node]\n"
        "  List LAT services on the LAN, or connect to one.\n"
        "  -i iface    interface (default: the one with the default route)\n"
        "  -w secs     how long to listen for announcements (default 45)\n"
        "  -n          do not use services remembered from before\n"
        "  --8bit      pass 8-bit characters and controls straight through\n"
        "  Ctrl-] q    leave; Ctrl-] b sends a break\n";
}

}   // namespace

// Run again through pnw-latsock, beside this program, which can open the
// socket.  Returns only if that is not possible.
void through_latsock (char **argv)
{
    char self[4096];
    ssize_t n = ::readlink ("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0 || std::getenv ("PNW_LAT_FD")) return;
    self[n] = 0;
    std::string dir (self);
    dir.erase (dir.rfind ('/') + 1);
    // In the build tree the helper sits beside us; installed, likewise.
    std::string helper = dir + "pnw-latsock";
    if (::access (helper.c_str (), X_OK) != 0) return;
    std::vector<char *> args { helper.data (), self };
    for (char **a = argv + 1; *a; ++a) args.push_back (*a);
    args.push_back (nullptr);
    ::execv (helper.c_str (), args.data ());
}

int main (int argc, char **argv)
{
    // Unprivileged, with a helper that is not: let it open the socket.
    if (!std::getenv ("PNW_LAT_FD")) {
        int probe = ::socket (AF_PACKET, SOCK_DGRAM, htons (pnw::LAT_ETHERTYPE));
        if (probe >= 0) ::close (probe);
        else if (errno == EPERM) through_latsock (argv);
    }

    std::string ifname = default_interface ();
    int wait = 45;
    bool list = false, raw8 = false, fresh = false;
    std::string service, node;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-i" && i + 1 < argc)        ifname = argv[++i];
        else if (a == "-w" && i + 1 < argc)   wait = std::atoi (argv[++i]);
        else if (a == "-l")                   list = true;
        else if (a == "--8bit")               raw8 = true;
        else if (a == "-n")                   fresh = true;
        else if (a == "-h" || a == "--help")  { usage (); return 0; }
        else if (service.empty ())            service = a;
        else if (node.empty ())               node = a;
        else                                  { usage (); return 2; }
    }
    auto upper = [] (std::string s) {
        for (char &c : s) c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
        return s;
    };
    service = upper (service);
    node = upper (node);
    if (!list && service.empty ()) { usage (); return 2; }
    if (const char *t = std::getenv ("PNW_TRACE")) trace_file = std::fopen (t, "w");

    try {
        LatSocket sock (ifname);
        if (list) {
            std::cerr << "Listening on " << ifname << " for " << wait << " seconds...\n";
            auto nodes = listen (sock, wait, nullptr);
            save_cache (nodes);
            if (nodes.empty ()) { std::cout << "No LAT services heard.\n"; return 1; }
            std::printf ("%-16s %-8s %-18s %s\n", "Service", "Rating", "Node", "Address");
            for (const auto &[name, h] : nodes)
                for (const auto &s : h.a.services)
                    std::printf ("%-16s %-8u %-18s %s\n", s.name.c_str (), s.rating,
                                 name.c_str (), mac_text (h.mac).c_str ());
            return 0;
        }

        // Find the service: remembered from before, or announced now.
        std::optional<Heard> host;
        bool from_cache = false;
        if (!fresh) {
            auto known = load_cache ();
            auto it = known.find (service);
            if (it != known.end () && (node.empty () || it->second.node == node)) {
                Heard h;
                h.mac = it->second.mac;
                h.a.node = it->second.node;
                h.a.mtu = static_cast<std::uint16_t> (it->second.mtu);
                host = h;
                from_cache = true;
            }
        }
        if (!host) {
            std::cerr << "%PNW-I-LOOKING, for LAT service " << service << " on "
                      << ifname << "\n";
            auto heard = listen (sock, wait, [&] (const Heard &h) {
                if (!node.empty () && h.a.node != node) return false;
                for (const auto &s : h.a.services)
                    if (upper (s.name) == service) { host = h; return true; }
                return false;
            });
            save_cache (heard);
        }
        if (!host) {
            std::cerr << "pnw-lat: no LAT service " << service << " heard in "
                      << wait << " seconds\n";
            return 1;
        }

        pnw::Lat::Target t;
        t.node = host->a.node;
        t.service = service;
        t.mtu = std::min<std::uint16_t> (1500, host->a.mtu ? host->a.mtu : 1500);
        bool bridge = !raw8 && pnw::utf8_locale ();
        pnw::Utf8Bridge utf8;
        pnw::Lat lat (t,
            [&] (decnet::ByteView m) { sock.send (host->mac, m); },
            [&] (decnet::ByteView b) {
                if (bridge) {
                    decnet::Bytes u = utf8.out (b);
                    pnw::write_all (STDOUT_FILENO, u.data (), u.size ());
                } else {
                    pnw::write_all (STDOUT_FILENO, b.data (), b.size ());
                }
            });

        std::cerr << "%PNW-S-CONNECTING, to " << service << " on " << t.node
                  << " (" << mac_text (host->mac) << "); Ctrl-] q to leave\r\n";
        std::string ended;
        {
            pnw::RawTerminal raw;
            lat.start (Clock::now ());
            bool escape = false, announced = false;
            while (!lat.closed ()) {
                if (lat.running () && !announced) {
                    announced = true;
                    std::cerr << "%PNW-S-CONNECTED\r\n";
                }
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                    lat.deadline () - Clock::now ()).count ();
                pollfd p[2] = { { STDIN_FILENO, POLLIN, 0 }, { sock.fd (), POLLIN, 0 } };
                int r = ::poll (p, 2, ms < 0 ? 0 : static_cast<int> (std::min<long> (ms, 1000)));
                if (r < 0 && errno == EINTR) continue;
                if (p[0].revents & (POLLIN | POLLHUP)) {
                    std::uint8_t buf[512];
                    ssize_t n = ::read (STDIN_FILENO, buf, sizeof buf);
                    if (n <= 0) { lat.stop (); break; }
                    decnet::Bytes keys;
                    for (ssize_t i = 0; i < n; ++i) {
                        std::uint8_t c = buf[i];
                        if (escape) {
                            escape = false;
                            if (c == 'q' || c == 'Q') { lat.stop (); break; }
                            if (c == 'b' || c == 'B') { lat.send_break (); continue; }
                            if (c != ESCAPE_KEY) continue;
                        } else if (c == ESCAPE_KEY) {
                            escape = true;
                            continue;
                        }
                        keys.push_back (c);
                    }
                    lat.from_terminal (bridge ? utf8.in (keys) : keys);
                }
                if (p[1].revents & POLLIN) {
                    auto f = sock.recv ();
                    if (f && f->first == host->mac) lat.from_host (f->second, Clock::now ());
                }
                lat.tick (Clock::now ());
            }
            ended = lat.why ();
            if (from_cache && !announced) {
                // The remembered address did not answer: it may have moved.
                auto known = load_cache ();
                known.erase (service);
                ended += " (the address remembered for " + service
                       + " did not answer; try again to listen for it, or -l)";
                std::ofstream f (cache_path (), std::ios::trunc);
                for (const auto &[s, e] : known)
                    f << s << ' ' << e.node << ' ' << mac_text (e.mac) << ' ' << e.mtu << '\n';
            }
        }
        std::cerr << "\r\n%PNW-S-DISCONNECTED, " << ended << "\n";
    } catch (const std::exception &e) {
        std::cerr << "pnw-lat: " << e.what () << "\n";
        return 1;
    }
    return 0;
}
