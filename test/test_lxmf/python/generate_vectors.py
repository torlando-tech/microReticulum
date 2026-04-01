#!/usr/bin/env python3
"""
Generate LXMF test vectors for C++ interoperability testing.

Produces:
  /tmp/lxmf_test_vector.json       - single basic vector (consumed by testPythonInterop)
  /tmp/lxmf_test_vectors_full.json - all test vectors (consumed by expanded interop tests)

Usage:
    python3 generate_vectors.py
"""

import json
import sys
import time

try:
    import RNS
    import LXMF
except ImportError:
    print("Error: RNS and LXMF required. Run: pip install rns lxmf", file=sys.stderr)
    sys.exit(1)


def make_identity():
    """Create a fresh RNS Identity."""
    return RNS.Identity()


def make_destinations(source_id, dest_id):
    """Create source and destination objects (OUT direction to avoid Transport init)."""
    source = RNS.Destination(source_id, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
    dest = RNS.Destination(dest_id, RNS.Destination.OUT, RNS.Destination.SINGLE, "lxmf", "delivery")
    return source, dest


def identity_to_dict(identity):
    """Extract identity key material as hex strings."""
    return {
        "prv": identity.get_private_key().hex(),
        "pub": identity.get_public_key().hex(),
    }


def message_to_vector(msg, source_id, dest_id, name, extra=None):
    """Convert a packed LXMessage to a test vector dict."""
    vector = {
        "name": name,
        "source_identity_prv": source_id.get_private_key().hex(),
        "source_identity_pub": source_id.get_public_key().hex(),
        "dest_identity_pub": dest_id.get_public_key().hex(),
        "source_hash": msg.source_hash.hex(),
        "dest_hash": msg.destination_hash.hex(),
        "packed": msg.packed.hex(),
        "content": msg.content.decode("utf-8", errors="replace") if msg.content else "",
        "title": msg.title.decode("utf-8", errors="replace") if msg.title else "",
        "message_hash": msg.hash.hex(),
        "timestamp": msg.timestamp,
        "has_stamp": msg.stamp is not None and len(msg.stamp) > 0,
        "stamp": msg.stamp.hex() if msg.stamp and len(msg.stamp) > 0 else "",
    }

    # Encode fields — Python LXMF uses integer keys
    if msg.fields:
        fields_dict = {}
        for k, v in msg.fields.items():
            key_hex = k.to_bytes(1, "big").hex() if isinstance(k, int) else k.hex()
            val_hex = v.hex() if isinstance(v, (bytes, bytearray)) else str(v).encode().hex()
            fields_dict[key_hex] = val_hex
        vector["fields"] = fields_dict
    else:
        vector["fields"] = {}

    if extra:
        vector.update(extra)

    return vector


def generate_basic():
    """Basic message — simple title + content, DIRECT method."""
    source_id = make_identity()
    dest_id = make_identity()
    source, dest = make_destinations(source_id, dest_id)

    msg = LXMF.LXMessage(
        dest, source,
        "Hello from Python LXMF!",
        "Test Message",
        desired_method=LXMF.LXMessage.DIRECT,
    )
    msg.pack()

    return message_to_vector(msg, source_id, dest_id, "basic")


def generate_empty():
    """Empty content and title."""
    source_id = make_identity()
    dest_id = make_identity()
    source, dest = make_destinations(source_id, dest_id)

    msg = LXMF.LXMessage(
        dest, source,
        "",
        "",
        desired_method=LXMF.LXMessage.DIRECT,
    )
    msg.pack()

    return message_to_vector(msg, source_id, dest_id, "empty")


def generate_with_fields():
    """Message with integer-keyed fields (standard LXMF format)."""
    source_id = make_identity()
    dest_id = make_identity()
    source, dest = make_destinations(source_id, dest_id)

    fields = {
        0x01: b"field_value_one",
        0x02: b"field_value_two",
        0x03: b"field_value_three",
    }

    msg = LXMF.LXMessage(
        dest, source,
        "Content with fields",
        "Fields Test",
        fields=fields,
        desired_method=LXMF.LXMessage.DIRECT,
    )
    msg.pack()

    return message_to_vector(msg, source_id, dest_id, "with_fields")


def generate_large():
    """Large content (>319 bytes, would be RESOURCE representation)."""
    source_id = make_identity()
    dest_id = make_identity()
    source, dest = make_destinations(source_id, dest_id)

    # 500 bytes of content
    content = "A" * 500
    msg = LXMF.LXMessage(
        dest, source,
        content,
        "Large Message",
        desired_method=LXMF.LXMessage.DIRECT,
    )
    msg.pack()

    return message_to_vector(msg, source_id, dest_id, "large")


def generate_unicode():
    """Unicode content and title."""
    source_id = make_identity()
    dest_id = make_identity()
    source, dest = make_destinations(source_id, dest_id)

    msg = LXMF.LXMessage(
        dest, source,
        "Hello \u4e16\u754c! \U0001f680 Caf\u00e9",
        "\u2603 Unicode Title \U0001f4ac",
        desired_method=LXMF.LXMessage.DIRECT,
    )
    msg.pack()

    return message_to_vector(msg, source_id, dest_id, "unicode")


def generate_with_stamp():
    """Message with a stamp (proof-of-work).

    LXMF stamps are appended as the 5th element of the payload array.
    Hash and signature are computed over the 4-element payload (WITHOUT stamp),
    then the stamp is appended for the wire format.
    """
    from RNS.vendor import umsgpack

    source_id = make_identity()
    dest_id = make_identity()
    source, dest = make_destinations(source_id, dest_id)

    msg = LXMF.LXMessage(
        dest, source,
        "Stamped message",
        "Stamp Test",
        desired_method=LXMF.LXMessage.DIRECT,
        stamp_cost=8,  # Low cost for fast test generation
    )
    msg.pack()

    # Generate stamp (PoW against the message hash, which is over 4-element payload)
    stamp = msg.get_stamp(timeout=30)

    # Hash and signature from pack() are already correct (over 4-element payload).
    # Just append stamp as 5th element to the wire payload.
    dest_hash = msg.packed[:16]
    src_hash = msg.packed[16:32]
    original_sig = msg.packed[32:96]
    payload_bytes = msg.packed[96:]

    # Append stamp as 5th element
    payload_arr = umsgpack.unpackb(payload_bytes)
    payload_arr.append(stamp)
    new_payload_bytes = umsgpack.packb(payload_arr)

    # Assemble with original signature (computed over 4-element payload)
    new_packed = dest_hash + src_hash + original_sig + new_payload_bytes

    vector = {
        "name": "with_stamp",
        "source_identity_prv": source_id.get_private_key().hex(),
        "source_identity_pub": source_id.get_public_key().hex(),
        "dest_identity_pub": dest_id.get_public_key().hex(),
        "source_hash": src_hash.hex(),
        "dest_hash": dest_hash.hex(),
        "packed": new_packed.hex(),
        "content": "Stamped message",
        "title": "Stamp Test",
        "message_hash": msg.hash.hex(),  # Hash over 4-element payload
        "timestamp": msg.timestamp,
        "has_stamp": True,
        "stamp": stamp.hex(),
        "fields": {},
    }

    return vector


def main():
    print(f"Generating LXMF test vectors (RNS {RNS.__version__}, LXMF {LXMF.__version__})",
          file=sys.stderr)

    vectors = []

    # Generate all test vectors
    print("  Generating basic...", file=sys.stderr)
    basic = generate_basic()
    vectors.append(basic)

    print("  Generating empty...", file=sys.stderr)
    vectors.append(generate_empty())

    print("  Generating with_fields...", file=sys.stderr)
    vectors.append(generate_with_fields())

    print("  Generating large...", file=sys.stderr)
    vectors.append(generate_large())

    print("  Generating unicode...", file=sys.stderr)
    vectors.append(generate_unicode())

    print("  Generating with_stamp (PoW, may take a moment)...", file=sys.stderr)
    vectors.append(generate_with_stamp())

    # Write single vector (backward compatible with existing testPythonInterop)
    single_path = "/tmp/lxmf_test_vector.json"
    with open(single_path, "w") as f:
        json.dump(basic, f, indent=2)
    print(f"  Wrote {single_path}", file=sys.stderr)

    # Write full vectors
    full_path = "/tmp/lxmf_test_vectors_full.json"
    with open(full_path, "w") as f:
        json.dump(vectors, f, indent=2)
    print(f"  Wrote {full_path} ({len(vectors)} vectors)", file=sys.stderr)

    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
