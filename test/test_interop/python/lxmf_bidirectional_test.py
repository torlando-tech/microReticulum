#!/usr/bin/env python3
"""
Bidirectional LXMF test between Python and C++.

This script:
1. Starts C++ LXMF client (which announces)
2. Waits to receive the C++ announce
3. Sends an LXMF message to the C++ client
4. Verifies message delivery
"""

import os
import sys
import time
import subprocess
import threading

# Use system RNS and repo LXMF
sys.path.insert(0, os.path.expanduser("~/repos/LXMF"))

import RNS
import LXMF

# Configuration
CONFIG_DIR = os.path.dirname(os.path.abspath(__file__)) + "/test_rns_config"
CPP_CLIENT_PATH = os.path.dirname(os.path.abspath(__file__)) + "/../../../examples/lxmf_client/.pio/build/native/program"

# State
cpp_destination_hash = None
announce_received = threading.Event()
message_delivered = threading.Event()


class LXMFAnnounceHandler:
    """Announce handler that captures C++ destination."""

    def __init__(self):
        self.aspect_filter = "lxmf.delivery"

    def received_announce(self, destination_hash, announced_identity, app_data):
        global cpp_destination_hash
        print(f"\n[ANNOUNCE] Received LXMF announce: {destination_hash.hex()}")
        if app_data:
            try:
                import RNS.vendor.umsgpack as msgpack
                data = msgpack.unpackb(app_data)
                if isinstance(data, list) and len(data) >= 1:
                    display_name = data[0].decode('utf-8') if isinstance(data[0], bytes) else data[0]
                    print(f"  Display Name: {display_name}")
                    if "microReticulum" in display_name:
                        cpp_destination_hash = destination_hash
                        announce_received.set()
                        print(f"  -> This is the C++ client!")
            except Exception as e:
                print(f"  App Data parse error: {e}")


def delivery_callback(message):
    """Called when message is delivered."""
    print(f"\n[DELIVERED] Message {message.hash.hex()} delivered!")
    message_delivered.set()


def failed_callback(message):
    """Called when message delivery fails."""
    print(f"\n[FAILED] Message delivery failed")


def main():
    global cpp_destination_hash

    print("=" * 60)
    print("LXMF Bidirectional Test: Python -> C++")
    print("=" * 60)

    # Initialize Reticulum with TCP interface
    print("\n[1] Initializing Reticulum...")
    reticulum = RNS.Reticulum(configdir=CONFIG_DIR)

    # Create identity
    identity_path = os.path.expanduser("~/.lxmf_test_identity")
    if os.path.exists(identity_path):
        identity = RNS.Identity.from_file(identity_path)
        print(f"    Loaded identity: {identity.hash.hex()}")
    else:
        identity = RNS.Identity()
        identity.to_file(identity_path)
        print(f"    Created identity: {identity.hash.hex()}")

    # Create LXMF router
    print("\n[2] Creating LXMF router...")
    router = LXMF.LXMRouter(identity=identity, storagepath="/tmp/lxmf_bidir_test")
    local_dest = router.register_delivery_identity(identity, "Python Test Sender")
    print(f"    Local destination: {local_dest.hash.hex()}")

    # Register announce handler
    RNS.Transport.register_announce_handler(LXMFAnnounceHandler())

    # Start C++ client in background
    print("\n[3] Starting C++ LXMF client...")
    cpp_process = subprocess.Popen(
        [CPP_CLIENT_PATH, "--tcp"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )

    # Read C++ output in a thread
    def read_cpp_output():
        for line in cpp_process.stdout:
            print(f"    [C++] {line.rstrip()}")

    cpp_thread = threading.Thread(target=read_cpp_output, daemon=True)
    cpp_thread.start()

    # Wait for C++ announce
    print("\n[4] Waiting for C++ announce...")
    if not announce_received.wait(timeout=15):
        print("    TIMEOUT: No C++ announce received!")
        cpp_process.terminate()
        return 1

    print(f"    C++ destination: {cpp_destination_hash.hex()}")

    # Give some time for announce to propagate
    time.sleep(2)

    # Send message to C++
    print("\n[5] Sending LXMF message to C++...")

    # Recall identity
    target_identity = RNS.Identity.recall(cpp_destination_hash)
    if not target_identity:
        print("    ERROR: Could not recall C++ identity")
        cpp_process.terminate()
        return 1

    print(f"    Target identity found: {target_identity.hash.hex()}")

    # Create destination
    dest = RNS.Destination(
        target_identity,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )

    # Create and send message
    lxm = LXMF.LXMessage(
        dest,
        local_dest,
        "Hello from Python LXMF!",
        "Test Message",
        desired_method=LXMF.LXMessage.DIRECT
    )
    lxm.register_delivery_callback(delivery_callback)
    lxm.register_failed_callback(failed_callback)

    router.handle_outbound(lxm)
    print(f"    Message queued: {lxm.hash.hex() if lxm.hash else '(pending)'}")

    # Wait for delivery
    print("\n[6] Waiting for message delivery...")
    if message_delivered.wait(timeout=30):
        print("\n" + "=" * 60)
        print("SUCCESS: Message delivered to C++!")
        print("=" * 60)
        result = 0
    else:
        print(f"\n    Message state: {lxm.state}")
        print("\n" + "=" * 60)
        print("TIMEOUT: Message not delivered within 30 seconds")
        print("=" * 60)
        result = 1

    # Cleanup
    print("\n[7] Cleaning up...")
    cpp_process.terminate()
    time.sleep(1)

    return result


if __name__ == "__main__":
    sys.exit(main())
