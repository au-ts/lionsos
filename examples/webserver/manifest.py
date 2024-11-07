# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

include("$(MPY_DIR)/extmod/asyncio/manifest.py")
include("$(MPY_DIR)/lib/micropython-lib/micropython/aiorepl/manifest.py")
package("microdot", base_path="$(PORT_DIR)/../../dep/microdot/src/")
module("fs_async.py", base_path="$(PORT_DIR)")
module("webserver.py")
module("config.py")
