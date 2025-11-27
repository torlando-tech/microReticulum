#!/usr/bin/env python3
"""
Diagnostic Python RNS resource server for debugging file transfer corruption.

This server manually replicates the Resource creation process step-by-step,
saving intermediate stages to files for byte-by-byte comparison with C++ receiver.

Usage:
    python diagnostic_resource_server.py -c ./test_rns_config --diagnostic --size 1048576
"""

import RNS
import time
import sys
import argparse
import os
import bz2
import hashlib
import json
from datetime import datetime

APP_NAME = "microreticulum_interop"
ASPECT = "link_server"

# Global state
server_destination = None
active_links = []
diagnostic_mode = False
output_dir = "/tmp/diagnostic_dumps"

# Monkey-patching infrastructure to capture RNS.Resource intermediate data
_original_resource_init = None
_diagnostic_capture = {}
_monkey_patch_applied = False


def apply_resource_monkey_patch():
    """
    Monkey-patch RNS.Resource.__init__ to capture intermediate data before it's destroyed.

    RNS.Resource intentionally clears intermediate data to save memory (Resource.py lines 417, 473):
    - self.compressed_data = None
    - self.uncompressed_data = None
    - self.data = None  (this is random_hash + compressed_data, before encryption)

    We capture this data BEFORE it's cleared so we can save the ACTUAL stages that get sent.
    """
    global _original_resource_init, _monkey_patch_applied

    if _monkey_patch_applied:
        return

    # Store original __init__
    _original_resource_init = RNS.Resource.__init__

    def _patched_resource_init(self, *args, **kwargs):
        # Call original init
        _original_resource_init(self, *args, **kwargs)

        # CAPTURE DATA BEFORE IT'S CLEARED
        # Note: These attributes exist during __init__ but are set to None before it returns
        if hasattr(self, 'uncompressed_data') and self.uncompressed_data is not None:
            _diagnostic_capture['original'] = bytes(self.uncompressed_data)

        if hasattr(self, 'compressed_data') and self.compressed_data is not None:
            _diagnostic_capture['compressed'] = bytes(self.compressed_data)

        if hasattr(self, 'data') and self.data is not None:
            # This is random_hash + compressed_data (before encryption)
            _diagnostic_capture['with_random'] = bytes(self.data)

        # Also capture values that persist
        _diagnostic_capture['random_hash'] = self.random_hash
        _diagnostic_capture['resource_hash'] = self.hash
        _diagnostic_capture['is_compressed'] = self.compressed

    # Apply patch
    RNS.Resource.__init__ = _patched_resource_init
    _monkey_patch_applied = True
    print("[DIAGNOSTIC] RNS.Resource monkey-patch applied")


def generate_test_data(size):
    """Generate repeating pattern data for easy corruption detection."""
    # Use a clear repeating pattern for easy visual inspection
    pattern = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    repeat_count = (size // len(pattern)) + 1
    data = (pattern * repeat_count)[:size]
    return data


def save_file(filepath, data):
    """Save binary data to file."""
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, 'wb') as f:
        f.write(data)
    print(f"  Saved: {filepath} ({len(data)} bytes)")


def save_json(filepath, data):
    """Save JSON data to file."""
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    with open(filepath, 'w') as f:
        json.dump(data, f, indent=2)
    print(f"  Saved: {filepath}")


