<!--
     Copyright 2024, UNSW
     Copyright 2026, Chase Bryan
     SPDX-License-Identifier: CC-BY-SA-4.0
-->

# EaglesOS

**EaglesOS** is a downstream operating-system project maintained by Chase Bryan.
It begins as a fork of [LionsOS](https://github.com/au-ts/lionsos), an operating
system built on the seL4 microkernel with the seL4 Microkit.

> **Status:** fork bootstrap. The inherited baseline has not yet been
> independently validated as an EaglesOS build. EaglesOS currently makes no
> project-specific claim of security, performance, hardware support, or formal
> verification.

## Purpose

EaglesOS carries forward the modular, performance-oriented foundation of
LionsOS as the starting point for a distinct line of systems research and
development.

The initial priorities are to:

- reproduce and document the inherited build;
- inventory source, submodule, toolchain, and hardware dependencies;
- establish EaglesOS-specific supported targets and acceptance tests;
- preserve explicit component boundaries and least-authority design;
- measure divergence from upstream at every release; and
- attach every security or verification statement to evidence that applies to
  the exact EaglesOS revision being described.

## Upstream baseline

EaglesOS was created on July 28, 2026 from:

| Field | Baseline |
| --- | --- |
| Upstream | `au-ts/lionsos` |
| Branch | `main` |
| Commit | `4a5656a32574049817f62054832abeae85861ff5` |
| Commit date | July 15, 2026 (UTC) |
| Foundation | seL4 Microkit and the inherited LionsOS component architecture |

The upstream commit history, copyright notices, SPDX identifiers, license
texts, and authorship records are intentionally retained.

EaglesOS is not affiliated with or endorsed by UNSW, the Trustworthy Systems
group, LionsOS, the seL4 Foundation, or upstream contributors. Security,
performance, and verification results published for upstream LionsOS do not
automatically apply after EaglesOS changes the inherited source.

## Compatibility during bootstrap

Inherited paths and programmatic identifiers such as `include/lions` remain in
place initially. Renaming those interfaces without a compatibility plan would
create unnecessary breakage and obscure whether the upstream baseline still
builds. Identity changes therefore begin at the project boundary; internal
renaming will occur only through tested, reviewable changes.

## Licensing

Licensing remains file-specific and is identified by each file's SPDX header.
The root [LICENSE](LICENSE) contains the BSD 2-Clause license. Documentation is
generally licensed under CC BY-SA 4.0 as described in
[LICENSE.md](LICENSE.md). When those summaries and an individual SPDX header
differ, the individual file header controls.

## Next gate

The first EaglesOS development gate is a reproducible upstream-baseline build
with recorded host prerequisites, submodule revisions, target configuration,
commands, outputs, and failures. Until that evidence exists, this repository is
a correctly attributed fork—not a validated EaglesOS release.
