// pnw/lat.h -- LAT, the terminal end of one session.
//
// LAT (Local Area Transport) is how DEC terminal servers reached VMS and
// RSX hosts over Ethernet, without DECnet: EtherType 0x6004, straight on the
// wire, with each station's own MAC address.  Hosts announce their services
// to the multicast address 09-00-2B-00-00-0F; a terminal server opens a
// virtual circuit to a host and runs sessions on it in slots.
//
// DEC never published the specification.  This follows the formats that
// latd (Linux) documents in lat.h and the behaviour it shows, checked
// against RSX-11M-PLUS.
//
// Lat is the protocol alone, one circuit with one session: frames and
// keys go in, frames for the wire and bytes for the terminal come out.
// The circuit is window 1: one data message outstanding, acknowledged
// before the next.

#ifndef PNW_LAT_H
#define PNW_LAT_H

#include "decnet/common/types.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pnw {

using decnet::Bytes;
using decnet::ByteView;
using Mac = std::array<std::uint8_t, 6>;

inline constexpr std::uint16_t LAT_ETHERTYPE = 0x6004;
inline constexpr Mac LAT_MULTICAST { 0x09, 0x00, 0x2b, 0x00, 0x00, 0x0f };

// A host's service announcement.
struct LatAnnouncement {
    std::string   node;
    std::string   description;
    std::uint16_t mtu = 0;
    unsigned      multicast_timer = 0;          // seconds between them
    struct Service {
        std::string   name;
        std::string   description;
        unsigned      rating = 0;
    };
    std::vector<Service> services;
};

std::optional<LatAnnouncement> parse_announcement (ByteView msg);

// A session's end, as the host or the circuit gave it.
std::string lat_reason (unsigned slot_reason);

class Lat {
public:
    using Clock = std::chrono::steady_clock;

    struct Target {
        std::string node;                       // the host, from its announcement
        std::string service;
        std::string local_node = "PNW";         // what we call ourselves
        std::string port = "PNWLAT";            // our "port" name
        std::uint16_t mtu = 1500;
    };

    // send gets a LAT message for the host; to_terminal bytes to show.
    Lat (Target t, std::function<void (ByteView)> send,
         std::function<void (ByteView)> to_terminal);

    // Open the circuit and session.
    void start (Clock::time_point now);

    // A LAT message from the host.
    void from_host (ByteView msg, Clock::time_point now);

    // Keys typed by the user.
    void from_terminal (ByteView keys);

    // A break, as a terminal's BREAK key sends.
    void send_break ();

    // End the session and the circuit from this end.
    void stop ();

    // The circuit timer: send what is waiting, acknowledge, retransmit,
    // keep the circuit alive.  Call at deadline () or sooner.
    void tick (Clock::time_point now);
    Clock::time_point deadline () const;

    bool running () const noexcept { return state_ == State::running; }
    bool closed () const noexcept { return state_ == State::closed; }
    const std::string &why () const noexcept { return why_; }

    // Message types (the header's first byte).  Bit 1 set: to the host.
    enum : std::uint8_t {
        c_run_from_host = 0x00, c_run_to_host = 0x02, c_start_ack = 0x04,
        c_start = 0x06, c_stop_from_host = 0x08, c_stop = 0x0a,
        c_announce = 0x28
    };

private:
    enum class State { idle, starting, running, closed };

    // Queue a slot: dst and src session, command with its credit nibble.
    void queue_slot (std::uint8_t cmd, ByteView data);
    void send_message (bool data_message, Clock::time_point now);
    void close (const std::string &why);
    void slot (std::uint8_t cmd, std::uint8_t src, ByteView data);

    Target                          t_;
    std::function<void (ByteView)>  send_, to_terminal_;
    State                           state_ = State::idle;
    std::string                     why_;

    std::uint16_t local_id_ = 0, remote_id_ = 0;    // circuit ids
    std::uint8_t  local_slot_ = 1, remote_slot_ = 0; // session ids
    std::uint8_t  sent_seq_ = 0xff;                  // last sequence sent
    std::uint8_t  recv_seq_ = 0;                     // last received
    bool          have_recv_ = false;
    bool          outstanding_ = false;              // data message unacked
    Bytes         last_data_;                        // for retransmission
    unsigned      retries_ = 0;
    bool          ack_due_ = false;

    unsigned      credit_ = 0;                       // slots we may send
    unsigned      given_ = 0;                        // slots the host may send
    std::deque<Bytes> keys_;                         // typed, waiting for credit
    std::deque<Bytes> slots_;                        // encoded, waiting to go
    std::uint16_t max_msg_ = 1500;
    std::uint8_t  max_data_slot_ = 255;

    Clock::time_point last_sent_ {};
    Clock::time_point last_heard_ {};
    std::chrono::milliseconds circuit_timer_ { 80 };
    std::chrono::seconds      keepalive_ { 20 };
};

}   // namespace pnw

#endif  // PNW_LAT_H
