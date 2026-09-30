// pnw-dir, pnw-type, pnw-copy -- remote files over DECnet.
//
// One source for three commands, built with PNW_COMMAND set to which one.
//
//     pnw-dir  'MIM::[USER]*.COM'
//     pnw-type 'VMS"user password"::LOGIN.COM'
//     pnw-copy 'MIM::HECNET.DAT' nodes.dat
//
// Quote remote specs: the shell has opinions about [ ] * ; and ".

#include "pnw/dap.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef PNW_COMMAND
#error "build with PNW_COMMAND defined as \"dir\", \"type\" or \"copy\""
#endif

namespace dapm = decnet::dap;

namespace {

struct Options {
    std::string              socket = pnw::Api::default_path ();
    bool                     trace = false;
    pnw::Transfer            mode = pnw::Transfer::automatic;
    std::vector<std::string> args;
};

void usage ()
{
    std::string cmd = PNW_COMMAND;
    if (cmd == "dir")
        std::cerr << "usage: pnw-dir [options] NODE::spec\n"
                     "  List remote files.\n";
    else if (cmd == "type")
        std::cerr << "usage: pnw-type [options] NODE::file\n"
                     "  Write a remote file to standard output.\n";
    else
        std::cerr << "usage: pnw-copy [options] NODE::file local\n"
                     "  Copy a remote file here.  local may be a directory.\n"
                     "  --text      ask for text records, one line each\n"
                     "  --binary    take the bytes as stored\n";
    std::cerr << "  -s socket   decnetd API socket (default $DECNETAPI or "
                 "/tmp/decnetapi.sock)\n"
                 "  --trace     show DAP messages\n"
                 "Access control: NODE\"user password account\"::file\n";
}

// The file name part of a remote spec: after the directory, before the
// version.  "DUA0:[USER]LOGIN.COM;3" and "/home/u/x.txt" both work.
std::string base_name (const std::string &spec)
{
    std::size_t cut = spec.find_last_of ("]>:/");
    std::string n = cut == std::string::npos ? spec : spec.substr (cut + 1);
    std::size_t semi = n.find (';');
    if (semi != std::string::npos) n.erase (semi);
    return n;
}

int dir (pnw::Api &api, const Options &o)
{
    if (o.args.size () != 1) { usage (); return 2; }
    auto spec = pnw::RemoteSpec::parse (o.args[0]);
    pnw::DapSession s (api, spec);
    s.set_trace (o.trace);
    auto entries = s.directory (spec.path);

    std::string where;
    unsigned files = 0;
    std::uint64_t blocks = 0;
    for (const auto &e : entries) {
        std::string here = e.volume + e.directory;
        if (here != where || files == 0) {
            if (files) std::cout << "\n";
            std::cout << "Directory " << spec.node << "::" << here << "\n\n";
            where = here;
        }
        ++files;
        char line[256];
        std::string size = "";
        if (auto b = e.blocks ()) { size = std::to_string (*b); blocks += *b; }
        std::string date;
        if (e.dates)
            date = !e.dates->rdt.empty () ? e.dates->rdt : e.dates->cdt;
        std::snprintf (line, sizeof line, "%-30s %8s  %-18s", e.name.c_str (),
                       size.c_str (), date.c_str ());
        std::cout << line;
        if (e.protection) {
            if (!e.protection->owner.empty ())
                std::cout << "  [" << e.protection->owner << "]";
            std::cout << "  " << e.protection->vms ();
        }
        std::cout << "\n";
    }
    if (!files) {
        std::cout << "No files found.\n";
        return 1;
    }
    std::cout << "\nTotal of " << files << (files == 1 ? " file" : " files")
              << ", " << blocks << (blocks == 1 ? " block.\n" : " blocks.\n");
    return 0;
}

int type (pnw::Api &api, const Options &o)
{
    if (o.args.size () != 1) { usage (); return 2; }
    auto spec = pnw::RemoteSpec::parse (o.args[0]);
    pnw::DapSession s (api, spec);
    s.set_trace (o.trace);
    s.get (spec.path, o.mode, [] (decnet::ByteView b) {
        std::fwrite (b.data (), 1, b.size (), stdout);
    });
    std::fflush (stdout);
    return 0;
}

int copy (pnw::Api &api, const Options &o)
{
    if (o.args.size () != 2) { usage (); return 2; }
    auto spec = pnw::RemoteSpec::parse (o.args[0]);
    std::string local = o.args[1];
    struct stat st;
    if (::stat (local.c_str (), &st) == 0 && S_ISDIR (st.st_mode)) {
        if (local.back () != '/') local += '/';
        local += base_name (spec.path);
    }

    // Write to a temporary name so a failed copy leaves nothing behind.
    std::string tmp = local + ".pnw-partial";
    std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw pnw::ApiError ("cannot create " + tmp + ": "
                                   + std::strerror (errno));
    pnw::DapSession s (api, spec);
    s.set_trace (o.trace);
    std::uint64_t bytes = 0;
    try {
        auto a = s.get (spec.path, o.mode, [&] (decnet::ByteView b) {
            out.write (reinterpret_cast<const char *> (b.data ()),
                       static_cast<std::streamsize> (b.size ()));
            bytes += b.size ();
        });
        out.close ();
        if (!out) throw pnw::ApiError ("error writing " + tmp);
        if (std::rename (tmp.c_str (), local.c_str ()) != 0)
            throw pnw::ApiError ("cannot rename to " + local + ": "
                                 + std::strerror (errno));
        std::cerr << spec.node << "::" << spec.path << " -> " << local << " ("
                  << bytes << " bytes, " << dapm::Attributes::rfm_name (a.rfm)
                  << (a.text () ? ", text" : "") << ")\n";
    } catch (...) {
        std::remove (tmp.c_str ());
        throw;
    }
    return 0;
}

}   // namespace

int main (int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-s" && i + 1 < argc)       o.socket = argv[++i];
        else if (a == "--trace")              o.trace = true;
        else if (a == "--text")               o.mode = pnw::Transfer::text;
        else if (a == "--binary")             o.mode = pnw::Transfer::binary;
        else if (a == "-h" || a == "--help") { usage (); return 0; }
        else                                  o.args.push_back (a);
    }
    std::string cmd = PNW_COMMAND;
    try {
        pnw::Api api (o.socket);
        if (cmd == "dir")  return dir (api, o);
        if (cmd == "type") return type (api, o);
        return copy (api, o);
    } catch (const pnw::DapError &e) {
        std::cerr << "pnw-" << cmd << ": " << e.what () << "\n";
    } catch (const pnw::Rejected &e) {
        std::cerr << "pnw-" << cmd << ": cannot connect to FAL: " << e.what ()
                  << "\n";
    } catch (const std::exception &e) {
        std::cerr << "pnw-" << cmd << ": " << e.what () << "\n";
    }
    return 1;
}
