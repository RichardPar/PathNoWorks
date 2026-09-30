// Unit tests for pnw::Lat, driven with frames RSX-11M-PLUS sent.  No
// framework: each CHECK prints what failed, and the exit status says
// whether anything did.

#include "pnw/lat.h"

#include <iostream>
#include <string>
#include <vector>

using pnw::Bytes;
using pnw::ByteView;
using pnw::Lat;

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

// BAJI's service announcement, as captured.
const Bytes baji {
    0x28, 0x00, 0x05, 0x05, 0x05, 0x01, 0x2f, 0x00, 0x52, 0x02, 0x14, 0x00,
    0x01, 0x01, 0x04, 'B', 'A', 'J', 'I', 0x00, 0x01, 0x01, 0x04, 'B', 'A',
    'J', 'I', 0x00, 0x01, 0x01, 0x00, 0x00 };

struct Rig {
    std::vector<Bytes> sent;
    std::string        screen;
    Lat::Clock::time_point now = Lat::Clock::now ();
    Lat                lat;

    Rig ()
        : lat ({ "RAXDA", "RAXDA", "PNW", "PNWLAT", 594 },
               [this] (ByteView m) { sent.emplace_back (m.begin (), m.end ()); },
               [this] (ByteView b) { screen.append (b.begin (), b.end ()); })
    {}

    std::uint16_t our_id () const { return sent.at (0)[4] | (sent.at (0)[5] << 8); }

    // A host message addressed to us.
    Bytes host (std::uint8_t cmd, std::uint8_t seq, std::uint8_t ack,
                std::vector<Bytes> slots = {}) const
    {
        std::uint16_t id = our_id ();
        Bytes m { cmd, static_cast<std::uint8_t> (slots.size ()),
                  static_cast<std::uint8_t> (id), static_cast<std::uint8_t> (id >> 8),
                  0x01, 0x00, seq, ack };
        for (const Bytes &s : slots) {
            m.insert (m.end (), s.begin (), s.end ());
            if (m.size () % 2) m.push_back (0);
        }
        return m;
    }

    void step (std::chrono::milliseconds d)
    {
        now += d;
        lat.tick (now);
    }

    // Start, and the host's answer: the circuit and session up.
    void up ()
    {
        lat.start (now);
        Bytes ack { 0x04, 0x00, 0, 0, 0x01, 0x00, 0x00, 0x00, 0x52, 0x02 };
        ack[2] = static_cast<std::uint8_t> (our_id ());
        ack[3] = static_cast<std::uint8_t> (our_id () >> 8);
        lat.from_host (ack, now);
        step (std::chrono::milliseconds (100));
        // The host's start slot: session 1, 2 credits.
        lat.from_host (host (0x01, 1, sent.back ()[6],
                             { Bytes { 0x01, 0x01, 0x06, 0x92, 0x01, 0xff, 0xff, 0, 0, 0 } }),
                       now);
    }
};

void announcements ()
{
    auto a = pnw::parse_announcement (baji);
    CHECK (a.has_value ());
    CHECK_EQ (a->node, std::string ("BAJI"));
    CHECK_EQ (a->mtu, 594);
    CHECK_EQ (a->multicast_timer, 20u);
    CHECK_EQ (a->services.size (), std::size_t (1));
    CHECK_EQ (a->services[0].name, std::string ("BAJI"));
    CHECK (!pnw::parse_announcement (Bytes { 0x06, 0 }));
}

void starting ()
{
    Rig r;
    r.lat.start (r.now);
    Bytes s = r.sent.at (0);
    CHECK_EQ (int (s[0]), 0x06);                    // Start
    CHECK_EQ (int (s[2] | s[3]), 0);                // host's id not known
    CHECK (r.our_id () != 0);
    r.up ();
    CHECK (r.lat.running ());
    // After the host's Start: our session start slot, 15 credits.
    bool start_slot = false;
    for (const Bytes &m : r.sent)
        if (m[0] == 0x02 && m[1] >= 1 && m[11] == 0x9f) start_slot = true;
    CHECK (start_slot);
}

void output_and_input ()
{
    Rig r;
    r.up ();
    r.step (std::chrono::milliseconds (100));
    // "\r\n>" in a data slot, response requested.
    r.lat.from_host (r.host (0x01, 2, r.sent.back ()[6],
                             { Bytes { 0x01, 0x01, 0x03, 0x00, 0x0d, 0x0a, '>' } }),
                     r.now);
    CHECK_EQ (r.screen, std::string ("\r\n>"));
    r.step (std::chrono::milliseconds (100));
    CHECK_EQ (int (r.sent.back ()[7]), 2);          // acknowledged
    // Typing goes as a data slot, using a credit.
    r.lat.from_terminal (Bytes { 'T', 'I', 'M', '\r' });
    r.step (std::chrono::milliseconds (100));
    Bytes m = r.sent.back ();
    CHECK_EQ (int (m[1]), 1);
    CHECK_EQ (std::string (m.begin () + 12, m.begin () + 16), std::string ("TIM\r"));
}

void no_acknowledgement_storm ()
{
    // A host message that asks for no response gets none: answering every
    // one of them made BAJI answer back, as fast as the wire would go.
    Rig r;
    r.up ();
    r.step (std::chrono::milliseconds (100));
    std::size_t before = r.sent.size ();
    for (std::uint8_t seq = 2; seq < 20; ++seq) {
        r.lat.from_host (r.host (0x00, seq, r.sent.back ()[6]), r.now);
        r.step (std::chrono::milliseconds (10));
    }
    CHECK_EQ (r.sent.size (), before);
    // And even asked, not faster than the circuit timer.
    r.lat.from_host (r.host (0x01, 20, r.sent.back ()[6]), r.now);
    r.lat.from_host (r.host (0x01, 21, r.sent.back ()[6]), r.now);
    CHECK (r.sent.size () <= before + 1);
}

void stopping ()
{
    Rig r;
    r.up ();
    r.lat.from_host (r.host (0x01, 2, 0, { Bytes { 0x01, 0x01, 0x00, 0xd7 } }), r.now);
    CHECK (r.lat.closed ());
    CHECK_EQ (r.lat.why (), std::string ("no such service"));

    Rig q;
    q.up ();
    q.lat.stop ();
    CHECK (q.lat.closed ());
    CHECK_EQ (int (q.sent.back ()[0]), 0x0a);       // circuit stop
}

}   // namespace

int main ()
{
    announcements ();
    starting ();
    output_and_input ();
    no_acknowledgement_storm ();
    stopping ();
    if (failures) {
        std::cerr << failures << " failed\n";
        return 1;
    }
    std::cout << "all passed\n";
    return 0;
}
