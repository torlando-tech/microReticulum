#!/usr/bin/env python3
"""
Python RNS resource server for microReticulum interop testing.

This server:
- Creates an INBOUND SINGLE destination
- Accepts link connections from C++ clients
- Sends a 1KB resource to the client when link is established
- Logs all events for debugging

Usage:
    python resource_server.py -c ./test_rns_config

The server prints its destination hash on startup. Use this hash
to connect from C++ clients.

Press Ctrl+C to stop the server.
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
resource_data = None


def generate_test_data(size=1024):
    """Generate predictable test data."""
    # Create a repeating pattern for easy verification
    pattern = b"HELLO_RETICULUM_RESOURCE_TEST_DATA_"
    repeat_count = (size // len(pattern)) + 1
    data = (pattern * repeat_count)[:size]
    return data


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
    elif resource.status == RNS.Resource.FAILED:
        print(f"  Transfer FAILED!")
    print(f"  Transfer size: {resource.total_size} bytes")


def send_resource(link, data):
    """Send a resource over the link."""
    print(f"\n[SENDING RESOURCE]")
    print(f"  Data size: {len(data)} bytes")
    print(f"  Data (first 50 bytes): {data[:50]}")

    try:
        resource = RNS.Resource(
            data,
            link,
            callback=resource_concluded
        )
        print(f"  Resource hash: {resource.hash.hex()}")
        print(f"  Resource created and advertised")
        return resource
    except Exception as e:
        print(f"  Failed to create resource: {e}")
        import traceback
        traceback.print_exc()
        return None


def link_established(link):
    """Called when a new link is established."""
    global resource_data

    print(f"\n{'='*60}")
    print(f"LINK ESTABLISHED")
    print(f"  Link ID: {link.link_id.hex()}")
    print(f"  Status: {link.status}")
    print(f"  MDU: {link.mdu}")
    print(f"{'='*60}\n")

    active_links.append(link)

    # Set up link closed callback
    link.set_link_closed_callback(link_closed)

    # Accept all incoming resources
    link.set_resource_strategy(RNS.Link.ACCEPT_ALL)

    # Wait a moment for link to fully stabilize
    time.sleep(0.5)

    # Send the resource to the client
    print("Sending test resource to client...")
    send_resource(link, resource_data)


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


def setup_server(config_path=None, data_size=1024):
    """Set up the RNS server."""
    global server_destination, resource_data

    # Generate test data
    resource_data = generate_test_data(data_size)
    print(f"Generated {len(resource_data)} bytes of test data")

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
    print(f"RESOURCE SERVER READY")
    print(f"{'='*60}")
    print(f"  App name: {APP_NAME}")
    print(f"  Aspect: {ASPECT}")
    print(f"  Destination hash: {server_destination.hash.hex()}")
    print(f"  Resource size: {len(resource_data)} bytes")
    print(f"{'='*60}")
    print(f"\nUse this destination hash to connect from C++ client.")
    print(f"When a link is established, the server will send a resource.\n")

    # Announce the destination
    server_destination.announce()
    print("Destination announced on network.\n")

    return reticulum


def main():
    parser = argparse.ArgumentParser(description="microReticulum Resource Test Server")
    parser.add_argument("-c", "--config",
                        help="Path to Reticulum config directory",
                        default=None)
    parser.add_argument("-s", "--size",
                        help="Size of test data in bytes (default: 1024)",
                        type=int,
                        default=1024)
    parser.add_argument("-v", "--verbose",
                        help="Increase output verbosity",
                        action="store_true")
    args = parser.parse_args()

    if args.verbose:
        RNS.loglevel = RNS.LOG_DEBUG

    try:
        reticulum = setup_server(args.config, args.size)

        # Main loop - just keep running
        while True:
            time.sleep(1)

    except KeyboardInterrupt:
        print("\n\nShutting down server...")

        # Clean up active links
        for link in active_links:
            try:
                link.teardown()
            except:
                pass

        print("Server stopped.")
        sys.exit(0)


if __name__ == "__main__":
    main()
