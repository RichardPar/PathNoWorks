// pnwmail/mail11.cc -- DECnet mail (Mail-11), both ways.

#include "pnw/mail11.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <iostream>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace pnw {

namespace {

constexpr const char *MAIL_OBJECT = "27";
constexpr Timeout MAIL_TIMEOUT { 120000 };

Bytes bytes (const std::string &s) { return Bytes (s.begin (), s.end ()); }
std::string text (const Bytes &b) { return std::string (b.begin (), b.end ()); }

std::string trim (std::string s)
{
    while (!s.empty () && (s.back () == ' ' || s.back () == '\0')) s.pop_back ();
    return s;
}

void show (bool trace, const char *dir, const Bytes &b)
{
    if (!trace) return;
    std::cerr << "mail" << dir << " \"";
    for (std::uint8_t c : b) {
        if (c >= 0x20 && c < 0x7f) std::cerr << char (c);
        else { char h[8]; std::snprintf (h, sizeof h, "\\x%02x", c); std::cerr << h; }
    }
    std::cerr << "\"\n";
}

Bytes need (Link &link, bool trace)
{
    auto r = link.recv (MAIL_TIMEOUT);
    if (!r) throw ApiError ("the other end stopped answering");
    show (trace, "<", *r);
    return *r;
}

// A status record: 01 00 00 00 is success.  Anything else is a failure,
// VMS's condition code, followed by its text, one or more records, and a
// NUL record to end them.
MailResult status (Link &link, const std::string &user, bool trace)
{
    Bytes r = need (link, trace);
    MailResult m { user, !r.empty () && r[0] == 1, {} };
    if (!m.ok) {
        for (;;) {
            Bytes t = need (link, trace);
            if (t.size () == 1 && t[0] == 0) break;
            if (!m.error.empty ()) m.error += " ";
            m.error += trim (text (t));
        }
        if (m.error.empty ()) m.error = "refused";
    }
    return m;
}

}   // namespace

std::vector<MailResult> mail11_send (Api &api, const std::string &node,
                                     const MailLogin &login,
                                     const std::string &from,
                                     const std::vector<std::string> &users,
                                     const std::string &to_line,
                                     const std::string &subject,
                                     const std::vector<std::string> &body,
                                     bool trace)
{
    ConnectOptions o;
    o.dest = node;
    o.object = MAIL_OBJECT;
    o.username = login.user;
    o.password = login.password;
    auto link = api.connect (o);
    show (trace, "< accept", link->accept_data ());
    auto put = [&] (const std::string &s) {
        Bytes b = bytes (s);
        show (trace, ">", b);
        link->send (b);
    };

    put (from);
    std::vector<MailResult> results;
    std::vector<std::string> accepted;
    for (const std::string &u : users) {
        put (u);
        MailResult r = status (*link, u, trace);
        if (r.ok) accepted.push_back (u);
        results.push_back (r);
    }
    put (std::string (1, '\0'));
    if (accepted.empty ()) {
        link->disconnect ();
        return results;
    }
    put (to_line);
    put (subject.empty () ? "No subject" : subject);
    for (std::string line : body) {
        // An empty record is not a line to VMS: send a space instead.
        if (line.empty ()) line = " ";
        put (line);
    }
    put (std::string (1, '\0'));

    // One status for each recipient that was accepted.
    for (MailResult &r : results) {
        if (!r.ok) continue;
        MailResult f = status (*link, r.user, trace);
        r.ok = f.ok;
        r.error = f.error;
    }
    link->disconnect ();
    return results;
}

Bytes mail11_accept_data ()
{
    // Mail-11 3.1, from an ULTRIX-32 system; no CC line; block mode 0xa0 02
    // -- what dnprogs' vmsmaild answers, and VMS accepts.
    return Bytes { 3, 1, 0, 18, 0, 0, 0, 0, 0xa0, 2, 0, 0, 1, 0, 0, 0 };
}

