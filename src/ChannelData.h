#pragma once

#ifndef RNS_CHANNEL_DATA_H
#define RNS_CHANNEL_DATA_H

#include "Bytes.h"
#include "Link.h"
#include "Packet.h"
#include "MessageBase.h"
#include "Type.h"
#include "Log.h"

#include <deque>
#include <map>
#include <vector>
#include <functional>
#include <memory>

namespace RNS {

// Forward declaration
class Channel;

// Envelope wraps a message with protocol metadata
class Envelope {
public:
    Envelope() = default;
    Envelope(uint16_t msgtype, uint16_t sequence, const Bytes& raw)
        : _msgtype(msgtype), _sequence(sequence), _raw(raw) {}

    // Move semantics (unique_ptr makes class non-copyable)
    Envelope(Envelope&&) = default;
    Envelope& operator=(Envelope&&) = default;

    // Delete copy operations
    Envelope(const Envelope&) = delete;
    Envelope& operator=(const Envelope&) = delete;

    uint16_t msgtype() const { return _msgtype; }
    uint16_t sequence() const { return _sequence; }
    const Bytes& raw() const { return _raw; }

    // For TX tracking
    Packet packet() const { return _packet; }
    void set_packet(const Packet& packet) { _packet = packet; }
    uint8_t tries() const { return _tries; }
    void increment_tries() { _tries++; }
    double timestamp() const { return _timestamp; }
    void set_timestamp(double ts) { _timestamp = ts; }
    bool tracked() const { return _tracked; }
    void set_tracked(bool tracked) { _tracked = tracked; }

    // Message instance (for RX)
    std::unique_ptr<MessageBase>& message() { return _message; }
    void set_message(std::unique_ptr<MessageBase> msg) { _message = std::move(msg); }

    // Pack envelope to wire format (big-endian)
    Bytes pack() const {
        // Wire format: MSGTYPE(2) + SEQUENCE(2) + LENGTH(2) + DATA(N)
        // All values big-endian
        Bytes result;

        // Allocate exact size needed
        size_t data_len = _raw.size();
        result.reserve(6 + data_len);

        // MSGTYPE (2 bytes, big-endian)
        result += (uint8_t)((_msgtype >> 8) & 0xFF);
        result += (uint8_t)(_msgtype & 0xFF);

        // SEQUENCE (2 bytes, big-endian)
        result += (uint8_t)((_sequence >> 8) & 0xFF);
        result += (uint8_t)(_sequence & 0xFF);

        // LENGTH (2 bytes, big-endian)
        result += (uint8_t)((data_len >> 8) & 0xFF);
        result += (uint8_t)(data_len & 0xFF);

        // DATA
        result += _raw;

        return result;
    }

    // Unpack envelope from wire format (big-endian)
    static bool unpack(const Bytes& wire_data, Envelope& out) {
        // Need at least 6 bytes for header
        if (wire_data.size() < 6) {
            return false;
        }

        const uint8_t* data = wire_data.data();

        // MSGTYPE (2 bytes, big-endian)
        uint16_t msgtype = (static_cast<uint16_t>(data[0]) << 8) | data[1];

        // SEQUENCE (2 bytes, big-endian)
        uint16_t sequence = (static_cast<uint16_t>(data[2]) << 8) | data[3];

        // LENGTH (2 bytes, big-endian)
        uint16_t length = (static_cast<uint16_t>(data[4]) << 8) | data[5];

        // Validate length
        if (wire_data.size() < 6 + static_cast<size_t>(length)) {
            return false;
        }

        // Extract data payload
        Bytes raw;
        if (length > 0) {
            raw = wire_data.mid(6, length);
        }

        out = Envelope(msgtype, sequence, raw);
        return true;
    }

private:
    uint16_t _msgtype = 0;
    uint16_t _sequence = 0;
    Bytes _raw;
    Packet _packet = {Type::NONE};
    uint8_t _tries = 0;
    double _timestamp = 0.0;
    bool _tracked = false;
    std::unique_ptr<MessageBase> _message;
};

// Internal Channel data structure
class ChannelData {
public:
    enum class WindowTier { FAST, MEDIUM, SLOW, VERY_SLOW };

    ChannelData() { MEM("ChannelData object created"); }
    ChannelData(const Link& link) : _link(link) { MEM("ChannelData object created with link"); }
    virtual ~ChannelData() { MEM("ChannelData object destroyed"); }

private:
    friend class Channel;

    // Link reference
    Link _link = {Type::NONE};

    // Sequencing
    uint16_t _next_sequence = 0;
    uint16_t _next_rx_sequence = 0;

    // Ring buffers
    std::deque<Envelope> _rx_ring;
    std::deque<Envelope> _tx_ring;

    // Message dispatch
    // Factory: msgtype -> function that creates a new message instance
    std::map<uint16_t, std::function<std::unique_ptr<MessageBase>()>> _message_factories;
    // Handlers: list of callbacks, first returning true stops dispatch
    std::vector<std::function<bool(MessageBase&)>> _message_callbacks;

    // Window management
    uint16_t _window = Type::Channel::WINDOW_INITIAL;
    uint16_t _window_min = Type::Channel::WINDOW_MIN;
    uint16_t _window_max = Type::Channel::WINDOW_MAX;
    uint16_t _fast_rate_rounds = 0;

    // Timing/RTT
    double _rtt = 0.0;
    uint8_t _max_tries = Type::Channel::MAX_TRIES;
    WindowTier _current_tier = WindowTier::MEDIUM;

    // State
    bool _ready = false;
};

} // namespace RNS

#endif // RNS_CHANNEL_DATA_H
