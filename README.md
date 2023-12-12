# The Lions Operating System

## Getting started with LionsOS

Right now we are working towards making the new Kitty based on Microkit using the seL4 Device
Driver Framework and libvmm. Kitty is a Point-Of-Sale (POS) system for the snacks and drinks in the
lab.

This repository contains the build-system for the Kitty example system and other
infrastructure that we will need. It also contains the sDDF and libvmm
repositories. In the future, we might change the structure but for now we will use
Git submodules to version control LionsOS.

### Installing dependencies

On Ubuntu/Debian:
```sh
sudo apt update && sudo apt install make clang lld device-tree-compiler unzip git
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

### Getting the Microkit SDK

There are a lot of changes to Microkit that have been made over the past year which means
we cannot use the mainline one just yet.

When developing LionsOS, there should be no reason to modify the SDK itself, hence you can
download the pre-built one that the CI builds for every commit.

1. Go to https://github.com/Ivan-Velickovic/microkit/actions/runs/6453315358
2. Scroll to the bottom and you will see "Artifacts" and below that you can
choose what SDK you need depending on what computer you are working on.
3. Click on the SDK you want to download and it should start downloading.

Now you can unpack the SDK (this is on Linux but if you're on macOS just replace the name of the ZIP):
```sh
unzip microkit-sdk-dev-f97a02d-linux-x86-64.zip
tar xf microkit-sdk-1.2.6.tar.gz
```

If you have *any* issues with Microkit or think something about it can be improved,
tell Ivan or open an issue on the GitHub.

### Building and running the Kitty example

To build Kitty run:
```sh
# Clone the LionsOS repository along with the submodules (e.g sDDF and the VMM)
git clone git@github.com:au-ts/LionsOS.git
cd LionsOS
git submodule update --init
# Enter the Kitty demo directory
cd examples/kitty
# Now compile the demo
make MICROKIT_SDK=/path/to/sdk
```

If you need to build a release version of Kitty instead run:
```sh
make MICROKIT_SDK=/path/to/sdk MICROKIT_CONFIG=release
```

After building, then you can use machine queue to run `build/kitty.img`. Right now you
should see the message:
```
MICROPYTHON|INFO: initialising!
MicroPython ac783c460-dirty on 2023-07-31; Odroid-C4 with Cortex A55
>>>
```

This shows the basic MicroPython REPL, if you want to input into the REPL you can do so
by connecting to the console via TFTP with `console odroidc4-<NUM> -f`. You can also run
MQ with the `-a` flag to automatically get a connection where you can also input, like so:
```sh
mq.sh run -c "MicroPython" -a -l mqlog -s odroidc4_pool -f build/kitty.img
```

## Working on Kitty components

For now, all experimentation is to be done in the respective repository.

* If you are working on a driver for LionsOS, create an example system in sDDF and experiment
  there before integrating with LionsOS. See the
  [sDDF README for details](https://github.com/au-ts/sddf/tree/lionsos#adding-a-new-driver).
    * Make sure that you are working on top of the `lionsos` branch [here](https://github.com/au-ts/sddf/tree/lionsos).
      Before making a PR, push your code to `<name>/<branch-name>` on the `au-ts`
      repository. Then, make a PR from `<name>/<branch-name>` to merge into `restructure`.
* If you are working on libvmm, create an example system and experiment there
  before integrating with LionsOS.
    * After you have something working, make a PR [here](https://github.com/au-ts/libvmm).

### Integrating with Kitty example

TODO

## Documentation

The documentation of the project lives in `docs/`.

To view the web-version of the documentation run the following:
```
# Install dependencies
cargo install --locked mdbook mdbook-variables
cd docs
# Build and open the documentation
mdbook serve --open
```

## Useful links
* [Microkit Tutorial](https://trustworthy.systems/projects/microkit/tutorial/)
* [Microkit manual](https://github.com/Ivan-Velickovic/microkit/blob/dev/docs/manual.md)
* [Microkit development source code](https://github.com/Ivan-Velickovic/microkit)
* [Odroid-C4 wiki](https://wiki.odroid.com/odroid-c4/odroid-c4)
* [Odroid-C4 SoC Techincal Reference Manual](https://dn.odroid.com/S905X3/ODROID-C4/Docs/S905X3_Public_Datasheet_Hardkernel.pdf)

