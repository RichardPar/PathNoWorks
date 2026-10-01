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
#include "pnw/term.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <clocale>
#include <cstring>
#include <iostream>
#include <string>

#include "decnet/common/platform.h"

#ifdef _WIN32
#include <io.h>
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#endif

namespace {

constexpr std::uint8_t ESCAPE_KEY = 0x1d;           // Ctrl-]

#ifdef _WIN32
// No SIGWINCH: the size is checked at least this often instead.
constexpr int resize_check_ms = 500;
#else
volatile std::sig_atomic_t resized = 0;
void on_winch (int) { resized = 1; }
#endif

// A pollfd for input on fd.  On Windows the fd member is a SOCKET, so a
// braced initialiser from an int will not do.
pollfd poll_in (int fd)
{
    pollfd p {};
    p.fd = fd;
    p.events = POLLIN;
    return p;
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
    pnw::terminal_size (term.width, term.height);
    if (const char *t = std::getenv ("PNW_TRACE")) trace_file = std::fopen (t, "w");
    bool bridge = !raw8 && pnw::utf8_locale ();
    pnw::Utf8Bridge utf8;

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
#ifndef _WIN32
    std::signal (SIGWINCH, on_winch);
#endif
    std::string ended = "connection closed by " + node;
    {
        pnw::RawTerminal raw;
        pnw::TerminalInput keyboard;
        pnw::Cterm cterm (term,
            [&] (decnet::ByteView m) { trace (">", m); link->send (m); },
            [&] (decnet::ByteView b) {
                if (bridge) {
                    decnet::Bytes u = utf8.out (b);
                    pnw::write_all (STDOUT_FILENO, u.data (), u.size ());
                } else {
                    pnw::write_all (STDOUT_FILENO, b.data (), b.size ());
                }
            });

        bool escape = false;
        try {
            while (!cterm.unbound ()) {
#ifdef _WIN32
                {
                    std::uint16_t w = term.width, h = term.height;
                    pnw::terminal_size (w, h);
                    if (w != term.width || h != term.height) {
                        term.width = w;
                        term.height = h;
                        cterm.resize (w, h);
                    }
                }
#else
                if (resized) {
                    resized = 0;
                    std::uint16_t w = term.width, h = term.height;
                    pnw::terminal_size (w, h);
                    cterm.resize (w, h);
                }
#endif
                int wait = -1;
                if (auto due = cterm.deadline ()) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        *due - pnw::Cterm::Clock::now ()).count ();
                    wait = ms < 0 ? 0 : static_cast<int> (ms);
                }
                if (api->buffered ()) wait = 0;
#ifdef _WIN32
                if (wait < 0 || wait > resize_check_ms) wait = resize_check_ms;
#endif
                pollfd p[2] = { poll_in (keyboard.fd ()), poll_in (api->fd ()) };
                int r = decnet::sock_poll (p, 2, wait);
                if (r < 0 && decnet::sock_interrupted (decnet::sock_errno ())) continue;
                cterm.tick (pnw::Cterm::Clock::now ());

                if (p[0].revents & (POLLIN | POLLHUP)) {
                    std::uint8_t buf[512];
                    long n = keyboard.read (buf, sizeof buf);
                    if (n <= 0) { ended = "end of input"; break; }
                    decnet::Bytes keys;
                    bool quit = false;
                    for (long i = 0; i < n; ++i) {
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
