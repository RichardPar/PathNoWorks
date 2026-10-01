// pnw-ncp -- Network Control Program for decnetd.
//
// A subset of VMS NCP: SHOW and LIST read network management information
// from a node's NML listener (object 19), and LOOP NODE loops messages
// through a node's MIRROR (object 25).  Commands go to the local node
// unless prefixed with TELL.  With no arguments it prompts, as NCP does.
//
//     pnw-ncp show known nodes
//     pnw-ncp tell MIM show executor characteristics
//     pnw-ncp loop node MIM count 5 length 100

#include "pnw/api.h"

#include "decnet/nice/nml.h"
#include "decnet/nice/packets.h"

#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "decnet/common/platform.h"

#ifdef _WIN32
#include <io.h>
#define STDIN_FILENO 0
#else
#include <unistd.h>
#endif

namespace nm = decnet::nice;
using decnet::Nodeid;

namespace {

// A command error: bad syntax, or something the network said.
struct Failure {
    std::string text;
};

std::string upper (std::string s)
{
    for (char &c : s) c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    return s;
}

// NCP accepts any abbreviation of a keyword down to three letters, or the
// whole word if shorter.
bool is (const std::string &word, const char *keyword)
{
    std::string w = upper (word), k = keyword;
    if (w.size () > k.size ()) return false;
    if (w.size () < std::min<std::size_t> (3, k.size ())) return false;
    return k.compare (0, w.size (), w) == 0;
}

// The words of a command, consumed left to right.
class Words {
public:
    explicit Words (std::vector<std::string> w) : w_ (std::move (w)) {}

