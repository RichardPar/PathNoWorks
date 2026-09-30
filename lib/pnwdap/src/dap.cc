// pnwdap/dap.cc -- file access over DECnet (the NFT side of DAP).
//
// The message sequences follow what PyDECnet's FAL (applications/fal.py)
// and VMS FAL expect:
//
//   directory:  Access(DIRECTORY) -> per file: [Name vol] [Name dir]
//               Name file, Attributes, Date, Protection, [Ack];
//               then Access Complete
//   get:        [Attributes] Access(OPEN) -> Attributes ... Ack
//               Control(CONNECT) -> Ack
//               Control(GET, file transfer mode) -> Data ... Status(EOF)
//               Access Complete(CLOSE) -> Access Complete

#include "pnw/dap.h"

#include <cctype>
#include <cstdio>
#include <iostream>

#include <pwd.h>
#include <unistd.h>

namespace pnw {

using namespace decnet::dap;
using decnet::Bytes;
using decnet::ByteView;

namespace {

// FAL is object 17.
constexpr const char *FAL_OBJECT = "17";

// How long to wait for any one message.  Listing a big directory on a slow
// node can take a while between messages.
constexpr Timeout DAP_TIMEOUT { 120000 };

// Our configuration: what PyDECnet's DapSession sends, which is known to
// satisfy VMS and RSX.
Config our_config ()
{
    Config c;
    c.bufsiz = 65535;
    c.ostype = 192;
    c.filesys = 13;
    for (unsigned b : { Config::cap_fo_seq, Config::cap_seq_xfer,
                        Config::cap_blocking, Config::cap_len2, Config::cap_dir,
                        Config::cap_dattim_xa, Config::cap_fprot_xa,
                        Config::cap_seq_ra, Config::cap_glob, Config::cap_name })
        c.syscap.set (b);
    return c;
}

std::string describe (const Message &m)
{
    return type_name (type_of (m));
}

}   // namespace

// ----------------------------------------------------------- RemoteSpec

bool RemoteSpec::is_remote (const std::string &s)
{
    return s.find ("::") != std::string::npos;
}

RemoteSpec RemoteSpec::parse (const std::string &s)
{
    std::size_t colons = s.find ("::");
    if (colons == std::string::npos)
        throw ApiError ("not a remote file: " + s + " (expected NODE::file)");
    RemoteSpec r;
    r.path = s.substr (colons + 2);
    std::string head = s.substr (0, colons);

    // NODE"user password account"
    std::size_t q = head.find ('"');
    if (q != std::string::npos) {
        std::size_t q2 = head.rfind ('"');
        if (q2 == q) throw ApiError ("unbalanced quote in " + s);
        std::string ac = head.substr (q + 1, q2 - q - 1);
        head = head.substr (0, q);
        std::vector<std::string> words;
        std::string w;
        for (char c : ac) {
            if (c == ' ') { if (!w.empty ()) words.push_back (w); w.clear (); }
            else w += c;
        }
        if (!w.empty ()) words.push_back (w);
        if (words.size () > 0) r.user = words[0];
        if (words.size () > 1) r.password = words[1];
        if (words.size () > 2) r.account = words[2];
    }
    for (char &c : head)
        c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    if (head.empty ()) throw ApiError ("no node name in " + s);
    r.node = head;
    return r;
}

// ------------------------------------------------------------- DirEntry

std::optional<std::uint64_t> DirEntry::blocks () const
{
    if (!attributes || !attributes->has (Attributes::m_ebk)) return std::nullopt;
    const Attributes &a = *attributes;
    // The end of file block counts only if something is in it.
    if (a.ebk == 0) return 0;
    return a.has (Attributes::m_ffb) && a.ffb == 0 ? a.ebk - 1 : a.ebk;
}

// ------------------------------------------------------------ DapSession

DapSession::DapSession (Api &api, const RemoteSpec &spec, bool proxy)
{
    ConnectOptions o;
    o.dest = spec.node;
    o.object = FAL_OBJECT;
    o.username = spec.user;
    o.password = spec.password;
    o.account = spec.account;
    if (spec.user.empty () && proxy) {
        // Proxy access: say who we are here and let the far end decide.
        if (const passwd *pw = ::getpwuid (::geteuid ())) {
            o.username = pw->pw_name;
            for (char &ch : o.username)
                ch = static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
            o.proxy = true;
        }
    }
    link_ = api.connect (o);

    send (our_config ());
    Message m = recv ();
    if (auto *c = std::get_if<Config> (&m)) remote_ = *c;
    else throw ApiError ("FAL answered with " + describe (m)
                         + " instead of its configuration");
    configured_ = true;
}

void DapSession::send (const Message &m)
{
    if (trace_) std::cerr << "dap> " << describe (m) << "\n";
    link_->send (encode (m));
}

Message DapSession::recv ()
{
    for (;;) {
        if (!pending_.empty ()) {
            Message m = std::move (pending_.front ());
            pending_.erase (pending_.begin ());
            if (trace_) std::cerr << "dap< " << describe (m) << "\n";
            // VMS sometimes sends its configuration again after a file.
            // DAP does not allow that, but PyDECnet answers it and goes on,
            // and so do we.
            if (std::holds_alternative<Config> (m) && configured_) {
                send (our_config ());
                continue;
            }
            return m;
        }
        auto data = link_->recv (DAP_TIMEOUT);
        if (!data) throw ApiError ("no answer from FAL");
        try {
            pending_ = decode (*data);
        } catch (const std::exception &e) {
            throw ApiError (std::string ("bad DAP message from FAL: ") + e.what ());
        }
    }
}

std::vector<DirEntry> DapSession::directory (const std::string &path)
{
    Access a;
    a.accfunc = Access::directory;
    a.filespec = path;
    a.display.set (Access::d_main).set (Access::d_date).set (Access::d_fprot)
             .set (Access::d_name);
    send (a);

    std::vector<DirEntry> out;
    std::string vol, dir;
    auto current = [&] () -> DirEntry & {
        if (out.empty ()) out.push_back (DirEntry { vol, dir, {}, {}, {}, {} });
        return out.back ();
    };
    for (;;) {
        Message m = recv ();
        if (auto *n = std::get_if<Name> (&m)) {
            if (n->nametype[Name::volname])      vol = n->namespec;
            else if (n->nametype[Name::dirname]) dir = n->namespec;
            else if (n->nametype[Name::filename])
                out.push_back (DirEntry { vol, dir, n->namespec, {}, {}, {} });
            else if (n->nametype[Name::filespec]) current ().name = n->namespec;
        } else if (auto *at = std::get_if<Attributes> (&m)) {
            current ().attributes = *at;
        } else if (auto *d = std::get_if<DateTime> (&m)) {
            current ().dates = *d;
        } else if (auto *p = std::get_if<Protection> (&m)) {
            current ().protection = *p;
        } else if (std::holds_alternative<AccessComplete> (m)) {
            return out;
        } else if (auto *s = std::get_if<Status> (&m)) {
            if (s->maccode == Status::success) continue;
            throw DapError (*s);
        }
        // Acknowledge (after each entry, in DAP 7) and anything else: skip.
    }
}

namespace {

// Turns the records of a file into local bytes.
class Converter {
public:
    Converter (const Attributes &a, bool text,
               const std::function<void (ByteView)> &sink)
        : a_ (a), text_ (text), sink_ (sink), size_ (a.size ()) {}

