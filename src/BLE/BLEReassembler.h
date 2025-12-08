/**
 * @file BLEReassembler.h
 * @brief BLE-Reticulum Protocol v2.2 fragment reassembler
 *
 * Reassembles incoming BLE fragments into complete Reticulum packets.
 * Handles timeout for incomplete reassemblies and per-peer tracking.
 * This class has no BLE dependencies and can be used for testing on native builds.
 *
 * The reassembler is keyed by peer identity (16 bytes), not MAC address,
 * to survive BLE MAC address rotation.
 */
#pragma once

#include "BLETypes.h"
#include "BLEFragmenter.h"
#include "../Bytes.h"
#include "../Utilities/OS.h"

#include <map>
#include <vector>
#include <functional>
#include <cstdint>

namespace RNS { namespace BLE {

class BLEReassembler {
public:
    /**
     * @brief Callback for successfully reassembled packets
     * @param peer_identity The 16-byte identity of the sending peer
     * @param packet The complete reassembled packet
     */
    using ReassemblyCallback = std::function<void(const Bytes& peer_identity, const Bytes& packet)>;

    /**
     * @brief Callback for reassembly timeout/failure
     * @param peer_identity The 16-byte identity of the peer
     * @param reason Description of the failure
     */
    using TimeoutCallback = std::function<void(const Bytes& peer_identity, const std::string& reason)>;

public:
    /**
     * @brief Construct a reassembler with default timeout
     */
    BLEReassembler();

    /**
     * @brief Set callback for successfully reassembled packets
     */
    void setReassemblyCallback(ReassemblyCallback callback);

    /**
     * @brief Set callback for reassembly timeouts/failures
     */
    void setTimeoutCallback(TimeoutCallback callback);

    /**
     * @brief Set the reassembly timeout
     * @param timeout_seconds Seconds to wait before timing out incomplete reassembly
     */
    void setTimeout(double timeout_seconds);

    /**
     * @brief Process an incoming fragment
     *
     * @param peer_identity The 16-byte identity of the sending peer
     * @param fragment The received fragment with header
     * @return true if fragment was processed successfully, false on error
     *
     * When a packet is fully reassembled, the reassembly callback is invoked.
     */
    bool processFragment(const Bytes& peer_identity, const Bytes& fragment);

    /**
     * @brief Check for timed-out reassemblies and clean them up
     *
     * Should be called periodically from the interface loop().
     * Invokes timeout callback for each expired reassembly.
     */
    void checkTimeouts();

    /**
     * @brief Get count of pending (incomplete) reassemblies
     */
    size_t pendingCount() const { return _pending.size(); }

    /**
     * @brief Clear all pending reassemblies for a specific peer
     * @param peer_identity Clear only for this peer
     */
    void clearForPeer(const Bytes& peer_identity);

    /**
     * @brief Clear all pending reassemblies
     */
    void clearAll();

    /**
     * @brief Check if there's a pending reassembly for a peer
     */
    bool hasPending(const Bytes& peer_identity) const;

private:
    /**
     * @brief Information about a single received fragment
     */
    struct FragmentInfo {
        Bytes data;
        bool received = false;
    };

    /**
     * @brief State for a pending (incomplete) reassembly
     */
    struct PendingReassembly {
        Bytes peer_identity;
        uint16_t total_fragments = 0;
        uint16_t received_count = 0;
        std::vector<FragmentInfo> fragments;
        double started_at = 0.0;
        double last_activity = 0.0;
    };

    /**
     * @brief Concatenate all fragments in order to produce the complete packet
     */
    Bytes assembleFragments(const PendingReassembly& reassembly);

    /**
     * @brief Start a new reassembly session
     */
    void startReassembly(const Bytes& peer_identity, uint16_t total_fragments);

    // Pending reassemblies keyed by peer identity
    std::map<Bytes, PendingReassembly> _pending;

    // Callbacks
    ReassemblyCallback _reassembly_callback = nullptr;
    TimeoutCallback _timeout_callback = nullptr;

    // Timeout configuration
    double _timeout_seconds = Timing::REASSEMBLY_TIMEOUT;
};

}} // namespace RNS::BLE
