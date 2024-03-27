#!/usr/bin/env python3

# This Python script acts as a demo server to test simple
# client to server interactions for the Kitty system.
# It is a stripped down version of the real Kitty server that
# includes things like user registration and a database for
# persistence. However, that's not useful for testing the Kitty
# client's functionality so we use a stripped down version here.

import sys
import socket
import select
import traceback
import random
from datetime import datetime
import time as justtime
from typing import Literal
import threading

display_delay = 5
client_timeout = 5

PORT = 3738


class MessageThread(threading.Thread):
    def __init__(self, conn: socket.socket):
        super(MessageThread, self).__init__()
        self._stop = threading.Event()
        self.conn = conn

    def stop(self) -> None:
        self._stop.set()

    def isStopped(self) -> bool:
        return self._stop.is_set()

    def run(self) -> None:
        while True:
            if self.isStopped():
                break

            justtime.sleep(display_delay)

            try:
                set_display_msg(self.conn)
            except Exception as e:
                log_msg(f"Exception occurred when setting display msg: {e}")
                continue


class ExSockClosed(Exception):
    pass


def log_msg(msg: str) -> None:
    print("%s: %s" % (datetime.today().strftime("%A %d %B, %H:%M:%S "), msg))


def recv_line(sock: socket.socket) -> bytes:
    buf = b''

    while True:
        ret = sock.recv(1)

        if len(ret) == 0:
            log_msg("error reading socket")
            raise ExSockClosed
        else:
            buf += ret

        if buf != b'' and buf[-1:] == b'\n':
            break

    return buf.strip()


# Send a message on the connection
def set_display_msg(conn: socket.socket) -> None:
    msg = "Server Time: %s" % justtime.strftime("%I:%M:%S %p")

    # add the header
    msg_bytes = b"100 %s\n" % msg.encode()

    log_msg("Sending msg {!r}".format(msg_bytes))

    conn.sendall(msg_bytes)


# Wait for a request or an error
def wait_for_req(conn: socket.socket, uber: socket.socket, heartbeat_received: datetime) -> Literal[0, 1]:
    while True:
        # Find if we have a new connection
        log_msg("select")
        got = select.select([conn, uber], [], [conn, uber], client_timeout)
        log_msg("select returned")

        # New connection on uber
        if len(got[2]):
            log_msg("bailing!")
            conn.close()
            return 1

        if len(got[0]) > 0 and got[0][0] == uber:
            log_msg("bailing 2!")
            conn.close()
            return 1

        # check if the timeout occured
        if len(got[0]) == 0:
            log_msg("select timeout")
            if (datetime.now() - heartbeat_received).seconds > client_timeout:
                log_msg("client timeout")
                conn.close()
                return 1

        elif len(got[2]) > 0:
            log_msg("client closed socket")
            conn.close()
            return 1

        else:
            log_msg("stuff to read")
            return 0


def handle_connection(conn: socket.socket, uber: socket.socket) -> None:
    # pick a random initial transaction ID
    cur_txid = random.randint(1, 1000000)
    log_msg("initial transaction ID: %d" % cur_txid)
    heartbeat_received = datetime.now()
    # Send initial txid
    conn.sendall(b"101 %i\n" % cur_txid)

    # set an initial message
    set_display_msg(conn)

    # Now wait for stuff
    while True:
        # wait for a request
        status = wait_for_req(conn, uber, heartbeat_received)

        # some error occured...
        if status == 1:
            return

        line = recv_line(conn)

        log_msg("Received packet: {!r}".format(line))

        if line == b"":
            conn.sendall(b"400 Protocol/checksum error!\n")
        else:
            (cmd, txid, payload) = line.split(b' ', 2)

            if cmd == b'200':  # heartbeat
                heartbeat_received = datetime.now()
            elif cmd == b'100':
                # Check the transaction
                if int(txid) != cur_txid:
                    conn.sendall(b"400 Transaction error!\n")
                    log_msg("Bad transaction id! Expected %d, got %d"
                            % (cur_txid, int(txid)))
                    continue

                (cardno, ser_value) = payload.split(b' ')
                # value = float(ser_value.decode())

                # Increment for the next transaction
                cur_txid = (cur_txid + 1) % 1000000

                # Make the magic numbers
                # card_id = int(b"0x%s" % cardno, 16)
                # issue_lvl = 1  # Let's pretend that it's 1.
                # region_id = 1          # UNSW magic
                # facility_id = 32510    # UNSW magic
                conn.sendall("200 Thanks Alwin Credit $1000\n".encode())


class ConnectionThread(threading.Thread):
    def __init__(self, conn: socket.socket, sock: socket.socket):
        super(ConnectionThread, self).__init__()
        self._stop = threading.Event()
        self.conn = conn
        self.sock = sock

    def stop(self) -> None:
        self._stop.set()

    def isStopped(self) -> bool:
        return self._stop.is_set()

    def run(self) -> None:
        message_thread = MessageThread(self.conn)
        try:
            message_thread.daemon = True
            message_thread.start()
            handle_connection(self.conn, self.sock)
            log_msg("handle returned")
            message_thread.stop()

        except ExSockClosed:
            self.conn.close()
            message_thread.stop()
            log_msg("Socket closed")
            self.stop()


# Setup the server socket and run
def run_server() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", PORT))
    sock.listen(1)

    # Spawn a new connection thread for every successful connection
    while True:
        log_msg("Bound port, waiting...")
        (conn, addr) = sock.accept()
        log_msg("Got connection from %s" % (addr,))
        new_conn = ConnectionThread(conn, sock)
        new_conn.daemon = True
        new_conn.start()


def main() -> int:
    while True:
        try:
            run_server()
        except KeyboardInterrupt:
            log_msg("Interrupt, quitting...")
            return 0
        except Exception as e:
            traceback.print_exc(file=sys.stderr)
            log_msg(f"Exception in run_server '{e}' continuing")

        # Sleep for a bit
        justtime.sleep(5)


if __name__ == "__main__":
    log_msg(f"Kitty server starting on port {PORT}")

    # Seed random
    random.seed()

    r = main()
    sys.exit(r)
