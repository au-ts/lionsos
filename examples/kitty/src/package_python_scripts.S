/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

.section .kitty_python_script, "aw", @progbits
.global _kitty_python_script, _kitty_python_script_end
_kitty_python_script:
.incbin KITTY_PYTHON_SCRIPT_PATH
