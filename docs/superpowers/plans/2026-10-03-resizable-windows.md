<!--
    Copyright 2026, LionsOS Contributors

    SPDX-License-Identifier: BSD-2-Clause
-->

# Resizable Desktop Windows Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user resize the Notes and Sketch windows by dragging the bottom-right corner, with the size bounded by a 2 MiB surface per application slot.

**Architecture:** The compositor owns the window frame and posts a new `GUI_EV_RESIZE` when a corner drag ends; the application adopts the size, redraws and commits. The surface keeps `stride == width`, so a resize genuinely relayouts pixels and needs a full redraw. Resizability is declared by the application in its state page, and the compositor never trusts that page.

**Tech Stack:** C11, Microkit, sDDF (GPU and input device classes), the desktop's `gfx` layer, host tests under ASan/UBSan via `make -C test`.

**Spec:** `docs/superpowers/specs/2026-10-03-resizable-windows-design.md` — the plan argues from the spec; read both.

## Global Constraints

- `GUI_SURFACE_REGION_SIZE` must be identical in `examples/desktop/meta.py` and `examples/desktop/include/gui_config.h`.
- Every new file needs an SPDX header (`Copyright 2026, LionsOS Contributors` / `SPDX-License-Identifier: BSD-2-Clause`). CI enforces REUSE and rejects trailing whitespace.
- The compositor treats every application region as untrusted: sizes, flags and damage rectangles are validated and clamped before use.
- A size the shared helper accepts must never be one the compositor rejects. `APP_MAX_DIMENSION` in `src/compositor.c` therefore derives from `GUI_MAX_DIMENSION` in `include/lions/gui/protocol.h`, rather than being a second 2048 that can drift; otherwise a resize could close the window it resized.
- Host tests run under `-fsanitize=address,undefined`; `test/include` must never land on a target build's include path.
- Build check: `cd examples/desktop && MICROKIT_BOARD=qemu_virt_aarch64 make`, and again with sandboxes, to confirm the larger regions still fit.
- 4-space indent, `snake_case`, `/* */` comments, matching the surrounding desktop and protocol files.

## Review Focus

1. Drag beyond the maximum — clamps to the largest size fitting the surface; no oversized frame, no read past the surface.
2. Drag past the minimum — clamps to 64x64; the window never collapses or inverts.
3. `GUI_EV_RESIZE` dropped because the queue was full — the frame is restored to the application's current size straight away, not on a later commit that may never arrive.
4. Resize while the application has uncommitted damage — the compositor keeps blitting only the declared size, so partial content is never read outside it.
5. Stride change in either direction — growing the width copies bottom-up and shrinking copies top-down, so preserved content never shears.

---

### Task 1: Protocol — size limits, resizable flag, resize event

**Files:**
- Modify: `include/lions/gui/protocol.h`
- Create: `test/test_gui_size.c`
- Modify: `test/Makefile`

**Interfaces:**
- Produces: `GUI_EV_RESIZE`, `GUI_FLAG_RESIZABLE`, `GUI_MIN_WIDTH`, `GUI_MIN_HEIGHT`, `gui_size_fits()`, `gui_size_clamp()`. Consumed by Tasks 4, 5, 6 and 7.

- [ ] **Step 1: Write the failing test** in `test/test_gui_size.c`, following the shape of `test/test_fs_server_memory.c` (fork per case, `#include <lions/gui/protocol.h>`). Assert: a size using exactly the capacity fits; one pixel more does not; either dimension below the minimum does not; **a dimension above `GUI_MAX_DIMENSION` does not fit even when the area does** (3000x64 is 768,000 bytes, well inside 2 MiB, and must be rejected, because `src/compositor.c:506-507` would reject it and close the window); `gui_size_clamp` raises a 1x1 request to 64x64; clamping a request too wide for the surface lowers the height to fit and never below 64; clamping 3000x64 brings the width down to 2048; a fitting request is returned unchanged.

- [ ] **Step 2: Run it to verify it fails**

Run: `make -C test`
Expected: compile error, `gui_size_clamp` undeclared.

