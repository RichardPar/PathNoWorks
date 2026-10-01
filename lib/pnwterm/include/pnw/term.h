// pnw/term.h -- the local terminal, for tools that give it to a remote
// system: raw 8-bit mode, its size, and the bridge between a remote
// system's 8-bit character set and a UTF-8 terminal.

#ifndef PNW_TERM_H
#define PNW_TERM_H

#include "decnet/common/types.h"

#include <cstdint>
#include <memory>

#ifndef _WIN32
#include <termios.h>
#endif

namespace pnw {

// The local terminal in raw, 8-bit mode for as long as this lives.  On
// Windows: the console with line editing and echo off, VT sequences on in
// both directions, and the UTF-8 code page.
class RawTerminal {
public:
    RawTerminal ();
    ~RawTerminal ();
    RawTerminal (const RawTerminal &) = delete;
    RawTerminal &operator= (const RawTerminal &) = delete;

private:
    bool    ok_ = false;
#ifdef _WIN32
    bool          out_ok_ = false;
    unsigned long in_mode_ = 0, out_mode_ = 0;
    unsigned      in_cp_ = 0, out_cp_ = 0;
#else
    termios saved_ {};
#endif
};

// Keyboard input, waitable alongside sockets.  On POSIX this is stdin.  On
// Windows a console or pipe cannot be polled with sockets, so a thread
// reads stdin and signals a loopback socket; poll fd() with the sockets.
class TerminalInput {
public:
    TerminalInput ();
    ~TerminalInput ();
    TerminalInput (const TerminalInput &) = delete;
    TerminalInput &operator= (const TerminalInput &) = delete;

    // What to poll for POLLIN.
    int fd () const noexcept;

    // What has been typed: the count, 0 at end of input, -1 on error.
    // Call when fd() polls readable.
    long read (std::uint8_t *buf, std::size_t n);

private:
#ifdef _WIN32
    struct Shared;
    std::shared_ptr<Shared> s_;
#endif
};

// The terminal's size, if it has one; w and h are left alone if not.
void terminal_size (std::uint16_t &w, std::uint16_t &h);

// Write everything, retrying after interruptions.  Bytes go out as they
// are: no newline translation on Windows.
void write_all (int fd, const std::uint8_t *p, std::size_t n);

// Is the locale's character set UTF-8?  On Windows: is stdout a console,
// which RawTerminal puts in the UTF-8 code page.
bool utf8_locale ();

// Between a DEC system's 8-bit character set and a UTF-8 terminal.  VMS
// and RSX send a VT300 8-bit controls (CSI as the single byte 0x9b) and DEC
// Multinational -- as good as Latin-1 -- for text; a UTF-8 terminal takes
// neither.  So: C1 controls go out as their 7-bit equivalents, ESC and the
// byte less 0x40, which every VT terminal and emulator accepts; Latin-1
// goes out as UTF-8; UTF-8 typed comes in as Latin-1.  Sixel and ReGIS data
// are 7-bit inside their control strings, and pass as they are.
class Utf8Bridge {
public:
    decnet::Bytes out (decnet::ByteView b) const;
    decnet::Bytes in (decnet::ByteView b);

private:
    unsigned code_ = 0, need_ = 0;
};

}   // namespace pnw

#endif  // PNW_TERM_H
