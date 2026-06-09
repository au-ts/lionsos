let
  nixpkgs = builtins.fetchTarball {
    name = "source";
    url = "https://github.com/nixos/nixpkgs/archive/da044451c6a70518db5b730fe277b70f494188f1.tar.gz";
    sha256 = "sha256:11z08fa0s7r9hryllhjj7kyn4z6bsixlqz7iwgsmf1k4p3hcl692";
  };

in

{ pkgs ? (import nixpkgs {
    overlays = [
      (self: super: {
        python3 = super.python3.override {
          packageOverrides = _: pySuper: {
            pyfdt = pySuper.buildPythonPackage rec {
              name = "pyfdt";
              src = pySuper.fetchPypi {
                pname = name;
                version = "0.3";
                sha256 = "sha256-YWAcIAX/OUolpshMbaIIi7+IgygDhADSfk7rGwS59PA=";
              };
            };
          };
        };
      })
    ];
  })
}:

pkgs.mkShellNoCC {
  name = "time-protection-sel4";

  nativeBuildInputs = with pkgs; [
    qemu
    cacert
    cmake
    cpio
    dtc
    gdb
    ubootTools
    # (pkgsCross.riscv64-embedded.stdenv.cc.cc.override { enableMultilib = true; })
    # pkgsCross.riscv64-embedded.stdenv.cc.cc
    # pkgsCross.riscv64-embedded.stdenv.cc.bintools.bintools
    pkgsCross.aarch64-embedded.stdenv.cc.cc
    pkgsCross.aarch64-embedded.stdenv.cc.bintools.bintools
    # pkgsCross.arm-embedded.stdenv.cc.cc
    # pkgsCross.arm-embedded.stdenv.cc.bintools.bintools
    libxml2
    ninja
    # camkes. not this doesn't like nix so needs gmp installed
    pkgs.stack
    # cheshire
    pkgs.gptfdisk
    pkgs.openfpgaloader

    (pkgs.stdenv.mkDerivation rec {
      pname = "bender";
      version = "v0.29.0";

      src = pkgs.fetchzip {
        url = "https://github.com/pulp-platform/bender/releases/download/v0.29.0/bender-0.29.0-x86_64-linux-gnu.tar.gz";
        hash = "sha256-ssVqe1d8a3XtFDMAZJHomY34IAu/tGFuvLxdaTh/R2M=";
      };

      installPhase = ''
        mkdir -p $out/bin
        cp $src/bender $out/bin/
      '';
    })

    # openjdk # leakiest
  ];

  env.CMAKE_EXPORT_COMPILE_COMMANDS = "1";
}

