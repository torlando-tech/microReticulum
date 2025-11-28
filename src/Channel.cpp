#include "Channel.h"
#include "ChannelData.h"

#include "Reticulum.h"
#include "Transport.h"
#include "Packet.h"
#include "Log.h"
#include "Utilities/OS.h"

#include <algorithm>
#include <cmath>

using namespace RNS;
using namespace RNS::Type::Channel;
using namespace RNS::Utilities;

Channel::Channel(const Link& link) : _object(new ChannelData(link)) {
    MEM("Channel object created with link");
    if (link) {
        _object->_ready = true;
        TRACE("Channel: Initialized with link");
    }
}

void Channel::add_message_handler(std::function<bool(MessageBase&)> callback) {
    if (!_object) return;
    _object->_message_callbacks.push_back(callback);
    TRACE("Channel: Added message handler");
}

void Channel::remove_message_handler(std::function<bool(MessageBase&)> callback) {
    if (!_object) return;
    // Note: std::function doesn't support direct comparison
    // For now, this is a placeholder - Buffer uses the same callback reference
    // In practice, handlers are rarely removed individually
    TRACE("Channel: remove_message_handler called (not fully implemented)");
}

bool Channel::is_ready_to_send() const {
    if (!_object || !_object->_link) return false;
    // Check link is usable and outstanding < window
    return _outstanding_count() < _object->_window;
}

size_t Channel::_outstanding_count() const {
    if (!_object) return 0;
    size_t count = 0;
    for (const auto& env : _object->_tx_ring) {
        if (env.tracked()) count++;
    }
    return count;
}

size_t Channel::mdu() const {
    if (!_object || !_object->_link) return 0;
    // Link MDU minus Channel envelope header
    size_t link_mdu = _object->_link.get_mdu();
    if (link_mdu > Type::Channel::ENVELOPE_HEADER_SIZE) {
        return link_mdu - Type::Channel::ENVELOPE_HEADER_SIZE;
    }
    return 0;
}

size_t Channel::tx_ring_size() const {
    if (!_object) return 0;
    return _object->_tx_ring.size();
}

double Channel::link_rtt() const {
    if (!_object || !_object->_link) return 0.0;
    return _object->_link.rtt();
}

Link Channel::link() const {
    if (!_object) return {Type::NONE};
    return _object->_link;
}

void Channel::_shutdown() {
    if (!_object) return;
    TRACE("Channel: Shutting down");
    _object->_ready = false;
    _object->_tx_ring.clear();
    _object->_rx_ring.clear();
}

void Channel::send(const MessageBase& message) {
    if (!_object) {
        ERROR("Channel::send: No channel object");
        return;
    }
    if (!_object->_link) {
        ERROR("Channel::send: No link");
        return;
    }
    if (!_object->_ready) {
        ERROR("Channel::send: Channel not ready");
        return;
    }

    // Pack the message
    Bytes packed_data = message.pack();

    // Check MDU
    size_t max_data = mdu();
    if (packed_data.size() > max_data) {
        ERRORF("Channel::send: Message too big (%zu > %zu)", packed_data.size(), max_data);
        return;
    }

    // Create envelope with current sequence number
    uint16_t sequence = _object->_next_sequence;
    Envelope envelope(message.msgtype(), sequence, packed_data);

    // Increment sequence (with wraparound)
    _object->_next_sequence = (_object->_next_sequence + 1) % Type::Channel::SEQ_MODULUS;

    // Pack envelope to wire format
    Bytes wire_data = envelope.pack();

    DEBUGF("Channel::send: Sending message type 0x%04X, seq=%u, data_len=%zu",
           message.msgtype(), sequence, packed_data.size());

    // Create and send packet via Link
    // Use CHANNEL context (0x0E)
    Packet packet(_object->_link, wire_data, Type::Packet::DATA, Type::Packet::CHANNEL);

    if (!packet) {
        ERROR("Channel::send: Failed to create packet");
        return;
    }

    // Send the packet
    PacketReceipt receipt = packet.send();

    if (!receipt) {
        ERROR("Channel::send: Failed to send packet");
        return;
    }

    // Store in TX ring for tracking
    envelope.set_packet(packet);
    envelope.set_timestamp(OS::time());
    envelope.set_tracked(true);
    _object->_tx_ring.push_back(std::move(envelope));

    TRACEF("Channel::send: Packet sent, TX ring size=%zu", _object->_tx_ring.size());
}