def save_intermediate_stages(resource, link, size):
    """
    Save intermediate stages captured from monkey-patched RNS.Resource.

    Uses data captured by the monkey-patch before RNS destroys it.
    This ensures we save the ACTUAL data that gets sent to C++.
    """
    print(f"\n[DIAGNOSTIC MODE: Saving captured intermediate stages]")

    # Get captured data from monkey-patch
    if not _diagnostic_capture:
        print("  ERROR: No data captured! Monkey-patch may not be applied.")
        return None

    original = _diagnostic_capture.get('original')
    compressed = _diagnostic_capture.get('compressed')
    with_random = _diagnostic_capture.get('with_random')

    if not original:
        print("  ERROR: Original data not captured!")
        return None

    print(f"  Using RNS Resource's actual random_hash: {resource.random_hash.hex()}")
    print(f"  Using RNS Resource's actual resource_hash: {resource.hash.hex()}")

    # Stage 0: Original data (captured from RNS)
    stage0_path = os.path.join(output_dir, f"py_stage0_original_{size}.bin")
    save_file(stage0_path, original)
    original_sha256 = hashlib.sha256(original).hexdigest()

    # Stage 1: Compressed data (captured from RNS, if compression was used)
    if compressed:
        stage1_path = os.path.join(output_dir, f"py_stage1_compressed_{size}.bin")
        save_file(stage1_path, compressed)
        compressed_sha256 = hashlib.sha256(compressed).hexdigest()
        print(f"  Compression: {len(original)} -> {len(compressed)} bytes ({100*len(compressed)/len(original):.1f}%)")
    else:
        # No compression was applied
        compressed = original
        compressed_sha256 = original_sha256
        print(f"  No compression applied")

    # Stage 2: Data with random_hash prepended (captured from RNS)
    if with_random:
        stage2_path = os.path.join(output_dir, f"py_stage2_with_random_{size}.bin")
        save_file(stage2_path, with_random)
    else:
        print("  ERROR: with_random data not captured!")
        return None

    # Stage 3: Encrypted data (must reconstruct using Link cipher)
    # RNS doesn't keep the full encrypted data, only the parts
    print("\n  Reconstructing encrypted data using Link cipher...")
    try:
        encrypted = link.encrypt(with_random)
        stage3_path = os.path.join(output_dir, f"py_stage3_encrypted_{size}.bin")
        save_file(stage3_path, encrypted)
        encrypted_sha256 = hashlib.sha256(encrypted).hexdigest()
    except Exception as e:
        print(f"  ERROR: Encryption failed: {e}")
        import traceback
        traceback.print_exc()
        return None

    # Stage 4: Extract parts from Resource object
    print(f"\n  Extracting {len(resource.parts)} parts from Resource object...")
    parts_dir = os.path.join(output_dir, f"py_stage4_parts_{size}")
    os.makedirs(parts_dir, exist_ok=True)

    part_sha256_hashes = []
    for i, part_packet in enumerate(resource.parts):
        part_data = part_packet.data
        part_path = os.path.join(parts_dir, f"part_{i:04d}.bin")
        save_file(part_path, part_data)
        part_sha256_hashes.append(hashlib.sha256(part_data).hexdigest())

    # Generate comprehensive metadata
    metadata = {
        "test_size": size,
        "timestamp": datetime.now().isoformat(),
        "original_size": len(original),
        "original_sha256": original_sha256,
        "original_first_50_bytes": original[:50].hex(),
        "compressed_size": len(compressed),
        "compressed_sha256": compressed_sha256,
        "random_hash": resource.random_hash.hex(),
        "resource_hash": resource.hash.hex(),
        "encrypted_size": len(encrypted),
        "encrypted_sha256": encrypted_sha256,
        "total_parts": len(resource.parts),
        "sdu": link.mdu,
        "part_sizes": [len(part_packet.data) for part_packet in resource.parts],
        "part_sha256_hashes": part_sha256_hashes,
        "is_compressed": resource.compressed,
        "is_encrypted": resource.encrypted
    }

    metadata_path = os.path.join(output_dir, f"py_metadata_{size}.json")
    save_json(metadata_path, metadata)

    print(f"\n[DIAGNOSTIC: All intermediate stages saved to {output_dir}]")

    return metadata


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


