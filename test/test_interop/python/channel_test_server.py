#!/usr/bin/env python3
"""
Automated Channel test server for microReticulum interoperability testing.

This server outputs structured markers for automated test parsing:
- [TEST:test_name:START|PASS|FAIL:reason]
- [WIRE:TX|RX:hex_bytes]
- [DATA:field=value]

Usage:
    python channel_test_server.py -c test_rns_config
"""

import RNS
import time
import sys
import argparse
import uuid
import struct
from RNS.vendor import umsgpack

APP_NAME = "microreticulum_interop"
ASPECT = "link_server"  # Must match C++ client in examples/link/main.cpp

# Global state
server_destination = None
active_links = []
test_state = {
    "messages_received": 0,
    "messages_sent": 0,
    "last_sequence_rx": None,
    "sequences_seen": [],
}


def log_test(test_name, status, reason=None):
    """Output structured test marker."""
    if reason:
        print(f"[TEST:{test_name}:{status}:{reason}]", flush=True)
    else:
        print(f"[TEST:{test_name}:{status}]", flush=True)


def log_wire(direction, data):
    """Output wire format in hex."""
    if isinstance(data, bytes):
        hex_str = data.hex()
    else:
        hex_str = bytes(data).hex()
    print(f"[WIRE:{direction}:{hex_str}]", flush=True)


def log_data(field, value):
    """Output structured data field."""
    print(f"[DATA:{field}={value}]", flush=True)


# Define MessageTest class (must match C++ implementation)
class MessageTest(RNS.MessageBase):
    MSGTYPE = 0xabcd

    def __init__(self):
        self.id = ""
        self.data = ""
        self.not_serialized = str(uuid.uuid4())

    def pack(self):
        packed = umsgpack.packb([self.id, self.data])
        return packed

    def unpack(self, raw):
        unpacked = umsgpack.unpackb(raw)
        if len(unpacked) >= 2:
            self.id = unpacked[0]
            self.data = unpacked[1]


def link_established(link):
    """Called when a new link is established."""
    print(f"\n[LINK:ESTABLISHED:id={link.link_id.hex()}]", flush=True)

    active_links.append(link)

    # Set up packet callback
    link.set_packet_callback(packet_received)
    link.set_link_closed_callback(link_closed)
    link.set_resource_strategy(RNS.Link.ACCEPT_ALL)

    # Set up channel with wire format logging
    setup_channel(link)


