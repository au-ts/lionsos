# I hate make with all my heart.
# Copies the relevant makefiles into the build directory
# And then you can build the project by running:
#    "make" or "make qemu" or "make qemud"
import argparse
import pathlib as pl
import os

MAKE_PY_PATH = pl.Path(__file__)
RR_COMPONENT_PATH = pl.Path(MAKE_PY_PATH.parent)
TEST_PATH = pl.Path(RR_COMPONENT_PATH / "tests")
LIONSOS = pl.Path(RR_COMPONENT_PATH.parent.parent)
MICROKIT_SDK = pl.Path(os.environ.get("MICROKIT_SDK"))
MICROKIT_BOARD = os.environ.get("MICROKIT_BOARD", default="qemu_virt_aarch64")
MICROKIT_CONFIG = os.environ.get("MICROKIT_CONFIG", default="debug")
BUILD_DIR = pl.Path(RR_COMPONENT_PATH / "build")

assert MAKE_PY_PATH.exists()
assert RR_COMPONENT_PATH.exists()
assert TEST_PATH.exists()
assert LIONSOS.exists()
assert MICROKIT_SDK.exists()

def main():
    ap = argparse.ArgumentParser()
    test_choices = [child.name for child in TEST_PATH.iterdir()]
    ap.add_argument("--test-target", required=True, choices=test_choices)

    args = ap.parse_args()

    test_target_dir = pl.Path(TEST_PATH / args.test_target)

    assert test_target_dir.exists()

    BUILD_DIR.mkdir(exist_ok=True)
    makefile_path = pl.Path(BUILD_DIR / "Makefile")
    rr_component_mk_path = pl.Path(test_target_dir / "rr_component.mk")

    assert rr_component_mk_path.exists()

    with makefile_path.open(mode="w") as makefile:
        makefile.write(
        f"""\
LIONSOS ?= {str(LIONSOS)}
RR_COMPONENT_DIR ?= {str(RR_COMPONENT_PATH)}
BUILD_DIR := {str(BUILD_DIR)}
MICROKIT_BOARD ?= {MICROKIT_BOARD}
MICROKIT_SDK ?= {str(MICROKIT_SDK)}
MICROKIT_CONFIG ?= {MICROKIT_CONFIG}
TOP_DIR := {str(test_target_dir)}
CFLAGS += -Werror -DARCH_aarch64
{rr_component_mk_path.read_text(encoding="utf-8")}\
        """)

if __name__ == "__main__":
    main()
