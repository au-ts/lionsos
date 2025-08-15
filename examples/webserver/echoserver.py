import socket
import time

def basic_echo_server(host='0.0.0.0', port=8080):
    """Simple blocking TCP echo server"""

    # Create TCP socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    try:
        # Bind and listen
        server_socket.bind((host, port))
        server_socket.listen(5)
        print(f"Echo server listening on {host}:{port}")

        while True:
            client_socket, client_addr = server_socket.accept()

            try:
                while True:
                    # Receive data
                    data = client_socket.recv(1024)
                    if not data:
                        print(f"Client {client_addr} disconnected")
                        break

                    # Echo back the data
                    client_socket.send(data)

            except Exception as e:
                print(f"Error handling client {client_addr}: {e}")
            finally:
                client_socket.close()

    except KeyboardInterrupt:
        print("\nShutting down server...")
    finally:
        server_socket.close()


def basic_udp_echo_server(host='0.0.0.0', port=8080):
    """Simple UDP echo server for MicroPython"""

    # Create UDP socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    try:
        # Bind to address
        server_socket.bind((host, port))
        print(f"UDP Echo server listening on {host}:{port}")

        while True:
            try:
                # Receive data and client address
                data, client_addr = server_socket.recvfrom(1024)

                print(f"Received from {client_addr}: {data}")

                # Echo back to the same client
                server_socket.sendto(data, client_addr)

            except Exception as e:
                print(f"Error handling packet: {e}")

    except KeyboardInterrupt:
        print("\nShutting down UDP server...")
    finally:
        server_socket.close()

basic_echo_server()
# basic_udp_echo_server()
