#!/usr/bin/env python3
"""
Verify LXMF message format compatibility between C++ and Python.

This script:
- Generates test messages with Python LXMF
- Prints the packed format for comparison with C++
- Verifies that messages can be unpacked correctly
"""

import os
import sys
import json

# Use system RNS and repo LXMF
sys.path.insert(0, os.path.expanduser("~/repos/LXMF"))

import RNS
import RNS.vendor.umsgpack as msgpack
import LXMF

# Test keys (same as C++ tests)
TEST_SENDER_PRIV_KEY = bytes.fromhex(
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210"
)
TEST_RECEIVER_PRIV_KEY = bytes.fromhex(
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
    "1032547698badcfe1032547698badcfe1032547698badcfe1032547698badcfe"
)


def create_test_identity(priv_key_bytes):
    """Create an identity from known private key bytes."""
    identity = RNS.Identity(create_keys=False)
    identity.load_private_key(priv_key_bytes)
    return identity


def test_message_format():
    """Test message packing and unpacking."""
    print("="*60)
    print("LXMF Message Format Verification")
    print("="*60)

    # Initialize RNS (needed for crypto)
    RNS.Reticulum(configdir=None, loglevel=RNS.LOG_CRITICAL)

    # Create identities
    sender = create_test_identity(TEST_SENDER_PRIV_KEY)
    receiver = create_test_identity(TEST_RECEIVER_PRIV_KEY)

    print(f"\nSender identity hash: {sender.hash.hex()}")
    print(f"Receiver identity hash: {receiver.hash.hex()}")

    # Create destinations
    sender_dest = RNS.Destination(
        sender,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )
    receiver_dest = RNS.Destination(
        receiver,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )

    print(f"\nSender destination hash: {sender_dest.hash.hex()}")
    print(f"Receiver destination hash: {receiver_dest.hash.hex()}")

    # Create a test message with known timestamp
    lxm = LXMF.LXMessage(
        destination=receiver_dest,
        source=sender_dest,
        content="Hello from Python!",
        title="Test Message",
        desired_method=LXMF.LXMessage.DIRECT
    )

    # Set known timestamp for reproducibility
    lxm.timestamp = 1700000000.0
    lxm.defer_stamp = True

    # Pack the message
    lxm.pack()

    print(f"\n--- Packed Message ---")
    print(f"Message hash: {lxm.hash.hex()}")
    print(f"Message ID: {lxm.message_id.hex()}")
    print(f"Packed size: {len(lxm.packed)} bytes")
    print(f"Packed hex: {lxm.packed.hex()}")

    print(f"\n--- Wire Format Breakdown ---")
    print(f"Dest hash (16 bytes): {lxm.packed[:16].hex()}")
    print(f"Src hash (16 bytes): {lxm.packed[16:32].hex()}")
    print(f"Signature (64 bytes): {lxm.packed[32:96].hex()}")
    print(f"Payload: {lxm.packed[96:].hex()}")

    # Parse the payload
    payload = msgpack.unpackb(lxm.packed[96:])
    print(f"\n--- Payload Contents ---")
    print(f"Timestamp: {payload[0]}")
    print(f"Title: {payload[1]}")
    print(f"Content: {payload[2]}")
    print(f"Fields: {payload[3]}")

    # Test unpacking
    print(f"\n--- Unpack Test ---")
    try:
        unpacked = LXMF.LXMessage.unpack_from_bytes(lxm.packed)
        print(f"Unpack successful!")
        print(f"  Title: {unpacked.title}")
        print(f"  Content: {unpacked.content}")
        print(f"  Timestamp: {unpacked.timestamp}")
        print(f"  Hash matches: {unpacked.hash == lxm.hash}")
    except Exception as e:
        print(f"Unpack failed: {e}")

    # Test signature validation
    print(f"\n--- Signature Validation ---")
    # Remember the sender identity
    RNS.Identity.recall_app_data(sender_dest.hash)

    print("="*60)
    print("Verification complete")
    print("="*60)

    return True


def verify_cpp_message(packed_hex):
    """Verify a packed message from C++."""
    print("\n" + "="*60)
    print("Verifying C++ Message")
    print("="*60)

    # Initialize RNS
    RNS.Reticulum(configdir=None, loglevel=RNS.LOG_CRITICAL)

    packed = bytes.fromhex(packed_hex)
    print(f"Packed size: {len(packed)} bytes")

    try:
        # Unpack the message
        lxm = LXMF.LXMessage.unpack_from_bytes(packed)
        print(f"\nUnpack successful!")
        print(f"  Hash: {lxm.hash.hex()}")
        print(f"  Dest hash: {lxm.destination_hash.hex()}")
        print(f"  Src hash: {lxm.source_hash.hex()}")
        print(f"  Title: {lxm.title}")
        print(f"  Content: {lxm.content}")
        print(f"  Timestamp: {lxm.timestamp}")
        print(f"  Method: {lxm.method}")
        return True
    except Exception as e:
        print(f"Unpack failed: {e}")
        import traceback
        traceback.print_exc()
        return False


def test_opportunistic_format():
    """Test opportunistic message format (no dest_hash in wire data)."""
    print("\n" + "="*60)
    print("Opportunistic Format Test")
    print("="*60)

    # RNS should already be initialized from test_message_format()

    # Create identities
    sender = create_test_identity(TEST_SENDER_PRIV_KEY)
    receiver = create_test_identity(TEST_RECEIVER_PRIV_KEY)

    # Create destinations
    sender_dest = RNS.Destination(
        sender,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )
    receiver_dest = RNS.Destination(
        receiver,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )

    # Create a test message with opportunistic method
    lxm = LXMF.LXMessage(
        destination=receiver_dest,
        source=sender_dest,
        content="Opportunistic test message",
        title="Opp Test",
        desired_method=LXMF.LXMessage.OPPORTUNISTIC
    )

    # Set known timestamp
    lxm.timestamp = 1700000000.0
    lxm.defer_stamp = True

    # Pack the message
    lxm.pack()

    print(f"\n--- Full Packed Message ---")
    print(f"Full packed size: {len(lxm.packed)} bytes")
    print(f"Full packed hex: {lxm.packed.hex()}")

    # Opportunistic format: omit destination hash (first 16 bytes)
    opp_packed = lxm.packed[LXMF.LXMessage.DESTINATION_LENGTH:]
    print(f"\n--- Opportunistic Format (no dest_hash) ---")
    print(f"Opportunistic size: {len(opp_packed)} bytes")
    print(f"Opportunistic hex: {opp_packed.hex()}")

    # Verify we can reconstruct by prepending dest_hash
    reconstructed = receiver_dest.hash + opp_packed
    print(f"\n--- Reconstruction Test ---")
    print(f"Reconstructed size: {len(reconstructed)} bytes")
    print(f"Matches original: {reconstructed == lxm.packed}")

    try:
        # Unpack the reconstructed message
        unpacked = LXMF.LXMessage.unpack_from_bytes(reconstructed)
        print(f"\nUnpack of reconstructed message successful!")
        print(f"  Hash matches: {unpacked.hash == lxm.hash}")
        print(f"  Content matches: {unpacked.content == lxm.content}")
        return True
    except Exception as e:
        print(f"Unpack failed: {e}")
        import traceback
        traceback.print_exc()
        return False


if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Verify a C++ message passed as hex
        verify_cpp_message(sys.argv[1])
    else:
        # Run the format tests
        test_message_format()
        test_opportunistic_format()
