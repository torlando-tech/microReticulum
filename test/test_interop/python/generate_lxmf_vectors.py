#!/usr/bin/env python3
"""
Generate LXMF test vectors for C++ interoperability testing.

This script creates LXMF messages with known content and outputs them in a format
that can be used to verify the C++ implementation produces identical wire format.

Usage:
    /usr/bin/python generate_lxmf_vectors.py > lxmf_vectors.json
"""

import os
import sys
import json
import time

# Add the LXMF library to path (use installed versions if repo has issues)
sys.path.insert(0, os.path.expanduser("~/repos/LXMF"))
# Use system-installed RNS to avoid development issues
# sys.path.insert(0, os.path.expanduser("~/repos/Reticulum"))

import RNS
import RNS.vendor.umsgpack as msgpack
import LXMF

# Use deterministic keys for testing
# These are well-known test keys - DO NOT USE IN PRODUCTION
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


def generate_vector(name, title, content, fields, timestamp):
    """Generate a test vector with the given parameters."""

    # Create identities
    sender_identity = create_test_identity(TEST_SENDER_PRIV_KEY)
    receiver_identity = create_test_identity(TEST_RECEIVER_PRIV_KEY)

    # Create destinations
    sender_dest = RNS.Destination(
        sender_identity,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )
    receiver_dest = RNS.Destination(
        receiver_identity,
        RNS.Destination.OUT,
        RNS.Destination.SINGLE,
        "lxmf",
        "delivery"
    )

    # Create message
    lxm = LXMF.LXMessage(
        destination=receiver_dest,
        source=sender_dest,
        content=content,
        title=title,
        fields=fields,
        desired_method=LXMF.LXMessage.DIRECT
    )

    # Override timestamp for deterministic output
    lxm.timestamp = timestamp

    # Don't defer stamp (but we're not using stamps for v1)
    lxm.defer_stamp = True

    # Pack the message
    lxm.pack()

    # Extract components for verification
    # Convert content to string/hex for JSON
    if isinstance(content, bytes):
        content_str = None  # Binary content can't be represented as string
        content_hex = content.hex()
    else:
        content_str = content
        content_hex = content.encode("utf-8").hex()

    if isinstance(title, bytes):
        title_str = None
        title_hex = title.hex()
    else:
        title_str = title
        title_hex = title.encode("utf-8").hex()

    vector = {
        "name": name,
        "timestamp": timestamp,
        "title": title_str,
        "title_hex": title_hex,
        "content": content_str,
        "content_hex": content_hex,
        "fields": {str(k): v.hex() if isinstance(v, bytes) else v for k, v in (fields or {}).items()},

        # Identity info
        "sender_public_key": sender_identity.get_public_key().hex(),
        "sender_hash": sender_identity.hash.hex(),
        "receiver_public_key": receiver_identity.get_public_key().hex(),
        "receiver_hash": receiver_identity.hash.hex(),

        # Message components
        "destination_hash": lxm.destination_hash.hex(),
        "source_hash": lxm.source_hash.hex(),
        "signature": lxm.signature.hex(),
        "message_hash": lxm.hash.hex(),
        "message_id": lxm.message_id.hex(),

        # Full packed message
        "packed": lxm.packed.hex(),
        "packed_size": len(lxm.packed),

        # Payload breakdown
        "payload": msgpack.packb([lxm.timestamp, lxm.title, lxm.content, lxm.fields or {}]).hex(),
    }

    return vector


def main():
    # Initialize RNS (needed for crypto)
    # Use in-memory config to avoid touching user's actual RNS config
    RNS.Reticulum(configdir=None, loglevel=RNS.LOG_CRITICAL)

    vectors = []

    # Vector 1: Simple message
    vectors.append(generate_vector(
        name="simple_message",
        title="Hello",
        content="This is a test message",
        fields={},
        timestamp=1700000000.0
    ))

    # Vector 2: Empty title and content
    vectors.append(generate_vector(
        name="empty_message",
        title="",
        content="",
        fields={},
        timestamp=1700000001.0
    ))

    # Vector 3: Message with fields
    vectors.append(generate_vector(
        name="message_with_fields",
        title="Test",
        content="Content with fields",
        fields={
            LXMF.FIELD_RENDERER: bytes([LXMF.RENDERER_MARKDOWN]),
        },
        timestamp=1700000002.0
    ))

    # Vector 4: Unicode content
    vectors.append(generate_vector(
        name="unicode_message",
        title="Unicode Title",
        content="Hello World!",
        fields={},
        timestamp=1700000003.0
    ))

    # Vector 5: Binary content
    vectors.append(generate_vector(
        name="binary_content",
        title="Binary",
        content=bytes([0x00, 0x01, 0x02, 0xff, 0xfe, 0xfd]),
        fields={},
        timestamp=1700000004.0
    ))

    # Vector 6: Maximum single-packet content (for testing size limits)
    # ~295 bytes is the limit for opportunistic, ~319 for link packet
    vectors.append(generate_vector(
        name="near_max_content",
        title="",
        content="X" * 280,  # Near but under the limit
        fields={},
        timestamp=1700000005.0
    ))

    # Output as JSON
    output = {
        "description": "LXMF test vectors for C++ interoperability",
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "constants": {
            "DESTINATION_LENGTH": LXMF.LXMessage.DESTINATION_LENGTH,
            "SIGNATURE_LENGTH": LXMF.LXMessage.SIGNATURE_LENGTH,
            "LXMF_OVERHEAD": LXMF.LXMessage.LXMF_OVERHEAD,
            "ENCRYPTED_PACKET_MAX_CONTENT": LXMF.LXMessage.ENCRYPTED_PACKET_MAX_CONTENT,
            "LINK_PACKET_MAX_CONTENT": LXMF.LXMessage.LINK_PACKET_MAX_CONTENT,
        },
        "vectors": vectors
    }

    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