    bool done () const noexcept { return i_ >= w_.size (); }
    const std::string &peek () const
    {
        static const std::string none;
        return done () ? none : w_[i_];
    }
    std::string take ()
    {
        if (done ()) throw Failure { "incomplete command" };
        return w_[i_++];
    }
    bool accept (const char *keyword)
    {
        if (!done () && is (w_[i_], keyword)) { ++i_; return true; }
        return false;
    }

private:
    std::vector<std::string> w_;
    std::size_t              i_ = 0;
};

std::vector<std::string> split (const std::string &line)
{
    std::istringstream in (line);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back (w);
    return out;
}

std::string now_text ()
{
    static const char *const months[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    std::time_t t = std::time (nullptr);
    std::tm tm {};
    ::localtime_r (&t, &tm);
    char buf[32];
    std::snprintf (buf, sizeof buf, "%2d-%s-%04d %02d:%02d:%02d", tm.tm_mday,
                   months[tm.tm_mon], tm.tm_year + 1900, tm.tm_hour,
                   tm.tm_min, tm.tm_sec);
    return buf;
}

// ----------------------------------------------------------------- SHOW

struct ShowRequest {
    nm::NiceRequest req;
    std::string       title;            // "Known Node"
};

// SHOW's entity and information type.  NICE says nothing about LIST
// beyond "permanent", and decnetd has no permanent database, so LIST gets
// the same request with that flag and whatever answer the node gives.
ShowRequest parse_show (Words &w, bool permanent)
{
    ShowRequest s;
    nm::NiceRequest &r = s.req;
    r.function = nm::fn_read;
    r.permanent = permanent;

    using nm::Entity;
    using nm::ReqEntity;

    // Plural forms: KNOWN NODES, ACTIVE CIRCUITS, ...
    std::int8_t wild = 0;
    std::string wildname;
    if      (w.accept ("KNOWN"))       { wild = ReqEntity::known;       wildname = "Known"; }
    else if (w.accept ("ACTIVE"))      { wild = ReqEntity::active;      wildname = "Active"; }
    else if (w.accept ("ADJACENT"))    { wild = ReqEntity::adjacent;    wildname = "Adjacent"; }
    else if (w.accept ("SIGNIFICANT")) { wild = ReqEntity::significant; wildname = "Significant"; }
    else if (w.accept ("LOOP"))        { wild = ReqEntity::loop;        wildname = "Loop"; }

    struct Kind { const char *singular, *plural, *title; std::uint8_t kind; };
    static const Kind kinds[] = {
        { "NODE",    "NODES",    "Node",    Entity::node },
        { "CIRCUIT", "CIRCUITS", "Circuit", Entity::circuit },
        { "LINE",    "LINES",    "Line",    Entity::line },
        { "AREA",    "AREAS",    "Area",    Entity::area },
        { "MODULE",  "MODULES",  "Module",  Entity::module },
    };

    if (wild) {
        const Kind *k = nullptr;
        for (const Kind &c : kinds)
            if (w.accept (c.plural) || w.accept (c.singular)) { k = &c; break; }
        if (!k) throw Failure { "expected NODES, CIRCUITS, LINES or AREAS" };
        r.entity_type = k->kind;
        r.entity = ReqEntity::make_wild (k->kind, wild);
        s.title = wildname + " " + k->title;
    } else if (w.accept ("EXECUTOR")) {
        r.entity_type = Entity::node;
        r.entity = ReqEntity::make_node (Nodeid ());
        s.title = "Node";
    } else {
        const Kind *k = nullptr;
        for (const Kind &c : kinds)
            if (w.accept (c.singular)) { k = &c; break; }
        if (!k) throw Failure { "expected EXECUTOR, NODE, CIRCUIT, LINE, "
                                "AREA, MODULE, or KNOWN/ACTIVE/ADJACENT" };
        std::string name = w.take ();
        r.entity_type = k->kind;
        if (k->kind == Entity::node) {
            try { r.entity = ReqEntity::make_node (Nodeid::parse (name)); }
            catch (const std::exception &) {
                r.entity = ReqEntity::make_named (Entity::node, upper (name));
            }
        } else if (k->kind == Entity::area) {
            try { r.entity = ReqEntity::make_area (std::stoul (name)); }
            catch (const std::exception &) {
                throw Failure { "area must be a number: " + name };
            }
        } else {
            r.entity = ReqEntity::make_named (k->kind, upper (name));
        }
        s.title = k->title;
    }

    // What to show.
    r.info = nm::info_summary;
    std::string infoname = "Summary";
    if      (w.accept ("SUMMARY"))         { r.info = nm::info_summary; }
    else if (w.accept ("STATUS"))          { r.info = nm::info_status;   infoname = "Status"; }
    else if (w.accept ("CHARACTERISTICS")) { r.info = nm::info_char;     infoname = "Characteristics"; }
    else if (w.accept ("COUNTERS"))        { r.info = nm::info_counters; infoname = "Counters"; }
    else if (w.accept ("EVENTS"))          { r.info = nm::info_events;   infoname = "Events"; }
    if (!w.done ()) throw Failure { "unexpected \"" + w.peek () + "\"" };

    s.title += std::string (permanent ? " Permanent " : " Volatile ")
             + infoname + " as of " + now_text ();
    return s;
}

std::string error_text (const nm::NiceReply &r)
{
    std::string text;
    if (const char *t = nm::retcode_text (r.retcode)) text = t;
    else text = "error " + std::to_string (r.retcode);
    std::string detail = nm::detail_text (r.retcode, r.detail);
    if (!detail.empty ()) text += ", " + detail;
    if (!r.message.empty ()) text += ", " + r.message;
    return text;
}

void show (pnw::Api &api, const std::string &target, Words &w, bool permanent)
{
    ShowRequest s = parse_show (w, permanent);

    pnw::ConnectOptions o;
    o.dest = target;
    o.object = "19";
    o.data = decnet::Bytes (std::begin (nm::nice_version),
                            std::end (nm::nice_version));
    auto link = api.connect (o);
    link->send (s.req.encode ());

    // One reply is the answer; code 2 announces several, ended by -128.
    bool multiple = false, printed_title = false;
    for (;;) {
        auto msg = link->recv ();
        if (!msg) throw Failure { "no reply from " + target };
        nm::NiceReply hdr = nm::NiceReply::parse_header (*msg);
        if (hdr.retcode == nm::rc_multiple) { multiple = true; continue; }
        if (hdr.retcode == nm::rc_done) break;
        if (hdr.retcode < 0) throw Failure { error_text (hdr) };

        nm::NiceReply r = nm::NiceReply::parse (*msg, s.req.entity_type);
        if (!printed_title) {
            std::cout << "\n" << s.title << "\n\n";
            printed_title = true;
        }
        std::cout << r.format (nm::params_for (s.req.entity_type)) << "\n\n";
        if (!multiple) break;
    }
    if (!printed_title) std::cout << "\n" << s.title << "\n\n(none)\n";
    link->disconnect ();
}

// ----------------------------------------------------------------- LOOP

void loop (pnw::Api &api, Words &w)
{
    if (!w.accept ("NODE")) throw Failure { "only LOOP NODE is supported" };
    std::string node = w.take ();
    unsigned count = 1, length = 40;
    while (!w.done ()) {
        if (w.accept ("COUNT"))       count = std::stoul (w.take ());
        else if (w.accept ("LENGTH")) length = std::stoul (w.take ());
        else throw Failure { "unexpected \"" + w.peek () + "\"" };
    }
    if (count == 0 || length == 0) throw Failure { "COUNT and LENGTH must be positive" };

    pnw::ConnectOptions o;
    o.dest = upper (node);
    o.object = "25";
    auto link = api.connect (o);
    // MIRROR's accept data is the largest message it will loop.
    const decnet::Bytes &ad = link->accept_data ();
    unsigned max = ad.size () >= 2 ? (ad[0] | (ad[1] << 8)) : 0xffff;
    if (length > max)
        throw Failure { "LENGTH too large; " + node + " loops at most "
                        + std::to_string (max) + " bytes" };

    decnet::Bytes msg { 0x00 };             // "loop this back"
    for (unsigned i = 0; i < length; ++i)
        msg.push_back (static_cast<std::uint8_t> ((i & 1) ? 0x55 : 0xaa));

    unsigned good = 0;
    double total = 0;
    for (unsigned i = 0; i < count; ++i) {
        auto t0 = std::chrono::steady_clock::now ();
        link->send (msg);
        auto reply = link->recv (std::chrono::seconds (10));
        double ms = std::chrono::duration<double, std::milli> (
            std::chrono::steady_clock::now () - t0).count ();
        if (!reply) { std::cout << "  " << i + 1 << ": no reply\n"; continue; }
        decnet::Bytes want = msg;
        want[0] = 0x01;
        if (*reply == want) { ++good; total += ms; }
        else std::cout << "  " << i + 1 << ": reply did not match\n";
    }
    link->disconnect ();
    if (good != count)
        throw Failure { std::to_string (count - good) + " of "
                        + std::to_string (count) + " messages not looped" };
    char avg[32];
    std::snprintf (avg, sizeof avg, "%.1f", total / good);
    std::cout << "Loop node " << upper (node) << ": " << count << " x "
              << length << " bytes looped, average " << avg << " ms\n";
}

// ------------------------------------------------------------- commands

void help ()
{
    std::cout <<
        "Commands:\n"
        "  [TELL node] SHOW|LIST entity [SUMMARY|STATUS|CHARACTERISTICS|COUNTERS]\n"
        "      entity: EXECUTOR, NODE n, CIRCUIT c, LINE l, AREA a, MODULE m,\n"
        "              KNOWN|ACTIVE|ADJACENT|SIGNIFICANT NODES|CIRCUITS|LINES|AREAS\n"
        "  LOOP NODE n [COUNT c] [LENGTH l]\n"
        "  HELP, EXIT\n"
        "Keywords may be abbreviated to three letters.\n";
}

// Run one command.  False for EXIT.
bool run (pnw::Api &api, const std::vector<std::string> &words)
{
    Words w (words);
    if (w.done ()) return true;
    if (w.accept ("EXIT") || w.accept ("QUIT")) return false;
    if (w.accept ("HELP")) { help (); return true; }

    std::string target = api.system ();
    if (w.accept ("TELL")) target = upper (w.take ());

    if (w.accept ("SHOW"))      show (api, target, w, false);
    else if (w.accept ("LIST")) show (api, target, w, true);
    else if (w.accept ("LOOP")) loop (api, w);
    else throw Failure { "unrecognized command \"" + w.peek ()
                         + "\"; try HELP" };
    return true;
}

void usage ()
{
    std::cerr <<
        "usage: pnw-ncp [-s socket] [command]\n"
        "  Network Control Program for decnetd.  With no command, prompts.\n"
        "  -s socket   decnetd API socket (default $DECNETAPI or "
        "/tmp/decnetapi.sock)\n\n";
    help ();
}

}   // namespace

int main (int argc, char **argv)
{
    std::string sock = pnw::Api::default_path ();
    std::vector<std::string> words;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-s" && i + 1 < argc)       sock = argv[++i];
        else if (a == "-h" || a == "--help") { usage (); return 0; }
        else                                  words.push_back (a);
    }

    std::unique_ptr<pnw::Api> api;
    try {
        api = std::make_unique<pnw::Api> (sock);
    } catch (const std::exception &e) {
        std::cerr << "pnw-ncp: " << e.what () << "\n";
        return 2;
    }

    auto attempt = [&] (const std::vector<std::string> &cmd) -> int {
        try {
            return run (*api, cmd) ? 0 : -1;
        } catch (const Failure &f) {
            std::cerr << "%NCP-F-FAIL, " << f.text << "\n";
        } catch (const pnw::Rejected &r) {
            std::cerr << "%NCP-F-CONNECT, " << r.what () << "\n";
        } catch (const std::exception &e) {
            std::cerr << "%NCP-F-ERROR, " << e.what () << "\n";
        }
        return 1;
    };

    if (!words.empty ()) return attempt (words) > 0 ? 1 : 0;

    // Interactive.
    bool tty = ::isatty (STDIN_FILENO);
    std::string line;
    for (;;) {
        if (tty) std::cout << "NCP>" << std::flush;
        if (!std::getline (std::cin, line)) break;
        if (attempt (split (line)) < 0) break;
    }
    if (tty) std::cout << "\n";
    return 0;
}
