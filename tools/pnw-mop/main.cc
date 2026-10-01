// pnw-mop -- MOP, the Maintenance Operation Protocol, through decnetd.
//
// Who is on the LAN, by the system IDs stations announce; ask one who it
// is; read its Ethernet counters; loop messages through it.  The circuit
// is decnetd's: one with "--mop" in its configuration.
//
//     pnw-mop list
//     pnw-mop id BAJI
//     pnw-mop counters aa-00-04-00-9f-74
//     pnw-mop loop BAJI -n 5
//
// A station is an Ethernet address, or a DECnet node name or address,
// which stands for its DECnet Ethernet address (AA-00-04-00-...).

#include "pnw/api.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace json = pnw::json;

void usage ()
{
    std::cerr <<
        "usage: pnw-mop [options] list\n"
        "       pnw-mop [options] id STATION\n"
        "       pnw-mop [options] counters STATION\n"
        "       pnw-mop [options] loop [STATION[,STATION...]] [-n count] [-f]\n"
        "       pnw-mop [options] circuits\n"
        "  list       stations heard on the LAN, from their system IDs\n"
        "  id         ask a station who it is\n"
        "  counters   read a station's Ethernet counters\n"
        "  loop       loop messages through up to three stations and back;\n"
        "             with none, through whoever answers first\n"
        "  STATION    an Ethernet address, or a DECnet node name or address\n"
        "  --socket s decnetd API socket (default $DECNETAPI or "
        "/tmp/decnetapi.sock)\n"
        "  -c circuit the MOP circuit, if decnetd has more than one\n"
        "  -t secs    how long to wait for each answer (default 3)\n"
        "  -n count   loop messages to send (default 1)\n"
        "  -f         loop without a second's pause between messages\n";
}

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

std::string join (const json::Value *v, const char *sep = ", ")
{
    if (!v || !v->is_array ()) return {};
    std::string out;
    for (const json::Value &e : v->as_array ()) {
        if (!out.empty ()) out += sep;
        out += e.to_text ();
    }
    return out;
}

std::string age_text (std::int64_t secs)
{
    char b[32];
    if (secs < 60)        std::snprintf (b, sizeof b, "%llds", static_cast<long long> (secs));
    else if (secs < 3600) std::snprintf (b, sizeof b, "%lldm", static_cast<long long> (secs / 60));
    else                  std::snprintf (b, sizeof b, "%lldh%02lldm",
                                         static_cast<long long> (secs / 3600),
                                         static_cast<long long> (secs / 60 % 60));
    return b;
}

