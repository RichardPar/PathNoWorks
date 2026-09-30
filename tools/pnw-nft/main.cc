// pnw-dir, pnw-type, pnw-copy, pnw-delete, pnw-rename -- remote files
// over DECnet.
//
// One source for all five, built with PNW_COMMAND set to which one.
//
//     pnw-dir    'MIM::[USER]*.COM'
//     pnw-type   'VMS"user password"::LOGIN.COM'
//     pnw-copy   'MIM::HECNET.DAT' nodes.dat
//     pnw-copy   notes.txt 'VMS"user password"::[USER]'
//     pnw-delete 'VMS"user password"::NOTES.TXT;*'
//     pnw-rename 'VMS"user password"::NOTES.TXT' OLDNOTES.TXT
//
// Quote remote specs: the shell has opinions about [ ] * ; and ".

#include "pnw/dap.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef PNW_COMMAND
#error "build with PNW_COMMAND defined as dir, type, copy, delete or rename"
#endif

namespace dapm = decnet::dap;

namespace {

struct Options {
    std::string              socket = pnw::Api::default_path ();
    bool                     trace = false;
    bool                     proxy = false;
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
    else if (cmd == "delete")
        std::cerr << "usage: pnw-delete [options] NODE::spec\n"
                     "  Delete remote files.\n";
    else if (cmd == "rename")
        std::cerr << "usage: pnw-rename [options] NODE::file newname\n"
                     "  Rename a remote file on the same node.\n";
    else
        std::cerr << "usage: pnw-copy [options] NODE::files local\n"
                     "       pnw-copy [options] local... NODE::destination\n"
                     "  Copy files from or to a remote node.  A remote wildcard\n"
                     "  copies every match into a local directory; several local\n"
                     "  files go into a remote directory (NODE::[DIR], NODE::dir/\n"
                     "  or NODE::).\n"
                     "  --text      text: one record per line\n"
                     "  --binary    bytes as stored\n"
                     "  Without either, text files are recognised by content\n"
                     "  (sending) or record attributes (receiving).\n";
    std::cerr << "  -s socket   decnetd API socket (default $DECNETAPI or "
                 "/tmp/decnetapi.sock)\n"
                 "  --trace     show DAP messages\n"
                 "  --proxy     without a user, ask for proxy access as the\n"
                 "              local user, as VMS does\n"
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
    pnw::DapSession s (api, spec, o.proxy);
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
            const std::string &own = e.protection->owner;
            // VMS sends a UIC with its brackets; a Unix FAL a bare name.
            if (!own.empty ())
                std::cout << "  " << (own[0] == '[' ? own : "[" + own + "]");
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
    pnw::DapSession s (api, spec, o.proxy);
    s.set_trace (o.trace);
    s.get (spec.path, o.mode, [] (decnet::ByteView b) {
        std::fwrite (b.data (), 1, b.size (), stdout);
    });
    std::fflush (stdout);
    return 0;
}

bool is_directory (const std::string &path)
{
    struct stat st;
    return ::stat (path.c_str (), &st) == 0 && S_ISDIR (st.st_mode);
}

// Does a remote spec name a directory, so that a file name goes after it?
bool names_directory (const std::string &path)
{
    return path.empty () || path.back () == ']' || path.back () == '>'
        || path.back () == '/' || path.back () == ':';
}

bool has_wildcard (const std::string &path)
{
    return path.find_first_of ("*?%") != std::string::npos;
}

// Local files to a remote node, one DAP session for all of them.
int upload (pnw::Api &api, const Options &o)
{
    std::vector<std::string> locals (o.args.begin (), o.args.end () - 1);
    auto spec = pnw::RemoteSpec::parse (o.args.back ());
    if (locals.size () > 1 && !names_directory (spec.path))
        throw pnw::ApiError ("copying several files needs a directory as the "
                             "destination, such as NODE::[DIR] or NODE::dir/");

    pnw::DapSession s (api, spec, o.proxy);
    s.set_trace (o.trace);
    for (const std::string &local : locals) {
        std::ifstream in (local, std::ios::binary);
        if (!in || is_directory (local))
            throw pnw::ApiError ("cannot read " + local + ": "
                                 + (is_directory (local) ? std::string ("a directory")
                                                         : std::strerror (errno)));
        std::string path = spec.path;
        if (names_directory (path))
            path += local.substr (local.find_last_of ('/') + 1);

        bool text;
        if (o.mode == pnw::Transfer::automatic) {
            char sample[4096];
            in.read (sample, sizeof sample);
            text = pnw::looks_like_text (decnet::ByteView (
                reinterpret_cast<const std::uint8_t *> (sample),
                static_cast<std::size_t> (in.gcount ())));
            in.clear ();
            in.seekg (0);
        } else {
            text = o.mode == pnw::Transfer::text;
        }

        std::uint64_t bytes = 0;
        struct stat st {};
        std::optional<std::uint64_t> size;
        if (::stat (local.c_str (), &st) == 0)
            size = static_cast<std::uint64_t> (st.st_size);
        std::string name = s.put (path, text, [&] {
            decnet::Bytes b (8192);
            in.read (reinterpret_cast<char *> (b.data ()),
                     static_cast<std::streamsize> (b.size ()));
            b.resize (static_cast<std::size_t> (in.gcount ()));
            bytes += b.size ();
            return b;
        }, size);
        if (in.bad ()) throw pnw::ApiError ("error reading " + local);
        std::cerr << local << " -> " << spec.node << "::"
                  << (name.empty () ? path : name) << " (" << bytes
                  << " bytes, " << (text ? "text" : "binary") << ")\n";
    }
    return 0;
}

// Remote files here: one, or every file a wildcard matches.
int download (pnw::Api &api, const Options &o)
{
    if (o.args.size () != 2)
        throw pnw::ApiError ("copy from a remote node takes one remote "
                             "specification (wildcards allowed) and one local "
                             "destination");
    auto spec = pnw::RemoteSpec::parse (o.args[0]);
    std::string dest = o.args[1];
    bool into_dir = is_directory (dest);
    if (has_wildcard (spec.path) && !into_dir)
        throw pnw::ApiError (dest + " is not a directory; a wildcard copy "
                             "needs one");
    if (into_dir && dest.back () != '/') dest += '/';

    pnw::DapSession s (api, spec, o.proxy);
    s.set_trace (o.trace);

    // The file being written, so a failure can remove it.  Each goes to a
    // temporary name and is renamed once complete.
    std::unique_ptr<std::ofstream> out;
    std::string tmp, local;
    std::uint64_t bytes = 0;
    dapm::Attributes attrs;
    try {
        unsigned n = s.get_files (spec.path, o.mode,
            [&] (const std::string &name, const dapm::Attributes &a)
                -> std::optional<pnw::DapSession::FileTarget> {
                local = into_dir ? dest + base_name (name) : dest;
                if (base_name (name).empty () || base_name (name).back () == '/')
                    return std::nullopt;        // a directory: nothing to copy
                tmp = local + ".pnw-partial";
                out = std::make_unique<std::ofstream> (
                    tmp, std::ios::binary | std::ios::trunc);
                if (!*out) throw pnw::ApiError ("cannot create " + tmp + ": "
                                                + std::strerror (errno));
                bytes = 0;
                attrs = a;
                pnw::DapSession::FileTarget t;
                t.write = [&] (decnet::ByteView b) {
                    out->write (reinterpret_cast<const char *> (b.data ()),
                                static_cast<std::streamsize> (b.size ()));
                    bytes += b.size ();
                };
                t.finish = [&, name] {
                    out->close ();
                    if (!*out) throw pnw::ApiError ("error writing " + tmp);
                    out.reset ();
                    if (std::rename (tmp.c_str (), local.c_str ()) != 0)
                        throw pnw::ApiError ("cannot rename to " + local + ": "
                                             + std::strerror (errno));
                    tmp.clear ();
                    std::cerr << spec.node << "::" << name << " -> " << local
                              << " (" << bytes << " bytes, "
                              << dapm::Attributes::rfm_name (attrs.rfm)
                              << (attrs.text () ? ", text" : "") << ")\n";
                };
                return t;
            });
        if (n == 0) {
            std::cerr << "pnw-copy: no files matched " << spec.node << "::"
                      << spec.path << "\n";
            return 1;
        }
        if (n > 1) std::cerr << n << " files copied\n";
    } catch (...) {
        out.reset ();
        if (!tmp.empty ()) std::remove (tmp.c_str ());
        throw;
    }
    return 0;
}

int copy (pnw::Api &api, const Options &o)
{
    if (o.args.size () < 2) { usage (); return 2; }
    bool to = pnw::RemoteSpec::is_remote (o.args.back ());
    std::size_t remote_sources = 0;
    for (std::size_t i = 0; i + 1 < o.args.size (); ++i)
        if (pnw::RemoteSpec::is_remote (o.args[i])) ++remote_sources;
    if (to && remote_sources == 0) return upload (api, o);
    if (!to && remote_sources == o.args.size () - 1) return download (api, o);
    throw pnw::ApiError ("copy either local files to NODE::destination, or "
                         "NODE::files to a local destination");
}

int erase (pnw::Api &api, const Options &o)
{
    if (o.args.size () != 1) { usage (); return 2; }
    auto spec = pnw::RemoteSpec::parse (o.args[0]);
    pnw::DapSession s (api, spec, o.proxy);
    s.set_trace (o.trace);
    s.erase (spec.path);
    return 0;
}

int rename (pnw::Api &api, const Options &o)
{
    if (o.args.size () != 2) { usage (); return 2; }
    auto spec = pnw::RemoteSpec::parse (o.args[0]);
    std::string to = o.args[1];
    // The new name may repeat the node; it cannot change it.
    if (pnw::RemoteSpec::is_remote (to)) {
        auto t = pnw::RemoteSpec::parse (to);
        if (t.node != spec.node)
            throw pnw::ApiError ("cannot rename across nodes");
        to = t.path;
    }
    pnw::DapSession s (api, spec, o.proxy);
    s.set_trace (o.trace);
    s.rename (spec.path, to);
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
        else if (a == "--proxy")              o.proxy = true;
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
        if (cmd == "delete") return erase (api, o);
        if (cmd == "rename") return rename (api, o);
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
