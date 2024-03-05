from machine import I2C
import time

PN532_I2C_BUS_ADDRESS = (0x48 >> 1)

PN532_CMD_GETFIRMWAREVERSION = 0x02
PN532_FIRMWAREVERSION_LENGTH = 6

DEFAULT_READ_ACK_FRAME_RETRIES = 20
DEFAULT_READ_RESPONSE_RETRIES = 100

PN532_PREAMBLE = 0x00
PN532_STARTCODE1 = 0x00
PN532_STARTCODE2 = 0xFF
PN532_POSTAMBLE = 0x00

PN532_HOSTTOPN532 = 0xD4

PN532_ACK_FRAME_SIZE = 0x07
PN532_ACK_FRAME = [0, 0, 0xFF, 0, 0xFF, 0]
PN532_NACK = [0, 0, 0xFF, 0xFF, 0, 0]

def pn532_write_command(header, body, retries):
	data = bytearray(8 + len(header) + len(body))

	data[0] = PN532_PREAMBLE
	data[1] = PN532_STARTCODE1
	data[2] = PN532_STARTCODE2

	length = len(header) + len(body) + 1
	data[3] = length
	data[4] = ~length + 1

	sum = PN532_HOSTTOPN532
	data[5] = PN532_HOSTTOPN532

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

	data[i] = PN532_POSTAMBLE

	print("about to write to")
	i2c_bus.writeto(PN532_I2C_BUS_ADDRESS, data)
	print("finished writing to")
	return pn532_read_ack_frame(retries)


def pn532_read_ack_frame(retries):
	attempts = 0
	while (attempts < retries):
		data = i2c_bus.readfrom(PN532_I2C_BUS_ADDRESS, PN532_ACK_FRAME_SIZE)

		if data[0] & 1:
			for i in range(0, PN532_ACK_FRAME_SIZE - 1):
				value = data[i + 1]
				if value != PN532_ACK_FRAME[i]:
					print("ACK malformed")
					return -1

			return 0

		attempts += 1
		time.sleep_ms(1)

	print("read_ack_frame: device is not ready yet")
	return -1

def pn532_read_response_length(retries):
	time.sleep_ms(1)
	length = 0
	attempts = 0

	while True:
		data = i2c_bus.readfrom(PN532_I2C_BUS_ADDRESS, 6)

		if (data[0] & 1):
			length = data[4]
			break
		elif attempts == retries:
				return -1

		attempts += 1
		time.sleep_ms(1)

	# Send NACK
	nack_buf = bytearray(PN532_NACK)
	i2c_bus.writeto(PN532_I2C_BUS_ADDRESS, nack_buf)
	return length

def pn532_read_response(retries):
	length = pn532_read_response_length(retries)
	if (length < 0):
		print("READ RESPONSE - Length was less than zero")
		return []

	attempts = 0
	num_data_tokens = 7 + length + 2
	while True:
		data = i2c_bus.readfrom(PN532_I2C_BUS_ADDRESS, num_data_tokens)
		if (data[0] & 1):
			break
		elif attempts == retries:
			print("READ RESPONSE - Ran out of attempts")
			return []

		attempts += 1
		time.sleep_ms(1)

	ret_buf = bytearray(length)
	assert(data[1] == PN532_PREAMBLE, "preamble failed")
	assert(data[2] == PN532_STARTCODE1, "startcode1 failed")
	assert(data[3] == PN532_STARTCODE2, "startcode2 failed")
	assert(data[4] == length, "length failed")
	assert(data[5] == ~length + 1, "checksum failed") # @alwin: not too sure about this
	# data[6] is probably PN532_TO_HOST command
	# data[7] is probably an echo of the command
	i = 0
	while i < length:
		ret_buf[i] = data[8 + i]
		i += 1
	i += 8
	# data[i] is checksum of data
	# data[i + 1] is postamble

	# @alwin: For some reaosn, we need to read another buffer here

	return ret_buf


print("Beginning PN532 driver")

# Get the i2c device
i2c_bus = I2C(1)

print("hello!")

# Getting firmware number
pn532_write_command([PN532_CMD_GETFIRMWAREVERSION], [], DEFAULT_READ_ACK_FRAME_RETRIES)
firmware_version = pn532_read_response(DEFAULT_READ_RESPONSE_RETRIES)
print(list(firmware_version))


