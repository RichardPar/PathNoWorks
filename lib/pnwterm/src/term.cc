// pnwterm/term.cc -- the local terminal.

#include "pnw/term.h"

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstring>
#include <string>

#ifdef _WIN32
#include "decnet/common/platform.h"

#include <io.h>
#include <mutex>
#include <thread>
#include <windows.h>
#else
#include <langinfo.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace pnw {

#ifdef _WIN32

// ------------------------------------------------------------- Windows

RawTerminal::RawTerminal ()
{
    HANDLE in = ::GetStdHandle (STD_INPUT_HANDLE);
    DWORD mode = 0;
    ok_ = ::GetConsoleMode (in, &mode) != 0;
    if (ok_) {
        in_mode_ = mode;
        // Keys as bytes, as typed: no line editing, no echo, Ctrl-C as 0x03,
        // and cursor and function keys as VT sequences.
        mode &= ~static_cast<DWORD> (ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT
                                     | ENABLE_PROCESSED_INPUT);
        ::SetConsoleMode (in, mode | ENABLE_VIRTUAL_TERMINAL_INPUT);
        in_cp_ = ::GetConsoleCP ();
        ::SetConsoleCP (CP_UTF8);
    }
    HANDLE out = ::GetStdHandle (STD_OUTPUT_HANDLE);
    out_ok_ = ::GetConsoleMode (out, &mode) != 0;
    if (out_ok_) {
        out_mode_ = mode;
        // The console as a VT terminal; a bare LF moves down only, as on a VT.
        ::SetConsoleMode (out, mode | ENABLE_PROCESSED_OUTPUT
                                    | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                    | DISABLE_NEWLINE_AUTO_RETURN);
        out_cp_ = ::GetConsoleOutputCP ();
        ::SetConsoleOutputCP (CP_UTF8);
    }
}

RawTerminal::~RawTerminal ()
{
    if (ok_) {
        ::SetConsoleMode (::GetStdHandle (STD_INPUT_HANDLE), in_mode_);
        ::SetConsoleCP (in_cp_);
    }
    if (out_ok_) {
        ::SetConsoleMode (::GetStdHandle (STD_OUTPUT_HANDLE), out_mode_);
        ::SetConsoleOutputCP (out_cp_);
    }
}

// The reader thread and the poller share this.  A byte waits on the read
// end of the socket pair exactly while there is input or end of input to
// collect, so polling it is level triggered, as polling stdin is.
struct TerminalInput::Shared {
    std::mutex  m;
    std::string buf;
    bool        eof = false;
    int         rd = -1, wr = -1;

    ~Shared ()
    {
        if (rd >= 0) decnet::sock_close (rd);
        if (wr >= 0) decnet::sock_close (wr);
    }

    void wake () { std::uint8_t b = 0; decnet::sock_send (wr, &b, 1); }
};

namespace {

// A connected pair of loopback TCP sockets: Windows has no socketpair.
bool loopback_pair (int &a, int &b)
{
    int l = decnet::sock_open (AF_INET, SOCK_STREAM);
    if (l < 0) return false;
    sockaddr_in sa {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    socklen_t len = sizeof sa;
    bool ok = ::bind (l, reinterpret_cast<sockaddr *> (&sa), sizeof sa) == 0
           && ::listen (l, 1) == 0
           && ::getsockname (l, reinterpret_cast<sockaddr *> (&sa), &len) == 0;
    if (ok) {
        a = decnet::sock_open (AF_INET, SOCK_STREAM);
        ok = a >= 0
          && ::connect (a, reinterpret_cast<sockaddr *> (&sa), sizeof sa) == 0;
        if (ok) b = decnet::sock_accept (l);
        ok = ok && b >= 0;
    }
    decnet::sock_close (l);
    return ok;
}

}   // namespace

TerminalInput::TerminalInput () : s_ (std::make_shared<Shared> ())
{
    if (!loopback_pair (s_->wr, s_->rd)) return;
    std::thread ([s = s_] {
        HANDLE in = ::GetStdHandle (STD_INPUT_HANDLE);
        for (;;) {
            char b[512];
            DWORD n = 0;
            bool got = ::ReadFile (in, b, sizeof b, &n, nullptr) && n > 0;
            std::lock_guard l (s->m);
            if (!got) {
                if (s->buf.empty () && !s->eof) s->wake ();
                s->eof = true;
                return;
            }
            if (s->buf.empty () && !s->eof) s->wake ();
            s->buf.append (b, n);
        }
    }).detach ();
    // Detached: a thread blocked reading the console cannot be joined
    // without input.  It holds its own reference to the shared state, and
    // ends with the process.
}

TerminalInput::~TerminalInput () = default;

int TerminalInput::fd () const noexcept { return s_->rd; }

long TerminalInput::read (std::uint8_t *buf, std::size_t n)
{
    std::lock_guard l (s_->m);
    if (s_->buf.empty ()) return s_->eof ? 0 : -1;
    std::size_t k = std::min (n, s_->buf.size ());
    std::memcpy (buf, s_->buf.data (), k);
    s_->buf.erase (0, k);
    if (s_->buf.empty () && !s_->eof) {
        std::uint8_t b;
        decnet::sock_recv (s_->rd, &b, 1);      // all collected
    }
    return static_cast<long> (k);
}

void terminal_size (std::uint16_t &w, std::uint16_t &h)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (::GetConsoleScreenBufferInfo (::GetStdHandle (STD_OUTPUT_HANDLE), &info)) {
        w = static_cast<std::uint16_t> (info.srWindow.Right - info.srWindow.Left + 1);
        h = static_cast<std::uint16_t> (info.srWindow.Bottom - info.srWindow.Top + 1);
    }
}

