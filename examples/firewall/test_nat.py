import sys
import socket

"""
Script that listens for UDP packets and prints source IP and port.
Optionally accepts an expected IP to test against.
"""

expected_ip = sys.argv[1] if len(sys.argv) == 2 else None


HOST = ''
PORT = 65444
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.bind((HOST, PORT))
    while True:
        data, addr = s.recvfrom(1024)

        status = ("| PASS" if addr[0] == expected_ip else "| FAIL") if expected_ip is not None else ""

        print(f"[{addr[0]}:{addr[1]}{status}] {data}")

        # Return traffic
        s.sendto(b"return traffic\n", addr)
