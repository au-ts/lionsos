# The KISS Operating System

The repository for the KISS Operating System.

## Getting started with KISS

Right now we are working towards making the new Kitty based on seL4CP using the seL4 Device
Driver Framework and VMM. Kitty is a Point-Of-Sale (POS) system for the snacks and drinks in the
lab.

This repository contains the build-system for the Kitty example system and other
infrastructure that we will need. It also contains the sDDF and seL4CP VMM
repositories. In the future, we might change the structure but for now we will use
Git submodules to version control KISS.

In order to get Kitty working we will need the following working on the Odroid-C4:
* [ ] UART serial driver
* [ ] I2C driver
* [ ] Ethernet driver
* [ ] Timer driver
* [ ] VMM with graphics either passed-through or via driver VM
* [ ] MicroPython interpreter

### Installing dependencies

On Ubuntu/Debian:
```sh
sudo apt update && sudo apt install make clang lld device-tree-compiler
```

On Arch Linux:
```sh
sudo pacman -Sy make clang lld dtc
```

On macOS:
```
brew install make llvm dtc
# Add LLD to PATH
echo export PATH="/opt/homebrew/Cellar/llvm/16.0.6/bin:$PATH" >> ~/.zshrc
source ~/.zshrc
```

On Nix/NixOS:
```sh
nix-shell --pure
```

### Getting the seL4CP SDK

There are a lot of changes to seL4CP that have been made over the past year which means
we cannot use the mainline one just yet.

When developing KISS, there should be no reason to modify the SDK itself, hence you can
download the pre-built one that the CI builds for every commit.

1. Go to https://github.com/Ivan-Velickovic/sel4cp/actions/runs/5511099343.
2. Scroll to the bottom and you will see "Artifcats" and below that you can
choose what SDK you need depending on what computer you are working on.
3. Click on the SDK you want to download and it should start downloading.

Now you can unpack the SDK (this is on Linux but if you're on macOS just replace the name of the ZIP):
```sh
unzip sel4cp-sdk-dev-bf06734-linux-x86-64.zip
tar xf sel4cp-sdk-1.2.6.tar.gz
```

If you have *any* issues with seL4CP or think something about it can be improved,
tell Ivan or open an issue on the GitHub.

### Building and running the Kitty example

To build Kitty run:
```sh
git clone git@github.com:au-ts/kiss.git --recursive
cd kiss/examples/kitty
make SEL4CP_SDK=/path/to/sel4cp-sdk-1.2.6
```

If you need to build a release version of Kitty instead run:
```sh
make SEL4CP_SDK=/path/to/sdk SEL4CP_CONFIG=release
```

After building, then you can use machine queue to run `build/kitty.img`. Right now you
should see the message:
```
KITTY|INFO: Welcome to Kitty!
```

## Working on Kitty components

For now, all experimentation is to be done in the respective repository.

* If you are working on a driver for KISS, create an example system in sDDF and experiment
  there before integrating with KISS. See the
  [sDDF README for details](https://github.com/au-ts/sddf/tree/restructure#adding-a-new-driver).
    * Make sure that you are working on top of the `restructure` branch [here](https://github.com/au-ts/sddf/tree/restructure).
      Before making a PR, puush your code to `<name>/<branch-name>` on the `au-ts`
      repository. Then, make a PR from `<name>/<branch-name>` to merge into `restructure`.
* If you are working on the VMM, create an example system in the VMM and experiment there
  before integrating with KISS.
    * After you have something working, make a PR [here](https://github.com/Ivan-Velickovic/sel4cp_vmm).

### Integrating with Kitty example

TODO

##

## Useful links
* [seL4CP Tutorial](https://dsn.ivanvelickovic.com/)
* [seL4CP manual](https://github.com/Ivan-Velickovic/sel4cp/blob/dev/docs/manual.md)
* [seL4CP development source code](https://github.com/Ivan-Velickovic/sel4cp)
* [Odroid-C4 wiki](https://wiki.odroid.com/odroid-c4/odroid-c4)
* [Odroid-C4 SoC Techincal Reference Manual](https://dn.odroid.com/S905X3/ODROID-C4/Docs/S905X3_Public_Datasheet_Hardkernel.pdf)

