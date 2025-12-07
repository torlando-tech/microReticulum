#!/usr/bin/env python3
"""
Python LXMF server for microReticulum interop testing.

This server:
- Creates an LXMF delivery destination
- Receives messages from C++ clients
- Sends test messages to known destinations
- Logs all events for debugging

Usage:
    python lxmf_server.py [--send-to HASH]

Press Ctrl+C to stop the server.
"""

import os
import sys
import time
import argparse

# Use system RNS and repo LXMF
sys.path.insert(0, os.path.expanduser("~/repos/LXMF"))

import RNS
import LXMF

# Server state
router = None
local_lxmf_destination = None
messages_received = []


def message_received(message):
    """Called when an LXMF message is received."""
    print(f"\n{'='*60}")
    print(f"MESSAGE RECEIVED")
    print(f"  Hash: {message.hash.hex()}")
    print(f"  From: {message.source_hash.hex()}")
    print(f"  To:   {message.destination_hash.hex()}")
    print(f"  Title: {message.title.decode('utf-8') if message.title else '(none)'}")
    print(f"  Content: {message.content.decode('utf-8') if message.content else '(none)'}")
    print(f"  Method: {message.method}")
    print(f"  Timestamp: {message.timestamp}")
    if message.fields:
        print(f"  Fields: {message.fields}")
    print(f"{'='*60}\n")

    messages_received.append(message)

    # Send acknowledgment back (if we know the source)
    # This would require establishing a link back to the source


def send_test_message(dest_hash_hex):
    """Send a test message to the specified destination hash."""
    global router, local_lxmf_destination

    try:
        dest_hash = bytes.fromhex(dest_hash_hex)
    except ValueError:
        print(f"Invalid destination hash: {dest_hash_hex}")
        return False

    # Check if we know the identity for this destination
    identity = RNS.Identity.recall(dest_hash)
    if not identity:
        print(f"Unknown identity for {dest_hash_hex}, requesting path...")
        RNS.Transport.request_path(dest_hash)
        time.sleep(2)
        identity = RNS.Identity.recall(dest_hash)
        if not identity:
            print(f"Still unknown, cannot send message")
            return False

    # Create destination
    dest = RNS.Destination(
        identity,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )

    # Create message
    lxm = LXMF.LXMessage(
        dest,
        local_lxmf_destination,
        "Hello from Python LXMF!",
        "Test Message",
        desired_method=LXMF.LXMessage.DIRECT
    )

    # Register for delivery notification
    lxm.register_delivery_callback(delivery_callback)
    lxm.register_failed_callback(failed_callback)

    # Send via router
    router.handle_outbound(lxm)

    print(f"Message queued for delivery to {dest_hash_hex}")
    print(f"  Hash: {lxm.hash.hex() if lxm.hash else '(not packed yet)'}")

    return True


def delivery_callback(message):
    """Called when message is delivered."""
    print(f"\n[DELIVERED] Message {message.hash.hex()} delivered successfully")


def failed_callback(message):
    """Called when message delivery fails."""
    print(f"\n[FAILED] Message delivery failed: {message.hash.hex() if message.hash else 'unknown'}")


class LXMFAnnounceHandler:
    """Announce handler for LXMF destinations."""

    def __init__(self, auto_reply=False):
        self.aspect_filter = "lxmf.delivery"
        self.auto_reply = auto_reply

    def received_announce(self, destination_hash, announced_identity, app_data):
        """Called when an LXMF announce is received."""
        print(f"\n[ANNOUNCE] LXMF destination: {destination_hash.hex()}")
        if app_data:
            try:
                import RNS.vendor.umsgpack as msgpack
                data = msgpack.unpackb(app_data)
                if isinstance(data, list) and len(data) >= 2:
                    display_name = data[0].decode('utf-8') if isinstance(data[0], bytes) else data[0]
                    stamp_cost = data[1]
                    print(f"  Display Name: {display_name}")
                    print(f"  Stamp Cost: {stamp_cost}")
            except Exception as e:
                print(f"  App Data (raw): {app_data.hex()}")

        # Auto-reply with a test message after receiving announce
        if self.auto_reply:
            print(f"  Sending test message to {destination_hash.hex()}...")
            time.sleep(1)  # Small delay to let things settle
            send_test_message(destination_hash.hex())


def main():
    global router, local_lxmf_destination

    parser = argparse.ArgumentParser(description="LXMF test server")
    parser.add_argument("--send-to", help="Destination hash to send test message to")
    parser.add_argument("--config", default=None, help="RNS config directory")
    parser.add_argument("--auto-reply", action="store_true", help="Automatically send test message when receiving an announce")
    args = parser.parse_args()

    # Initialize Reticulum
    print("Initializing Reticulum...")
    reticulum = RNS.Reticulum(configdir=args.config)

    # Create identity
    identity_path = os.path.expanduser("~/.lxmf_test_identity")
    if os.path.exists(identity_path):
        identity = RNS.Identity.from_file(identity_path)
        print(f"Loaded existing identity: {identity.hash.hex()}")
    else:
        identity = RNS.Identity()
        identity.to_file(identity_path)
        print(f"Created new identity: {identity.hash.hex()}")

    # Create LXMF router
    router = LXMF.LXMRouter(identity=identity, storagepath="~/.lxmf_test_storage")

    # Register delivery identity
    local_lxmf_destination = router.register_delivery_identity(
        identity,
        display_name="Python LXMF Test Server"
    )

    # Register message callback
    router.register_delivery_callback(message_received)

    # Register announce handler
    RNS.Transport.register_announce_handler(LXMFAnnounceHandler(auto_reply=args.auto_reply))

    print(f"\n{'='*60}")
    print(f"LXMF Server Started")
    print(f"  Identity Hash: {identity.hash.hex()}")
    print(f"  Destination Hash: {local_lxmf_destination.hash.hex()}")
    print(f"{'='*60}\n")

    # Announce ourselves
    router.announce(local_lxmf_destination.hash)
    print("Announced delivery destination")

    # If send-to specified, send a test message
    if args.send_to:
        print(f"\nWaiting 3 seconds for network discovery...")
        time.sleep(3)
        send_test_message(args.send_to)

    # Main loop
    print("\nServer running. Press Ctrl+C to stop.\n")
    try:
        while True:
            time.sleep(1)

            # Print status periodically
            # (router handles its own processing internally)

    except KeyboardInterrupt:
        print("\n\nShutting down...")

    print(f"\nMessages received: {len(messages_received)}")
    for i, msg in enumerate(messages_received):
        print(f"  {i+1}. From {msg.source_hash.hex()[:16]}... : {msg.title}")


if __name__ == "__main__":
    main()
