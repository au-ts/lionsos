# For our client code, we need the asyncio module from MicroPython
include("$(MPY_DIR)/extmod/asyncio/manifest.py")
# Include our client scripts
module("kitty.py")
module("pn532.py")
module("font.py")
module("writer.py")
module("config.py")