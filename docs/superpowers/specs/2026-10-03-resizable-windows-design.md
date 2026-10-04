<!--
    Copyright 2026, LionsOS Contributors

    SPDX-License-Identifier: BSD-2-Clause
-->

# Resizable desktop windows

Status: design approved, awaiting spec review.

## Goal

Let the user resize a desktop window by dragging it, so that every application
benefits at once. The compositor already owns window movement, focus and
drawing, so the change is: a resize event in the GUI protocol, a drag handle in
the compositor, and redraw support in the applications that opt in.

First milestone is deliberately small: **Notes and Sketch become resizable.**
Every other application keeps its current fixed size and needs no change.

## Decisions already made

| Decision | Choice |
| --- | --- |
| Surface budget | 2 MiB per slot, up from 1 MiB, and the state/events windows move up to make room |
| Who declares resizability | The application, with a `flags` field in its state page |
| Bounds | Global minimum of 64x64; maximum derived from the surface region |
| When the app is told | The frame follows the pointer live; `GUI_EV_RESIZE` is sent on release |
| Scope | Notes and Sketch only; sandbox (WebAssembly) windows stay fixed |

## Current state

- `GUI_SURFACE_REGION_SIZE` is `0x100_000` in `examples/desktop/meta.py:43` and
  `examples/desktop/include/gui_config.h:40`; the two must agree.
- The size limit is **already enforced**: `src/compositor.c:506-507` rejects a
  commit whose dimensions exceed `APP_MAX_DIMENSION` (2048) or whose
  `width * height * 4` exceeds the surface region, and `apps/gui_app.c:24`
  checks the same at init. At 1 MiB the effective cap is 262,144 pixels:
  512x512 exactly, 640x400 at 0.977 MiB. The screen is whatever the GPU
  scanout reports (likely 1024x768 or larger), so no window can fill it.
- The compositor maps slot *i* at `gui_surfaces + i * GUI_SURFACE_REGION_SIZE`
  (`0x6000_0000`), `gui_states + i * GUI_STATE_REGION_SIZE` (`0x6100_0000`) and
  `gui_events + i * GUI_EVENTS_REGION_SIZE` (`0x6200_0000`)
  (`meta.py:253-259`, `src/compositor.c:1162-1164`).
- **Why surfaces cannot simply grow:** only 16 MiB separates the surface window
  from the state window, so 1 MiB supports 16 slots. At 2 MiB six applications
  need 12 MiB (fits), but four sandboxes would need 20 MiB and collide with
  state. Hence the state and event windows move too.
- `gpu_data` is 4 MiB at `0x4080_0000` (`meta.py:195`), far from these windows.
- The protocol fixes **stride == width** (`include/lions/gui/protocol.h:11`) and
  the compositor blits with stride = width (`src/compositor.c:599`). Resizing
  therefore changes the pixel layout: content must be redrawn, and there is no
  cheap "draw the newly exposed strip" path.

## Design

### Protocol — `include/lions/gui/protocol.h`

- Add `#include <stddef.h>` for `size_t`; the header includes only `<stdint.h>`
  and `<stdbool.h>` today.
- Add `#define GUI_MIN_WIDTH 64`, `#define GUI_MIN_HEIGHT 64` and
  `#define GUI_MAX_DIMENSION 2048`, with `gui_size_fits()` and
  `gui_size_clamp()` enforcing **all three**, not just the surface area. The
  2048 matters: without it a size such as 3000x64 fits in 2 MiB and would be
  handed out, only for the compositor's existing check (`src/compositor.c:506`)
  to reject it and close the window. `APP_MAX_DIMENSION` in the compositor
  therefore derives from `GUI_MAX_DIMENSION` instead of being a second
  constant that can drift.
- Append `uint32_t flags;` to `gui_state_t`, with
  `#define GUI_FLAG_RESIZABLE (1u << 0)`. Both sides are recompiled together.
- Add `#define GUI_EV_RESIZE 6`, carrying the new content size in `x` (width)
  and `y` (height). `code` and `value` are unused and stay 0.
- Document that `GUI_EV_RESIZE` is only sent to slots whose state carries the
  flag, and that a commit's size is validated exactly as it is today.

### Client helper — `examples/desktop/apps/gui_app.c` / `.h`

- `bool gui_app_resize(uint32_t width, uint32_t height)` — reject a size that
  does not fit the surface region, re-point the surface (width, height and
  stride), reset the clip, update `state->width` and `state->height`. It does
  not commit; the caller redraws and then calls `gui_app_commit_all()`.