void Channel::_receive(const Bytes& plaintext) {
    if (!_object) {
        ERROR("Channel::_receive: No channel object");
        return;
    }

    TRACEF("Channel::_receive: Received %zu bytes", plaintext.size());

    // Unpack wire data to envelope
    Envelope envelope;
    if (!Envelope::unpack(plaintext, envelope)) {
        ERROR("Channel::_receive: Failed to unpack envelope");
        return;
    }

    uint16_t msgtype = envelope.msgtype();
    uint16_t sequence = envelope.sequence();

    DEBUGF("Channel::_receive: msgtype=0x%04X, seq=%u, data_len=%zu",
           msgtype, sequence, envelope.raw().size());

    // Look up message factory
    auto factory_it = _object->_message_factories.find(msgtype);
    if (factory_it == _object->_message_factories.end()) {
        ERRORF("Channel::_receive: Unknown message type 0x%04X", msgtype);
        return;
    }

    // Create message instance using factory
    std::unique_ptr<MessageBase> message = factory_it->second();
    if (!message) {
        ERROR("Channel::_receive: Factory returned null");
        return;
    }

    // Unpack message data
    message->unpack(envelope.raw());
    envelope.set_message(std::move(message));

    // Handle sequencing
    // Check if sequence is too old (outside valid window)
    uint16_t expected = _object->_next_rx_sequence;

    // Calculate sequence distance (accounting for wraparound)
    int32_t distance = static_cast<int32_t>(sequence) - static_cast<int32_t>(expected);
    if (distance < 0) {
        distance += Type::Channel::SEQ_MODULUS;
    }

    // If distance is greater than window, it's either very old or wrapped badly
    if (distance >= Type::Channel::WINDOW_MAX) {
        DEBUGF("Channel::_receive: Sequence %u outside window (expected %u)",
               sequence, expected);
        // Check if it's actually behind us (already received)
        int32_t behind = static_cast<int32_t>(expected) - static_cast<int32_t>(sequence);
        if (behind < 0) {
            behind += Type::Channel::SEQ_MODULUS;
        }
        if (behind < Type::Channel::WINDOW_MAX) {
            // This is a duplicate/old packet, silently drop
            TRACE("Channel::_receive: Dropping old/duplicate packet");
            return;
        }
    }

    // Check for duplicate in RX ring
    for (const auto& existing : _object->_rx_ring) {
        if (existing.sequence() == sequence) {
            TRACEF("Channel::_receive: Duplicate sequence %u, dropping", sequence);
            return;
        }
    }

    // Emplace envelope in RX ring (in sequence order)
    _emplace_envelope(envelope);

    // Process contiguous messages from RX ring
    _process_rx_ring();
}

void Channel::_emplace_envelope(Envelope& envelope) {
    if (!_object) return;

    uint16_t sequence = envelope.sequence();

    // Insert in sequence order
    auto it = _object->_rx_ring.begin();
    while (it != _object->_rx_ring.end()) {
        // Calculate relative position
        int32_t diff = static_cast<int32_t>(sequence) - static_cast<int32_t>(it->sequence());
        if (diff < 0) {
            diff += Type::Channel::SEQ_MODULUS;
        }
        // Normalize to handle wraparound
        if (diff >= static_cast<int32_t>(Type::Channel::SEQ_MODULUS / 2)) {
            diff -= Type::Channel::SEQ_MODULUS;
        }

        if (diff < 0) {
            // Insert before this position
            break;
        }
        ++it;
    }

    _object->_rx_ring.insert(it, std::move(envelope));
    TRACEF("Channel::_emplace_envelope: Inserted seq=%u, ring size=%zu",
           sequence, _object->_rx_ring.size());
}

void Channel::_process_rx_ring() {
    if (!_object) return;

    // Process contiguous messages starting from expected sequence
    while (!_object->_rx_ring.empty()) {
        Envelope& front = _object->_rx_ring.front();

        if (front.sequence() != _object->_next_rx_sequence) {
            // Gap in sequence, stop processing
            TRACEF("Channel::_process_rx_ring: Gap at seq=%u (expected %u)",
                   front.sequence(), _object->_next_rx_sequence);
            break;
        }

        // Dispatch to callbacks
        _run_callbacks(front);

        // Advance expected sequence (with wraparound)
        _object->_next_rx_sequence =
            (_object->_next_rx_sequence + 1) % Type::Channel::SEQ_MODULUS;

        // Remove from ring
        _object->_rx_ring.pop_front();
    }
}

void Channel::_run_callbacks(Envelope& envelope) {
    if (!_object) return;

    MessageBase* msg = envelope.message().get();
    if (!msg) {
        ERROR("Channel::_run_callbacks: No message in envelope");
        return;
    }

    TRACEF("Channel::_run_callbacks: Dispatching msgtype=0x%04X", envelope.msgtype());

    // Call handlers until one returns true
    for (auto& callback : _object->_message_callbacks) {
        if (callback(*msg)) {
            TRACE("Channel::_run_callbacks: Handler returned true, stopping dispatch");
            return;
        }
    }

    // No handler claimed the message
    TRACEF("Channel::_run_callbacks: No handler claimed message type 0x%04X",
           envelope.msgtype());
}

