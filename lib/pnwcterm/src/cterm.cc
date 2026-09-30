// pnwcterm/cterm.cc -- the terminal end of a CTERM session.
//
// Written from the DNA Terminal Foundation Services (V2.4) and Network
// Command Terminal (V1.4) specifications.  Where those leave a detail open
// -- the width of Start Read's flags, what Read Data carries -- the choice
// is what VMS is known to accept.

#include "pnw/cterm.h"

#include <algorithm>
#include <cstring>

namespace pnw {

namespace {

std::uint16_t get16 (ByteView b, std::size_t at)
{
    return at + 1 < b.size ()
        ? static_cast<std::uint16_t> (b[at] | (b[at + 1] << 8)) : 0;
}

void put16 (Bytes &b, unsigned v)
{
    b.push_back (static_cast<std::uint8_t> (v));
    b.push_back (static_cast<std::uint8_t> (v >> 8));
}

Bytes boolean (bool v) { return Bytes { static_cast<std::uint8_t> (v ? 1 : 0) }; }
Bytes integer16 (unsigned v) { Bytes b; put16 (b, v); return b; }
Bytes string (const std::string &s)
{
    Bytes b { static_cast<std::uint8_t> (s.size ()) };
    b.insert (b.end (), s.begin (), s.end ());
    return b;
}

// The encoded size of a characteristic's value, from its type; nothing if
// the selector is unknown.
enum class Kind { boolean, integer, string, bitmap1, bitmap2, compound };
std::optional<Kind> kind_of (std::uint16_t sel)
{
    std::uint8_t id = static_cast<std::uint8_t> (sel);
    switch (sel >> 8) {
    case 0:     // physical
        if (id == 1 || id == 2 || id == 3 || id == 5) return Kind::integer;
        if (id == 9 || id == 10) return Kind::string;
        if (id >= 1 && id <= 12) return Kind::boolean;
        break;
    case 1:     // logical
        if (id == 2) return Kind::bitmap2;
        if (id == 3) return Kind::string;
        if (id >= 9 && id <= 17) return Kind::integer;
        if (id >= 1 && id <= 8) return Kind::boolean;
        break;
    case 2:     // handler
        if (id == 2) return Kind::compound;
        if (id == 8) return Kind::integer;
        if (id == 10) return Kind::bitmap1;
        if (id >= 1 && id <= 9) return Kind::boolean;
        break;
    }
    return std::nullopt;
}

// Character attribute bits: .FEE DIOO
constexpr std::uint8_t a_oob = 0x03, a_include = 0x04, a_discard = 0x08,
                       a_echo = 0x30, a_special = 0x40;

constexpr std::uint8_t CR = 0x0d, LF = 0x0a, ESC = 0x1b, DEL = 0x7f,
                       CSI8 = 0x9b, SS3_8 = 0x8f;

bool is_control (std::uint8_t c) { return c < 0x20 || c == DEL; }

}   // namespace

Cterm::Cterm (Terminal t, std::function<void (ByteView)> to_host,
              std::function<void (ByteView)> to_terminal)
    : term_ (std::move (t)), to_host_ (std::move (to_host)),
      to_terminal_ (std::move (to_terminal))
{
    // Physical.
    chars_[physical (1)] = integer16 (9600);            // input speed
    chars_[physical (2)] = integer16 (9600);            // output speed
    chars_[physical (3)] = integer16 (8);               // character size
    chars_[physical (4)] = boolean (false);             // parity enable
    chars_[physical (5)] = integer16 (1);               // parity type
    chars_[physical (6)] = boolean (false);             // modem present
    chars_[physical (7)] = boolean (false);             // auto baud
    chars_[physical (8)] = boolean (false);             // management guaranteed
    chars_[physical (9)] = string ("");                 // switch character 1
    chars_[physical (10)] = string ("");                // switch character 2
    chars_[physical (11)] = boolean (true);             // eight bit
    chars_[physical (12)] = boolean (false);            // management enabled
    // Logical.
    chars_[logical (1)] = boolean (false);              // mode writing allowed
    chars_[logical (2)] = integer16 (3);                // terminal attributes
    chars_[logical (3)] = string (term_.type);
    chars_[logical (4)] = boolean (true);               // output flow control
    chars_[logical (5)] = boolean (false);              // output page stop
    chars_[logical (6)] = boolean (false);              // flow char pass-through
    chars_[logical (7)] = boolean (true);               // input flow control
    chars_[logical (8)] = boolean (true);               // loss notification
    chars_[logical (9)] = integer16 (term_.width);
    chars_[logical (10)] = integer16 (term_.height);
    for (std::uint8_t id = 11; id <= 14; ++id)          // stop, fills, wrap
        chars_[logical (id)] = integer16 (id == 14 ? 1 : 0);
    for (std::uint8_t id = 15; id <= 17; ++id)          // tabs, form feed
        chars_[logical (id)] = integer16 (1);
    // Handler.
    chars_[handler (1)] = boolean (false);              // ignore input
    chars_[handler (3)] = boolean (false);              // ^O pass-through
    chars_[handler (4)] = boolean (false);              // raise input
    chars_[handler (5)] = boolean (true);               // normal echo
    chars_[handler (6)] = boolean (true);               // input escapes
    chars_[handler (7)] = boolean (true);               // output escapes
    chars_[handler (8)] = integer16 (0);                // input count state
    chars_[handler (9)] = boolean (false);              // auto prompt
    chars_[handler (10)] = Bytes { 0 };                 // error processing

    // Control characters echo in standard form ("^X") with their special
    // functions enabled; everything else echoes as itself.
    for (unsigned c = 0; c < 256; ++c)
        char_attr_[c] = is_control (static_cast<std::uint8_t> (c))
            ? static_cast<std::uint8_t> (a_special | 0x20) : 0x10;
}

// ------------------------------------------------------------- plumbing

void Cterm::send_cterm (const Bytes &m)
{
    Bytes f { f_common_data, 0 };
    put16 (f, static_cast<unsigned> (m.size ()));
    f.insert (f.end (), m.begin (), m.end ());
    to_host_ (f);
}

void Cterm::write_terminal (ByteView b)
{
    if (b.empty ()) return;
    to_terminal_ (b);
    last_out_ = b.back ();
}

bool Cterm::flag (std::uint16_t sel) const
{
    auto it = chars_.find (sel);
    return it != chars_.end () && !it->second.empty () && (it->second[0] & 1);
}

std::uint16_t Cterm::integer (std::uint16_t sel) const
{
    auto it = chars_.find (sel);
    return it == chars_.end () ? 0 : get16 (it->second, 0);
}

std::uint8_t Cterm::attributes (std::uint8_t c) const { return char_attr_[c]; }

void Cterm::resize (std::uint16_t width, std::uint16_t height)
{
    term_.width = width;
    term_.height = height;
    chars_[logical (9)] = integer16 (width);
    chars_[logical (10)] = integer16 (height);
}

// ------------------------------------------------------------ foundation

void Cterm::from_host (ByteView msg)
{
    if (msg.empty ()) return;
    switch (msg[0]) {
    case f_bind: {
        // We are the server; accept.  Version 2.4.0, and we say VMS, as
        // dnlogin does: VMS is known to be content with that.
        Bytes a { f_bind_accept, 2, 4, 0, 7, 0 };
        const char rev[] = "PNWSTHST";
        a.insert (a.end (), rev, rev + 8);
        a.insert (a.end (), { 0, 0, 0 });              // ID, OPTIONS
        to_host_ (a);
        break;
    }
    case f_unbind:
        unbound_ = true;
        unbind_reason_ = get16 (msg, 1);
        break;
    case f_enter_mode:
        // Only command mode, which needs no entering.
        to_host_ (Bytes { f_no_mode });
        break;
    case f_exit_mode:
        to_host_ (Bytes { f_no_mode });
        break;
    case f_common_data:
    case f_mode_data: {
        std::size_t at = 2;
        while (at + 2 <= msg.size ()) {
            std::size_t len = get16 (msg, at);
            at += 2;
            if (at + len > msg.size ()) break;
            if (msg[0] == f_common_data) cterm_message (msg.subspan (at, len));
            at += len;
        }
        break;
    }
    default:
        break;
    }
}

// ----------------------------------------------------------------- CTERM

void Cterm::cterm_message (ByteView m)
{
    if (m.empty ()) return;
    switch (m[0]) {
    case m_initiate:
        if (!initiated_) {
            initiated_ = true;
            Bytes i { m_initiate, 0, 1, 0, 0 };
            const char rev[] = "PNW     ";
            i.insert (i.end (), rev, rev + 8);
            i.insert (i.end (), { 1, 2 }); put16 (i, 512);     // max message
            i.insert (i.end (), { 2, 2 }); put16 (i, 1024);    // input buffer
            i.insert (i.end (), { 3, 2, 0xfe, 0x7f });         // messages 1-14
            send_cterm (i);
        }
        break;
    case m_start_read:     start_read (m); break;
    case m_unread:
        if (read_.active
            && (!(m.size () > 1 && (m[1] & 1))
                || (read_.input.empty () && typeahead_.empty ())))
            complete (c_unread);
        break;
    case m_clear_input:
        typeahead_.clear ();
        read_.input.clear ();
        break;
    case m_write:          do_write (m); break;
    case m_read_char:      read_characteristics (m); break;
    case m_characteristics: set_characteristics (m); break;
    case m_check_input: {
        Bytes r { m_input_count, 0 };
        put16 (r, static_cast<unsigned> (typeahead_.size () + read_.input.size ()));
        send_cterm (r);
        break;
    }
    default:
        // Anything else -- including VMS's reserved 15 to 17 -- is ignored
        // rather than taken as a protocol error; a session that carries on
        // is more use than one torn down.
        break;
    }
}

void Cterm::start_read (ByteView m)
{
    // FLAGS is three bytes, whatever the heading says: .... ..EE ZZQT NDDD
    // IIKV FCUU needs 22 bits.
    if (m.size () < 17) return;
    std::uint32_t fl = m[1] | (m[2] << 8) | (static_cast<std::uint32_t> (m[3]) << 16);
    std::size_t max = get16 (m, 4);
    std::size_t eod = get16 (m, 6);
    std::uint16_t timeout = get16 (m, 8);
    std::size_t eop = get16 (m, 10);
    std::size_t sod = get16 (m, 12);
    std::size_t at = 16;                            // after LOW-WATER
    std::size_t tlen = m[at++];
    ByteView tset = m.subspan (at, std::min (tlen, m.size () - at));
    at += tset.size ();
    ByteView data = at <= m.size () ? m.subspan (at) : ByteView ();

    Read r;
    r.active = true;
    unsigned uu = fl & 3, dd = (fl >> 8) & 7, ii = (fl >> 6) & 3,
             zz = (fl >> 14) & 3, ee = (fl >> 16) & 3;
    bool clear = fl & 0x4, format = fl & 0x8, noecho = fl & 0x800,
         termecho = fl & 0x1000, timed = fl & 0x2000;
    r.underflow = uu;
    r.edit = dd;
    r.upcase = ii == 2 || (ii == 0 && flag (handler (4)));
    r.echo = !noecho && flag (handler (5));
    r.echo_term = termecho;
    r.escapes = ee == 2 || (ee == 0 && flag (handler (6)));
    if (timed) r.timeout = timeout;

    if (zz == 1) {
        for (std::size_t i = 0; i < tset.size (); ++i)
            for (unsigned b = 0; b < 8; ++b)
                if (tset[i] & (1u << b)) r.terms.set (i * 8 + b);
        last_terms_ = r.terms;
    } else if (zz == 2) {
        // Universal: every control character but ^R, ^U, ^W, BS and HT.
        // Not DEL, which is how the user deletes a character: VMS asks for
        // this set for DCL's command line.
        for (unsigned c = 0; c < 32; ++c) r.terms.set (c);
        for (unsigned c : { 0x12u, 0x15u, 0x17u, 0x08u, 0x09u }) r.terms.reset (c);
    } else {
        r.terms = last_terms_;
    }

    eop = std::min (eop, data.size ());
    eod = std::min (eod, data.size ());
    r.prompt.assign (data.begin (), data.begin () + static_cast<std::ptrdiff_t> (eop));
    r.input.assign (data.begin () + static_cast<std::ptrdiff_t> (eop),
                    data.begin () + static_cast<std::ptrdiff_t> (std::max (eop, eod)));
    r.max = max > eop ? max - eop : 0;

    if (clear) typeahead_.clear ();
    if (format && last_out_ == CR) {
        write_terminal (Bytes { LF });
        if (!r.input.empty () && r.input[0] == LF) r.input.erase (r.input.begin ());
    }
    read_ = std::move (r);
    sod = std::min (sod, data.size ());
    write_terminal (data.subspan (sod));            // prompt and preload

    if (read_.timeout && *read_.timeout)
        read_.due = Clock::now () + std::chrono::seconds (*read_.timeout);
    feed_typeahead ();
    if (read_.active && read_.timeout && *read_.timeout == 0)
        complete (c_timeout);
}

void Cterm::feed_typeahead ()
{
    // Characters typed before the read, taken in order until the read ends.
    bool had = !typeahead_.empty ();
    feeding_ = true;
    while (read_.active && !typeahead_.empty ()) {
        std::uint8_t c = typeahead_.front ();
        typeahead_.erase (typeahead_.begin ());
        read_char (c);
    }
    feeding_ = false;
    if (had && typeahead_.empty () && integer (handler (8)))
        send_cterm (Bytes { m_input_state, 0 });
}

void Cterm::complete (std::uint8_t code, ByteView tail)
{
    // The data is what was typed, then what ended it; the termination
    // position is where the typed part stops.
    Bytes d { m_read_data,
              static_cast<std::uint8_t> (code | (typeahead_nonempty () ? 0x10 : 0)) };
    put16 (d, 0);                                   // low water
    d.push_back (0);                                // vertical position
    d.push_back (0);                                // horizontal position
    put16 (d, static_cast<unsigned> (read_.input.size ()));
    d.insert (d.end (), read_.input.begin (), read_.input.end ());
    d.insert (d.end (), tail.begin (), tail.end ());
    read_.active = false;
    read_.esc.clear ();
    send_cterm (d);
}

// ----------------------------------------------------------------- input

void Cterm::from_terminal (ByteView keys)
{
    for (std::uint8_t c : keys) key (c);
}

void Cterm::key (std::uint8_t c)
{
    if (flag (handler (1))) return;                 // ignore input

    // VMS's terminal driver takes a ^C that no program has asked for as ^Y,
    // and a VMS host leaves that to the terminal end: it makes ^Y out of
    // band and ^C not.
    constexpr std::uint8_t CTRL_C = 0x03, CTRL_Y = 0x19;
    if (c == CTRL_C && !(attributes (CTRL_C) & a_oob)
        && (attributes (CTRL_Y) & a_oob))
        c = CTRL_Y;

    std::uint8_t at = attributes (c);
    if (at & a_oob) {
        // Out of band: straight to the host, whatever else is going on.
        if ((at & a_oob) != 3) {                    // clear: drop the input
            typeahead_.clear ();
            if (read_.active) read_.input.clear ();
        }
        if (at & a_discard) discard_ = true;
        send_cterm (Bytes { m_oob, static_cast<std::uint8_t> (at & a_discard ? 1 : 0), c });
        // Echoed as a VMS terminal does, if its attributes say to echo.
        if (at & a_echo) {
            if (c == CTRL_Y)
                write_terminal (std::string ("\x1b[7m Interrupt \x1b[0m\r\n"));
            else if (c == CTRL_C)
                write_terminal (std::string ("\x1b[7m Cancel \x1b[0m\r\n"));
        }
        if ((at & a_oob) != 3 || !(at & a_include)) return;
    }

    // ^O: output discard on and off, unless the host wants it as data.
    if (c == 0x0f && !flag (handler (3)) && (at & a_special)) {
        discard_ = !discard_;
        send_cterm (Bytes { m_discard_state, static_cast<std::uint8_t> (discard_ ? 0 : 1) });
        if (discard_) write_terminal (std::string ("^O\r\n"));
        return;
    }

    if (!read_.active) {
        bool was_empty = typeahead_.empty ();
        typeahead_.push_back (c);
        if (was_empty && integer (handler (8)))
            send_cterm (Bytes { m_input_state, 1 });
        return;
    }
    read_char (c);
}

void Cterm::echo (std::uint8_t c)
{
    if (!read_.echo) return;
    if (!is_control (c)) { write_terminal (Bytes { c }); return; }
    unsigned ee = (attributes (c) & a_echo) >> 4;
    auto standard = [&] {
        if (c == CR || c == LF) write_terminal (std::string ("\r\n"));
        else if (c == ESC)      write_terminal (std::string ("$"));
        else write_terminal (Bytes { '^', static_cast<std::uint8_t> ((c + 64) & 0x7f) });
    };
    switch (ee) {
    case 1: write_terminal (Bytes { c }); break;
    case 2: standard (); break;
    case 3: standard (); write_terminal (Bytes { c }); break;
    default: break;
    }
}

void Cterm::rub_out (std::size_t n)
{
    if (!read_.echo) return;
    for (std::size_t i = 0; i < n; ++i) write_terminal (std::string ("\b \b"));
}

void Cterm::redisplay ()
{
    write_terminal (std::string ("\r\n"));
    write_terminal (read_.prompt);
    if (read_.echo) write_terminal (read_.input);
}

void Cterm::read_char (std::uint8_t c)
{
    // The timeout is between characters: each one starts it again.
    if (read_.timeout && *read_.timeout)
        read_.due = Clock::now () + std::chrono::seconds (*read_.timeout);
    if (read_.upcase && c >= 'a' && c <= 'z') c = static_cast<std::uint8_t> (c - 32);

    // An escape sequence being typed: collect it to its final character.
    if (!read_.esc.empty ()) {
        read_.esc.push_back (c);
        std::uint8_t first = read_.esc[0];
        bool csi = first == CSI8 || (read_.esc.size () > 1 && read_.esc[1] == '['
                                     && first == ESC);
        bool ss3 = first == SS3_8 || (read_.esc.size () > 1 && read_.esc[1] == 'O'
                                      && first == ESC);
        std::size_t n = read_.esc.size ();
        if (first == ESC && n == 2 && (c == '[' || c == 'O')) return;
        if (csi) {
            if (c >= 0x20 && c <= 0x3f) return;     // parameters, intermediates
            Bytes seq = read_.esc;
            complete (c >= 0x40 && c <= 0x7e ? c_escape : c_bad_escape, seq);
            return;
        }
        if (ss3) {
            Bytes seq = read_.esc;
            complete (c >= 0x20 && c <= 0x7e ? c_escape : c_bad_escape, seq);
            return;
        }
        if (c >= 0x20 && c <= 0x2f) return;         // ESC intermediates
        Bytes seq = read_.esc;
        complete (c >= 0x30 && c <= 0x7e ? c_escape : c_bad_escape, seq);
        return;
    }
    if (read_.escapes && (c == ESC || c == CSI8 || c == SS3_8)) {
        read_.esc.push_back (c);
        return;
    }

    // A terminator ends the read.
    if (read_.terms[c]) {
        if (read_.echo_term) echo (c);
        complete (c_terminator, Bytes { c });
        return;
    }

    // Editing, unless the read turns it off.
    bool special = (attributes (c) & a_special) && read_.edit < 2;
    if (special && (c == DEL || c == 0x08)) {
        if (read_.input.empty ()) {
            if (read_.underflow == 1) write_terminal (Bytes { 0x07 });
            else if (read_.underflow == 2) complete (c_underflow);
            return;
        }
        std::uint8_t gone = read_.input.back ();
        read_.input.pop_back ();
        rub_out (is_control (gone) ? 2 : 1);
        return;
    }
    if (special && read_.edit != 1 && (c == 0x15 || c == 0x18)) {   // ^U, ^X
        std::size_t cols = 0;
        for (std::uint8_t k : read_.input) cols += is_control (k) ? 2 : 1;
        read_.input.clear ();
        if (c == 0x18) typeahead_.clear ();
        rub_out (cols);
        return;
    }
    if (special && read_.edit != 1 && c == 0x12) {                  // ^R
        redisplay ();
        return;
    }
    if (special && c == 0x17) {                                     // ^W
        std::size_t cols = 0;
        while (!read_.input.empty () && read_.input.back () == ' ') {
            read_.input.pop_back (); ++cols;
        }
        while (!read_.input.empty () && read_.input.back () != ' ') {
            cols += is_control (read_.input.back ()) ? 2 : 1;
            read_.input.pop_back ();
        }
        rub_out (cols);
        return;
    }

    read_.input.push_back (c);
    echo (c);
    if (read_.max && read_.input.size () >= read_.max) complete (c_full);
}

// ---------------------------------------------------------------- output

void Cterm::do_write (ByteView m)
{
    if (m.size () < 5) return;
    unsigned fl = get16 (m, 1);
    std::uint8_t pre = m[3], post = m[4];
    ByteView data = m.subspan (5);
    unsigned pp = (fl >> 6) & 3, qq = (fl >> 8) & 3;
    if (fl & 0x08) discard_ = false;                // D: stop discarding
    bool lost = false;

    auto out = [&] (ByteView b) {
        if (discard_) { lost = true; return; }
        write_terminal (b);
    };
    if (pp == 1) for (unsigned i = 0; i < pre; ++i) out (Bytes { LF });
    if (pp == 2) out (Bytes { pre });
    if (skip_lf_ && !data.empty () && data[0] == LF) data = data.subspan (1);
    skip_lf_ = false;
    out (data);
    if (qq == 1) for (unsigned i = 0; i < post; ++i) out (Bytes { LF });
    if (qq == 2) out (Bytes { post });
    if (fl & 0x04) {                                // L: newline at the end
        out (Bytes { LF });
        skip_lf_ = true;
    }
    if ((fl & 3) == 3 && read_.active) redisplay ();
    if (fl & 0x400) {                               // S: completion wanted
        Bytes c { m_write_complete, static_cast<std::uint8_t> (lost ? 1 : 0) };
        put16 (c, 0);
        put16 (c, 0);
        send_cterm (c);
    }
}

// ------------------------------------------------------- characteristics

void Cterm::read_characteristics (ByteView m)
{
    Bytes r { m_characteristics, 0 };
    std::size_t at = 2;
    while (at + 2 <= m.size ()) {
        std::uint16_t sel = get16 (m, at);
        at += 2;
        put16 (r, sel);
        if (sel == handler (2)) {
            // Character attributes: the character asked about follows.
            std::uint8_t c = at < m.size () ? m[at++] : 0;
            r.insert (r.end (), { c, 0xff, char_attr_[c] });
            continue;
        }
        auto it = chars_.find (sel);
        if (it != chars_.end ()) {
            r.insert (r.end (), it->second.begin (), it->second.end ());
        } else if (auto k = kind_of (sel)) {
            // Known but unset: a zero of the right shape.
            switch (*k) {
            case Kind::integer: case Kind::bitmap2: put16 (r, 0); break;
            case Kind::string: r.push_back (0); break;
            default: r.push_back (0); break;
            }
        } else {
            r.resize (r.size () - 2);               // not a characteristic
        }
    }
    send_cterm (r);
}

void Cterm::set_characteristics (ByteView m)
{
    std::size_t at = 2;
    while (at + 2 <= m.size ()) {
        std::uint16_t sel = get16 (m, at);
        at += 2;
        auto k = kind_of (sel);
        if (!k) return;                             // cannot tell its size
        std::size_t len = 0;
        switch (*k) {
        case Kind::boolean: case Kind::bitmap1: len = 1; break;
        case Kind::integer: case Kind::bitmap2: len = 2; break;
        case Kind::compound: len = 3; break;
        case Kind::string: len = at < m.size () ? 1u + m[at] : 1; break;
        }
        if (at + len > m.size ()) return;
        ByteView v = m.subspan (at, len);
        at += len;
        if (*k == Kind::compound) {
            std::uint8_t c = v[0], mask = v[1], val = v[2];
            char_attr_[c] = static_cast<std::uint8_t> ((char_attr_[c] & ~mask)
                                                       | (val & mask));
            continue;
        }
        chars_[sel] = Bytes (v.begin (), v.end ());
    }
}

// --------------------------------------------------------------- timeouts

std::optional<Cterm::Clock::time_point> Cterm::deadline () const
{
    return read_.active ? read_.due : std::nullopt;
}

void Cterm::tick (Clock::time_point now)
{
    if (read_.active && read_.due && now >= *read_.due) complete (c_timeout);
}

}   // namespace pnw
