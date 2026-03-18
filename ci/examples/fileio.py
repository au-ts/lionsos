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


LIONSOS = Path(__file__).parents[2]
mkvirtdisk = (LIONSOS / "dep" / "sddf" / "tools" / "mkvirtdisk").resolve()


def backend_fn(test_config: common.TestConfig, loader_img: Path) -> HardwareBackend:
    backend = common.backend_fn(test_config, loader_img)

    if isinstance(backend, QemuBackend):
        tmpdir = tempfile.TemporaryDirectory(suffix="lionsos_blk_disks")

        (fd, disk_path) = tempfile.mkstemp(dir=tmpdir.name)
        os.close(fd)

        subprocess.run(
            [mkvirtdisk, disk_path, "1", "512", "16777216", "GPT"],
            check=True,
            capture_output=True,
        )

        # fmt: off
        backend.invocation_args.extend([
            "-global", "virtio-mmio.force-legacy=false",
            "-drive", "file={},if=none,format=raw,id=hd".format(disk_path),
            "-device", "virtio-blk-device,drive=hd",
        ])
        # fmt: on

        orig_stop = backend.stop

        async def stop_with_cleanup(self):
            try:
                await orig_stop()
            finally:
                tmpdir.cleanup()

        backend.stop = types.MethodType(stop_with_cleanup, backend)

    return backend


async def test(backend: HardwareBackend, test_config: common.TestConfig):
    async with asyncio.timeout(30):
        await wait_for_output(backend, b"MicroPython v1")
        await wait_for_output(backend, b"\r\n")
        await wait_for_output(backend, b">>> ")

        # TODO: ??? What is the test ???


# export
TEST_CASES = matrix.generate_example_test_cases(
    "fileio",
    matrix.EXAMPLES["fileio"],
    test_fn=test,
    backend_fn=backend_fn,
    no_output_timeout_s=matrix.NO_OUTPUT_DEFAULT_TIMEOUT_S,
)


if __name__ == "__main__":
    common.run_tests(TEST_CASES)
