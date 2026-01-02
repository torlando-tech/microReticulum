/**
 * @file BLEReassembler.cpp
 * @brief BLE-Reticulum Protocol v2.2 fragment reassembler implementation
 */

#include "BLEReassembler.h"
#include "../Log.h"

namespace RNS { namespace BLE {

BLEReassembler::BLEReassembler() {
    // Default timeout from protocol spec
    _timeout_seconds = Timing::REASSEMBLY_TIMEOUT;
}

void BLEReassembler::setReassemblyCallback(ReassemblyCallback callback) {
    _reassembly_callback = callback;
}

void BLEReassembler::setTimeoutCallback(TimeoutCallback callback) {
    _timeout_callback = callback;
}

void BLEReassembler::setTimeout(double timeout_seconds) {
    _timeout_seconds = timeout_seconds;
}

bool BLEReassembler::processFragment(const Bytes& peer_identity, const Bytes& fragment) {
    // Validate fragment
    if (!BLEFragmenter::isValidFragment(fragment)) {
        TRACE("BLEReassembler: Invalid fragment header");
        return false;
    }

    // Parse header
    Fragment::Type type;
    uint16_t sequence;
    uint16_t total_fragments;
    if (!BLEFragmenter::parseHeader(fragment, type, sequence, total_fragments)) {
        TRACE("BLEReassembler: Failed to parse fragment header");
        return false;
    }

    double now = Utilities::OS::time();

    // Handle START fragment - begins a new reassembly
    if (type == Fragment::START) {
        // Clear any existing incomplete reassembly for this peer
        auto it = _pending.find(peer_identity);
        if (it != _pending.end()) {
            TRACE("BLEReassembler: Discarding incomplete reassembly for new START");
        }

        // Start new reassembly
        startReassembly(peer_identity, total_fragments);
    }

    // Look up pending reassembly
    auto it = _pending.find(peer_identity);
    if (it == _pending.end()) {
        // No pending reassembly and this isn't a START
        if (type != Fragment::START) {
            // For single-fragment packets (type=END, total=1, seq=0), start immediately
            if (type == Fragment::END && total_fragments == 1 && sequence == 0) {
                startReassembly(peer_identity, total_fragments);
                it = _pending.find(peer_identity);
            } else {
                TRACE("BLEReassembler: Received fragment without START, discarding");
                return false;
            }
        } else {
            it = _pending.find(peer_identity);
        }
    }

    if (it == _pending.end()) {
        ERROR("BLEReassembler: Failed to find/create reassembly session");
        return false;
    }

    PendingReassembly& reassembly = it->second;

    // Validate total_fragments matches
    if (total_fragments != reassembly.total_fragments) {
        char buf[80];
        snprintf(buf, sizeof(buf), "BLEReassembler: Fragment total mismatch, expected %u got %u",
                 reassembly.total_fragments, total_fragments);
        TRACE(buf);
        return false;
    }

    // Validate sequence is in range
    if (sequence >= reassembly.total_fragments) {
        char buf[64];
        snprintf(buf, sizeof(buf), "BLEReassembler: Sequence out of range: %u", sequence);
        TRACE(buf);
        return false;
    }

    // Check for duplicate
    if (reassembly.fragments[sequence].received) {
        char buf[64];
        snprintf(buf, sizeof(buf), "BLEReassembler: Duplicate fragment %u", sequence);
        TRACE(buf);
        // Still update last_activity to keep session alive
        reassembly.last_activity = now;
        return true;  // Not an error, just duplicate
    }

    // Store fragment payload
    reassembly.fragments[sequence].data = BLEFragmenter::extractPayload(fragment);
    reassembly.fragments[sequence].received = true;
    reassembly.received_count++;
    reassembly.last_activity = now;

    {
        char buf[64];
        snprintf(buf, sizeof(buf), "BLEReassembler: Received fragment %u/%u", sequence + 1, reassembly.total_fragments);
        TRACE(buf);
    }

    // Check if complete
    if (reassembly.received_count == reassembly.total_fragments) {
        // Assemble complete packet
        Bytes complete_packet = assembleFragments(reassembly);

        {
            char buf[64];
            snprintf(buf, sizeof(buf), "BLEReassembler: Completed reassembly, %zu bytes", complete_packet.size());
            TRACE(buf);
        }

        // Remove from pending before callback (callback might trigger new data)
        Bytes identity_copy = reassembly.peer_identity;
        _pending.erase(it);

        // Invoke callback
        if (_reassembly_callback) {
            _reassembly_callback(identity_copy, complete_packet);
        }
    }

    return true;
}

void BLEReassembler::checkTimeouts() {
    double now = Utilities::OS::time();
    std::vector<Bytes> expired_peers;

    // Find expired reassemblies
    for (auto& kv : _pending) {
        PendingReassembly& reassembly = kv.second;
        double age = now - reassembly.started_at;

        if (age > _timeout_seconds) {
            expired_peers.push_back(kv.first);
        }
    }

    // Clean up expired reassemblies
    for (const Bytes& peer_identity : expired_peers) {
        auto it = _pending.find(peer_identity);
        if (it != _pending.end()) {
            PendingReassembly& reassembly = it->second;

            {
                char buf[80];
                snprintf(buf, sizeof(buf), "BLEReassembler: Timeout waiting for fragments, received %u/%u",
                         reassembly.received_count, reassembly.total_fragments);
                WARNING(buf);
            }

            // Invoke timeout callback
            if (_timeout_callback) {
                _timeout_callback(peer_identity, "Reassembly timeout");
            }

            _pending.erase(it);
        }
    }
}

void BLEReassembler::clearForPeer(const Bytes& peer_identity) {
    auto it = _pending.find(peer_identity);
    if (it != _pending.end()) {
        TRACE("BLEReassembler: Clearing pending reassembly for peer");
        _pending.erase(it);
    }
}

void BLEReassembler::clearAll() {
    char buf[64];
    snprintf(buf, sizeof(buf), "BLEReassembler: Clearing all pending reassemblies (%zu sessions)", _pending.size());
    TRACE(buf);
    _pending.clear();
}

bool BLEReassembler::hasPending(const Bytes& peer_identity) const {
    return _pending.find(peer_identity) != _pending.end();
}

void BLEReassembler::startReassembly(const Bytes& peer_identity, uint16_t total_fragments) {
    double now = Utilities::OS::time();

    PendingReassembly reassembly;
    reassembly.peer_identity = peer_identity;
    reassembly.total_fragments = total_fragments;
    reassembly.received_count = 0;
    reassembly.fragments.resize(total_fragments);
    reassembly.started_at = now;
    reassembly.last_activity = now;

    _pending[peer_identity] = std::move(reassembly);

    char buf[64];
    snprintf(buf, sizeof(buf), "BLEReassembler: Starting reassembly for %u fragments", total_fragments);
    TRACE(buf);
}

Bytes BLEReassembler::assembleFragments(const PendingReassembly& reassembly) {
    // Calculate total size
    size_t total_size = 0;
    for (const FragmentInfo& frag : reassembly.fragments) {
        total_size += frag.data.size();
    }

    // Allocate result buffer
    Bytes result(total_size);
    uint8_t* ptr = result.writable(total_size);
    result.resize(total_size);

    // Concatenate fragments in order
    size_t offset = 0;
    for (const FragmentInfo& frag : reassembly.fragments) {
        if (frag.data.size() > 0) {
            memcpy(ptr + offset, frag.data.data(), frag.data.size());
            offset += frag.data.size();
        }
    }

    return result;
}

}} // namespace RNS::BLE
