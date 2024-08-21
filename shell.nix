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

