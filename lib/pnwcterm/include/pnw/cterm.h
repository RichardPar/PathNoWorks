// pnw/cterm.h -- the terminal end of a CTERM session.
//
// CTERM (Network Command Terminal, DNA Command Terminal V1.4, over Terminal
// Foundation Services V2.5) is how VMS and RSX give a remote terminal a
// command line: SET HOST.  The host (VMS) drives it.  It asks this end,
// the server, to read a line -- with a prompt, a set of terminator
// characters and echo and editing rules -- and this end collects the line
// locally, echoing and editing, and sends it back when a terminator is
// typed.  Output is sent to be written as it is.
//
// Cterm is the protocol alone: messages from the host and keys from the
// user go in, bytes for the terminal and messages for the host come out,
// through the two callbacks.  Terminal emulation is not its business:
// escape sequences, Sixel graphics and 8-bit controls pass through
// untouched both ways, so the local terminal emulator is the terminal VMS
// talks to.

#ifndef PNW_CTERM_H
#define PNW_CTERM_H

#include "decnet/common/types.h"

#include <bitset>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace pnw {

using decnet::Bytes;
using decnet::ByteView;

class Cterm {
public:
    using Clock = std::chrono::steady_clock;

    struct Terminal {
        std::string   type = "VT300";   // TERMINAL-TYPE as the host sees it
        std::uint16_t width = 80;
        std::uint16_t height = 24;
    };

    // to_host gets whole Foundation messages, one session message each;
    // to_terminal gets bytes to write to the terminal.
    Cterm (Terminal t,
           std::function<void (ByteView)> to_host,
           std::function<void (ByteView)> to_terminal);

    // One session message from the host.
    void from_host (ByteView msg);

    // Keys typed by the user.
    void from_terminal (ByteView keys);

    // The terminal was resized.  The host reads the new size the next time
    // it asks.
    void resize (std::uint16_t width, std::uint16_t height);

    // Read timeouts: when the next one falls due, and run any that have.
    std::optional<Clock::time_point> deadline () const;
    void tick (Clock::time_point now);

    // The host has unbound: the session is over.
    bool unbound () const noexcept { return unbound_; }
    unsigned unbind_reason () const noexcept { return unbind_reason_; }

    // Foundation message types.
    enum : std::uint8_t {
        f_bind = 1, f_unbind = 3, f_bind_accept = 4, f_enter_mode = 5,
        f_exit_mode = 6, f_confirm_mode = 7, f_no_mode = 8,
        f_common_data = 9, f_mode_data = 10
    };
    // CTERM message types.
    enum : std::uint8_t {
        m_initiate = 1, m_start_read = 2, m_read_data = 3, m_oob = 4,
        m_unread = 5, m_clear_input = 6, m_write = 7, m_write_complete = 8,
        m_discard_state = 9, m_read_char = 10, m_characteristics = 11,
        m_check_input = 12, m_input_count = 13, m_input_state = 14
    };
    // Read completion codes.
    enum : std::uint8_t {
        c_terminator = 0, c_escape = 1, c_bad_escape = 2, c_oob = 3,
        c_full = 4, c_timeout = 5, c_unread = 6, c_underflow = 7
    };
    // Characteristic selectors: identifier in the low byte, class high.
    static constexpr std::uint16_t physical (std::uint8_t id) { return id; }
    static constexpr std::uint16_t logical (std::uint8_t id)
    { return static_cast<std::uint16_t> (0x100 | id); }
    static constexpr std::uint16_t handler (std::uint8_t id)
    { return static_cast<std::uint16_t> (0x200 | id); }

private:
    void cterm_message (ByteView m);
    void send_cterm (const Bytes &m);
    void write_terminal (ByteView b);
    void write_terminal (const std::string &s)
    {
        write_terminal (ByteView (reinterpret_cast<const std::uint8_t *> (s.data ()),
                                  s.size ()));
    }

    void start_read (ByteView m);
    void do_write (ByteView m);
    void read_characteristics (ByteView m);
    void set_characteristics (ByteView m);

    // One typed character, or a pending one from type-ahead.
    void key (std::uint8_t c);
    void read_char (std::uint8_t c);
    // End the read, sending the input and then tail: the terminator or
    // escape sequence that ended it, if any.
    void complete (std::uint8_t code, ByteView tail = {});
    void echo (std::uint8_t c);
    void rub_out (std::size_t n);
    void redisplay ();
    bool typeahead_nonempty () const { return !typeahead_.empty (); }
    void feed_typeahead ();

    // Characteristic helpers.
    bool flag (std::uint16_t sel) const;
    std::uint16_t integer (std::uint16_t sel) const;
    std::uint8_t attributes (std::uint8_t c) const;

    Terminal                            term_;
    std::function<void (ByteView)>      to_host_, to_terminal_;
    bool                                unbound_ = false;
    unsigned                            unbind_reason_ = 0;
    bool                                initiated_ = false;

    // Characteristics, as their encoded values.
    std::map<std::uint16_t, Bytes>      chars_;
    std::uint8_t                        char_attr_[256];

    // The read in progress.
    struct Read {
        bool          active = false;
        std::size_t   max = 0;          // characters of input allowed
        Bytes         prompt;
        Bytes         input;
        std::bitset<256> terms;
        bool          echo = true;
        bool          echo_term = false;
        unsigned      edit = 0;         // DDD: 1 no ^U ^R, 2 none, 3 all data
        bool          upcase = false;
        bool          escapes = false;
        unsigned      underflow = 0;
        std::optional<unsigned> timeout;
        std::optional<Clock::time_point> due;
        Bytes         esc;              // an escape sequence being typed
    } read_;
    std::bitset<256>                    last_terms_;
    Bytes                               typeahead_;
    bool                                feeding_ = false;

    bool          discard_ = false;     // output discarded (^O)
    std::uint8_t  last_out_ = 0;        // for the formatting flags
    bool          skip_lf_ = false;
};

}   // namespace pnw

#endif  // PNW_CTERM_H