- [ ] **Step 3: Implement in `include/lions/gui/protocol.h`** (compiles host-side once `<stddef.h>` is added for `size_t`)

  - Add `#include <stddef.h>`; the header currently includes only `<stdint.h>` and `<stdbool.h>`, so `size_t` is undeclared.
  - `#define GUI_MIN_WIDTH 64`, `#define GUI_MIN_HEIGHT 64`, and `#define GUI_MAX_DIMENSION 2048`. These are the limits `src/compositor.c:506-507` already enforces, now in one place both sides use.
  - `static inline bool gui_size_fits(uint32_t width, uint32_t height, size_t surface_bytes)` — both dimensions within `[GUI_MIN_*, GUI_MAX_DIMENSION]` and `(uint64_t)width * height * sizeof(uint32_t) <= surface_bytes`.
  - `static inline void gui_size_clamp(uint32_t *width, uint32_t *height, size_t surface_bytes)` — the tests pin the behaviour but not the order of operations, so use this single pass, which always yields a fitting size:

```c
    uint32_t max_width = surface_bytes / (GUI_MIN_HEIGHT * sizeof(uint32_t));
    if (max_width > GUI_MAX_DIMENSION) { max_width = GUI_MAX_DIMENSION; }
    uint32_t w = *width < GUI_MIN_WIDTH ? GUI_MIN_WIDTH : *width;
    if (w > max_width) { w = max_width; }
    uint32_t max_height = surface_bytes / (w * sizeof(uint32_t));
    if (max_height > GUI_MAX_DIMENSION) { max_height = GUI_MAX_DIMENSION; }
    uint32_t h = *height < GUI_MIN_HEIGHT ? GUI_MIN_HEIGHT : *height;
    if (h > max_height) { h = max_height; }
    *width = w;
    *height = h;
```

  - Append `uint32_t flags;` to `gui_state_t` with `#define GUI_FLAG_RESIZABLE (1u << 0)`, documented as set by the application and only ever affecting its own window.
  - `#define GUI_EV_RESIZE 6`, documented as carrying the new content width in `x` and height in `y`, sent only to slots whose state carries `GUI_FLAG_RESIZABLE`.

- [ ] **Step 4: Make the compositor use the shared limit** — replace `#define APP_MAX_DIMENSION 2048` at `src/compositor.c:68` with `GUI_MAX_DIMENSION`, so the check at `src/compositor.c:506-507` cannot disagree with what `gui_size_clamp()` hands out.

- [ ] **Step 5: Add the build rule** to `test/Makefile`: `$(BUILD_DIR)/test_gui_size` from `test_gui_size.c` with `$(INCLUDES)` and `$(SANITIZERS)`, and list it in `run`. (No component source is needed — the helpers are static inline in the header.)

- [ ] **Step 6: Run the tests to verify they pass**

Run: `make -C test`
Expected: all suites pass, including `test_gui_size`.

- [ ] **Step 7: Commit**

```bash
git add include/lions/gui/protocol.h test/test_gui_size.c test/Makefile
git commit -m "lions/gui: add a resize event, a resizable flag and size limits"
```

### Task 2: Grow the surface to 2 MiB and move the state and event windows

**Files:**
- Modify: `examples/desktop/meta.py:43`, `examples/desktop/meta.py:253-259`, `examples/desktop/meta.py:273-275`
- Modify: `examples/desktop/include/gui_config.h:9-11`, `examples/desktop/include/gui_config.h:40`

**Interfaces:**
- Produces: `GUI_SURFACE_REGION_SIZE == 0x200_000`, state at `0x6300_0000`, events at `0x6400_0000`. Consumed by every later task.

- [ ] **Step 1: Set `GUI_SURFACE_REGION_SIZE` to `0x200_000`** in `meta.py` and, identically, in `gui_config.h`.

- [ ] **Step 2: Move the compositor's state maps to `0x6300_0000` and event maps to `0x6400_0000`** — both the application loop and the sandbox loop in `meta.py`. Surfaces keep their base of `0x6000_0000`, which now leaves 48 MiB, enough for 24 slots.

