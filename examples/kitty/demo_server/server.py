#!/usr/bin/env python3

import socket
import threading
import display
import sys
import time

PORT = 1235
HEARTBEAT = 'H\n'.encode()
MESSAGE_PREFIX = 'M'.encode()
TAP_PREFIX = 'T'.encode()

if len(sys.argv) != 2:
    print("Usage: python3 heartbeat_script.py <IP_ADDRESS>")
    sys.exit(1)

target_ip = sys.argv[1]

def server():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        printed_port = False
        while True:
            # sock.sendto(HEARTBEAT_PACKET.to_bytes(1, byteorder="little"), (target_ip, PORT))
            sock.sendto(HEARTBEAT, (target_ip, PORT))
            if not printed_port:
                address, port = sock.getsockname()
                print("Receiving on " + str(address) + ":" + str(port))
                printed_port = True

            sock.settimeout(1)  # Set a 1-second timeout
            try:
                data, addr = sock.recvfrom(1024)  # buffer size is 1024 bytes
                if data[0] == TAP_PREFIX[0]:
                    snack = display.register_tap(data[1:].decode('utf-8').strip())
                    message = "Unknown card tapped!\n"
                    if snack is not None:
                        message = f"Thanks for your tap! Please take a {snack}.\n"
                    packet = MESSAGE_PREFIX + message.encode()
                    sock.sendto(packet, (target_ip, PORT))
            except socket.timeout:
                pass  # If we don't get a response within the timeout, just continue

            time.sleep(1)   

if __name__ == "__main__":
    threading.Thread(target=server).start()
    display.display_loop()
