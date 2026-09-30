// pnw/term.h -- the local terminal, for tools that give it to a remote
// system: raw 8-bit mode, its size, and the bridge between a remote
// system's 8-bit character set and a UTF-8 terminal.

#ifndef PNW_TERM_H
#define PNW_TERM_H

#include "decnet/common/types.h"

#include <cstdint>

#include <termios.h>

namespace pnw {

// The local terminal in raw, 8-bit mode for as long as this lives.
class RawTerminal {
public:
    RawTerminal ();
    ~RawTerminal ();
    RawTerminal (const RawTerminal &) = delete;
    RawTerminal &operator= (const RawTerminal &) = delete;

private:
    bool    ok_ = false;
    termios saved_ {};
};

// The terminal's size, if it has one; w and h are left alone if not.
void terminal_size (std::uint16_t &w, std::uint16_t &h);

// Write everything, retrying after interruptions.
void write_all (int fd, const std::uint8_t *p, std::size_t n);

// Is the locale's character set UTF-8?
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
