let
    pkgs = import <nixpkgs> {};
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
    ];
    hardeningDisable = [ "all" ];
}

