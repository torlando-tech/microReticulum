#!/usr/bin/env python3
"""
Persistent Python RNS link server for microReticulum interop testing.

This server:
- Creates an INBOUND SINGLE destination
- Accepts link connections from C++ clients
- Echoes received packets back with "Echo: " prefix
- Logs all events for debugging

Usage:
    python link_server.py

The server prints its destination hash on startup. Use this hash
to connect from C++ clients.

Press Ctrl+C to stop the server.
"""

import RNS
import RNS.Buffer
import time
import sys
import argparse
import uuid
from RNS.vendor import umsgpack

APP_NAME = "microreticulum_interop"
ASPECT = "link_server"

# Define MessageTest class (must match C++ implementation)
class MessageTest(RNS.MessageBase):
    MSGTYPE = 0xabcd

    def __init__(self):
        self.id = ""
        self.data = ""
        self.not_serialized = str(uuid.uuid4())

    def pack(self):
        return umsgpack.packb([self.id, self.data])

    def unpack(self, raw):
        unpacked = umsgpack.unpackb(raw)
        if len(unpacked) >= 2:
            self.id = unpacked[0]
            self.data = unpacked[1]

# Global state
server_destination = None
active_links = []


def link_established(link):
    """Called when a new link is established."""
    print(f"\n{'='*60}")
    print(f"LINK ESTABLISHED")
    print(f"  Link ID: {link.link_id.hex()}")
    print(f"  Status: {link.status}")
    print(f"  MTU: {link.mdu}")
    print(f"{'='*60}\n")

    active_links.append(link)

    # Set up packet callback for this link
    link.set_packet_callback(packet_received)

    # Set up link closed callback
    link.set_link_closed_callback(link_closed)

    # Accept all resources (for future Resource testing)
    link.set_resource_strategy(RNS.Link.ACCEPT_ALL)

    # Set up channel
    channel = link.get_channel()
    channel.register_message_type(MessageTest)

    def handle_channel_message(message):
        if isinstance(message, MessageTest):
            print(f"\n[CHANNEL MESSAGE RECEIVED]")
            print(f"  id: {message.id}")
            print(f"  data: {message.data}")
            print(f"  not_serialized: {message.not_serialized}")

            # Echo back with modified data
            response = MessageTest()
            response.id = message.id  # Preserve ID
            response.data = message.data + " back"  # Modify data
            print(f"\n[CHANNEL MESSAGE SENT]")
            print(f"  id: {response.id}")
            print(f"  data: {response.data}")
            print(f"  not_serialized: {response.not_serialized}")
            channel.send(response)
            return True
        return False

    channel.add_message_handler(handle_channel_message)
    print("Channel configured with MessageTest handler")

    # Set up bidirectional Buffer for stream testing
    # Both sides use stream_id 0 for simplicity
    def buffer_ready_callback(ready_bytes):
        print(f"\n[BUFFER:READY:{ready_bytes}]")
        sys.stdout.flush()

    buffer = RNS.Buffer.create_bidirectional_buffer(
        receive_stream_id=0,
        send_stream_id=0,
        channel=channel,
        ready_callback=buffer_ready_callback
    )

    # Store buffer in link for access in separate thread
    link.buffer = buffer

    # Start a thread to handle buffer echo
    import threading
    def buffer_echo_handler():
        try:
            while link.status == RNS.Link.ACTIVE:
                data = buffer.read(4096)
                if data:
                    print(f"\n[BUFFER:RX:{len(data)} bytes]")
                    try:
                        text = data.decode('utf-8')
                        print(f"[BUFFER:RX_TEXT:{repr(text)}]")
                    except:
                        print(f"[BUFFER:RX_HEX:{data.hex()}]")
                    sys.stdout.flush()

                    # Echo back with modification (matching Python RNS test pattern)
                    if data == b"PING\n":
                        response = b"PONG\n"
                    else:
                        response = data  # Simple echo

                    buffer.write(response)
                    buffer.flush()
                    print(f"[BUFFER:TX:{len(response)} bytes]")
                    sys.stdout.flush()
                else:
                    time.sleep(0.01)
        except Exception as e:
            print(f"[BUFFER:ERROR:{e}]")
            sys.stdout.flush()

    buffer_thread = threading.Thread(target=buffer_echo_handler, daemon=True)
    buffer_thread.start()
    print("Buffer configured for bidirectional echo")


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

    # Try to decode as UTF-8 for display
    try:
        text = data.decode('utf-8')
        print(f"  Data (text): {text}")
    except:
        print(f"  Data (text): <binary data>")

    # Echo the data back with prefix
    echo_data = b"Echo: " + data
    print(f"  Echoing back: {len(echo_data)} bytes")

    try:
        echo_packet = RNS.Packet(link, echo_data)
        echo_packet.send()
        print(f"  Echo sent successfully")
    except Exception as e:
        print(f"  Echo failed: {e}")


def resource_started(resource):
    """Called when a resource transfer starts."""
    print(f"\n[RESOURCE STARTED]")
    print(f"  Size: {resource.total_size} bytes")
    print(f"  Hash: {resource.hash.hex()}")


def resource_concluded(resource):
    """Called when a resource transfer completes."""
    print(f"\n[RESOURCE CONCLUDED]")
    print(f"  Status: {resource.status}")
    if resource.status == RNS.Resource.COMPLETE:
        print(f"  Data length: {len(resource.data)} bytes")
        print(f"  Data (first 100 bytes): {resource.data[:100].hex()}")


def setup_server(config_path=None):
    """Set up the RNS server."""
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
    print(f"SERVER READY")
    print(f"{'='*60}")
    print(f"  App name: {APP_NAME}")
    print(f"  Aspect: {ASPECT}")
    print(f"  Destination hash: {server_destination.hash.hex()}")
    print(f"{'='*60}")
    print(f"\nUse this destination hash to connect from C++ client.")
    print(f"Press Ctrl+C to stop the server.\n")

    # Announce the destination
    server_destination.announce()
    print("Destination announced on network.\n")

    return reticulum


def main():
    parser = argparse.ArgumentParser(description="microReticulum Link Test Server")
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
        reticulum = setup_server(args.config)

        # Main loop - just keep running
        while True:
            time.sleep(1)

            # Periodically print status
            if len(active_links) > 0:
                # Show active link count every 10 seconds
                pass

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
