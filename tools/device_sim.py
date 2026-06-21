#!/usr/bin/env python3
"""
device_sim.py -- simulates client devices speaking the gateway's UDP
wire protocol (discover -> handshake -> heartbeats -> bye), for manual
integration testing and live demos against gateway_main without needing
real hardware.

Mirrors include/gateway_protocol.h byte-for-byte: same CRC-16/CCITT-FALSE
algorithm, same packed field layout, same network byte order.
"""
import argparse
import socket
import struct
import time
import random

MAGIC = 0x474C574B
VERSION = 1

MSG_DISCOVER = 1
MSG_HANDSHAKE = 2
MSG_HEARTBEAT = 3
MSG_BYE = 4

# '>' = network (big-endian) byte order, matching gw_packet_t's wire format.
HEADER_FMT = ">IBBHI6sB31s"   # everything except the trailing crc16
CRC_FMT = ">H"


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def build_packet(msg_type: int, device_id: int, seq: int, mac: bytes, hostname: str) -> bytes:
    hostname_bytes = hostname.encode("ascii", "ignore")[:31]
    header = struct.pack(
        HEADER_FMT, MAGIC, VERSION, msg_type, device_id, seq,
        mac, len(hostname_bytes), hostname_bytes.ljust(31, b"\x00"),
    )
    crc = crc16_ccitt_false(header)
    return header + struct.pack(CRC_FMT, crc)


def random_mac() -> bytes:
    return bytes([0x02] + [random.randint(0, 255) for _ in range(5)])


def run_device(sock, addr, hostname: str, heartbeats: int, interval: float):
    mac = random_mac()
    seq = 0

    def send(msg_type):
        nonlocal seq
        seq += 1
        sock.sendto(build_packet(msg_type, 0, seq, mac, hostname), addr)

    print(f"[{hostname}] mac={mac.hex(':')} discovering...")
    send(MSG_DISCOVER)
    time.sleep(0.1)
    send(MSG_HANDSHAKE)

    for i in range(heartbeats):
        time.sleep(interval)
        send(MSG_HEARTBEAT)
        print(f"[{hostname}] heartbeat {i + 1}/{heartbeats} (seq={seq})")

    send(MSG_BYE)
    print(f"[{hostname}] said bye")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9500)
    ap.add_argument("--devices", type=int, default=1)
    ap.add_argument("--heartbeats", type=int, default=3)
    ap.add_argument("--interval", type=float, default=1.0)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = (args.host, args.port)

    names = ["kitchen-plug", "living-room-bulb", "thermostat", "front-door-cam", "garage-sensor"]
    for i in range(args.devices):
        run_device(sock, addr, names[i % len(names)] + (f"-{i}" if i >= len(names) else ""),
                   args.heartbeats, args.interval)


if __name__ == "__main__":
    main()
