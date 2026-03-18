#!/usr/bin/env python3
# Copyright 2026, UNSW
# SPDX-License-Identifier: BSD-2-Clause

from __future__ import annotations
from itertools import chain
from typing import TYPE_CHECKING, Any, Literal, Optional, Sequence, TypedDict

from ts_ci import MACHINE_QUEUE_BOARDS, matrix_product
from . import common
from .common import TestConfig, TestFunction, BackendFunction

NO_OUTPUT_DEFAULT_TIMEOUT_S: int = 60


EXAMPLES: dict[str, _ExampleMatrixType] = {
    "fileio": {
        "configs": ["debug"],
        "boards": [
            "maaxboard",
            "qemu_virt_aarch64",
        ],
        "tests_exclude": [],
    },
    "firewall": {
        "configs": ["debug", "release"],
        "boards": [
            "imx8mp_iotgate",
        ],
        "tests_exclude": [],
    },
    "kitty": {
        "configs": ["debug"],
        "boards": [
            "odroidc4",
            "qemu_virt_aarch64",
        ],
        "tests_exclude": [],
        "extra_build_args": [
            "NFS_SERVER=0.0.0.0",
            "NFS_DIRECTORY=test",
        ],
    },
    "posix_test": {
        "configs": ["debug"],
        "boards": [
            "qemu_virt_aarch64",
        ],
        "tests_exclude": [],
    },
    # FIXME: why wasn't this build in CI before?
    # "vmm": {
    #     "configs": ["debug"],
    #     "boards": [
    #         "odroidc4",
    #     ],
    #     "tests_exclude": [],
    # },
    "wasm_test": {
        "configs": ["debug"],
        "boards": [
            "qemu_virt_aarch64",
        ],
        "tests_exclude": [],
    },
    "webserver": {
        "configs": ["debug", "release"],
        "boards": [
            "odroidc4",
            "qemu_virt_aarch64",
        ],
        "tests_exclude": [],
        "extra_build_args": [
            "NFS_SERVER=0.0.0.0",
            "NFS_DIRECTORY=test",
            "WEBSITE_DIR=www",
        ],
    },
}

## Type Hinting + Sanity Checks ##
_BoardNames = Literal[
    "imx8mp_iotgate",
    "maaxboard",
    "odroidc4",
    "qemu_virt_aarch64",
]

known_board_names = set(MACHINE_QUEUE_BOARDS.keys()) | {
    # simulation boards
    "qemu_virt_aarch64",
    "qemu_virt_riscv64",
}
assert (
    set(_BoardNames.__args__) <= known_board_names  # type: ignore
), f"_BoardNames contains a board that is not valid {known_board_names ^ set(_BoardNames.__args__)}"  # type: ignore

for ex in EXAMPLES.values():
    for board in chain(
        ex["boards"], (excl["board"] for excl in ex["tests_exclude"] if "board" in excl)
    ):
        assert board in known_board_names, f"{board} not a valid board"

if TYPE_CHECKING:
    # only works in py3.11+, so we use a string below
    from typing import NotRequired

class _ExampleMatrixType(TypedDict):
    configs: list[Literal["debug", "release", "benchmark"]]
    boards: list[_BoardNames]
    tests_exclude: list[dict[str, str]]
    extra_build_args: "NotRequired[list[str]]"
