# The Lions Operating System

## Getting started with LionsOS

Right now we are working towards making the new Kitty based on Microkit using the seL4 Device
Driver Framework and libvmm. Kitty is a Point-Of-Sale (POS) system for the snacks and drinks in the
lab.

This repository contains the build-system for the Kitty example system and other
infrastructure that we will need. It also contains the sDDF and libvmm
repositories. In the future, we might change the structure but for now we will use
Git submodules to version control LionsOS.

For documentation on LionsOS itself as well as teh Kitty system, see the
[LionsOS website](https://lionsos.org).

## Development

The following section is for developers of LionsOS/Kitty.

### Working on Kitty components

For now, all experimentation is to be done in the respective repository.

* If you are working on a driver for LionsOS, create an example system in sDDF and experiment
  there before integrating with LionsOS. See the
  [sDDF README for details](https://github.com/au-ts/sddf/tree/lionsos#adding-a-new-driver).
    * Make sure that you are working on top of the `lionsos` branch [here](https://github.com/au-ts/sddf/tree/lionsos).
      Before making a PR, push your code to `<name>/<branch-name>` on the `au-ts`
      repository. Then, make a PR from `<name>/<branch-name>` to merge into `lionsos`.
* If you are working on libvmm, create an example system and experiment there
  before integrating with LionsOS.
    * After you have something working, make a PR [here](https://github.com/au-ts/libvmm).

### Useful links
* [Microkit Tutorial](https://trustworthy.systems/projects/microkit/tutorial/)
* [Microkit manual](https://github.com/Ivan-Velickovic/microkit/blob/dev/docs/manual.md)
* [Microkit development source code](https://github.com/Ivan-Velickovic/microkit)
* [Odroid-C4 wiki](https://wiki.odroid.com/odroid-c4/odroid-c4)
* [Odroid-C4 SoC Techincal Reference Manual](https://dn.odroid.com/S905X3/ODROID-C4/Docs/S905X3_Public_Datasheet_Hardkernel.pdf)
