let
    pkgs = import <nixpkgs> {};
    cross = import <nixpkgs> {
        crossSystem = { config = "aarch64-none-elf"; };
    };
in
  pkgs.mkShell {
    buildInputs = with pkgs.buildPackages; [
        jq
        curl
        python39
        unzip
        gnumake
        dtc
        llvmPackages_16.clang
        llvmPackages_16.lld
        cross.buildPackages.gcc10
    ];
}

