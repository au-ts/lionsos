# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# We must ignore these imports since they are not available in a normal Python
# environment where we do the type checking
from machine import I2C # type: ignore
from time import sleep_ms # type: ignore
import sys

MICROPYTHON = sys.implementation.name == "micropython"

# MicroPython does not provide the typing library so first we
# check that we can import it so we can do type checking with
# mypy.
if not MICROPYTHON:
    from typing import List, Optional

_PN532_I2C_BUS_ADDRESS = (0x48 >> 1)

_DEFAULT_READ_ACK_FRAME_RETRIES = 20
_DEFAULT_READ_RESPONSE_RETRIES = 100

_PN532_CMD_GETFIRMWAREVERSION = 0x02
_PN532_FIRMWAREVERSION_LENGTH = 6

_PN532_PREAMBLE = 0x00
_PN532_STARTCODE1 = 0x00
_PN532_STARTCODE2 = 0xFF
_PN532_POSTAMBLE = 0x00

_PN532_HOSTTOPN532 = 0xD4

_PN532_ACK_FRAME_SIZE = 0x07
_PN532_ACK_FRAME = bytearray([0, 0, 0xFF, 0, 0xFF, 0])
_PN532_NACK = bytearray([0, 0, 0xFF, 0xFF, 0, 0])

_PN532_CMD_RFCONFIGURATION = 0x32
_RFCONFIGURATION_HEADER = bytearray([_PN532_CMD_RFCONFIGURATION, 0x5, 0xFF, 0x1, 0xFF])

_PN532_CMD_SAMCONFIGURATION = 0x14
_SAM_CONFIGURE_HEADER = bytearray([_PN532_CMD_SAMCONFIGURATION, 0x1, 0x14, 0x1])

_PN532_CMD_INLISTPASSIVETARGET = 0x4a
_PN532_MIFARE_ISO14443A_BAUD_RATE = 0x0
_INLISTPASSIVETARGET_HEADER = bytearray([_PN532_CMD_INLISTPASSIVETARGET, 0x1, _PN532_MIFARE_ISO14443A_BAUD_RATE])

SLEEP_TIME = 1

class PN532:
    def __init__(self, i2c_bus_number: int):
        self.i2c_bus = I2C(i2c_bus_number)

    def _pn532_write_command(self, header: bytearray, body: bytearray, retries: int) -> bool:
        data = bytearray(8 + len(header) + len(body))

        data[0] = _PN532_PREAMBLE
        data[1] = _PN532_STARTCODE1
        data[2] = _PN532_STARTCODE2

        length = len(header) + len(body) + 1
        data[3] = length
        data[4] = ~length + 1

        sum = _PN532_HOSTTOPN532
        data[5] = _PN532_HOSTTOPN532

        i = 6
        for elem in header:
            sum += elem
            data[i] = elem
            i += 1

        for elem in body:
            sum += elem
            data[i] = elem
            i += 1

        data[i] = ~sum + 1
        i += 1

        data[i] = _PN532_POSTAMBLE

        ret = self.i2c_bus.writeto(_PN532_I2C_BUS_ADDRESS, data)
        return self._pn532_read_ack_frame(retries)

    def _pn532_read_ack_frame(self, retries: int) -> bool:
        attempts = 0
        while (attempts < retries):
            data = self.i2c_bus.readfrom(_PN532_I2C_BUS_ADDRESS, _PN532_ACK_FRAME_SIZE)
            if len(data) and data[0] & 1:
                for i in range(0, _PN532_ACK_FRAME_SIZE - 1):
                    value = data[i + 1]
                    if value != _PN532_ACK_FRAME[i]:
                        print("ACK malformed")
                        return False

                return True

            attempts += 1
            sleep_ms(SLEEP_TIME)

        print("read_ack_frame: device is not ready yet")
        return True

    def _pn532_read_response_length(self, retries: int) -> int:
        sleep_ms(SLEEP_TIME)
        length = 0
        attempts = 0

        while True:
            data = self.i2c_bus.readfrom(_PN532_I2C_BUS_ADDRESS, 6)

            if (data[0] & 1):
                length = data[4]
                break
            elif attempts == retries:
                    return -1

            attempts += 1
            sleep_ms(SLEEP_TIME)

        # Send NACK
        self.i2c_bus.writeto(_PN532_I2C_BUS_ADDRESS, _PN532_NACK)
        return length

    def pn532_read_response(self, retries: int) -> bytearray:
        length = self._pn532_read_response_length(retries)
        if (length < 0):
            # print("READ RESPONSE - Length was less than zero")
            return bytearray([])

        attempts = 0
        num_data_tokens = 7 + length + 2
        while True:
            data = self.i2c_bus.readfrom(_PN532_I2C_BUS_ADDRESS, num_data_tokens)
            if (data[0] & 1):
                break
            elif attempts == retries:
                print("READ RESPONSE - Ran out of attempts")
                return bytearray([])

            attempts += 1
            sleep_ms(SLEEP_TIME)

        ret_buf = bytearray(length)
        assert data[1] == _PN532_PREAMBLE, "preamble failed"
        assert data[2] == _PN532_STARTCODE1, "startcode1 failed"
        assert data[3] == _PN532_STARTCODE2, "startcode2 failed"
        assert data[4] == length, "length failed"
        assert data[5] == (~length + 1) + 2**8, "checksum failed"
        # data[6] is probably PN532_TO_HOST command
        # data[7] is probably an echo of the command
        i = 0
        while i < length:
            ret_buf[i] = data[8 + i]
            i += 1
        i += 8
        # data[i] is checksum of data
        # data[i + 1] is postamble

        return ret_buf

    def read_firmware_version(self) -> Optional[List[int]]:
        self._pn532_write_command(bytearray([_PN532_CMD_GETFIRMWAREVERSION]), bytearray(), _DEFAULT_READ_ACK_FRAME_RETRIES)
        firmware_version = self.pn532_read_response(_DEFAULT_READ_RESPONSE_RETRIES)
        return list(firmware_version)

    def rf_configure(self) -> bool:
        self._pn532_write_command(_RFCONFIGURATION_HEADER, bytearray(), _DEFAULT_READ_ACK_FRAME_RETRIES)
        self.pn532_read_response(_DEFAULT_READ_RESPONSE_RETRIES);
        return True

    def sam_configure(self) -> bool:
        self._pn532_write_command(_SAM_CONFIGURE_HEADER, bytearray(), _DEFAULT_READ_ACK_FRAME_RETRIES)
        self.pn532_read_response(_DEFAULT_READ_RESPONSE_RETRIES);
        return True

    def read_uid(self) -> List[int]:
        self._pn532_write_command(_INLISTPASSIVETARGET_HEADER, bytearray(), _DEFAULT_READ_ACK_FRAME_RETRIES)
        buf = self.pn532_read_response(_DEFAULT_READ_RESPONSE_RETRIES)
        if len(buf) == 0 or buf[0] != 1:
            return []

        sens_res = buf[2] << 8
        sens_res = sens_res | buf[3]

        uid_length = buf[5]

        return list(buf[6:6+uid_length])
