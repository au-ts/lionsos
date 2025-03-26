# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# For our client code, we need the asyncio module from MicroPython
include("$(MPY_DIR)/extmod/asyncio/manifest.py")
# Include our client scripts
module("kitty.py")
module("pn532.py")
module("font_height35.py")
module("font_height50.py")
module("writer.py")
module("config.py")
module("deploy.py")