#!/usr/bin/env python3
"""
UDP packet sniffer for debugging RNS interop.
Captures and decodes packets on ports 4242 and 4243.
"""

import socket
import struct
import sys
import threading
from datetime import datetime

# RNS packet type constants (from Packet.py)
PACKET_TYPES = {
    0x00: "DATA",
    0x01: "ANNOUNCE",
    0x02: "LINKREQUEST",
    0x03: "PROOF",
    0x04: "PATH_REQUEST",
}

# Header types
HEADER_TYPES = {
    0x00: "HEADER_1",  # 1 address field
    0x01: "HEADER_2",  # 2 address fields
}

def decode_rns_header(data):
    """Decode RNS packet header."""
    if len(data) < 2:
        return None, "Too short"

    # First byte: [IFAC flag (1)] [Header type (2)] [Context flag (1)] [Propagation type (2)] [Destination type (2)]
    # Second byte: [Packet type (4)] [Hops (4)]
    byte0 = data[0]
    byte1 = data[1]

    ifac_flag = (byte0 >> 7) & 0x01
    header_type = (byte0 >> 5) & 0x03
    context_flag = (byte0 >> 4) & 0x01
    propagation_type = (byte0 >> 2) & 0x03
    destination_type = byte0 & 0x03

    packet_type = (byte1 >> 4) & 0x0F
    hops = byte1 & 0x0F

    header_info = {
        "ifac_flag": ifac_flag,
        "header_type": header_type,
        "header_type_name": HEADER_TYPES.get(header_type, f"UNKNOWN({header_type})"),
        "context_flag": context_flag,
        "propagation_type": propagation_type,
        "destination_type": destination_type,
        "packet_type": packet_type,
        "packet_type_name": PACKET_TYPES.get(packet_type, f"UNKNOWN({packet_type})"),
        "hops": hops,
    }

    # Determine header length based on header type
    # HEADER_1: 2 bytes header + 16 bytes destination = 18 bytes minimum
    # HEADER_2: 2 bytes header + 16 bytes dest + 16 bytes transport = 34 bytes minimum
    if header_type == 0:
        header_len = 18
    else:
        header_len = 34

    if len(data) >= header_len:
        if header_type == 0:
            header_info["destination_hash"] = data[2:18].hex()
        else:
            header_info["transport_hash"] = data[2:18].hex()
            header_info["destination_hash"] = data[18:34].hex()

    return header_info, data[header_len:] if len(data) >= header_len else data[2:]


def format_packet(data, source_port, dest_port):
    """Format packet for display."""
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    direction = f":{source_port} -> :{dest_port}"

    output = []
    output.append(f"\n{'='*70}")
    output.append(f"[{timestamp}] UDP Packet {direction}")
    output.append(f"  Length: {len(data)} bytes")
    output.append(f"  Raw (first 64 bytes): {data[:64].hex()}")

    if len(data) >= 2:
        header_info, payload = decode_rns_header(data)
        if header_info:
            output.append(f"  --- RNS Header ---")
            output.append(f"  Packet Type: {header_info['packet_type_name']} ({header_info['packet_type']})")
            output.append(f"  Header Type: {header_info['header_type_name']}")
            output.append(f"  IFAC Flag: {header_info['ifac_flag']}")
            output.append(f"  Context Flag: {header_info['context_flag']}")
            output.append(f"  Propagation Type: {header_info['propagation_type']}")
            output.append(f"  Destination Type: {header_info['destination_type']}")
            output.append(f"  Hops: {header_info['hops']}")
            if 'destination_hash' in header_info:
                output.append(f"  Destination Hash: {header_info['destination_hash']}")
            if 'transport_hash' in header_info:
                output.append(f"  Transport Hash: {header_info['transport_hash']}")
            output.append(f"  Payload Length: {len(payload)} bytes")
            if len(payload) > 0:
                output.append(f"  Payload (first 64 bytes): {payload[:64].hex()}")

    output.append(f"{'='*70}")
    return "\n".join(output)


def sniff_port(port, log_file):
    """Sniff UDP packets on a specific port."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        pass

    try:
        sock.bind(('0.0.0.0', port))
        print(f"Sniffing on port {port}...")

        while True:
            data, addr = sock.recvfrom(4096)
            output = format_packet(data, addr[1], port)
            print(output)
            with open(log_file, 'a') as f:
                f.write(output + "\n")
    except Exception as e:
        print(f"Error on port {port}: {e}")


def main():
    print("RNS Packet Sniffer - Listening on ports 4242 and 4243")
    print("Press Ctrl+C to stop\n")

    # Clear log files
    open('/tmp/rns_packets_4242.log', 'w').close()
    open('/tmp/rns_packets_4243.log', 'w').close()

    # Start sniffers in threads
    t1 = threading.Thread(target=sniff_port, args=(4242, '/tmp/rns_packets_4242.log'), daemon=True)
    t2 = threading.Thread(target=sniff_port, args=(4243, '/tmp/rns_packets_4243.log'), daemon=True)

    t1.start()
    t2.start()

    try:
        while True:
            pass
    except KeyboardInterrupt:
        print("\nStopping sniffer...")


if __name__ == "__main__":
    main()