    void record (ByteView r)
    {
        if (text_) {
            // Print file control bytes lead VFC records; they are not text.
            if (a_.rfm == Attributes::fb_vfc) {
                std::size_t fsz = a_.fsz ? a_.fsz : 2;
                r = r.subspan (std::min (fsz, r.size ()));
            }
            // A record may carry its own line end (stream files, or a FAL
            // told to make them explicit).  One newline either way.
            while (!r.empty () && (r.back () == '\n' || r.back () == '\r'))
                r = r.first (r.size () - 1);
            sink_ (r);
            static const std::uint8_t nl = '\n';
            sink_ (ByteView (&nl, 1));
            return;
        }
        // Binary: the last block is padded; stop at the real end of file.
        if (size_) {
            if (written_ >= *size_) return;
            r = r.first (std::min<std::uint64_t> (r.size (), *size_ - written_));
        }
        written_ += r.size ();
        sink_ (r);
    }

private:
    const Attributes                         &a_;
    bool                                      text_;
    const std::function<void (ByteView)>     &sink_;
    std::optional<std::uint64_t>              size_;
    std::uint64_t                             written_ = 0;
};

}   // namespace

unsigned DapSession::get_files (const std::string &path, Transfer mode,
                                const OpenFile &open)
{
    // Tell FAL how we want the data, unless we will take it as it is.
    if (mode == Transfer::text) {
        Attributes want;
        want.menu.set (Attributes::m_datatype).set (Attributes::m_rfm)
                 .set (Attributes::m_rat);
        want.datatype = Ext ().set (Attributes::dt_ascii);
        want.rfm = Attributes::fb_var;
        want.rat.set (Attributes::rat_cr);
        send (want);
    } else if (mode == Transfer::binary) {
        Attributes want;
        want.menu.set (Attributes::m_datatype);
        want.datatype = Ext ().set (Attributes::dt_image);
        send (want);
    }

    Access a;
    a.accfunc = Access::open;
    a.filespec = path;
    a.display.set (Access::d_main).set (Access::d_name);
    send (a);

    auto expect_ack = [&] (const char *what) {
        Message m = recv ();
        if (auto *s = std::get_if<Status> (&m)) throw DapError (*s);
        if (!std::holds_alternative<Ack> (m))
            throw ApiError (std::string ("FAL answered ") + what + " with "
                            + describe (m));
    };

    // For each file FAL found: its names and attributes, then an
    // Acknowledge.  After the last, Access Complete.  A single file is a
    // wildcard that matched once.
    unsigned copied = 0;
    std::string name;
    for (;;) {
        Attributes attrs;
        bool got = false;
        for (;;) {
            Message m = recv ();
            if (auto *n = std::get_if<Name> (&m)) {
                if (n->nametype[Name::filename] || n->nametype[Name::filespec])
                    name = n->namespec;
            } else if (auto *at = std::get_if<Attributes> (&m)) {
                attrs = *at;
                got = true;
            } else if (auto *s = std::get_if<Status> (&m)) {
                if (s->maccode != Status::success) throw DapError (*s);
            } else if (std::holds_alternative<AccessComplete> (m)) {
                return copied;
            } else if (std::holds_alternative<Ack> (m)) {
                break;
            }
        }
        if (!got) throw ApiError ("FAL sent no attributes for " + path);
        std::string shown = name.empty () ? path : name;

        auto target = open (shown, attrs);
        if (!target) {
            // Skipped: closing before connecting says so.
            AccessComplete skip;
            skip.cmpfunc = AccessComplete::close;
            send (skip);
            continue;
        }

        Control c;
        c.ctlfunc = Control::connect;
        send (c);
        expect_ack ("CONNECT");

        Control g;
        g.ctlfunc = Control::get;
        g.menu.set (Control::m_rac);
        g.rac = Control::rb_seqf;           // the whole file
        send (g);

        bool text = mode == Transfer::text
                 || (mode == Transfer::automatic && attrs.text ());
        Converter conv (attrs, text, target->write);
        for (;;) {
            Message m = recv ();
            if (auto *d = std::get_if<Data> (&m)) {
                conv.record (d->payload);
            } else if (auto *s = std::get_if<Status> (&m)) {
                if (s->eof ()) break;
                throw DapError (*s);
            } else {
                throw ApiError ("FAL sent " + describe (m)
                                + " in the middle of " + shown);
            }
        }
        if (target->finish) target->finish ();
        ++copied;

        AccessComplete done;
        done.cmpfunc = AccessComplete::close;
        send (done);
        name.clear ();
    }
}

Attributes DapSession::get (const std::string &path, Transfer mode,
                            const std::function<void (ByteView)> &sink)
{
    Attributes result;
    unsigned n = get_files (path, mode,
        [&] (const std::string &, const Attributes &a)
            -> std::optional<FileTarget> {
            result = a;
            return FileTarget { sink, {} };
        });
    if (n == 0) {
        dapm::Status s;
        s.maccode = Status::open_error;
        s.miccode = 062;                // file not found
        throw DapError (s);
    }
    return result;
}

// ------------------------------------------------------------ writing

bool looks_like_text (ByteView sample)
{
    std::size_t odd = 0;
    for (std::uint8_t c : sample) {
        if (c == 0) return false;
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != '\f')
            ++odd;
    }
    return odd * 100 <= sample.size ();
}

