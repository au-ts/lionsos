# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

include("$(MPY_DIR)/extmod/asyncio/manifest.py")
package("microdot", base_path="$(PORT_DIR)/../../dep/microdot/src/")
module("ui_server.py")
