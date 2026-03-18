#!/usr/bin/env python3
# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import asyncio
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types

from ts_ci import (
    HardwareBackend,
    QemuBackend,
    wait_for_output,
)

sys.path.insert(1, Path(__file__).parents[2].as_posix())
from ci import common, matrix


def backend_fn(test_config: common.TestConfig, loader_img: Path) -> HardwareBackend:
    backend = common.backend_fn(test_config, loader_img)

    if isinstance(backend, QemuBackend):
        # fmt: off
        backend.invocation_args.extend([
            "-global", "virtio-mmio.force-legacy=false",
            "-device", "virtio-net-device,netdev=netdev0,bus=virtio-mmio-bus.0",
            "-netdev", "user,id=netdev0,hostfwd=tcp::5555-10.0.2.16:80",
        ])
        # fmt: on

    return backend


async def test(backend: HardwareBackend, test_config: common.TestConfig):
    async with asyncio.timeout(30):
        await wait_for_output(backend, b"MP|INFO: initialising!\r\n")
        await wait_for_output(backend, b"MicroPython v1")
        await wait_for_output(backend, b"\r\n")
        await wait_for_output(backend, b">>> ")

        # TODO: ??? What is the test ???


# export
TEST_CASES = matrix.generate_example_test_cases(
    "webserver",
    matrix.EXAMPLES["webserver"],
    test_fn=test,
    backend_fn=backend_fn,
    no_output_timeout_s=matrix.NO_OUTPUT_DEFAULT_TIMEOUT_S,
)


if __name__ == "__main__":
    common.run_tests(TEST_CASES)