- [ ] **Step 3: Update the address comment** at `gui_config.h:9-11` to match. No compositor code changes: it derives every address from the symbols and strides (`src/compositor.c:1162-1164`).

- [ ] **Step 4: Build to verify**

Run: `cd examples/desktop && MICROKIT_BOARD=qemu_virt_aarch64 make`
Expected: builds, and the memory report shows the larger regions with no overlap.

- [ ] **Step 5: Build again with sandboxes**, to confirm the moved windows leave room. **This only works with a patched Microkit SDK:** `SANDBOX=1` needs the tool patch carried by `examples/dynamic_caps` for the `<cspace>` elements that give the host each sandbox's Untyped and window frames, and it is not built by CI for that reason (see the "Sandboxed apps (experimental)" section of `examples/desktop/README.md`). With the stock SDK, skip this step and say so in the commit message rather than treating it as a failure.
Expected: builds, when the patched SDK is available.

- [ ] **Step 6: Commit**

```bash
git add examples/desktop/meta.py examples/desktop/include/gui_config.h
git commit -m "examples/desktop: 2 MiB surfaces, moving the state and event windows"
```

### Task 3: Stride-change relayout helper, with a host test

**Files:**
- Modify: `examples/desktop/src/gfx.h`, `examples/desktop/src/gfx.c`
- Create: `test/test_gui_relayout.c`
- Modify: `test/Makefile`

**Interfaces:**
- Produces: `void gfx_relayout(uint32_t *pixels, uint32_t old_width, uint32_t old_height, uint32_t new_width, uint32_t new_height, uint32_t fill)`. Consumed by Task 4. `gfx.c` is freestanding (no Microkit), so it compiles host-side.

- [ ] **Step 1: Write the failing test** in `test/test_gui_relayout.c`. Fill a buffer with a known pattern at one size, relayout to another, assert the overlapping region is preserved and correct, and assert that every pixel **outside** the overlap equals `fill` — that is the point: the newly exposed area must be defined, not left holding stale pixels from the old layout. Cases: wider, narrower, taller, shorter, and unchanged. Assert under ASan that nothing is read or written outside the buffer.

- [ ] **Step 2: Run it to verify it fails**

Run: `make -C test`
Expected: compile error, `gfx_relayout` undeclared.

- [ ] **Step 3: Implement `gfx_relayout()` in `examples/desktop/src/gfx.c`**

Copy `min(old_width, new_width)` by `min(old_height, new_height)` pixels from the old stride to the new one, and fill every pixel of the new layout outside that overlap with `fill`. The copy is in place, so the direction is what the tests pin and the signature does not say: iterate rows **bottom-up when `new_width > old_width`** and **top-down otherwise**, so a row is never overwritten before it is read. Fill the exposed right-hand strip and bottom band first, so the copy never has to avoid them.

- [ ] **Step 4: Add the build rule** to `test/Makefile`: compile `examples/desktop/src/gfx.c` with `test_gui_relayout.c`, adding `-I$(LIONSOS)/examples/desktop/src` to that rule's includes, and list the binary in `run`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make -C test`
Expected: all suites pass, including `test_gui_relayout`.

- [ ] **Step 6: Commit**

```bash
git add examples/desktop/src/gfx.c examples/desktop/src/gfx.h test/test_gui_relayout.c test/Makefile
git commit -m "examples/desktop: relayout a surface across a stride change"
```

### Task 4: Application-side resize API

**Files:**
- Modify: `examples/desktop/apps/gui_app.h`, `examples/desktop/apps/gui_app.c`

**Interfaces:**
- Consumes: `gui_size_fits()`, `GUI_FLAG_RESIZABLE` (Task 1), `gfx_relayout()` (Task 3).
- Produces: `gui_app_resize()`, `gui_app_resize_preserve()`, `gui_app_set_flags()`. Consumed by Tasks 6 and 7.
- [ ] **Step 1: Add `void gui_app_set_flags(uint32_t flags)`** — writes `state->flags`. Applications call it from `init()` after `gui_app_init()`.