void DapSession::expect_complete (const char *what)
{
    for (;;) {
        Message m = recv ();
        if (std::holds_alternative<AccessComplete> (m)) return;
        if (auto *s = std::get_if<Status> (&m)) {
            if (s->maccode == Status::success) return;
            throw DapError (*s);
        }
        if (!std::holds_alternative<Ack> (m) && !std::holds_alternative<Name> (m))
            throw ApiError (std::string ("FAL answered ") + what + " with "
                            + describe (m));
    }
}

std::string DapSession::put (const std::string &path, bool text,
                             const std::function<Bytes ()> &source)
{
    Attributes want;
    want.menu.set (Attributes::m_datatype).set (Attributes::m_org)
             .set (Attributes::m_rfm);
    want.org = Attributes::fb_seq;
    if (text) {
        // Variable length records with carriage return control: a text
        // file as VMS keeps one.
        want.datatype = Ext ().set (Attributes::dt_ascii);
        want.rfm = Attributes::fb_var;
        want.menu.set (Attributes::m_rat);
        want.rat.set (Attributes::rat_cr);
    } else {
        want.datatype = Ext ().set (Attributes::dt_image);
        want.rfm = Attributes::fb_fix;
        want.menu.set (Attributes::m_mrs);
        want.mrs = 512;
    }
    send (want);

    Access a;
    a.accfunc = Access::create;
    a.filespec = path;
    a.fac = Ext ().set (Access::fac_put);
    a.shr = Ext ();
    a.display.set (Access::d_main).set (Access::d_name);
    send (a);

    std::string name;
    for (;;) {
        Message m = recv ();
        if (auto *n = std::get_if<Name> (&m)) name = n->namespec;
        else if (auto *s = std::get_if<Status> (&m)) throw DapError (*s);
        else if (std::holds_alternative<Ack> (m)) break;
    }

    Control c;
    c.ctlfunc = Control::connect;
    send (c);
    Message m = recv ();
    if (auto *s = std::get_if<Status> (&m)) throw DapError (*s);
    if (!std::holds_alternative<Ack> (m))
        throw ApiError ("FAL answered CONNECT with " + describe (m));

    Control p;
    p.ctlfunc = Control::put;
    p.menu.set (Control::m_rac);
    p.rac = Control::rb_seqf;
    send (p);

    // Records.  A failure shows up as a Status the next time we listen,
    // which is at the close: FAL does not acknowledge each record.
    Bytes pending;
    std::uint64_t recnum = 0;
    auto record = [&] (ByteView r) {
        send (Data { recnum++, Bytes (r.begin (), r.end ()) });
    };
    for (;;) {
        Bytes chunk = source ();
        if (chunk.empty ()) break;
        pending.insert (pending.end (), chunk.begin (), chunk.end ());
        if (text) {
            auto start = pending.begin ();
            for (auto it = pending.begin (); it != pending.end (); ++it) {
                if (*it != '\n') continue;
                auto end = it;
                if (end != start && *(end - 1) == '\r') --end;
                record (ByteView (&*start, static_cast<std::size_t> (end - start)));
                start = it + 1;
            }
            pending.erase (pending.begin (), start);
        } else {
            std::size_t off = 0;
            for (; pending.size () - off >= 512; off += 512)
                record (ByteView (pending.data () + off, 512));
            pending.erase (pending.begin (),
                           pending.begin () + static_cast<std::ptrdiff_t> (off));
        }
    }
    // What is left: a last line with no newline, or a short last block.
    if (!pending.empty ()) record (pending);

    AccessComplete done;
    done.cmpfunc = AccessComplete::close;
    send (done);
    expect_complete ("CLOSE");
    return name;
}

void DapSession::erase (const std::string &path)
{
    Access a;
    a.accfunc = Access::erase;
    a.filespec = path;
    send (a);
    expect_complete ("ERASE");
}

void DapSession::rename (const std::string &from, const std::string &to)
{
    Access a;
    a.accfunc = Access::rename;
    a.filespec = from;
    send (a);
    Name n;
    n.nametype.set (Name::filespec);
    n.namespec = to;
    send (n);
    expect_complete ("RENAME");
}

}   // namespace pnw