- `void gui_app_resize_preserve(uint32_t width, uint32_t height, uint32_t fill)`
  — as above, but also carries the overlapping pixels across the stride change.
  Because the copy is in place, direction matters: top-down when the width
  shrinks and bottom-up when it grows. It **fills the area the resize exposes
  with `fill`**, so a newly revealed region is painted rather than left holding
  stale pixels from the old layout; Sketch passes its canvas background.
- A way to set `GUI_FLAG_RESIZABLE` at init.

### Compositor — `examples/desktop/src/compositor.c`

- Record `resizable` per slot from `state->flags`, validated alongside the
  existing size checks — the state page is untrusted, as always.
- `resize_handle_rect(frame)`: a roughly 12x12 hit area at the frame's
  bottom-right corner. A press inside it starts a grab, like the existing
  title-bar drag and close button.
- While grabbing, `frame_rect()` uses a pending size clamped to
  `[WINDOW_MIN, max]`; `content_rect()` keeps the application's *declared*
  size. During the drag the old content therefore sits in the corner of the
  enlarged frame: letterboxed, with no scaling code needed.
- On release: clamp, enqueue `GUI_EV_RESIZE`, notify the application. If the
  enqueue fails, the frame is restored to the application's current size
  immediately — an application may never commit again, so waiting for a commit
  that never comes would leave it letterboxed forever. When the enqueue
  succeeds the pending size stays until the slot's next commit, which clears
  it as a backstop.
- Maximum is unchanged in spirit: `APP_MAX_DIMENSION` plus the existing
  `width * height * 4 <= GUI_SURFACE_REGION_SIZE` check, now against 2 MiB.

### Layout — `meta.py` and `include/gui_config.h`

- `GUI_SURFACE_REGION_SIZE` becomes `0x200_000` in both.
- Compositor state maps move to `0x6300_0000` and events to `0x6400_0000`
  (`meta.py:255,257,274,275`). This leaves 48 MiB of surface space, enough for
  24 slots, so sandboxes are unaffected. No compositor code changes, since it
  derives every address from the symbols and strides.
- Update the address comment in `gui_config.h:9-11`.

### Applications

- **Notes** sets the flag and, on `GUI_EV_RESIZE`, resizes and re-wraps its
  text from its own buffer.
- **Sketch** sets the flag and uses the preserving variant so the drawing
  survives.
- Clock, calculator, widgets and the WebAssembly windows do not set the flag
  and are untouched. Sandbox windows stay fixed in particular because their
  surfaces are granted frame by frame, so resizing one would mean re-granting.

## Data flow

```
pointer press in the corner handle  ->  compositor starts a grab
pointer motion                      ->  frame grows/shrinks, content unchanged
pointer release                     ->  clamp to [64x64, surface limit]
                                        enqueue GUI_EV_RESIZE{width, height}
                                        notify the application
application                         ->  gui_app_resize[_preserve]()
                                        redraw
                                        gui_app_commit_all()
compositor                          ->  validates size, adopts it, clears pending
```

## Error handling

- A size that does not fit the surface is rejected by `gui_app_resize` and by
  the compositor's existing commit check, so a misbehaving or buggy application
  cannot make the compositor read outside its surface.
- All blits are clamped to the size the application declared, which is what
  keeps the letterboxed transient safe.
- A full event queue drops the resize event. The frame is then restored
  straight away, so the window converges instead of sticking.
- The area a resize exposes is filled, never left stale: `gfx_relayout()`
  paints outside the overlap, and Sketch passes its canvas background.
- Building with sandboxes needs a Microkit SDK carrying the `examples/dynamic_caps`
  tool patch for `<cspace>` elements, which is why CI does not build it; a
  stock SDK can verify the plain desktop build only.
- An application that sets the flag but ignores the event ends up with a frame
  larger than its content until its next commit; this is visible but not unsafe.

## Testing

CI is build-only, and the desktop has no runtime tests. So:

- Build both board configurations, with and without `--sandboxes`, to confirm
  the new region sizes still fit and nothing overlaps.
- The stride-change copy in `gui_app_resize_preserve` is pure memory logic and
  is exactly the kind of silent corruption the host suite exists for. Extract
  it so `test/` can cover it under ASan/UBSan, following the pattern in
  `test/test_fs_server_memory.c`.
- Everything else has to be checked by running QEMU.

## Out of scope

- Resizing sandbox and WebAssembly windows (needs frame re-granting).
- Live resize, where content tracks the pointer continuously.
- Scaling or letterboxing the old content to fill the new frame.
- The rest of the desktop roadmap: clipboard and text editing, a file manager
  with open/save dialogs, notifications and settings. Resizing comes first
  because it improves every application at once.

Application slots stay fixed and load changing content within them, which fits
Microkit's static system architecture and the design described in
`examples/desktop/README.md`.