- [ ] **Step 2: Add `bool gui_app_resize(uint32_t width, uint32_t height)`** — return false if `!gui_size_fits(width, height, GUI_SURFACE_REGION_SIZE)`. Otherwise update the surface's width, height and stride, `gfx_reset_clip()`, and set `state->width` and `state->height`. It does not commit; the caller redraws.

- [ ] **Step 3: Add `bool gui_app_resize_preserve(uint32_t width, uint32_t height, uint32_t fill)`** — as above, but call `gfx_relayout()` on the surface pixels with the old and new dimensions **before** updating them, passing `fill` so the area the resize exposes is painted rather than left holding stale pixels. The caller picks `fill`: Sketch passes its canvas background.

- [ ] **Step 4: Build to verify**

Run: `cd examples/desktop && MICROKIT_BOARD=qemu_virt_aarch64 make`
Expected: builds; no application calls the new functions yet, so behaviour is unchanged.

- [ ] **Step 5: Commit**

```bash
git add examples/desktop/apps/gui_app.c examples/desktop/apps/gui_app.h
git commit -m "examples/desktop: let an application resize its surface"
```

### Task 5: Compositor — handle, grab and resize event

**Files:**
- Modify: `examples/desktop/src/compositor.c`

**Interfaces:**
- Consumes: `GUI_FLAG_RESIZABLE`, `GUI_EV_RESIZE`, `gui_size_clamp()` (Task 1).
- Produces: the corner handle and the grab behaviour. Consumed by Tasks 6 and 7, which rely on the event arriving as specified.

- [ ] **Step 1: Track resizability** — where the compositor validates a commit (`src/compositor.c:494-547`), also read `state->flags` and store whether the slot is resizable, validated like everything else from that page.

- [ ] **Step 2: Add grab state** next to the existing drag state (`src/compositor.c:161-162`): a `resize_slot` initialised to -1, plus pending width and height.

- [ ] **Step 3: Add `resize_handle_rect()`** alongside the existing `frame_rect()`/`content_rect()`/`close_rect()` helpers (`src/compositor.c:244-256`): roughly a 12x12 area at the bottom-right of the frame.

- [ ] **Step 4: Hit-test in `pointer_pressed()`** (`src/compositor.c:847`): if the press lands in the handle of a resizable window, set `resize_slot` instead of starting a move.

- [ ] **Step 5: Update the pending size on motion** (the branch at `src/compositor.c:837`): when `resize_slot >= 0`, derive the content size from the pointer position relative to the content origin, clamp it with `gui_size_clamp(…, GUI_SURFACE_REGION_SIZE)`, and damage the rows covered by both the old and the new frame, as `move_window()` does.

- [ ] **Step 6: Make `frame_rect()` use the pending size** while `resize_slot` is the slot in question, and leave `content_rect()` on the application's declared size. That is what produces the letterboxed transient with no scaling code.

- [ ] **Step 7: On release** (`src/compositor.c:911`): clamp once more, then enqueue `GUI_EV_RESIZE` with the width in `x` and the height in `y` using the existing enqueue at `src/compositor.c:291`. **If the enqueue fails, restore the frame to the application's current size immediately** — clear the pending size and redamage the frame — because the application may never commit again, and waiting for a commit that never comes would leave it letterboxed forever. Only if the enqueue succeeds does the pending size stay until the next commit, which then also clears it as a backstop.

- [ ] **Step 8: Build to verify**

Run: `cd examples/desktop && MICROKIT_BOARD=qemu_virt_aarch64 make`
Expected: builds. No window sets the flag yet, so no handle appears anywhere.

- [ ] **Step 9: Commit**

```bash
git add examples/desktop/src/compositor.c
git commit -m "examples/desktop: resize a window by dragging its corner"
```

### Task 6: Notes becomes resizable

**Files:**
- Modify: `examples/desktop/apps/notes.c`

**Interfaces:**
- Consumes: `gui_app_set_flags()`, `gui_app_resize()` (Task 4); `GUI_FLAG_RESIZABLE`, `GUI_EV_RESIZE` (Task 1).

- [ ] **Step 1: Set the flag** in `init()` after `gui_app_init()` (`apps/notes.c:104`).

