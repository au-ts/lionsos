<!--
     Copyright 2024, UNSW
     SPDX-License-Identifier: CC-BY-SA-4.0
-->

# CI for LionsOS

## Examples

The CI currently checks that each example system builds successfully.
Right now there are no runtime checks.

If you have not run any LionsOS example systems before, please see
the instructions at https://lionsos.org/docs/kitty/building/ for
getting the source code and dependencies.

You can run the CI example script with:
```
./ci/examples.sh /path/to/lionsos /path/to/microkit/sdk
```

Note that the paths should be absolute paths, not relative paths.
