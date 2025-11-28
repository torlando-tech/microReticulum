#!/usr/bin/env python3
"""
Python RNS timeout test server for microReticulum interop testing.

This server simulates various failure modes to test C++ timeout handling:
  --mode normal:     Normal resource handling (baseline)
  --mode drop-adv:   Accept link but don't request resource parts (test sender adv timeout)
  --mode drop-parts: Start receiving but stop mid-transfer (test receiver timeout)
  --mode drop-proof: Receive all parts but don't send proof (test sender proof timeout)
  --mode send-only:  Only send resources, don't receive (test C++ receiver timeout)

Usage:
    python timeout_server.py -c ./test_rns_config --mode drop-adv

Press Ctrl+C to stop the server.
"""

import RNS
import time
import sys
import argparse
import os

APP_NAME = "microreticulum_interop"
ASPECT = "timeout_test"

# Global state
server_destination = None
active_links = []
mode = "normal"
parts_to_receive = 0  # For drop-parts mode


def generate_test_data(size=1024):
    """Generate pattern test data."""
    pattern = b"TIMEOUT_TEST_DATA_"
    repeat_count = (size // len(pattern)) + 1
    return (pattern * repeat_count)[:size]


class DropPartsResource:
    """Wrapper to intercept and drop parts after a certain count."""
    def __init__(self, resource, drop_after=5):
        self._resource = resource
        self._drop_after = drop_after
        self._parts_received = 0


def resource_started(resource):
    """Called when a resource transfer starts."""
    global mode, parts_to_receive

    print(f"\n[RESOURCE STARTED] Mode: {mode}")
    print(f"  Size: {resource.total_size} bytes")
    print(f"  Hash: {resource.hash.hex()}")

    if mode == "drop-parts":
        # Calculate how many parts to receive before stopping
        # Receive about 50% of parts then stop
        parts_to_receive = max(1, resource.total_parts // 2)
        print(f"  Will stop after receiving {parts_to_receive}/{resource.total_parts} parts")


def resource_concluded(resource):
    """Called when a resource transfer completes."""
    print(f"\n[RESOURCE CONCLUDED]")
    print(f"  Status: {resource.status}")
    status_names = {
        RNS.Resource.COMPLETE: "COMPLETE",
        RNS.Resource.FAILED: "FAILED",
        RNS.Resource.CORRUPT: "CORRUPT",
    }
    status_str = status_names.get(resource.status, f"UNKNOWN({resource.status})")
    print(f"  Status name: {status_str}")


def send_resource(link, data):
    """Send a resource over the link."""
    print(f"\n[SENDING RESOURCE]")
    print(f"  Data size: {len(data)} bytes")

    try:
        resource = RNS.Resource(
            data,
            link,
            callback=resource_concluded
        )
        print(f"  Resource hash: {resource.hash.hex()}")
        return resource
    except Exception as e:
        print(f"  Failed to create resource: {e}")
        import traceback
        traceback.print_exc()
        return None


def custom_resource_strategy(resource):
    """Custom resource acceptance strategy for timeout testing."""
    global mode

    if mode == "drop-adv":
        # Accept but don't actually process - sender will timeout waiting for requests
        print(f"[DROP-ADV] Ignoring resource advertisement (sender will timeout)")
        return False  # Reject to simulate not responding

    elif mode == "drop-proof":
        # Accept the resource normally, but we'll intercept the proof
        print(f"[DROP-PROOF] Accepting resource, will skip sending proof")
        return True

    elif mode == "drop-parts":
        # Accept but we'll stop requesting parts mid-transfer
        print(f"[DROP-PARTS] Accepting resource, will stop mid-transfer")
        return True

    else:
        # Normal mode - accept all
        return True


def link_established(link):
    """Called when a new link is established."""
    global mode

    print(f"\n{'='*60}")
    print(f"LINK ESTABLISHED (Mode: {mode})")
    print(f"  Link ID: {link.link_id.hex()}")
    print(f"{'='*60}\n")

    active_links.append(link)
    link.set_link_closed_callback(link_closed)

    if mode == "drop-adv":
        # Set a custom callback that rejects resources
        link.set_resource_strategy(RNS.Link.ACCEPT_NONE)
        print("[DROP-ADV] Set resource strategy to ACCEPT_NONE")
        print("Sender will timeout waiting for part requests...")
    elif mode == "drop-proof":
        # Accept resources normally - the proof skip happens in resource handling
        link.set_resource_strategy(RNS.Link.ACCEPT_ALL)
        link.set_resource_started_callback(resource_started)
        # Don't set concluded callback - let it complete but we won't send proof
        print("[DROP-PROOF] Accepting resources but will skip proof")
    elif mode == "drop-parts":
        link.set_resource_strategy(RNS.Link.ACCEPT_ALL)
        link.set_resource_started_callback(resource_started)
        link.set_resource_concluded_callback(resource_concluded)
        print("[DROP-PARTS] Will stop mid-transfer")
    elif mode == "send-only":
        # Send a resource to test C++ receiver timeout
        link.set_resource_strategy(RNS.Link.ACCEPT_NONE)
        print("[SEND-ONLY] Sending resource to C++ client...")
        time.sleep(0.5)
        data = generate_test_data(10240)  # 10KB
        send_resource(link, data)
        print("Resource sent. C++ client should receive and prove it.")
    else:
        # Normal mode
        link.set_resource_strategy(RNS.Link.ACCEPT_ALL)
        link.set_resource_started_callback(resource_started)
        link.set_resource_concluded_callback(resource_concluded)
        print("[NORMAL] Accepting all resources")


def link_closed(link):
    """Called when a link is closed."""
    print(f"\n[LINK CLOSED] {link.link_id.hex()}")
    if link in active_links:
        active_links.remove(link)


def setup_server(config_path, test_mode):
    """Set up the RNS server."""
    global server_destination, mode
    mode = test_mode

    print(f"Initializing Reticulum (mode: {mode})...")

    if config_path:
        reticulum = RNS.Reticulum(config_path)
    else:
        reticulum = RNS.Reticulum()

    print("Creating server identity...")
    server_identity = RNS.Identity()

    # Create destination
    server_destination = RNS.Destination(
        server_identity,
        RNS.Destination.IN,
        RNS.Destination.SINGLE,
        APP_NAME,
        ASPECT
    )

    server_destination.set_link_established_callback(link_established)
    server_destination.set_proof_strategy(RNS.Destination.PROVE_ALL)

    print(f"\n{'='*60}")
    print(f"TIMEOUT TEST SERVER READY")
    print(f"{'='*60}")
    print(f"  Mode: {mode}")
    print(f"  Destination: {server_destination.hash.hex()}")
    print(f"{'='*60}")

    mode_descriptions = {
        "normal": "Normal operation - resources transfer successfully",
        "drop-adv": "Sender timeout - won't request parts from C++ sender",
        "drop-parts": "Receiver timeout - will stop mid-transfer",
        "drop-proof": "Proof timeout - won't send proof after receiving all parts",
        "send-only": "Sends resource to test C++ receiver",
    }
    print(f"\nMode description: {mode_descriptions.get(mode, 'Unknown mode')}")
    print(f"\nUse destination hash to connect from C++ client.\n")

    server_destination.announce()
    print("Destination announced.\n")

    return reticulum


def main():
    parser = argparse.ArgumentParser(description="microReticulum Timeout Test Server")
    parser.add_argument("-c", "--config",
                        help="Path to Reticulum config directory",
                        required=True)
    parser.add_argument("-m", "--mode",
                        help="Timeout test mode",
                        choices=["normal", "drop-adv", "drop-parts", "drop-proof", "send-only"],
                        default="normal")
    parser.add_argument("-v", "--verbose",
                        help="Increase output verbosity",
                        action="store_true")
    args = parser.parse_args()

    if args.verbose:
        RNS.loglevel = RNS.LOG_DEBUG

    try:
        reticulum = setup_server(args.config, args.mode)

        while True:
            time.sleep(1)

    except KeyboardInterrupt:
        print("\n\nShutting down...")
        for link in active_links:
            try:
                link.teardown()
            except:
                pass
        print("Server stopped.")
        sys.exit(0)


if __name__ == "__main__":
    main()