- [ ] **Step 2: Make the layout runtime-sized** — `draw()` (`apps/notes.c:35`) currently fills `WIDTH`/`HEIGHT` and derives `COLS`/`ROWS` from those macros. Take the dimensions from `gui_app_surface()` instead and compute columns and rows per draw. Replace the `char line[COLS + 1]` buffer with a fixed `char line[NOTES_MAX + 1]`, since the text is bounded anyway. Keep `WIDTH`/`HEIGHT` only as the initial size passed to `gui_app_init()`.

- [ ] **Step 3: Handle `GUI_EV_RESIZE`** in the event loop (`apps/notes.c:117`): call `gui_app_resize(ev->x, ev->y)` and, if it succeeds, `draw()`. The text buffer is the model, so re-wrapping is just the existing wrap logic running at the new width.

- [ ] **Step 4: Build to verify**

Run: `cd examples/desktop && MICROKIT_BOARD=qemu_virt_aarch64 make`
Expected: builds.

- [ ] **Step 5: Commit**

```bash
git add examples/desktop/apps/notes.c
git commit -m "examples/desktop: make the Notes window resizable"
```

### Task 7: Sketch becomes resizable

**Files:**
- Modify: `examples/desktop/apps/sketch.c`

**Interfaces:**
- Consumes: `gui_app_set_flags()`, `gui_app_resize_preserve()` (Task 4); `GUI_FLAG_RESIZABLE`, `GUI_EV_RESIZE` (Task 1).

- [ ] **Step 1: Set the flag** in `init()` after `gui_app_init()` (`apps/sketch.c:151`).

- [ ] **Step 2: Make the layout runtime-sized** — the toolbar, canvas rect, clip and damage accumulation all use the `WIDTH`/`HEIGHT` macros (`apps/sketch.c:60-90`). Take the dimensions from `gui_app_surface()` instead, keeping the macros only as the initial size.

- [ ] **Step 3: Handle `GUI_EV_RESIZE`** in the event loop (`apps/sketch.c:168`): call `gui_app_resize_preserve(ev->x, ev->y, COLOUR_CANVAS)` so the drawing survives and the area the resize exposes is painted with the canvas background, then redraw the toolbar and commit the whole surface.

- [ ] **Step 4: Build to verify**

Run: `cd examples/desktop && MICROKIT_BOARD=qemu_virt_aarch64 make`
Expected: builds.

- [ ] **Step 5: Commit**

```bash
git add examples/desktop/apps/sketch.c
git commit -m "examples/desktop: make the Sketch window resizable, keeping the drawing"
```

### Task 8: End-to-end verification

**Files:** none

- [ ] **Step 1: Run the host tests**

Run: `make -C test`
Expected: all suites pass, including `test_gui_size` and `test_gui_relayout`.

- [ ] **Step 2: Build both configurations**

Run: `cd examples/desktop && MICROKIT_BOARD=qemu_virt_aarch64 make`, then the same with sandboxes **if a patched Microkit SDK is available** (see Task 2 step 5).
Expected: the plain build succeeds; the sandbox build succeeds only with the patched SDK, and skipping it is not a failure.

- [ ] **Step 3: Check the review-focus cases in QEMU**, since nothing else can

Expected: dragging Notes' corner re-wraps the text; dragging Sketch's corner keeps the drawing; the window cannot be dragged below 64x64 or above the largest size fitting the surface; Clock, Calculator, Widgets and the WebAssembly windows show no handle and are unchanged.

- [ ] **Step 4: Verify the two cases QEMU cannot provoke on demand**, by reading the code and reasoning about it, and record the result in the commit message

  - A `GUI_EV_RESIZE` that the queue drops: the pending size is cleared by the slot's next commit, so the frame returns to the application's real size instead of staying letterboxed.
  - A resize arriving while the application has uncommitted damage: the compositor's blit keeps using the declared size, so the partial content is never read outside it.

- [ ] **Step 5: Update the docs** — note the new surface size and the moved windows in `examples/desktop/README.md` if it states either, and add the resize event to the protocol's event list documentation.

- [ ] **Step 6: Commit**

```bash
git add examples/desktop/README.md
git commit -m "docs: note the larger surfaces and the resize event"
```
