// pnw-fs -- mount a remote DECnet directory, the Pathworks way.
//
//     pnw-fs 'VMS"user password"::DUA0:[USER]' ~/vms
//     pnw-fs --rw 'LINUX::pub/' ~/pub
//     fusermount3 -u ~/vms
//
// Files are fetched whole from the node's FAL when opened and, on a
// read/write mount, sent back whole when closed.  Directory listings are
// cached for a few seconds.  A VMS file's size is what FAL reports, which
// for a text file is not quite what arrives once records become lines, so
// reads bypass the page cache and return what really came.
//
// One DAP session serves every request, so FUSE runs single threaded.
// Any error drops the session; the next request makes a new one.

#define FUSE_USE_VERSION 31
#include <fuse.h>

#include "pnw/dap.h"
#include "pnw/names.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace dapm = decnet::dap;
using decnet::Bytes;
using decnet::ByteView;

namespace {

// How long a directory listing is believed.
constexpr std::time_t LISTING_TTL = 5;

struct Entry {
    bool        dir = false;
    off_t       size = 0;
    std::time_t mtime = 0;
    mode_t      mode = 0;
};

struct Listing {
    std::map<std::string, Entry> entries;
    std::time_t                  when = 0;
};

struct OpenFile {
    std::string path;
    Bytes       data;
    bool        dirty = false;
};

struct Mount {
    std::string     socket;
    pnw::RemoteSpec spec;
    std::unique_ptr<pnw::RemoteTree> tree;
    bool            rw = false;
    bool            trace = false;
    bool            proxy = false;

    std::unique_ptr<pnw::Api>        api;
    std::unique_ptr<pnw::DapSession> dap;
    std::map<std::string, Listing>   dirs;
    std::map<std::uint64_t, OpenFile> files;
    std::uint64_t                    next_fh = 1;

    pnw::DapSession &session ()
    {
        if (!dap) {
            if (!api) api = std::make_unique<pnw::Api> (socket);
            dap = std::make_unique<pnw::DapSession> (*api, spec, proxy);
            dap->set_trace (trace);
        }
        return *dap;
    }

