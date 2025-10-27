# LionsOS GDB Example

This is a showcase of using `libgdb` within a LionsOS system. This is currently
experimental pending the merging of seL4 and Microkit PR's.

This example connects to gdb on a target system over the network. Please
ensure that the target board is on a network and reachable by your host
machine. Alternatively, you can use QEMU to test out this demo.

# Components
Here as an overview of the architecture of this example:
![gdb example architecture](./LionsOS_GDB.svg)

**debugger component**

The debugger component is split up into 3 main parts:

***gdb.c***: This is the interface between the debugger and the
underlying GDB implementation. This component does things such as setting/unsetting
break points, reading from debuggee address space and generally handling all the GDB
commands passed from the user.

***debugger.c***: This is the interface between the user and `gdb.c`. This is where we
receive packets containing GDB commands over the network.

***libvspace***: This library provides the functionality to read/write to the debuggee
processes address space.

# Dependencies

Due to the pending PR's, you will need a separate version of Microkit, seL4 and
microkit_sdf_gen:

Microkit - https://github.com/au-ts/microkit/tree/child_vspace
seL4 - https://github.com/au-ts/seL4/tree/libgdb
microkit_sdf_gen - https://github.com/au-ts/microkit_sdf_gen/tree/libgdb_child_pts


Please build the dependencies and appropriately setup your environment. The
provided repos contain the build instructions necessary

# Building

Once you have your dependencies, build as such:

```bash
export MICROKIT_SDK=<path_to_sdk>
export MICROKIT_BOARD=<board>
export MICROKIT_CONFIG=debug

make
```

# Running

Load the image onto your target platform of choice. Once booted you should see
something similar to the following:

```bash
MON|INFO: Microkit Bootstrap
MON|INFO: bootinfo untyped list matches expected list
MON|INFO: Number of bootstrap invocations: 0x0000000c
MON|INFO: Number of system invocations:    0x00000665
MON|INFO: completed bootstrap invocations
MON|INFO: completed system invocations
MON|INFO: PD 'timer_driver' is now passive!
'debugger' is client 0
DHCP request finished, IP address for netif debugger is: 10.0.2.15
```

This IP address that is provided will be what we use to target the board with `gdb`.

In a separate terminal, navigate to your build directory for the example. We will then
start `gdb` and provide it the `ping.elf` file to read debug symbols from:

```bash
aarch64-none-elf-gdb ping.elf
```
We can then target our remote platform:

```bash
target remote <ip_addr>:1234
```

NOTE: If running on QEMU, replace `ip_addr` with `localhost`. This is the default gateway into QEMU's SLIRP network. We
are forwarding everything our host receives on port `1234` into QEMU.

You should see something similar to the following:
```bash
_text () at src/aarch64/crt0.s:13
13     b main
```

You can now continue the program execution by inputting `c` into the GDB terminal. You should now see the following
in the GDB terminal:

```bash
(gdb) c
Continuing.

Thread 1.1 received signal SIGABRT, Aborted.
notified (ch=ch@entry=0) at /Users/krishnanwinter/Documents/TS/lionsos_gdb_example/lionsos/examples/gdb/ping.c:30
30         volatile int denull = (volatile int) *null_ptr;
```

We have run into an error with our program!!! Luckily, GDB has kindly provided some information regarding the crash,
and the line number. We could also view the callstack that has lead us to this function, we can do that by typing
`backtrace` into the GDB terminal. This will show us:

```bash
(gdb) backtrace
#0  notified (ch=ch@entry=0) at /Users/krishnanwinter/Documents/TS/lionsos_gdb_example/lionsos/examples/gdb/ping.c:30
#1  0x0000000000200280 in handler_loop () at src/main.c:101
#2  main () at src/main.c:127
```

We can view the state at any of these frames like such:
```bash
(gdb) frame 1
#1  0x0000000000200280 in handler_loop () at src/main.c:101
101                    notified(idx);
```

Now that we have all the information we need to fix this crash, have a look at line 30 in `ping.c` and attempt to
fix the bug.

Once fixed you can follow the same steps to run and continue the program, and you should see the following strings repeating:
```bash
Ping!
Pong!
Ping!
Pong!
Ping!
Pong!
Ping!
Pong!
Ping!
Pong!
```
