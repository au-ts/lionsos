# For our client code, we need the asyncio module from MicroPython
include("../../dep/micropython/extmod/asyncio/manifest.py")
# Include our client scripts
module("kitty.py", base_path="client")
module("pn532.py", base_path="client")
module("font.py", base_path="client/font")
module("writer.py", base_path="client/font")