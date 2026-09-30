// pnw-sethost -- log in to a DECnet node, as SET HOST does.
//
//     pnw-sethost VAXXY
//     xterm -ti vt340 -e pnw-sethost VAXXY       # with Sixel graphics
//
// Speaks CTERM to the node's command terminal object (42).  Line editing
// and echo are done here, as CTERM asks; everything the node writes goes to
// the terminal untouched, so the terminal emulator you run this in is the
// terminal the node sees -- VT300 escape sequences, 8-bit controls, Sixel.
//
// Ctrl-] then q ends the session from this end, should the node stop
// answering.  Ctrl-] twice sends a Ctrl-].

#include "pnw/api.h"
#include "pnw/cterm.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <clocale>
#include <cstring>
#include <iostream>
#include <string>

#include <langinfo.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {

constexpr std::uint8_t ESCAPE_KEY = 0x1d;           // Ctrl-]

volatile std::sig_atomic_t resized = 0;
void on_winch (int) { resized = 1; }

// The local terminal in raw, 8-bit mode for as long as this lives.
class RawTerminal {
public:
    RawTerminal ()
    {
        ok_ = ::isatty (STDIN_FILENO) && ::tcgetattr (STDIN_FILENO, &saved_) == 0;
        if (!ok_) return;
        termios raw = saved_;
        ::cfmakeraw (&raw);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        ::tcsetattr (STDIN_FILENO, TCSANOW, &raw);
    }
    ~RawTerminal () { if (ok_) ::tcsetattr (STDIN_FILENO, TCSANOW, &saved_); }

private:
    bool    ok_ = false;
    termios saved_ {};
};

void size (std::uint16_t &w, std::uint16_t &h)
{
    winsize ws {};
    if (::ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        w = ws.ws_col;
        h = ws.ws_row;
    }
}

void write_all (int fd, const std::uint8_t *p, std::size_t n)
{
    while (n) {
        ssize_t w = ::write (fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;
        p += w;
        n -= static_cast<std::size_t> (w);
    }
}

// Between the node's 8-bit character set and a UTF-8 terminal.  VMS sends
// 8-bit controls (CSI as the single byte 0x9b) to a VT300 and DEC
// Multinational -- as good as Latin-1 -- for text; a UTF-8 terminal takes
// neither.  So: C1 controls go out as their 7-bit equivalents, ESC and the
// byte less 0x40, which every VT terminal and emulator accepts; Latin-1
// goes out as UTF-8; UTF-8 typed comes in as Latin-1.  Sixel and ReGIS
// data are 7-bit inside their control strings, and pass as they are.
class Utf8Bridge {
public:
    decnet::Bytes out (decnet::ByteView b) const
    {
        decnet::Bytes r;
        r.reserve (b.size ());
        for (std::uint8_t c : b) {
            if (c < 0x80) r.push_back (c);
            else if (c < 0xa0) { r.push_back (0x1b); r.push_back (static_cast<std::uint8_t> (c - 0x40)); }
            else { r.push_back (static_cast<std::uint8_t> (0xc0 | (c >> 6)));
                   r.push_back (static_cast<std::uint8_t> (0x80 | (c & 0x3f))); }
        }
        return r;
    }

    decnet::Bytes in (decnet::ByteView b)
    {
        decnet::Bytes r;
        for (std::uint8_t c : b) {
            if (need_ == 0) {
                if (c < 0x80) { r.push_back (c); continue; }
                if ((c & 0xe0) == 0xc0) { code_ = c & 0x1f; need_ = 1; }
                else if ((c & 0xf0) == 0xe0) { code_ = c & 0x0f; need_ = 2; }
                else if ((c & 0xf8) == 0xf0) { code_ = c & 0x07; need_ = 3; }
                else r.push_back ('?');
                continue;
            }
            if ((c & 0xc0) != 0x80) { need_ = 0; r.push_back ('?'); continue; }
            code_ = (code_ << 6) | (c & 0x3f);
            if (--need_ == 0)
                r.push_back (code_ <= 0xff ? static_cast<std::uint8_t> (code_) : '?');
        }
        return r;
    }

private:
    unsigned code_ = 0, need_ = 0;
};

bool utf8_locale ()
{
    std::setlocale (LC_CTYPE, "");
    const char *cs = ::nl_langinfo (CODESET);
    return cs && std::string (cs) == "UTF-8";
}

// PNW_TRACE=file: every message to and from the node, in hex.
std::FILE *trace_file = nullptr;
void trace (const char *dir, decnet::ByteView b)
{
    if (!trace_file) return;
    std::fprintf (trace_file, "%s", dir);
    for (std::uint8_t c : b) std::fprintf (trace_file, " %02x", c);
    std::fprintf (trace_file, "\n");
    std::fflush (trace_file);
}

void usage ()
{
    std::cerr <<
        "usage: pnw-sethost [options] node\n"
        "  Log in to a DECnet node over CTERM, as SET HOST does.\n"
        "  -s socket   decnetd API socket (default $DECNETAPI or "
        "/tmp/decnetapi.sock)\n"
        "  -t type     terminal type to report (default VT300)\n"
        "  --8bit      pass 8-bit characters and controls straight through;\n"
        "              by default they are translated for a UTF-8 terminal\n"
        "              when the locale is UTF-8\n"
        "  Ctrl-] q    end the session from this end\n";
}

}   // namespace