void Channel::_on_packet_delivered(const Packet& packet) {
    if (!_object) return;

    TRACE("Channel::_on_packet_delivered");

    // Find envelope in TX ring by packet
    for (auto it = _object->_tx_ring.begin(); it != _object->_tx_ring.end(); ++it) {
        if (it->packet() == packet) {
            uint16_t seq = it->sequence();
            DEBUGF("Channel::_on_packet_delivered: seq=%u delivered", seq);

            // Update RTT from packet receipt if available
            // Note: We need to make a mutable copy to call get_rtt() since it's not const
            if (packet.receipt()) {
                PacketReceipt receipt = packet.receipt();
                double packet_rtt = receipt.get_rtt();
                if (packet_rtt > 0) {
                    _update_rtt(packet_rtt);
                }
            }

            // Remove from TX ring
            _object->_tx_ring.erase(it);

            // Increase window (success)
            if (_object->_window < _object->_window_max) {
                _object->_window++;
                TRACEF("Channel: Window increased to %u", _object->_window);
            }
            return;
        }
    }

    TRACE("Channel::_on_packet_delivered: Packet not found in TX ring");
}

void Channel::_on_packet_timeout(const Packet& packet) {
    if (!_object || !_object->_link) return;

    TRACE("Channel::_on_packet_timeout");

    // Find envelope in TX ring
    for (auto& envelope : _object->_tx_ring) {
        if (envelope.packet() == packet) {
            envelope.increment_tries();
            uint8_t tries = envelope.tries();

            DEBUGF("Channel::_on_packet_timeout: seq=%u, tries=%u/%u",
                   envelope.sequence(), tries, _object->_max_tries);

            if (tries >= _object->_max_tries) {
                // Max retries exceeded - tear down link
                ERROR("Channel: Max retries exceeded, tearing down link");
                _object->_link.teardown();
                return;
            }

            // Decrease window on timeout
            if (_object->_window > _object->_window_min) {
                _object->_window--;
                TRACEF("Channel: Window decreased to %u", _object->_window);
            }

            // Resend the packet
            envelope.set_timestamp(OS::time());
            envelope.packet().resend();

            return;
        }
    }
}

void Channel::_update_rtt(double new_rtt) {
    if (!_object) return;

    // Exponential moving average
    if (_object->_rtt == 0.0) {
        _object->_rtt = new_rtt;
    } else {
        _object->_rtt = _object->_rtt * 0.7 + new_rtt * 0.3;
    }

    TRACEF("Channel: RTT updated to %.3fs", _object->_rtt);

    // Recalculate window limits based on new RTT
    _recalculate_window_limits();
}

void Channel::_recalculate_window_limits() {
    if (!_object) return;

    double rtt = _object->_rtt;
    ChannelData::WindowTier old_tier = _object->_current_tier;

    if (rtt <= Type::Channel::RTT_FAST) {
        _object->_window_max = Type::Channel::WINDOW_MAX_FAST;
        _object->_window_min = 16;  // Fast tier uses higher minimum
        _object->_current_tier = ChannelData::WindowTier::FAST;
    } else if (rtt <= Type::Channel::RTT_MEDIUM) {
        _object->_window_max = Type::Channel::WINDOW_MAX_MEDIUM;
        _object->_window_min = 5;
        _object->_current_tier = ChannelData::WindowTier::MEDIUM;
    } else if (rtt <= Type::Channel::RTT_SLOW) {
        _object->_window_max = Type::Channel::WINDOW_MAX_SLOW;
        _object->_window_min = Type::Channel::WINDOW_MIN;
        _object->_current_tier = ChannelData::WindowTier::SLOW;
    } else {
        // Very slow link
        _object->_window_max = 1;
        _object->_window_min = 1;
        _object->_current_tier = ChannelData::WindowTier::VERY_SLOW;
    }

    // Clamp current window to new limits
    if (_object->_window > _object->_window_max) {
        _object->_window = _object->_window_max;
    }
    if (_object->_window < _object->_window_min) {
        _object->_window = _object->_window_min;
    }

    if (old_tier != _object->_current_tier) {
        DEBUGF("Channel: Window tier changed, limits now [%u, %u]",
               _object->_window_min, _object->_window_max);
    }
}

double Channel::_calculate_timeout(const Envelope& envelope) const {
    if (!_object) return 5.0;  // Default

    double rtt = _object->_rtt > 0 ? _object->_rtt : 0.5;  // Default RTT 0.5s
    size_t ring_size = _object->_tx_ring.size();
    uint8_t tries = envelope.tries();

    // Formula from Python: 1.5^(tries-1) * max(rtt * 2.5, 0.025) * (ring_size + 1.5)
    double base = std::pow(1.5, tries > 0 ? tries - 1 : 0);
    double rtt_factor = std::max(rtt * 2.5, 0.025);
    double ring_factor = ring_size + 1.5;

    return base * rtt_factor * ring_factor;
}

void Channel::_job() {
    if (!_object) return;

    double now = OS::time();

    // Check for timeouts in TX ring
    for (auto& envelope : _object->_tx_ring) {
        if (!envelope.tracked()) continue;

        double timeout = _calculate_timeout(envelope);
        double age = now - envelope.timestamp();

        if (age > timeout) {
            DEBUGF("Channel::_job: Envelope seq=%u timed out (age=%.2fs > timeout=%.2fs)",
                   envelope.sequence(), age, timeout);
            _on_packet_timeout(envelope.packet());
        }
    }
}
