#pragma once

#ifndef RNS_CHANNEL_H
#define RNS_CHANNEL_H

#include "Bytes.h"
#include "Link.h"
#include "MessageBase.h"
#include "Type.h"
#include "Log.h"

#include <memory>
#include <functional>

namespace RNS {

class ChannelData;
class Envelope;

class Channel {
public:
    // Constructors
    Channel(Type::NoneConstructor none) { MEM("Channel NONE object created"); }
    Channel(const Link& link);
    Channel(const Channel& channel) : _object(channel._object) { MEM("Channel object copy created"); }
    virtual ~Channel() { MEM("Channel object destroyed"); }

    // Assignment
    Channel& operator=(const Channel& channel) {
        _object = channel._object;
        return *this;
    }

    // Validity check
    operator bool() const { return _object.get() != nullptr; }
    bool operator<(const Channel& channel) const {
        return _object.get() < channel._object.get();
    }

    // Message type registration
    // Template version for compile-time registration
    template<typename T>
    void register_message_type(bool is_system_type = false);

    // Handler registration
    void add_message_handler(std::function<bool(MessageBase&)> callback);
    void remove_message_handler(std::function<bool(MessageBase&)> callback);

    // Send/receive
    void send(const MessageBase& message);
    bool is_ready_to_send() const;

    // Called by Link when Channel packet received
    void _receive(const Bytes& plaintext);

    // Called by Link on teardown
    void _shutdown();

    // Called when a packet is delivered (ACK received)
    void _on_packet_delivered(const Packet& packet);

    // Called when a packet times out
    void _on_packet_timeout(const Packet& packet);

    // Periodic maintenance (check timeouts, adjust window)
    void _job();

    // Accessors for Buffer integration
    size_t mdu() const;
    size_t tx_ring_size() const;
    double link_rtt() const;
    Link link() const;

private:
    std::shared_ptr<ChannelData> _object;

    // Internal methods
    void _emplace_envelope(Envelope& envelope);
    void _run_callbacks(Envelope& envelope);
    void _process_rx_ring();
    size_t _outstanding_count() const;
    void _update_rtt(double new_rtt);
    void _recalculate_window_limits();
    double _calculate_timeout(const Envelope& envelope) const;
};

// Template implementation
template<typename T>
void Channel::register_message_type(bool is_system_type) {
    if (!_object) return;

    uint16_t msgtype = T::MSGTYPE;

    // Validate message type
    if (!is_system_type && msgtype >= Type::Channel::MSGTYPE_USER_MAX) {
        ERROR("Channel: User message type must be < 0xF000");
        return;
    }
    if (is_system_type && msgtype < Type::Channel::MSGTYPE_USER_MAX) {
        ERROR("Channel: System message type must be >= 0xF000");
        return;
    }

    // Check for duplicate
    if (_object->_message_factories.find(msgtype) != _object->_message_factories.end()) {
        ERROR("Channel: Message type already registered");
        return;
    }

    // Register factory
    _object->_message_factories[msgtype] = []() -> std::unique_ptr<MessageBase> {
        return std::unique_ptr<MessageBase>(new T());
    };

    DEBUGF("Channel: Registered message type 0x%04X", msgtype);
}

} // namespace RNS

#endif // RNS_CHANNEL_H
