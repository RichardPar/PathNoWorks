// pnwlat/lat.cc -- LAT, the terminal end of one session.

#include "pnw/lat.h"

#include <algorithm>

namespace pnw {

namespace {

constexpr std::uint8_t RESPONSE_REQUESTED = 0x01;

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

void put_ascic (Bytes &b, const std::string &s)
{
    b.push_back (static_cast<std::uint8_t> (std::min<std::size_t> (s.size (), 255)));
    b.insert (b.end (), s.begin (), s.begin () + static_cast<std::ptrdiff_t> (
                                           std::min<std::size_t> (s.size (), 255)));
}

// A counted string at *at, which moves past it; nothing if it runs out.
std::optional<std::string> get_ascic (ByteView b, std::size_t &at)
{
    if (at >= b.size ()) return std::nullopt;
    std::size_t n = b[at++];
    if (at + n > b.size ()) return std::nullopt;
    std::string s (b.begin () + static_cast<std::ptrdiff_t> (at),
                   b.begin () + static_cast<std::ptrdiff_t> (at + n));
    at += n;
    return s;
}

}   // namespace

// --------------------------------------------------------- announcements

std::optional<LatAnnouncement> parse_announcement (ByteView m)
{
    // cmd, circuit timer, high and low protocol version, LAT version and
    // ECO, incarnation, change flags, MTU (2), multicast timer, node status,
    // group mask length and mask; then node name and description, and the
    // services: count, then rating, name and description for each.
    if (m.size () < 14 || m[0] != Lat::c_announce) return std::nullopt;
    LatAnnouncement a;
    a.mtu = get16 (m, 8);
    a.multicast_timer = m[10];
    std::size_t at = 12;
    at += 1 + m[at];                                // group mask
    auto node = get_ascic (m, at);
    auto desc = get_ascic (m, at);
    if (!node || !desc || at >= m.size ()) return std::nullopt;
    a.node = *node;
    a.description = *desc;
    unsigned count = m[at++];
    for (unsigned i = 0; i < count && at < m.size (); ++i) {
        LatAnnouncement::Service s;
        s.rating = m[at++];
        auto name = get_ascic (m, at);
        auto sdesc = get_ascic (m, at);
        if (!name || !sdesc) break;
        s.name = *name;
        s.description = *sdesc;
        a.services.push_back (std::move (s));
    }
    return a;
}

std::string lat_reason (unsigned r)
{
    static const char *const text[] = {
        "unknown reason", "user requested disconnect",
        "system shutdown in progress", "invalid slot received",
        "invalid service class", "insufficient resources", "service in use",
        "no such service", "service is disabled",
        "service is not offered by the requested port", "port name is unknown",
        "invalid password", "entry is not in the queue",
        "immediate access rejected", "access denied",
        "corrupted solicit request"
    };
    return r < std::size (text) ? text[r] : "reason " + std::to_string (r);
}

// ----------------------------------------------------------------- Lat

Lat::Lat (Target t, std::function<void (ByteView)> send,
          std::function<void (ByteView)> to_terminal)
    : t_ (std::move (t)), send_ (std::move (send)),
      to_terminal_ (std::move (to_terminal))
{
    // Any non-zero circuit id; distinct runs get distinct ones.
    local_id_ = static_cast<std::uint16_t> (
        (Clock::now ().time_since_epoch ().count () & 0x7ffe) + 1);
    max_msg_ = t_.mtu;
}

void Lat::start (Clock::time_point now)
{
    // The Start message: our circuit parameters and names.
    Bytes m { c_start, 0 };
    put16 (m, 0);                                   // host's circuit: not known
    put16 (m, local_id_);
    m.push_back (++sent_seq_);                      // sequence 0
    m.push_back (0);                                // ack
    put16 (m, t_.mtu);
    m.insert (m.end (), { 5, 2,                     // LAT 5.2
                          254, 0,                   // sessions, extra buffers
                          8,                        // circuit timer, 10 ms units
                          20 });                    // keepalive, seconds
    put16 (m, 0);                                   // facility
    m.insert (m.end (), { 3, 3 });                  // product type and version
    put_ascic (m, t_.node);
    put_ascic (m, t_.local_node);
    put_ascic (m, "PathNoWorks");
    state_ = State::starting;
    last_data_ = m;
    outstanding_ = true;
    last_sent_ = last_heard_ = now;
    send_ (m);
}

void Lat::close (const std::string &why)
{
    if (state_ == State::closed) return;
    state_ = State::closed;
    why_ = why;
}

// ------------------------------------------------------------- receiving

void Lat::from_host (ByteView m, Clock::time_point now)
{
    if (m.size () < 8 || state_ == State::closed) return;
    std::uint8_t cmd = m[0] & ~RESPONSE_REQUESTED;
    std::uint8_t nslots = m[1];
    std::uint16_t dst = get16 (m, 2), src = get16 (m, 4);
    std::uint8_t seq = m[6], ack = m[7];
    if (dst != local_id_ && cmd != c_start_ack) return;     // not ours
    last_heard_ = now;

    if (cmd == c_stop_from_host) {
        close ("the host ended the circuit ("
               + std::to_string (m.size () > 8 ? m[8] : 0) + ")");
        return;
    }

    if (cmd == c_start_ack) {
        if (state_ != State::starting) { ack_due_ = true; tick (now); return; }
        remote_id_ = src;
        recv_seq_ = seq;
        have_recv_ = true;
        outstanding_ = false;
        retries_ = 0;
        if (m.size () >= 10) max_msg_ = std::min<std::uint16_t> (max_msg_, get16 (m, 8));
        // Circuit up: start the session.  Service class 1 (interactive
        // terminal), attention and data slot sizes, the service, and our
        // port name.  15 credits for the host to send us data.
        Bytes s { 1, 1, 0xfe };
        put_ascic (s, t_.service);
        s.push_back (0);                            // source service
        s.insert (s.end (), { 1, 2, 4, 0 });        // parameter 1
        s.push_back (5);                            // parameter 5: our port
        put_ascic (s, t_.port);
        given_ = 15;
        queue_slot (0x9f, s);
        state_ = State::running;
        tick (now);
        return;
    }

    if (cmd != c_run_from_host) return;
    if (have_recv_ && seq == recv_seq_) {
        // Seen it: our acknowledgement must have been lost.
        ack_due_ = true;
        tick (now);
        return;
    }
    recv_seq_ = seq;
    have_recv_ = true;
    if (outstanding_ && ack == sent_seq_) {
        outstanding_ = false;
        retries_ = 0;
    }

    std::size_t at = 8;
    for (unsigned i = 0; i < nslots && at + 4 <= m.size (); ++i) {
        std::uint8_t s_src = m[at + 1], len = m[at + 2], s_cmd = m[at + 3];
        at += 4;
        if (at + len > m.size ()) break;
        slot (s_cmd, s_src, m.subspan (at, len));
        at += len;
        if (at % 2) ++at;                           // slots are word aligned
        if (state_ == State::closed) return;
    }
    // We are the master: the host answers every message we send.  So we
    // answer it only when it asks -- otherwise each acknowledgement draws a
    // reply, and that another acknowledgement, as fast as the wire goes.
    if (m[0] & RESPONSE_REQUESTED) ack_due_ = true;
    tick (now);
}

void Lat::slot (std::uint8_t cmd, std::uint8_t src, ByteView data)
{
    unsigned credits = cmd & 0x0f;
    switch (cmd & 0xf0) {
    case 0x90:                                      // session started
        remote_slot_ = src;
        credit_ += credits;
        if (data.size () >= 3 && data[2]) max_data_slot_ = data[2];
        break;
    case 0x00:                                      // data
        credit_ += credits;
        if (!data.empty ()) {
            if (given_) --given_;
            to_terminal_ (data);
        }
        break;
    case 0xa0:                                      // port status: a credit
        credit_ += credits;
        if (given_) --given_;
        break;
    case 0xb0:                                      // attention
        credit_ += credits;
        break;
    case 0xc0:                                      // rejected
    case 0xd0:                                      // stopped
        close (credits == 1 ? "logged out" : lat_reason (credits));
        return;
    default:
        break;
    }
    // Keep the host supplied with credit to send us output.
    if (given_ <= 2 && state_ == State::running) {
        given_ += 15;
        queue_slot (0x0f, {});
    }
}

// ---------------------------------------------------------------- sending

void Lat::queue_slot (std::uint8_t cmd, ByteView data)
{
    Bytes s { remote_slot_, local_slot_, static_cast<std::uint8_t> (data.size ()), cmd };
    s.insert (s.end (), data.begin (), data.end ());
    slots_.push_back (std::move (s));
}

void Lat::from_terminal (ByteView keys)
{
    if (state_ == State::closed || keys.empty ()) return;
    keys_.emplace_back (keys.begin (), keys.end ());
}

void Lat::send_break ()
{
    if (state_ != State::running) return;
    // A port status slot with the break flag, as latd sends it.
    queue_slot (0xa0, Bytes { 0x10 });
}

void Lat::stop ()
{
    if (state_ == State::closed) return;
    if (state_ == State::running) {
        // End the circuit: the session with it.
        Bytes m { c_stop, 0 };
        put16 (m, remote_id_);
        put16 (m, local_id_);
        m.push_back (++sent_seq_);
        m.push_back (recv_seq_);
        m.push_back (1);                            // no more slots
        send_ (m);
    }
    close ("session ended here");
}

void Lat::send_message (bool data_message, Clock::time_point now)
{
    Bytes m { c_run_to_host, 0 };
    put16 (m, remote_id_);
    put16 (m, local_id_);
    m.push_back (data_message ? ++sent_seq_ : sent_seq_);
    m.push_back (recv_seq_);
    if (data_message) {
        std::uint8_t n = 0;
        while (!slots_.empty () && n < 4
               && m.size () + slots_.front ().size () + 1 <= max_msg_) {
            const Bytes &s = slots_.front ();
            m.insert (m.end (), s.begin (), s.end ());
            if (m.size () % 2) m.push_back (0);
            slots_.pop_front ();
            ++n;
        }
        m[1] = n;
        last_data_ = m;
        outstanding_ = true;
        retries_ = 0;
    }
    ack_due_ = false;
    last_sent_ = now;
    send_ (m);
}

void Lat::tick (Clock::time_point now)
{
    if (state_ == State::closed || state_ == State::idle) return;

    // Unacknowledged too long: send it again, and in the end give up.
    if (outstanding_) {
        if (now - last_sent_ >= std::chrono::seconds (1)) {
            if (++retries_ > 8) {
                close ("the host stopped answering");
                return;
            }
            last_sent_ = now;
            send_ (last_data_);
        } else if (ack_due_ && now - last_sent_ >= circuit_timer_) {
            // An acknowledgement may go while data is outstanding.
            send_message (false, now);
        }
        return;
    }
    if (state_ != State::running) return;

    // At most one message a circuit timer interval: what waits goes out
    // together, as a terminal server does.
    if (now - last_sent_ < circuit_timer_) return;

    // Typed keys become data slots, as far as credit allows.
    while (!keys_.empty () && credit_) {
        Bytes &k = keys_.front ();
        std::size_t n = std::min<std::size_t> (k.size (), max_data_slot_);
        std::uint8_t cmd = 0x00;
        if (given_ <= 2) { given_ += 15; cmd |= 0x0f; }
        queue_slot (cmd, ByteView (k.data (), n));
        --credit_;
        k.erase (k.begin (), k.begin () + static_cast<std::ptrdiff_t> (n));
        if (k.empty ()) keys_.pop_front ();
    }

    if (!slots_.empty ()) {
        send_message (true, now);
    } else if (ack_due_) {
        send_message (false, now);
    } else if (now - last_sent_ >= keepalive_ - std::chrono::seconds (3)) {
        // Nothing said for a while: an empty data message, which the host
        // must answer, shows the circuit is still there.
        send_message (true, now);
    }
}

Lat::Clock::time_point Lat::deadline () const
{
    auto now = Clock::now ();
    if (state_ == State::closed || state_ == State::idle)
        return now + std::chrono::hours (1);
    if (outstanding_) return last_sent_ + std::chrono::seconds (1);
    if (!slots_.empty () || ack_due_ || (!keys_.empty () && credit_))
        return std::max (now, last_sent_ + circuit_timer_);
    return last_sent_ + keepalive_ - std::chrono::seconds (3);
}

}   // namespace pnw
