<!--
     Copyright 2024, UNSW
     SPDX-License-Identifier: CC-BY-SA-4.0
-->

# The Lions Operating System

This is the source code for LionsOS, an operating system based on the
seL4 microkernel using the seL4 Microkit.

LionsOS is aimed at embedded, IoT and cyberphysical systems and is designed to
be formally verifiable, adaptable to a wide class of use cases in the target
domain, while at the same time setting the benchmark for performance of
microkernel-based operating systems. We aim to achieve all three goals by a
highly modular yet ruthlessly performance-oriented design and strict adherence
to the time-honoured KISS principle.

LionsOS and its verification story are under active research and development.

For more information, see the [website](https://lionsos.org).

## LLMs LionsOS

We do not allow LLM-authored pull-requests, commits or prose in this repository as they complicated maintenance.

* All prose in issues and pull-requests should be written by you (usage of translation tools is okay).
* Any PRs with code that is clearly labelled as being authored by an LLM such as Claude or Copilot will be closed. We find that these tend to introduce unnecessarily complex code and can be hard to review.
* We expect all contributors to understand their contribution and take responsibility for their code. LLMs can perform surprisingly poorly on experimental projects like LionsOS, so please take care if you use LLM assistance.

If you find an issue with an LLM, please open an issue to report it.
