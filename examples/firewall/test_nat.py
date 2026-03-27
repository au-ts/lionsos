# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import sys

"""
Script that listens for TCP/UDP packets and prints source IP and port.
Optionally accepts an expected IP to test against.
"""

parser = argparse.ArgumentParser()

parser.add_argument("address", nargs="?")
parser.add_argument("-u", "--udp", action="store_true")

args = parser.parse_args()

expected_ip = args.address
udp = args.udp


def print_status(addr: tuple[str, int], expected_ip: str, data: bytes):
    status = (
        ("| PASS" if addr[0] == expected_ip else "| FAIL")
        if expected_ip is not None
        else ""
    )

    print(f"[{addr[0]}:{addr[1]}{status}] {data}")


HOST = ""
PORT = 65444
with socket.socket(
    socket.AF_INET, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM
) as s:
    s.bind((HOST, PORT))
    if not udp:
        s.listen()
    while True:
        if udp:
            data, addr = s.recvfrom(1024)
            print_status(addr, expected_ip, data)
            # Return traffic
            s.sendto(b"return traffic\n", addr)
        else:
            conn, addr = s.accept()
            with conn:
                print("Connection established to ", addr)
                while True:
                    data = conn.recv(1024)
                    if not data:
                        break
                    print_status(addr, expected_ip, data)
                    conn.sendall(b"return traffic\n")
