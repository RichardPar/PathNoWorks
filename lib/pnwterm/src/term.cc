// pnwterm/term.cc -- the local terminal.

#include "pnw/term.h"

#include <cerrno>
#include <clocale>
#include <string>

#include <langinfo.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace pnw {

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