    // After any failure: a new session next time, and the API connection
    // too if that is what broke.
    void reset (bool api_too)
    {
        dap.reset ();
        if (api_too) api.reset ();
    }
};

Mount &mount ()
{
    return *static_cast<Mount *> (fuse_get_context ()->private_data);
}

std::string parent_of (const std::string &p)
{
    auto slash = p.rfind ('/');
    return slash == 0 || slash == std::string::npos ? "/" : p.substr (0, slash);
}

std::string name_of (const std::string &p)
{
    return p.substr (p.rfind ('/') + 1);
}

// DAP and RMS failures as errno values.
int errno_for (const pnw::DapError &e)
{
    switch (e.status ().miccode) {
    case 040: case 062: return ENOENT;          // directory, file not found
    case 055:           return EEXIST;
    case 063:           return EINVAL;          // bad file name
    case 0125:          return EACCES;          // privilege violation
    case 0164:          return EROFS;           // write locked
    case 065:           return ENOSPC;
    default:            return EIO;
    }
}

// Run a request, turning exceptions into -errno and dropping the session
// after anything that may have left it out of step.
template <typename F>
int guard (F f)
{
    Mount &m = mount ();
    try {
        return f (m);
    } catch (const pnw::DapError &e) {
        if (m.trace) std::cerr << "pnw-fs: " << e.what () << "\n";
        m.reset (false);
        return -errno_for (e);
    } catch (const pnw::Rejected &e) {
        std::cerr << "pnw-fs: " << e.what () << "\n";
        m.reset (false);
        return -EACCES;
    } catch (const std::exception &e) {
        std::cerr << "pnw-fs: " << e.what () << "\n";
        m.reset (true);
        return -EIO;
    }
}

mode_t mode_from (const dapm::Protection &p, bool dir)
{
    // Owner, group and world; the DEC system class has no Unix place.
    auto bits = [&] (const decnet::dap::Ext &deny) {
        mode_t m = 0;
        if (!deny[dapm::Protection::no_read])  m |= 4;
        if (!deny[dapm::Protection::no_write]) m |= 2;
        if (!deny[dapm::Protection::no_exec] || dir) m |= 1;
        return m;
    };
    mode_t m = 0;
    if (p.menu[dapm::Protection::m_own]) m |= bits (p.own) << 6;
    if (p.menu[dapm::Protection::m_grp]) m |= bits (p.grp) << 3;
    if (p.menu[dapm::Protection::m_wld]) m |= bits (p.wld);
    return m;
}

const Listing &list (Mount &m, const std::string &dir)
{
    std::time_t now = std::time (nullptr);
    auto it = m.dirs.find (dir);
    if (it != m.dirs.end () && now - it->second.when < LISTING_TTL)
        return it->second;

    Listing l;
    l.when = now;
    for (const pnw::DirEntry &e : m.session ().directory (m.tree->listing (dir))) {
        auto n = pnw::local_name (e.name, m.tree->vms ());
        if (!n || l.entries.count (n->name)) continue;
        Entry en;
        en.dir = n->directory;
        if (!en.dir && e.attributes) {
            if (auto sz = e.attributes->size ()) en.size = static_cast<off_t> (*sz);
            else if (auto b = e.blocks ()) en.size = static_cast<off_t> (*b * 512);
        }
        en.mtime = now;
        if (e.dates) {
            const std::string &d = !e.dates->rdt.empty () ? e.dates->rdt
                                                          : e.dates->cdt;
            if (auto t = pnw::parse_dap_date (d)) en.mtime = *t;
        }
        en.mode = e.protection && mode_from (*e.protection, en.dir)
                ? mode_from (*e.protection, en.dir)
                : (en.dir ? 0755 : 0644);
        if (!m.rw) en.mode &= static_cast<mode_t> (~0222);
        l.entries[n->name] = en;
    }
    return m.dirs[dir] = std::move (l);
}

void forget (Mount &m, const std::string &path)
{
    m.dirs.erase (parent_of (path));
}

// -------------------------------------------------------------- operations

void *fs_init (fuse_conn_info *, fuse_config *cfg)
{
    cfg->direct_io = 1;             // sizes are FAL's estimate; reads are not
    cfg->attr_timeout = 1;
    cfg->entry_timeout = 1;
    cfg->negative_timeout = 0;
    return fuse_get_context ()->private_data;
}

int fs_getattr (const char *cpath, struct stat *st, fuse_file_info *)
{
    return guard ([&] (Mount &m) {
        std::memset (st, 0, sizeof *st);
        std::string path = cpath;
        st->st_uid = ::getuid ();
        st->st_gid = ::getgid ();
        st->st_nlink = 1;
        if (path == "/") {
            st->st_mode = S_IFDIR | 0755;
            st->st_nlink = 2;
            return 0;
        }
        // A file being written that the node has not seen yet.
        for (const auto &[fh, f] : m.files) {
            if (f.path == path) {
                st->st_mode = S_IFREG | 0644;
                st->st_size = static_cast<off_t> (f.data.size ());
                st->st_mtime = std::time (nullptr);
                return 0;
            }
        }
        std::string parent = parent_of (path);
        const Listing &l = list (m, parent);
        auto it = l.entries.find (name_of (path));
        if (it == l.entries.end ()) return -ENOENT;
        const Entry &e = it->second;
        st->st_mode = (e.dir ? S_IFDIR : S_IFREG) | e.mode;
        st->st_size = e.size;
        st->st_blocks = (e.size + 511) / 512;
        st->st_mtime = st->st_ctime = st->st_atime = e.mtime;
        if (e.dir) st->st_nlink = 2;
        return 0;
    });
}

int fs_readdir (const char *cpath, void *buf, fuse_fill_dir_t fill, off_t,
                fuse_file_info *, fuse_readdir_flags)
{
    return guard ([&] (Mount &m) {
        std::string dir = cpath;
        const Listing &l = list (m, dir);
        fill (buf, ".", nullptr, 0, static_cast<fuse_fill_dir_flags> (0));
        fill (buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags> (0));
        for (const auto &[name, e] : l.entries)
            fill (buf, name.c_str (), nullptr, 0,
                  static_cast<fuse_fill_dir_flags> (0));
        return 0;
    });
}

int fs_open (const char *cpath, fuse_file_info *fi)
{
    return guard ([&] (Mount &m) {
        int acc = fi->flags & O_ACCMODE;
        if (acc != O_RDONLY && !m.rw) return -EROFS;
        OpenFile f;
        f.path = cpath;
        if (fi->flags & O_TRUNC) {
            f.dirty = true;
        } else {
            m.session ().get (m.tree->spec (cpath), pnw::Transfer::automatic,
                              [&] (ByteView b) {
                                  f.data.insert (f.data.end (), b.begin (), b.end ());
                              });
        }
        fi->fh = m.next_fh++;
        fi->direct_io = 1;
        m.files[fi->fh] = std::move (f);
        return 0;
    });
}

int fs_create (const char *cpath, mode_t, fuse_file_info *fi)
{
    return guard ([&] (Mount &m) {
        if (!m.rw) return -EROFS;
        OpenFile f;
        f.path = cpath;
        f.dirty = true;
        fi->fh = m.next_fh++;
        fi->direct_io = 1;
        m.files[fi->fh] = std::move (f);
        return 0;
    });
}

int fs_read (const char *, char *buf, size_t size, off_t off, fuse_file_info *fi)
{
    auto it = mount ().files.find (fi->fh);
    if (it == mount ().files.end ()) return -EBADF;
    const Bytes &d = it->second.data;
    if (off < 0 || static_cast<size_t> (off) >= d.size ()) return 0;
    size_t n = std::min (size, d.size () - static_cast<size_t> (off));
    std::memcpy (buf, d.data () + off, n);
    return static_cast<int> (n);
}

int fs_write (const char *, const char *buf, size_t size, off_t off,
              fuse_file_info *fi)
{
    auto it = mount ().files.find (fi->fh);
    if (it == mount ().files.end ()) return -EBADF;
    Bytes &d = it->second.data;
    size_t end = static_cast<size_t> (off) + size;
    if (d.size () < end) d.resize (end);
    std::memcpy (d.data () + off, buf, size);
    it->second.dirty = true;
    return static_cast<int> (size);
}

int fs_truncate (const char *cpath, off_t size, fuse_file_info *fi)
{
    return guard ([&] (Mount &m) {
        if (!m.rw) return -EROFS;
        if (fi) {
            auto it = m.files.find (fi->fh);
            if (it == m.files.end ()) return -EBADF;
            it->second.data.resize (static_cast<size_t> (size));
            it->second.dirty = true;
            return 0;
        }
        // Not open: fetch, cut and send back.
        Bytes data;
        if (size > 0)
            m.session ().get (m.tree->spec (cpath), pnw::Transfer::automatic,
                              [&] (ByteView b) {
                                  data.insert (data.end (), b.begin (), b.end ());
                              });
        data.resize (static_cast<size_t> (size));
        bool sent = false;
        m.session ().put (m.tree->spec (cpath), pnw::looks_like_text (data), [&] {
            if (sent) return Bytes ();
            sent = true;
            return data;
        });
        forget (m, cpath);
        return 0;
    });
}

int fs_flush (const char *, fuse_file_info *fi)
{
    return guard ([&] (Mount &m) {
        auto it = m.files.find (fi->fh);
        if (it == m.files.end () || !it->second.dirty) return 0;
        OpenFile &f = it->second;
        bool sent = false;
        m.session ().put (m.tree->spec (f.path), pnw::looks_like_text (f.data),
                          [&] {
                              if (sent) return Bytes ();
                              sent = true;
                              return f.data;
                          });
        f.dirty = false;
        forget (m, f.path);
        return 0;
    });
}

int fs_release (const char *, fuse_file_info *fi)
{
    mount ().files.erase (fi->fh);
    return 0;
}

int fs_unlink (const char *cpath)
{
    return guard ([&] (Mount &m) {
        if (!m.rw) return -EROFS;
        m.session ().erase (m.tree->spec (cpath));
        forget (m, cpath);
        return 0;
    });
}

int fs_rename (const char *from, const char *to, unsigned int flags)
{
    return guard ([&] (Mount &m) {
        if (!m.rw) return -EROFS;
        if (flags) return -EINVAL;
        m.session ().rename (m.tree->spec (from), m.tree->spec (to));
        forget (m, from);
        forget (m, to);
        return 0;
    });
}

// DAP has no way to make or remove a directory through FAL.
int fs_mkdir (const char *, mode_t) { return -EPERM; }
int fs_rmdir (const char *) { return -EPERM; }

// Attributes a remote file does not have in the Unix sense.  Accepted and
// ignored, so that "cp -p" and "touch" work.
int fs_chmod (const char *, mode_t, fuse_file_info *) { return 0; }
int fs_chown (const char *, uid_t, gid_t, fuse_file_info *) { return 0; }
int fs_utimens (const char *, const timespec *, fuse_file_info *) { return 0; }

int fs_statfs (const char *, struct statvfs *st)
{
    std::memset (st, 0, sizeof *st);
    st->f_bsize = st->f_frsize = 512;
    st->f_blocks = st->f_bfree = st->f_bavail = 1u << 30;
    st->f_namemax = 255;
    return 0;
}

void usage ()
{
    std::cerr <<
        "usage: pnw-fs [options] NODE::directory mountpoint\n"
        "  Mount a directory on a DECnet node.  Unmount with fusermount3 -u.\n"
        "  --rw        allow writing, deleting and renaming\n"
        "  --proxy     without a user, ask for proxy access as the local user\n"
        "  -s socket   decnetd API socket (default $DECNETAPI or "
        "/tmp/decnetapi.sock)\n"
        "  --trace     show DAP messages\n"
        "  -f          stay in the foreground\n"
        "  -o opt      FUSE mount options\n"
        "Access control: NODE\"user password\"::directory\n";
}

}   // namespace

