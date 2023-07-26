let
    pkgs = import <nixpkgs> {};
in
  pkgs.mkShell {
    buildInputs = with pkgs.buildPackages; [
        unzip
        gnumake
		dtc
        llvmPackages_16.clang
        llvmPackages_16.lld
    ];
}

