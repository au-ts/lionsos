# The KISS Operating System

The repository for the KISS Operating System.

## Getting started

### Installing dependencies:

On Ubuntu/Debian:
```sh
sudo apt update && sudo apt install make gcc-aarch64-linux-gnu device-tree-compiler
```

On macOS:
```
# Homebrew does not provide ARM cross compilers by default, so we use
# this repository (https://github.com/messense/homebrew-macos-cross-toolchains).
brew tap messense/macos-cross-toolchains
brew install make aarch64-unknown-linux-gnu dtc
```

### Getting the seL4CP SDK

There are a lot of changes to seL4CP that have been made over the past year which means
we cannot use the mainline one just yet. Thankfully, it should be fairly straight-forward
to get the SDK off of Ivan's dev branch.

When developing KISS, there should be no reason to modify the SDK itself, hence you can
download the pre-built one that the CI builds for every commit.

To do this, you want to first go to https://github.com/Ivan-Velickovic/sel4cp/actions/runs/5511099343.
If you scroll to the bottom you will see "Artifacts" and below that you can
choose what SDK you need depending on what computer you are working on. Click
on the SDK you want to download and it should start downloading, unfortunately
GitHub is annoying and right now you cannot just `wget` the SDK.

For example if you downloaded the Linux SDK you can unpack it with:
```sh
unzip sel4cp-sdk-dev-bf06734-linux-x86-64
tar xf sel4cp-sdk-1.2.6.tar.gz
```

If for some reason you need to build the SDK yourself, follow the instructions
[here](https://github.com/Ivan-Velickovic/sel4cp) or talk to Ivan.

## How KISS works right now

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
* [ ] VMM with graphics either passed-through or via driver VM

## Useful links
* [seL4CP Tutorial](https://dsn.ivanvelickovic.com/)
* [seL4CP manual](https://github.com/Ivan-Velickovic/sel4cp/blob/dev/docs/manual.md)
* [seL4CP development source code](https://github.com/Ivan-Velickovic/sel4cp)
* [OdroidC4 wiki](https://wiki.odroid.com/odroid-c4/odroid-c4)
* [OdroidC4 Techincal Reference Manual](https://dn.odroid.com/S905X3/ODROID-C4/Docs/S905X3_Public_Datasheet_Hardkernel.pdf)

