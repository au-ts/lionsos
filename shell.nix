# Copyright 2024, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

let
    pkgs = import <nixpkgs> {};
in
  pkgs.mkShell {
    buildInputs = with pkgs.buildPackages; [
        cmake
        curl
        python39
        unzip
        gnumake
        dtc
        llvmPackages_16.clang
        llvmPackages_16.lld
        llvmPackages_16.llvm
        dosfstools
        qemu
    ];
    hardeningDisable = [ "all" ];
}

