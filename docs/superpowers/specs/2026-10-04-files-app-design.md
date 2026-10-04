<!--
    Copyright 2026, LionsOS Contributors

    SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Files: a read-only view of the desktop namespace

Status: implemented, first slice. Read-only.

## Goal

Give the desktop a Files application that presents the friendly namespace from
`docs/typed-resource-model.md` and shows the real contents of the FAT `/apps`
directory as `C:\Applications`. Mutation is deliberately out of scope for this
slice.

## The constraint that shaped it

`fs_server_config_t` (`include/lions/fs/config.h`) holds a **single** client
connection, and sdfgen's `LionsOs.FileSystem.Fat()` helper takes one client PD.
So a second application cannot hold a filesystem connection of its own without
changing the shared filesystem server and every example that uses it, and
without a patched sdfgen.

**Decision:** the WebAssembly host, which already holds the only filesystem
connection, brokers a read-only view for the Files application. It owns both the
namespace and the decision about what may be served.

## Design

### Application: `files` (slot 5)

`examples/desktop/apps/files.c`. An ordinary GUI application: it calls
`gui_app_init()`, draws into its surface and drains `GUI_EV_*` events. It holds
no filesystem connection and links no filesystem library — that is the point.

A path bar shows where you are, then one line per entry with its size. Clicking
a directory enters it; clicking the path bar goes up; clicking a file shows its
first 4 KiB as text. It never blocks: it sends a request, then redraws when the
reply arrives.

### Protocol: one shared page

`examples/desktop/include/files_ns.h`. One page holds a single request and a
single response, with channels used only as wakeup hints, matching the
shared-memory style used everywhere else in LionsOS. Each reply carries the
request's sequence number. Status values are explicit: `OK`, `BAD_PATH`,
`DENIED`, `NOT_FOUND`, `UNAVAILABLE`, `TOO_LARGE`, `IO`.

Requests are `LIST` and `READ`. A read is bounded to one 4 KiB preview, which is
well inside the filesystem protocol's own 32 KiB slot limit.

### Namespace and enforcement

`examples/desktop/wasm_host/files_broker.c`, and nowhere else:

- `C:\` is a synthetic root: `Applications`, plus `Users`, `System`, `Shared`
  and `Devices` shown as **not available yet** rather than silently omitted.
- `C:\Applications\…` maps to FAT `/apps/…`, read-only.
- Anything else is `DENIED` (unknown root) or `UNAVAILABLE` (known root with no
  backing store yet).

Paths are walked component by component rather than concatenated, so `..`,
`.` and stray separators cannot escape. After mapping, the result must still
begin with `/apps` before it is used.

**Where enforcement actually happens.** The check is policy in the broker, not
an seL4 capability. It holds because nothing else in the system can reach the
filesystem: the Files application has no connection, and the broker is the only
client. It does **not** confine a compromised host, and it is not a substitute
for a typed, attenuated filesystem handle. `docs/typed-resource-model.md` asks
that documentation say where enforcement occurs; this is that statement.

## Limitations

Recorded rather than hidden, as the resource model asks:

- **Read-only.** No create, write, rename or delete anywhere, in the protocol or
  the UI. The request kinds are only `LIST` and `READ`.
- **Only `C:\Applications` is real.** `Users`, `System`, `Shared` and `Devices`
  are placeholders with no backing store.
- **Authority is policy in the broker**, not a capability. See above.
- **Subdirectories are not distinguished.** Every entry in a listing is treated
  as a file, because the broker does not probe each entry's type; entering a
  directory below `Applications` would therefore try to read it.
- **Previews are text only**, the first 4 KiB, and binary content is reported as
  binary rather than rendered.
- **Fixed window size.** Files does not set `GUI_FLAG_RESIZABLE` yet.
- **No permissions, ownership or symlinks.** FAT has none to represent, so the
  namespace cannot show them.
- **Path resolution is textual.** Case-insensitive matching is used only for
  root names; FAT's own rules apply below that.

## Verification

- Desktop builds for `qemu_virt_aarch64`, and boots: 7 slots, `Files` at slot 5,
  `Apps` moved to slot 6, no faults.
- Booting with Files started inside `C:\Applications` showed the round trip:
  `C:\Applications` resolved to `/apps`, and 11 entries were listed — the 5
  `.wasm` apps, their 5 `.caps` files and `readme.txt`.
- What cannot be checked without a display: clicking through the listing.

## Next steps

- Distinguish directories from files in a listing.
- Make the window resizable.
- Give the Files application a real attenuated filesystem handle once the
  server can serve more than one client, and drop the broker.
- Mutation, later and behind an explicit grant, starting with `Shared`.