void write_all (int fd, const std::uint8_t *p, std::size_t n)
{
    // WriteFile, not _write: the CRT would turn LF into CR LF.
    HANDLE h = reinterpret_cast<HANDLE> (::_get_osfhandle (fd));
    while (n) {
        DWORD w = 0;
        if (!::WriteFile (h, p, static_cast<DWORD> (n), &w, nullptr) || !w)
            return;
        p += w;
        n -= w;
    }
}

bool utf8_locale ()
{
    DWORD mode;
    return ::GetConsoleMode (::GetStdHandle (STD_OUTPUT_HANDLE), &mode) != 0;
}

#else

// --------------------------------------------------------------- POSIX

RawTerminal::RawTerminal ()
{
    ok_ = ::isatty (STDIN_FILENO) && ::tcgetattr (STDIN_FILENO, &saved_) == 0;
    if (!ok_) return;
    termios raw = saved_;
    ::cfmakeraw (&raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    ::tcsetattr (STDIN_FILENO, TCSANOW, &raw);
}

RawTerminal::~RawTerminal ()
{
    if (ok_) ::tcsetattr (STDIN_FILENO, TCSANOW, &saved_);
}

TerminalInput::TerminalInput () = default;
TerminalInput::~TerminalInput () = default;

int TerminalInput::fd () const noexcept { return STDIN_FILENO; }

long TerminalInput::read (std::uint8_t *buf, std::size_t n)
{
    for (;;) {
        ssize_t r = ::read (STDIN_FILENO, buf, n);
        if (r < 0 && errno == EINTR) continue;
        return static_cast<long> (r);
    }
}

void terminal_size (std::uint16_t &w, std::uint16_t &h)
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

bool utf8_locale ()
{
    std::setlocale (LC_CTYPE, "");
    const char *cs = ::nl_langinfo (CODESET);
    return cs && std::string (cs) == "UTF-8";
}

#endif

// ------------------------------------------------------------ Utf8Bridge

decnet::Bytes Utf8Bridge::out (decnet::ByteView b) const
{
    decnet::Bytes r;
    r.reserve (b.size ());
    for (std::uint8_t c : b) {
        if (c < 0x80) {
            r.push_back (c);
        } else if (c < 0xa0) {
            r.push_back (0x1b);
            r.push_back (static_cast<std::uint8_t> (c - 0x40));
        } else {
            r.push_back (static_cast<std::uint8_t> (0xc0 | (c >> 6)));
            r.push_back (static_cast<std::uint8_t> (0x80 | (c & 0x3f)));
        }
    }
    return r;
}

decnet::Bytes Utf8Bridge::in (decnet::ByteView b)
{
    decnet::Bytes r;
    for (std::uint8_t c : b) {
        if (need_ == 0) {
            if (c < 0x80) { r.push_back (c); continue; }
            if ((c & 0xe0) == 0xc0)      { code_ = c & 0x1f; need_ = 1; }
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

}   // namespace pnw