bool mail11_receive (Link &link, const Incoming &in,
                     const std::function<bool (const MailMessage &)> &deliver,
                     bool trace)
{
    try {
        show (trace, "< connect", in.data);
        MailMessage m;
        m.node = in.node;
        std::string who = trim (text (need (link, trace)));
        // VMS sends the bare user name; the node is the link's.
        m.from = who.find ("::") == std::string::npos ? in.node + "::" + who : who;

        for (;;) {
            Bytes r = need (link, trace);
            if (r.size () == 1 && r[0] == 0) break;
            m.recipients.push_back (trim (text (r)));
            Bytes ok { 1, 0, 0, 0 };
            show (trace, ">", ok);
            link.send (ok);
        }
        // Then To:, and from a sender whose connect data sets option flags
        // (VMS 6.2 sends 03 01 00 07 ...) a CC: line, empty if there is
        // none; the subject; and from that sender one more record before
        // the body, empty from VMS.  Which flag means what is not known: any
        // of them means VMS's layout.  A sender with no connect data, as
        // pnw-mail is, sends neither.
        bool extended = in.data.size () >= 4 && in.data[3] != 0;
        m.to = trim (text (need (link, trace)));
        if (extended) m.cc = trim (text (need (link, trace)));
        m.subject = trim (text (need (link, trace)));
        if (extended) need (link, trace);
        for (;;) {
            Bytes r = need (link, trace);
            if (r.size () == 1 && r[0] == 0) break;
            std::string line = text (r);
            if (line == " ") line.clear ();
            m.body.push_back (line);
        }
        bool stored = deliver (m);
        for (std::size_t i = 0; i < m.recipients.size (); ++i) {
            Bytes st = stored ? Bytes { 1, 0, 0, 0 } : Bytes { 0, 0, 0, 0 };
            show (trace, ">", st);
            link.send (st);
            if (!stored) {
                Bytes why = bytes ("PNW mail: could not store the message");
                show (trace, ">", why);
                link.send (why);
                link.send (Bytes { 0 });
            }
        }
        // The sender closes the link once it has read the statuses.  Closing
        // it first can lose the last of them: VMS then reports the last
        // recipient as failed.
        try {
            while (link.recv (std::chrono::seconds (10))) {}
        } catch (const ApiError &) {}
        return true;
    } catch (const ApiError &e) {
        if (trace) std::cerr << "mail: " << e.what () << "\n";
        return false;
    }
}

void append_mbox (const std::string &path, const MailMessage &m)
{
    int fd = ::open (path.c_str (), O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (fd < 0)
        throw ApiError ("cannot open " + path + ": " + std::strerror (errno));
    ::flock (fd, LOCK_EX);

    std::time_t now = std::time (nullptr);
    char date[64], asc[64];
    std::tm tm {};
    ::localtime_r (&now, &tm);
    std::strftime (date, sizeof date, "%a, %d %b %Y %H:%M:%S %z", &tm);
    std::strftime (asc, sizeof asc, "%a %b %e %H:%M:%S %Y", &tm);

    // A From: that mail readers can reply through a Mail-11 gateway to:
    // the DECnet address, quoted.
    std::string s = "From MAIL-11 " + std::string (asc) + "\n";
    s += "From: \"" + m.from + "\" <mail11@" + m.node + ".decnet>\n";
    s += "To: " + m.to + "\n";
    if (!m.cc.empty ()) s += "Cc: " + m.cc + "\n";
    s += "Subject: " + m.subject + "\n";
    s += "Date: " + std::string (date) + "\n";
    s += "X-Mail11-From: " + m.from + "\n";
    s += "\n";
    for (const std::string &line : m.body) {
        // mbox: a body line starting "From " would start a message.
        std::string l = line;
        std::size_t k = 0;
        while (k < l.size () && l[k] == '>') ++k;
        if (l.compare (k, 5, "From ") == 0) l.insert (0, ">");
        s += l + "\n";
    }
    s += "\n";
    ssize_t w = ::write (fd, s.data (), s.size ());
    ::flock (fd, LOCK_UN);
    ::close (fd);
    if (w != static_cast<ssize_t> (s.size ()))
        throw ApiError ("cannot write " + path);
}

}   // namespace pnw
