// Unit tests for pnw::Cterm, driven with the messages VMS sends.  No
// framework: each CHECK prints what failed, and the exit status says whether
// anything did.

#include "pnw/cterm.h"

#include <iostream>
#include <string>
#include <vector>

using pnw::Bytes;
using pnw::ByteView;
using pnw::Cterm;

namespace {

int failures = 0;

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #x "\n";        \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        auto a_ = (a);                                                       \
        auto b_ = (b);                                                       \
        if (!(a_ == b_)) {                                                   \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #a " is \""     \
                      << a_ << "\", expected \"" << b_ << "\"\n";            \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

Bytes bytes (const std::string &s) { return Bytes (s.begin (), s.end ()); }

// A Cterm with its output captured.
struct Rig {
    std::vector<Bytes> host;            // Foundation messages sent
    std::string        screen;
    Cterm              c;

    Rig ()
        : c ({}, [this] (ByteView m) { host.emplace_back (m.begin (), m.end ()); },
             [this] (ByteView b) { screen.append (b.begin (), b.end ()); })
    {}

    // Deliver CTERM messages as VMS does, in one Common Data message.
    void cterm (const Bytes &m)
    {
        Bytes f { 9, 0, static_cast<std::uint8_t> (m.size ()),
                  static_cast<std::uint8_t> (m.size () >> 8) };
        f.insert (f.end (), m.begin (), m.end ());
        c.from_host (f);
    }

    // The CTERM messages we sent, unwrapped.
    std::vector<Bytes> sent () const
    {
        std::vector<Bytes> out;
        for (const Bytes &f : host) {
            if (f.empty () || f[0] != 9) continue;
            std::size_t at = 2;
            while (at + 2 <= f.size ()) {
                std::size_t n = f[at] | (f[at + 1] << 8);
                at += 2;
                out.emplace_back (f.begin () + static_cast<std::ptrdiff_t> (at),
                                  f.begin () + static_cast<std::ptrdiff_t> (at + n));
                at += n;
            }
        }
        return out;
    }

    Bytes last () const { auto s = sent (); return s.empty () ? Bytes () : s.back (); }

    void type (const std::string &keys) { c.from_terminal (bytes (keys)); }
};

// DCL's Start Read, as VMS sent it: formatting, echo terminator, universal
// terminators, escape recognition; prompt "\r\n$ ".
Bytes dcl_read ()
{
    Bytes m { 2, 0x08, 0x90, 0x0a, 0x00, 0x01,      // flags, max 256
              0x04, 0x00, 0x00, 0x00,               // end of data, timeout
              0x04, 0x00, 0x00, 0x00, 0x04, 0x00,   // prompt, display, low water
              0x00 };                               // no terminator set
    Bytes p = bytes ("\r\n$ ");
    m.insert (m.end (), p.begin (), p.end ());
    return m;
}

// The typed data and terminator of a Read Data message.
std::string read_data (const Bytes &m) { return std::string (m.begin () + 8, m.end ()); }

void binding ()
{
    Rig r;
    r.c.from_host (Bytes { 1, 2, 4, 0, 7, 0, 0x10, 0 });
    CHECK_EQ (int (r.host.at (0)[0]), 4);           // Bind Accept
    r.c.from_host (Bytes { 5, 1, 0 });              // Enter Mode
    CHECK_EQ (int (r.host.back ()[0]), 8);          // No Mode
    r.cterm (Bytes { 1, 0, 1, 0, 0, 'V', 'M', 'S', ' ', ' ', ' ', ' ', ' ' });
    CHECK_EQ (int (r.last ()[0]), 1);               // our Initiate
    r.c.from_host (Bytes { 3, 3, 0 });
    CHECK (r.c.unbound ());
}

void a_command_line ()
{
    Rig r;
    r.cterm (dcl_read ());
    CHECK_EQ (r.screen, std::string ("\r\n$ "));
    r.type ("SHOW TIMX\x7f" "E\r");
    CHECK_EQ (r.screen, std::string ("\r\n$ SHOW TIMX\b \bE\r\n"));
    Bytes m = r.last ();
    CHECK_EQ (int (m[0]), 3);                       // Read Data
    CHECK_EQ (int (m[1] & 0x0f), 0);                // ended by a terminator
    CHECK_EQ (read_data (m), std::string ("SHOW TIME\r"));
    CHECK_EQ (int (m[6] | (m[7] << 8)), 9);         // termination position
}

void editing ()
{
    Rig r;
    r.cterm (dcl_read ());
    r.type ("GARBAGE\x15" "DIR\r");                  // ^U
    CHECK_EQ (read_data (r.last ()), std::string ("DIR\r"));
    r.cterm (dcl_read ());
    r.type ("ONE TWO\x17" "THREE\r");               // ^W
    CHECK_EQ (read_data (r.last ()), std::string ("ONE THREE\r"));
}

void escape_sequences ()
{
    Rig r;
    r.cterm (dcl_read ());
    r.type ("\x1b[A");                              // up arrow
    Bytes m = r.last ();
    CHECK_EQ (int (m[1] & 0x0f), 1);                // valid escape sequence
    CHECK_EQ (read_data (m), std::string ("\x1b[A"));
    r.cterm (dcl_read ());
    r.type ("\x9b" "B");                            // 8-bit CSI
    CHECK_EQ (read_data (r.last ()), std::string ("\x9b" "B"));
}

void out_of_band ()
{
    Rig r;
    // VMS: ^Y out of band, immediate clear with discard, echoed; ^C not.
    r.cterm (Bytes { 11, 0, 2, 2, 0x19, 0x3b, 0x29 });
    r.cterm (Bytes { 11, 0, 2, 2, 0x03, 0x3b, 0x00 });
    r.type ("\x19");
    CHECK (r.last () == (Bytes { 4, 1, 0x19 }));
    CHECK (r.screen.find ("Interrupt") != std::string::npos);
    // ^C that no program asked for goes as ^Y.
    r.type ("\x03");
    CHECK (r.last () == (Bytes { 4, 1, 0x19 }));
    // Made out of band itself, it goes as ^C.
    r.cterm (Bytes { 11, 0, 2, 2, 0x03, 0x3b, 0x29 });
    r.type ("\x03");
    CHECK (r.last () == (Bytes { 4, 1, 0x03 }));
}

void typeahead ()
{
    Rig r;
    r.type ("DIR\rSHOW");                           // before any read
    CHECK (r.sent ().empty ());
    r.cterm (dcl_read ());
    CHECK_EQ (read_data (r.last ()), std::string ("DIR\r"));
    CHECK ((r.last ()[1] & 0x10) != 0);             // more type-ahead
    r.cterm (dcl_read ());
    r.type ("\r");
    CHECK_EQ (read_data (r.last ()), std::string ("SHOW\r"));
}

void timed_read ()
{
    Rig r;
    // Zero timeout: take what has been typed, and no more.
    Bytes m = dcl_read ();
    m[2] |= 0x20;                                   // Q: timeout present
    r.type ("AB");
    r.cterm (m);
    CHECK_EQ (int (r.last ()[1] & 0x0f), 5);        // timeout
    CHECK_EQ (read_data (r.last ()), std::string ("AB"));
}

void writes ()
{
    Rig r;
    // One newline before, carriage return after: VMS carriage control.
    Bytes w { 7, 0x40, 0x02, 1, 0x0d };
    Bytes t = bytes ("hello");
    w.insert (w.end (), t.begin (), t.end ());
    r.cterm (w);
    CHECK_EQ (r.screen, std::string ("\nhello\r"));
    // Completion requested.
    r.cterm (Bytes { 7, 0x00, 0x04, 0, 0 });
    CHECK_EQ (int (r.last ()[0]), 8);
}

void characteristics ()
{
    Rig r;
    // Terminal type and width, as VMS asks for them.
    r.cterm (Bytes { 10, 0, 3, 1, 9, 1 });
    Bytes m = r.last ();
    CHECK_EQ (int (m[0]), 11);
    CHECK_EQ (std::string (m.begin () + 5, m.begin () + 10), std::string ("VT300"));
    CHECK_EQ (int (m[12] | (m[13] << 8)), 80);
    r.c.resize (132, 50);
    r.cterm (Bytes { 10, 0, 9, 1 });
    CHECK_EQ (int (r.last ()[4] | (r.last ()[5] << 8)), 132);
    // Echo off, then a read: nothing typed shows.
    r.cterm (Bytes { 11, 0, 5, 2, 0 });
    r.cterm (dcl_read ());
    r.screen.clear ();
    r.type ("secret");
    CHECK_EQ (r.screen, std::string (""));
}

}   // namespace

int main ()
{
    binding ();
    a_command_line ();
    editing ();
    escape_sequences ();
    out_of_band ();
    typeahead ();
    timed_read ();
    writes ();
    characteristics ();
    if (failures) {
        std::cerr << failures << " failed\n";
        return 1;
    }
    std::cout << "all passed\n";
    return 0;
}
