#!/usr/bin/env python3
"""
Python RNS resource client for microReticulum interop testing.

This client:
- Creates an INBOUND SINGLE destination
- Accepts link connections from C++ clients
- Receives resources sent by C++ clients
- Logs all events for debugging

Usage:
    python resource_client.py -c ./test_rns_config

The client prints its destination hash on startup. Use this hash
to connect from C++ and send resources.

Press Ctrl+C to stop the client.
"""

import RNS
import time
import sys
import argparse
import os

APP_NAME = "microreticulum_interop"
# Must match the C++ client's aspect for destination hash to match
ASPECT = "link_server"

# Global state
server_destination = None
active_links = []
received_resources = []


def resource_callback(resource):
    """Called when a resource advertisement is received - decide whether to accept."""
    print(f"\n[RESOURCE ADVERTISEMENT RECEIVED]")
    print(f"  Size: {resource.total_size} bytes")
    print(f"  Hash: {resource.hash.hex()}")
    print(f"  Compressed: {resource.compressed}")
    # Accept all resources
    return True


def resource_started(resource):
    """Called when a resource transfer starts."""
    print(f"\n[RESOURCE TRANSFER STARTED]")
    print(f"  Size: {resource.total_size} bytes")
    print(f"  Hash: {resource.hash.hex()}")


def resource_concluded(resource):
    """Called when a resource transfer completes."""
    print(f"\n[RESOURCE CONCLUDED]")
    print(f"  Status: {resource.status}")
    if resource.status == RNS.Resource.COMPLETE:
        print(f"  Transfer successful!")
        # resource.data is a BufferedReader, need to read from it
        data_stream = resource.data
        data = data_stream.read() if hasattr(data_stream, 'read') else data_stream
        print(f"  Received {len(data)} bytes")
        print(f"  Data (first 50 bytes): {data[:50]}")
        print(f"  Data (hex first 100): {data[:100].hex()}")

        # Verify the data pattern if it matches expected test data
        expected_pattern = b"HELLO_RETICULUM_RESOURCE_TEST_DATA_"
        if data.startswith(expected_pattern):
            print(f"  Data pattern: VERIFIED (starts with expected pattern)")
        else:
            print(f"  Data pattern: UNKNOWN (does not match expected pattern)")

        received_resources.append({
            'hash': resource.hash.hex(),
            'size': len(data),
            'data': data
        })
    elif resource.status == RNS.Resource.FAILED:
        print(f"  Transfer FAILED!")
    elif resource.status == RNS.Resource.CORRUPT:
        print(f"  Transfer CORRUPT!")
    print(f"  Transfer size: {resource.total_size} bytes")


def link_established(link):
    """Called when a new link is established."""
    print(f"\n{'='*60}")
    print(f"LINK ESTABLISHED")
    print(f"  Link ID: {link.link_id.hex()}")
    print(f"  Status: {link.status}")
    print(f"  MDU: {link.mdu}")
    print(f"{'='*60}\n")

    active_links.append(link)

    # Set up link closed callback
    link.set_link_closed_callback(link_closed)

    # Accept all incoming resources - the key setting!
    link.set_resource_strategy(RNS.Link.ACCEPT_ALL)

    # Set resource callbacks
    link.set_resource_callback(resource_callback)
    link.set_resource_started_callback(resource_started)
    link.set_resource_concluded_callback(resource_concluded)

    print("Ready to receive resources from C++...")


def link_closed(link):
    """Called when a link is closed."""
    print(f"\n{'='*60}")
    print(f"LINK CLOSED")
    print(f"  Link ID: {link.link_id.hex()}")
    print(f"{'='*60}\n")

    if link in active_links:
        active_links.remove(link)


def packet_received(data, packet):
    """Called when a packet is received on a link."""
    link = packet.link

    print(f"\n[PACKET RECEIVED]")
    print(f"  Link ID: {link.link_id.hex()}")
    print(f"  Data length: {len(data)} bytes")
    print(f"  Data (hex): {data.hex()}")


def setup_client(config_path=None):
    """Set up the RNS client."""
    global server_destination

    print("Initializing Reticulum...")

    # Initialize RNS
    if config_path:
        reticulum = RNS.Reticulum(config_path)
    else:
        reticulum = RNS.Reticulum()

    print(f"Reticulum initialized")

    # Create server identity
    print("Creating server identity...")
    server_identity = RNS.Identity()
    print(f"  Identity hash: {server_identity.hash.hex()}")

    # Create INBOUND SINGLE destination
    print("Creating server destination...")
    server_destination = RNS.Destination(
        server_identity,
        RNS.Destination.IN,
        RNS.Destination.SINGLE,
        APP_NAME,
        ASPECT
    )

    # Set up link callback
    server_destination.set_link_established_callback(link_established)

    # Set proof strategy
    server_destination.set_proof_strategy(RNS.Destination.PROVE_ALL)

    print(f"\n{'='*60}")
    print(f"RESOURCE CLIENT READY")
    print(f"{'='*60}")
    print(f"  App name: {APP_NAME}")
    print(f"  Aspect: {ASPECT}")
    print(f"  Destination hash: {server_destination.hash.hex()}")
    print(f"{'='*60}")
    print(f"\nUse this destination hash to connect from C++ client.")
    print(f"When a link is established, resources can be received.\n")

    # Announce the destination
    server_destination.announce()
    print("Destination announced on network.\n")

    return reticulum


def main():
    parser = argparse.ArgumentParser(description="microReticulum Resource Test Client")
    parser.add_argument("-c", "--config",
                        help="Path to Reticulum config directory",
                        default=None)
    parser.add_argument("-v", "--verbose",
                        help="Increase output verbosity",
                        action="store_true")
    args = parser.parse_args()

    if args.verbose:
        RNS.loglevel = RNS.LOG_DEBUG

    try:
        reticulum = setup_client(args.config)

        # Main loop - just keep running
        while True:
            time.sleep(1)

    except KeyboardInterrupt:
        print("\n\nShutting down client...")

        # Print summary
        if received_resources:
            print(f"\n{'='*60}")
            print(f"SUMMARY: Received {len(received_resources)} resources")
            for i, res in enumerate(received_resources):
                print(f"  Resource {i+1}: hash={res['hash'][:16]}..., size={res['size']} bytes")
            print(f"{'='*60}")

        # Clean up active links
        for link in active_links:
            try:
                link.teardown()
            except:
                pass

        print("Client stopped.")
        sys.exit(0)


if __name__ == "__main__":
    main()
