# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import socket
import threading

"""
Script that listens for TCP/UDP packets and prints source IP and port.
Optionally accepts an expected IP to test against.
Use -m/--multi for concurrent TCP connections.
"""

parser = argparse.ArgumentParser()

parser.add_argument("address", nargs="?")
parser.add_argument("-u", "--udp", action="store_true")
parser.add_argument("-m", "--multi", action="store_true", help="Handle multiple concurrent TCP connections")

args = parser.parse_args()

expected_ip = args.address
udp = args.udp
multi = args.multi


def print_status(addr: tuple[str, int], expected_ip: str, data: bytes):
    status = (
        ("| PASS" if addr[0] == expected_ip else "| FAIL")
        if expected_ip is not None
        else ""
    )

    print(f"[{addr[0]}:{addr[1]}{status}] {data}", flush=True)


def handle_tcp_conn(conn, addr):
    with conn:
        print(f"Connection established from {addr[0]}:{addr[1]}", flush=True)
        while True:
            data = conn.recv(1024)
            if not data:
                break
            print_status(addr, expected_ip, data)
            conn.sendall(b"return traffic\n")
    print(f"Connection closed from {addr[0]}:{addr[1]}", flush=True)


HOST = ""
PORT = 65444
with socket.socket(
    socket.AF_INET, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM
) as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    if not udp:
        s.listen()
    while True:
        if udp:
            data, addr = s.recvfrom(1024)
            print_status(addr, expected_ip, data)
            s.sendto(b"return traffic\n", addr)
        elif multi:
            conn, addr = s.accept()
            t = threading.Thread(target=handle_tcp_conn, args=(conn, addr), daemon=True)
            t.start()
        else:
            conn, addr = s.accept()
            with conn:
                print(f"Connection established from {addr[0]}:{addr[1]}", flush=True)
                while True:
                    data = conn.recv(1024)
                    if not data:
                        break
                    print_status(addr, expected_ip, data)
                    conn.sendall(b"return traffic\n")