def setup_channel(link):
    """Set up channel with test handlers and logging."""
    channel = link.get_channel()
    channel.register_message_type(MessageTest)

    # Store original _receive to intercept raw wire data
    original_receive = channel._receive

    def logged_receive(raw):
        """Intercept receive to log wire format."""
        log_wire("RX", raw)
        # Parse envelope header for logging
        if len(raw) >= 6:
            msgtype, seq, length = struct.unpack(">HHH", raw[:6])
            log_data("rx_msgtype", f"0x{msgtype:04X}")
            log_data("rx_sequence", seq)
            log_data("rx_length", length)
            test_state["last_sequence_rx"] = seq
            test_state["sequences_seen"].append(seq)
        return original_receive(raw)

    channel._receive = logged_receive

    def handle_channel_message(message):
        if isinstance(message, MessageTest):
            test_state["messages_received"] += 1

            log_data("msg_id", message.id)
            log_data("msg_data", message.data)
            log_data("messages_received", test_state["messages_received"])

            # Check for specific test scenarios based on message content
            if message.data == "PING":
                # Basic echo test
                log_test("channel_basic_roundtrip", "START")

                response = MessageTest()
                response.id = message.id
                response.data = "PONG"

                # Log outgoing wire format
                packed = response.pack()
                # Build envelope manually for logging (actual send adds header)
                log_data("tx_msgtype", f"0x{MessageTest.MSGTYPE:04X}")
                log_data("tx_payload_hex", packed.hex())

                channel.send(response)
                test_state["messages_sent"] += 1
                log_test("channel_basic_roundtrip", "PASS")
                return True

            elif message.data.startswith("SEQ_TEST_"):
                # Sequence test - echo back with sequence info
                response = MessageTest()
                response.id = message.id
                response.data = f"SEQ_ACK_{test_state['last_sequence_rx']}"
                channel.send(response)
                test_state["messages_sent"] += 1
                return True

            elif message.data == "WIRE_TEST":
                # Wire format verification test
                log_test("channel_wire_format", "START")

                response = MessageTest()
                response.id = "A"  # Single char for predictable encoding
                response.data = "B"

                packed = response.pack()
                log_data("tx_payload_hex", packed.hex())
                # Expected: 92 A1 41 A1 42 (msgpack [2-array, fixstr "A", fixstr "B"])
                expected_payload = bytes([0x92, 0xA1, 0x41, 0xA1, 0x42])

                if packed == expected_payload:
                    log_test("channel_wire_format", "PASS")
                else:
                    log_test("channel_wire_format", "FAIL",
                            f"payload_mismatch:expected={expected_payload.hex()},got={packed.hex()}")

                channel.send(response)
                test_state["messages_sent"] += 1
                return True

            elif message.data == "EMPTY_TEST":
                # Empty payload test
                log_test("channel_empty_payload", "START")

                response = MessageTest()
                response.id = ""
                response.data = ""

                packed = response.pack()
                log_data("tx_payload_hex", packed.hex())
                # Expected: 92 A0 A0 (msgpack [2-array, empty str, empty str])
                expected_payload = bytes([0x92, 0xA0, 0xA0])

                if packed == expected_payload:
                    log_test("channel_empty_payload", "PASS")
                else:
                    log_test("channel_empty_payload", "FAIL",
                            f"payload_mismatch:expected={expected_payload.hex()},got={packed.hex()}")

                channel.send(response)
                test_state["messages_sent"] += 1
                return True

            else:
                # Default: echo with " back" suffix (compatibility with link_server.py)
                response = MessageTest()
                response.id = message.id
                response.data = message.data + " back"
                channel.send(response)
                test_state["messages_sent"] += 1
                return True

        return False

    channel.add_message_handler(handle_channel_message)
    print("[CHANNEL:CONFIGURED]", flush=True)


def link_closed(link):
    """Called when a link is closed."""
    print(f"[LINK:CLOSED:id={link.link_id.hex()}]", flush=True)
    log_data("messages_received", test_state["messages_received"])
    log_data("messages_sent", test_state["messages_sent"])
    log_data("sequences_seen", ",".join(map(str, test_state["sequences_seen"])))

    if link in active_links:
        active_links.remove(link)

    # Reset state for next link
    test_state["messages_received"] = 0
    test_state["messages_sent"] = 0
    test_state["sequences_seen"] = []


def packet_received(data, packet):
    """Called when a non-channel packet is received."""
    print(f"[PACKET:RX:len={len(data)},hex={data.hex()}]", flush=True)


def setup_server(config_path=None):
    """Set up the RNS server."""
    global server_destination

    print("[SERVER:INITIALIZING]", flush=True)

    if config_path:
        reticulum = RNS.Reticulum(config_path)
    else:
        reticulum = RNS.Reticulum()

    server_identity = RNS.Identity()

    server_destination = RNS.Destination(
        server_identity,
        RNS.Destination.IN,
        RNS.Destination.SINGLE,
        APP_NAME,
        ASPECT
    )

    server_destination.set_link_established_callback(link_established)
    server_destination.set_proof_strategy(RNS.Destination.PROVE_ALL)

    print(f"[SERVER:READY:hash={server_destination.hash.hex()}]", flush=True)
    print(f"[DEST:{server_destination.hash.hex()}]", flush=True)

    server_destination.announce()
    print("[SERVER:ANNOUNCED]", flush=True)

    return reticulum


def main():
    parser = argparse.ArgumentParser(description="Channel Test Server")
    parser.add_argument("-c", "--config", help="Reticulum config directory", default=None)
    parser.add_argument("-v", "--verbose", help="Verbose output", action="store_true")
    args = parser.parse_args()

    if args.verbose:
        RNS.loglevel = RNS.LOG_DEBUG

    try:
        reticulum = setup_server(args.config)

        while True:
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\n[SERVER:SHUTDOWN]", flush=True)
        for link in active_links:
            try:
                link.teardown()
            except:
                pass
        sys.exit(0)


if __name__ == "__main__":
    main()