int main (int argc, char **argv)
{
    auto m = std::make_unique<Mount> ();
    m->socket = pnw::Api::default_path ();
    std::vector<std::string> fuse_args { argv[0] };
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-s" && i + 1 < argc)        m->socket = argv[++i];
        else if (a == "--rw")                 m->rw = true;
        else if (a == "--trace")              m->trace = true;
        else if (a == "--proxy")              m->proxy = true;
        else if (a == "-h" || a == "--help")  { usage (); return 0; }
        else if (a == "-o" && i + 1 < argc)   { fuse_args.push_back (a);
                                                fuse_args.push_back (argv[++i]); }
        else if (a == "-f" || a == "-d")      fuse_args.push_back (a);
        else                                  pos.push_back (a);
    }
    if (pos.size () != 2) { usage (); return 2; }

    try {
        m->spec = pnw::RemoteSpec::parse (pos[0]);
        m->tree = std::make_unique<pnw::RemoteTree> (m->spec.path);
        // Make sure the directory can be listed before mounting, so that a
        // mistake is reported here rather than as an empty mount.
        pnw::Api api (m->socket);
        pnw::DapSession s (api, m->spec, m->proxy);
        s.directory (m->tree->listing ("/"));
    } catch (const std::exception &e) {
        std::cerr << "pnw-fs: " << e.what () << "\n";
        return 1;
    }

    // Single threaded: one DAP session serves everything.
    fuse_args.push_back ("-s");
    fuse_args.push_back ("-o");
    // Say read only to the kernel too, so tools see it before trying.
    fuse_args.push_back ("fsname=" + m->spec.node + "::" + m->spec.path
                         + ",subtype=pnw" + (m->rw ? "" : ",ro"));
    fuse_args.push_back (pos[1]);

    fuse_operations ops {};
    ops.init = fs_init;
    ops.getattr = fs_getattr;
    ops.readdir = fs_readdir;
    ops.open = fs_open;
    ops.create = fs_create;
    ops.read = fs_read;
    ops.write = fs_write;
    ops.truncate = fs_truncate;
    ops.flush = fs_flush;
    ops.release = fs_release;
    ops.unlink = fs_unlink;
    ops.rename = fs_rename;
    ops.mkdir = fs_mkdir;
    ops.rmdir = fs_rmdir;
    ops.chmod = fs_chmod;
    ops.chown = fs_chown;
    ops.utimens = fs_utimens;
    ops.statfs = fs_statfs;

    std::vector<char *> fargv;
    for (std::string &s : fuse_args) fargv.push_back (s.data ());
    fargv.push_back (nullptr);
    return fuse_main (static_cast<int> (fargv.size () - 1), fargv.data (),
                      &ops, m.get ());
}