int main (int argc, char **argv)
{
    std::string sock = pnw::Api::default_path ();
    pnw::Cterm::Terminal term;
    bool raw8 = false;
    std::string node;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-s" && i + 1 < argc)        sock = argv[++i];
        else if (a == "-t" && i + 1 < argc)   term.type = argv[++i];
        else if (a == "--8bit")               raw8 = true;
        else if (a == "-h" || a == "--help")  { usage (); return 0; }
        else if (node.empty ())               node = a;
        else                                  { usage (); return 2; }
    }
    if (node.empty ()) { usage (); return 2; }
    for (char &c : node) c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    size (term.width, term.height);
    if (const char *t = std::getenv ("PNW_TRACE")) trace_file = std::fopen (t, "w");
    bool bridge = !raw8 && utf8_locale ();
    Utf8Bridge utf8;

    std::unique_ptr<pnw::Api> api;
    std::unique_ptr<pnw::Link> link;
    try {
        api = std::make_unique<pnw::Api> (sock);
        pnw::ConnectOptions o;
        o.dest = node;
        o.object = "42";
        link = api->connect (o);
    } catch (const pnw::Rejected &e) {
        std::cerr << "pnw-sethost: " << node << ": " << e.what () << "\n";
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "pnw-sethost: " << e.what () << "\n";
        return 1;
    }

    std::cerr << "%PNW-S-CONNECTED, to " << node << "; Ctrl-] q to leave\r\n";
    std::signal (SIGWINCH, on_winch);
    std::string ended = "connection closed by " + node;
    {
        RawTerminal raw;
        pnw::Cterm cterm (term,
            [&] (decnet::ByteView m) { trace (">", m); link->send (m); },
            [&] (decnet::ByteView b) {
                if (bridge) {
                    decnet::Bytes u = utf8.out (b);
                    write_all (STDOUT_FILENO, u.data (), u.size ());
                } else {
                    write_all (STDOUT_FILENO, b.data (), b.size ());
                }
            });

        bool escape = false;
        try {
            while (!cterm.unbound ()) {
                if (resized) {
                    resized = 0;
                    std::uint16_t w = term.width, h = term.height;
                    size (w, h);
                    cterm.resize (w, h);
                }
                int wait = -1;
                if (auto due = cterm.deadline ()) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        *due - pnw::Cterm::Clock::now ()).count ();
                    wait = ms < 0 ? 0 : static_cast<int> (ms);
                }
                if (api->buffered ()) wait = 0;
                pollfd p[2] = { { STDIN_FILENO, POLLIN, 0 }, { api->fd (), POLLIN, 0 } };
                int r = ::poll (p, 2, wait);
                if (r < 0 && errno == EINTR) continue;
                cterm.tick (pnw::Cterm::Clock::now ());

                if (p[0].revents & (POLLIN | POLLHUP)) {
                    std::uint8_t buf[512];
                    ssize_t n = ::read (STDIN_FILENO, buf, sizeof buf);
                    if (n <= 0) { ended = "end of input"; break; }
                    decnet::Bytes keys;
                    bool quit = false;
                    for (ssize_t i = 0; i < n; ++i) {
                        std::uint8_t c = buf[i];
                        if (escape) {
                            escape = false;
                            if (c == 'q' || c == 'Q') { quit = true; break; }
                            if (c != ESCAPE_KEY) continue;
                        } else if (c == ESCAPE_KEY) {
                            escape = true;
                            continue;
                        }
                        keys.push_back (c);
                    }
                    cterm.from_terminal (bridge ? utf8.in (keys) : keys);
                    if (quit) { ended = "session ended here"; break; }
                }
                if ((p[1].revents & (POLLIN | POLLHUP)) || api->buffered ()) {
                    // Everything that has arrived, without waiting for more.
                    while (true) {
                        auto m = link->recv (std::chrono::milliseconds (0));
                        if (!m) break;
                        trace ("<", *m);
                        cterm.from_host (*m);
                        if (!api->buffered ()) break;
                    }
                }
            }
            if (cterm.unbound ()) ended = "unbound by " + node;
        } catch (const std::exception &e) {
            ended = e.what ();
        }
    }
    std::cerr << "\r\n%PNW-S-DISCONNECTED, " << ended << "\n";
    return 0;
}