// The DECnet node an Ethernet address stands for, if it is one.
std::string decnet_of (const std::string &mac)
{
    unsigned b[6];
    if (std::sscanf (mac.c_str (), "%x-%x-%x-%x-%x-%x",
                     &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return {};
    if (b[0] != 0xaa || b[1] != 0 || b[2] != 4 || b[3] != 0) return {};
    unsigned id = b[4] | (b[5] << 8);
    return std::to_string (id >> 10) + "." + std::to_string (id & 1023);
}

json::Object mop (const std::string &type, const std::string &circuit)
{
    json::Object o;
    o.set ("api", "mop");
    o.set ("type", type);
    if (!circuit.empty ()) o.set ("circuit", circuit);
    return o;
}

void show_station (const json::Object &s)
{
    std::string addr = upper (s.str ("srcaddr"));
    std::string dn = decnet_of (s.str ("srcaddr"));
    std::cout << "Station " << addr;
    if (!dn.empty ()) std::cout << "  (DECnet " << dn << ")";
    std::cout << "\n";
    auto line = [&] (const char *label, const std::string &v) {
        if (!v.empty ()) std::printf ("    %-22s %s\n", label, v.c_str ());
    };
    line ("Software", s.str ("software"));
    line ("Device", s.str ("device"));
    line ("Processor", s.str ("processor"));
    line ("Datalink", s.str ("datalink"));
    line ("Hardware address", upper (s.str ("hwaddr")));
    line ("MOP version", s.str ("version"));
    line ("Services", join (s.get ("services")));
    if (s.has ("bufsize")) line ("Data link buffer size", std::to_string (s.num ("bufsize")));
    if (s.has ("console_user")) line ("Console user", upper (s.str ("console_user")));
    if (s.has ("reservation_timer"))
        line ("Reservation timer", std::to_string (s.num ("reservation_timer")) + " s");
}

int list (pnw::Api &api, const std::string &circuit)
{
    json::Object r = api.request (mop ("sysid", circuit));
    const json::Value *v = r.get ("sysid");
    if (!v || !v->is_array () || v->as_array ().empty ()) {
        std::cout << "%PNW-I-NONE, no system IDs heard yet; stations announce "
                     "themselves every 8 to 12 minutes\n";
        return 0;
    }
    std::printf ("%-17s  %-9s  %-24s  %-22s  %s\n",
                 "Station", "DECnet", "Software", "Device", "Heard");
    for (const json::Value &e : v->as_array ()) {
        const json::Object &s = e.as_object ();
        // The short device name is enough here; "id" gives the long one.
        std::string dev = s.str ("device");
        if (dev.size () > 22) dev = dev.substr (0, 21) + "~";
        std::printf ("%-17s  %-9s  %-24s  %-22s  %s ago\n",
                     upper (s.str ("srcaddr")).c_str (),
                     decnet_of (s.str ("srcaddr")).c_str (),
                     s.str ("software").substr (0, 24).c_str (),
                     dev.c_str (),
                     age_text (s.num ("age")).c_str ());
    }
    return 0;
}

int id (pnw::Api &api, const std::string &circuit, const std::string &station,
        int timeout)
{
    json::Object q = mop ("sysid", circuit);
    q.set ("dest", station);
    q.set ("timeout", timeout);
    json::Object r = api.request (q, pnw::Timeout ((timeout + 5) * 1000));
    if (r.str ("status") != "ok") {
        std::cerr << "%PNW-E-NOANSWER, " << station << " did not answer\n";
        return 1;
    }
    show_station (r.get ("sysid")->as_array ().at (0).as_object ());
    return 0;
}

int counters (pnw::Api &api, const std::string &circuit,
              const std::string &station, int timeout)
{
    json::Object q = mop ("counters", circuit);
    q.set ("dest", station);
    q.set ("timeout", timeout);
    json::Object r = api.request (q, pnw::Timeout ((timeout + 5) * 1000));
    if (r.str ("status") != "ok") {
        std::cerr << "%PNW-E-NOANSWER, " << station << " did not answer\n";
        return 1;
    }
    std::cout << "Counters from " << upper (r.str ("srcaddr")) << "\n";
    // In the order and words of NCP's line counters.
    static const std::pair<const char *, const char *> rows[] = {
        { "time_since_zeroed",   "Seconds since last zeroed" },
        { "bytes_recv",          "Bytes received" },
        { "bytes_sent",          "Bytes sent" },
        { "mcbytes_recv",        "Multicast bytes received" },
        { "pkts_recv",           "Data blocks received" },
        { "pkts_sent",           "Data blocks sent" },
        { "mcpkts_recv",         "Multicast blocks received" },
        { "pkts_deferred",       "Blocks sent, initially deferred" },
        { "pkts_1_collision",    "Blocks sent, single collision" },
        { "pkts_mult_collision", "Blocks sent, multiple collisions" },
        { "send_fail",           "Send failures" },
        { "recv_fail",           "Receive failures" },
        { "unk_dest",            "Unrecognized frame destination" },
        { "data_overrun",        "Data overrun" },
        { "no_sys_buf",          "System buffer unavailable" },
        { "no_user_buf",         "User buffer unavailable" },
    };
    for (const auto &[key, label] : rows)
        std::printf ("    %12lld  %s\n", static_cast<long long> (r.num (key)), label);
    return 0;
}

int loop (pnw::Api &api, const std::string &circuit,
          const std::vector<std::string> &path, int timeout, int count,
          bool fast)
{
    json::Object q = mop ("loop", circuit);
    if (path.size () == 1) {
        q.set ("dest", path[0]);
    } else if (!path.empty ()) {
        json::Value::Array a;
        for (const std::string &p : path) a.emplace_back (p);
        q.set ("dest", json::Value (std::move (a)));
    }
    q.set ("timeout", timeout);
    q.set ("packets", count);
    q.set ("fast", fast);
    // Every message may take its timeout, and a second's pause besides.
    int worst = count * (timeout + (fast ? 0 : 1)) + 5;
    json::Object r = api.request (q, pnw::Timeout (worst * 1000LL));
    if (r.str ("status") != "ok") {
        std::cerr << "%PNW-E-LOOP, " << r.str ("status") << "\n";
        return 1;
    }
    const json::Value *d = r.get ("delays");
    int lost = 0, n = 0;
    double total = 0, best = 1e9, worst_t = 0;
    for (const json::Value &e : d->as_array ()) {
        ++n;
        double t = e.as_double ();
        if (t < 0) {
            ++lost;
            std::printf ("%3d  no reply\n", n);
            continue;
        }
        total += t;
        best = std::min (best, t);
        worst_t = std::max (worst_t, t);
        std::printf ("%3d  reply from %s in %.2f ms\n", n,
                     upper (r.str ("dest")).c_str (), t * 1000);
    }
    int got = n - lost;
    std::printf ("%%PNW-%s-LOOPED, %d of %d answered", got == n ? "S" : "W", got, n);
    if (got)
        std::printf (", round trip %.2f/%.2f/%.2f ms (min/avg/max)",
                     best * 1000, total / got * 1000, worst_t * 1000);
    std::printf ("\n");
    return lost ? 1 : 0;
}

int circuits (pnw::Api &api)
{
    json::Object r = api.request (mop ("get", ""));
    for (const json::Value &e : r.get ("circuits")->as_array ()) {
        const json::Object &c = e.as_object ();
        std::printf ("%-8s  hardware %s  address %s  offers %s\n",
                     c.str ("name").c_str (), upper (c.str ("hwaddr")).c_str (),
                     upper (c.str ("macaddr")).c_str (),
                     join (c.get ("services")).c_str ());
    }
    return 0;
}

}   // namespace

int main (int argc, char **argv)
{
    std::string sock = pnw::Api::default_path (), circuit, cmd;
    int timeout = 3, count = 1;
    bool fast = false;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto num = [&] (int lo, int hi) {
            if (i + 1 >= argc) return -1;
            int v = std::atoi (argv[++i]);
            return v < lo || v > hi ? -1 : v;
        };
        if (a == "--socket" && i + 1 < argc)    sock = argv[++i];
        else if (a == "-c" && i + 1 < argc)     circuit = argv[++i];
        else if (a == "-t") { if ((timeout = num (1, 60)) < 0) { usage (); return 2; } }
        else if (a == "-n") { if ((count = num (1, 10000)) < 0) { usage (); return 2; } }
        else if (a == "-f")                     fast = true;
        else if (a == "-h" || a == "--help")    { usage (); return 0; }
        else if (cmd.empty ())                  cmd = a;
        else                                    args.push_back (a);
    }
    try {
        if (cmd == "list" && args.empty ()) {
            pnw::Api api (sock);
            return list (api, circuit);
        }
        if (cmd == "id" && args.size () == 1) {
            pnw::Api api (sock);
            return id (api, circuit, args[0], timeout);
        }
        if (cmd == "counters" && args.size () == 1) {
            pnw::Api api (sock);
            return counters (api, circuit, args[0], timeout);
        }
        if (cmd == "loop" && args.size () <= 1) {
            auto path = args.empty () ? std::vector<std::string> () : split (args[0], ',');
            if (path.size () > 3) {
                std::cerr << "pnw-mop: at most three stations to loop through\n";
                return 2;
            }
            pnw::Api api (sock);
            return loop (api, circuit, path, timeout, count, fast);
        }
        if (cmd == "circuits" && args.empty ()) {
            pnw::Api api (sock);
            return circuits (api);
        }
    } catch (const std::exception &e) {
        std::cerr << "pnw-mop: " << e.what () << "\n";
        return 1;
    }
    usage ();
    return 2;
}