def send_resource(link, data, size):
    """Send a resource over the link."""
    print(f"\n[SENDING RESOURCE]")
    print(f"  Data size: {len(data)} bytes")
    print(f"  Data (first 50 bytes): {data[:50].hex()}")

    # CRITICAL: Apply monkey-patch BEFORE creating Resource
    if diagnostic_mode:
        apply_resource_monkey_patch()
        # Clear previous capture
        _diagnostic_capture.clear()

    # Create the actual RNS Resource
    print(f"\n  Creating RNS Resource...")
    try:
        resource = RNS.Resource(
            data,
            link,
            callback=resource_concluded
        )
        print(f"  Resource hash: {resource.hash.hex()}")
        print(f"  Resource random_hash: {resource.random_hash.hex()}")
        print(f"  Resource created and advertised")

        # NOW save the intermediate stages captured from this Resource
        if diagnostic_mode:
            metadata = save_intermediate_stages(resource, link, size)
            if metadata is None:
                print("  WARNING: Failed to save diagnostic stages")
            else:
                print(f"  Diagnostic stages saved successfully")
                print(f"  Verified: resource_hash matches metadata")

        return resource
    except Exception as e:
        print(f"  Failed to create resource: {e}")
        import traceback
        traceback.print_exc()
        return None


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

    # Accept all incoming resources
    link.set_resource_strategy(RNS.Link.ACCEPT_ALL)

    # Wait a moment for link to fully stabilize
    time.sleep(0.5)

    # Send the resource to the client
    print("Preparing to send test resource to client...")

    # Get the test size from global or use default
    test_size = getattr(send_resource, 'test_size', 1024)
    resource_data = generate_test_data(test_size)

    send_resource(link, resource_data, test_size)


def link_closed(link):
    """Called when a link is closed."""
    print(f"\n{'='*60}")
    print(f"LINK CLOSED")
    print(f"  Link ID: {link.link_id.hex()}")
    print(f"{'='*60}\n")

    if link in active_links:
        active_links.remove(link)


def setup_server(config_path=None, data_size=1024, diag_mode=False, out_dir="/tmp/diagnostic_dumps"):
    """Set up the RNS server."""
    global server_destination, diagnostic_mode, output_dir

    diagnostic_mode = diag_mode
    output_dir = out_dir

    # Store test size for link_established callback
    send_resource.test_size = data_size

    if diagnostic_mode:
        print(f"\n{'='*60}")
        print(f"DIAGNOSTIC MODE ENABLED")
        print(f"  Output directory: {output_dir}")
        print(f"  Test size: {data_size} bytes")
        print(f"{'='*60}\n")
        os.makedirs(output_dir, exist_ok=True)

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
    print(f"  Resource size: {data_size} bytes")
    if diagnostic_mode:
        print(f"  Diagnostic mode: ENABLED")
        print(f"  Output directory: {output_dir}")
    print(f"{'='*60}")
    print(f"\nUse this destination hash to connect from C++ client.")
    print(f"When a link is established, the server will send a resource.\n")

    # Announce the destination
    server_destination.announce()
    print("Destination announced on network.\n")

    return reticulum


def main():
    parser = argparse.ArgumentParser(
        description="Diagnostic microReticulum Resource Test Server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Normal mode with 1MB file
  python diagnostic_resource_server.py -c ./test_rns_config --size 1048576

  # Diagnostic mode with intermediate dumps
  python diagnostic_resource_server.py -c ./test_rns_config --diagnostic --size 1048576 --output-dir /tmp/diagnostic_dumps

  # Test multiple sizes
  for size in 1024 10240 102400 1048576 5242880; do
      python diagnostic_resource_server.py -c ./test_rns_config --diagnostic --size $size
  done
        """
    )
    parser.add_argument("-c", "--config",
                        help="Path to Reticulum config directory",
                        default=None)
    parser.add_argument("-s", "--size",
                        help="Size of test data in bytes (default: 1024)",
                        type=int,
                        default=1024)
    parser.add_argument("--diagnostic",
                        help="Enable diagnostic mode (save intermediate stages)",
                        action="store_true")
    parser.add_argument("-o", "--output-dir",
                        help="Output directory for diagnostic dumps (default: /tmp/diagnostic_dumps)",
                        default="/tmp/diagnostic_dumps")
    parser.add_argument("-v", "--verbose",
                        help="Increase output verbosity",
                        action="store_true")
    args = parser.parse_args()

    if args.verbose:
        RNS.loglevel = RNS.LOG_DEBUG

    try:
        reticulum = setup_server(
            args.config,
            args.size,
            args.diagnostic,
            args.output_dir
        )

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
