# Usage
1. Dependencies
  - `libgdb`
  - `libvspace` (part of libgdb)
  - Either `sddf_net` or `sddf_serial`. This means adding `serial_*.elf` and building them as required, and
    vice versa for `network_*.elf` and related includes.
  - `libsddf_util_debug.a`
  - `libc.a` (LionsOS' version)
  - `libco.a` (coroutines)
2. Makefile additions:
```makefile
DEBUGGER_DIR := $(LIONSOS)/components/debugger
DEBUGGER_BACKEND := (either "serial" or "net")
LIBGDB_DIR := $(LIONSOS)/dep/libgdb
LIBVSPACE_DIR := $(LIBGDB_DIR)/libvspace

IMAGES += debugger.elf
LIBS += libvspace.a libsddf_util_debug.a

include {Serial or network driver backends}
include $(LIBGDB_DIR)/libgdb.mk
include $(LIBVSPACE_DIR)/libvspace.mk
include $(DEBUGGER_DIR)/debugger.mk

# Add the debugger directory to the python path
PYTHONPATH := 'PYTHONPATH="$(DEBUGGER_DIR):...:$$PYTHONPATH"'

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHONPATH) $(PYTHON) $(METAPROGRAM) ...
```

3. Metaprogram additions:
```python
# import
import LionsOS_debugger
board = ...

...
# if using a serial_system
uart_driver = ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
serial_virt_tx = ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
serial_virt_rx = ProtectionDomain("serial_virt_rx", "serial_virt_rx.elf", priority=99)
serial_system = Sddf.Serial(sdf, uart_node, uart_driver, serial_virt_tx, virt_rx=serial_virt_rx)

# backend
debugger_backend = LionsOS_debugger.Debugger.SerialBackend(serial_system, [
	# other systems you want to connect to the debugger.
])

# There is an equivalent system for the NetBackend.

debugger = LionsOS_debugger.Debugger(sdf, backend, priority=95, budget=20000)
# Set the priority to be higher than the children, but lower than the drivers.
# Default stack_size is 0x100000
# has the same inputs as a PD, but does not inherit from PDs due to sdfgen constraints
# TODO: change this when acacia is official.

debugger.add_debuggees([
	# PDs to debug.
])
# second one only applies for network.
debuggerPd, lwipIfNetwork = debugger.finalise()
sdf.add_pd(debuggerPd)

# and assert the connection of the systems and backends.
```

# Caveats
1. Not optimized for large gdb transmissions (no batching). Sometimes transmitting can be a bit slow.

# Adding backends
There is an interop layer, which is just "gdbcomp.c". All it needs are:
1. `debugger_put_char`, a non-blocking-non-flushing char push function
2. `debugger_flush`, a function to flush the output buffer
3. `debugger_get_char`, a non-blocking char get function, which returns false or true if a character was gotten.

# TODO
Move defining the number of children PDs outside of comptime.

# Behind the scenes
1. `libgdb` makes use of `libvspace` as a backend to allow accessing memory of children PDs.
  However, `libgdb` does not provide easily integratable "frontends", which stitch together
  a communication protocol + basic packet processing.
2. This debugger component adds this stitching together, as a `PD` which you add debuggees as
  children to this `PD`. These are exposed as a basic python API wrapper on top of `sdfgen`.
3. Runs a coroutine system to allow progression of a "main event loop" in an event based OS.
