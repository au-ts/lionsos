#!/usr/bin/env python3
# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

from __future__ import annotations
import argparse
from datetime import datetime
from dataclasses import dataclass
from pathlib import Path
import itertools
import sys
from typing import Callable, Awaitable

sys.path.insert(1, Path(__file__).parents[1].as_posix())

from ts_ci import (
    add_runner_arguments,
    apply_runner_arguments,
    execute_tests,
    ArgparseActionList,
    HardwareBackend,
    QemuBackend,
    MachineQueueBackend,
    MACHINE_QUEUE_BOARDS,
    MACHINE_QUEUE_BOARD_OPTIONS,
    TestCase,
)

CI_BUILD_DIR = Path(__file__).parents[1] / "ci_build"


def example_build_path(test_config: TestConfig):
    return (
        CI_BUILD_DIR
        / "examples"
        / test_config.example
        / test_config.board
        / test_config.config
    )


def backend_fn(
    test_config: TestCase,
    loader_img: Path,
) -> HardwareBackend:

    if test_config.is_qemu():
        QEMU_COMMON_FLAGS = (
            # fmt: off
            "-m", "size=2G",
            "-serial", "mon:stdio",
            "-nographic",
            "-d", "guest_errors",
            # fmt: on
        )

        if test_config.board == "qemu_virt_aarch64":
            return QemuBackend(
                "qemu-system-aarch64",
                # fmt: off
                "-machine", "virt,virtualization=on",
                "-cpu", "cortex-a53",
                "-device", f"loader,file={loader_img.resolve()},addr=0x70000000,cpu-num=0",
                # fmt: on
                *QEMU_COMMON_FLAGS,
            )
        else:
            raise NotImplementedError(f"unknown qemu board {test_config.board}")

    else:
        mq_boards: list[str] = MACHINE_QUEUE_BOARDS[test_config.board]
        options = MACHINE_QUEUE_BOARD_OPTIONS.get(test_config.board, {})
        return MachineQueueBackend(loader_img.resolve(), mq_boards, **options)


TestFunction = Callable[[HardwareBackend, "TestConfig"], Awaitable[None]]
BackendFunction = Callable[["TestConfig", Path], HardwareBackend]


# Implements 'TestCase' protocol
@dataclass(order=True, frozen=True)
class TestConfig(TestCase):
    example: str
    board: str
    config: str

    extra_build_args: list[str]

    test_fn: TestFunction
    backend_fn: BackendFunction
    no_output_timeout_s: int

    def is_qemu(self):
        return self.board.startswith("qemu")

    def pretty_name(self) -> str:
        return f"{self.example} on {self.board} ({self.config})"

    def backend(self, loader_img: Path) -> HardwareBackend:
        return self.backend_fn(self, loader_img)

    def loader_img(self) -> Path:
        return example_build_path(self) / f"{self.example}.img"

    async def run(self, backend: HardwareBackend) -> None:
        await self.test_fn(backend, self)

    def log_file_path(self, logs_dir: Path, now: datetime) -> Path:
        return (
            logs_dir
            / self.example
            / self.board
            / self.config
            / f"{now.strftime('%Y-%m-%d_%H.%M.%S')}.log"
        )


def test_case_summary(tests: list[TestConfig]):
    if len(tests) == 0:
        return "   (none)"

    lines = []
    for example, subtests in itertools.groupby(tests, key=lambda c: c.example):
        lines.append(f"--- Example: {example} ---")

        for board, group in itertools.groupby(subtests, key=lambda c: c.board):
            lines.append(
                " - {}: {}".format(
                    board, ", ".join(f"{c.config}/make" for c in group)
                )
            )

    return "\n".join(lines)


def subset_test_cases(
    tests: list[TestConfig], filters: argparse.Namespace
) -> list[TestConfig]:
    # This works under the assumption that all elements of tests are the same
    # subset of TestConfig.

    def filter_check(test: TestConfig):
        implies = lambda a, b: not a or b
        return all(
            [
                (test.example in filters.examples),
                (test.board in filters.boards),
                (test.config in filters.configs),
                (implies(filters.only_qemu is True, test.is_qemu())),
                (implies(filters.only_qemu is False, not test.is_qemu())),
            ]
        )

    return list(sorted(set(filter(filter_check, tests))))


def run_tests(tests: list[TestConfig]) -> None:
    parser = argparse.ArgumentParser(description="Run tests")

    filters = parser.add_argument_group(title="filters")
    filters.add_argument(
        "--examples",
        default={test.example for test in tests},
        action=ArgparseActionList,
    )
    filters.add_argument(
        "--boards", default={test.board for test in tests}, action=ArgparseActionList
    )
    filters.add_argument(
        "--configs", default={test.config for test in tests}, action=ArgparseActionList
    )
    filters.add_argument(
        "--only-qemu",
        action=argparse.BooleanOptionalAction,
        help="select only QEMU tests",
    )

    add_runner_arguments(parser)

    args = parser.parse_args()

    filter_args = argparse.Namespace(
        **{a.dest: getattr(args, a.dest) for a in filters._group_actions}
    )

    tests = subset_test_cases(tests, filter_args)

    tests = apply_runner_arguments(parser, args, tests, test_case_summary)

    execute_tests(tests, args, test_case_summary)
