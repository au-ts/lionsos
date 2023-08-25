# Setting up your Odroid-C4 hardware

## What you will need

* Power supply (TODO add detail)
* UART to USB adapter (3.3V)
* microSD card or eMMC module (TODO does it need to be a specific kind or a specific size? Probably can't be too big)

TODO pictures of setting everything up (UART, power, microSD or eMMC)

## Power and serial connections

TODO

## Flashing U-Boot

Follow the instructions [here](https://github.com/Ivan-Velickovic/flash_uboot_odroidc4).

TODO, reproduce the instructions from https://github.com/Ivan-Velickovic/flash_uboot_odroidc4
here.

TODO, have instructions for building custom U-Boot in case the reader needs some specific
config option in U-Boot that isn't in the default one.

TODO, an option is to have a tar.gz that contians the image *as well as* the `uboot.env` file
so that they don't have to configure U-Boot manually.

## Configuring U-Boot

When you see this message from U-Boot:
```
Hit any key to stop autoboot:  1
```

You will want to stop the autoboot immediately. From here,
we want to setup the U-Boot environment to automatically load
the system image. There are two main ways of getting system images
into U-Boot:
* The frst is booting via the network (**recommended**), which is where you have a TFTP server
  on some other computer that transfers the system image to U-Boot
  on the Odroid-C4 via the network.
* The other option is to use a storage device such as an SD card or eMMC.

TODO, only tested these instructions via SD card, not via eMMC yet.

### Option 1 - Booting via the network

By default U-Boot is set to auto-boot into a Linux system, we want to change that obviously.
This can be done by changing the value of the U-Boot environment variable `bootcmd`, the contents
of which is executed automatically when U-Boot starts, unless U-Boot is interrupted.

When you are in the U-Boot console, enter the following commands:

```
=> setenv bootcmd 'dhcp; tftpboot 0x20000000 <SYSTEM IMAGE PATH>; go 0x20000000'
=> saveenv
```

You should see the following output:
```
Saving Environment to MMC... Writing to MMC(0)... OK
```

Note that the `<SYSTEM IMAGE PATH>` will be the path to the system image in your TFTP server.
(TODO isn't is relative to some root directory of the TFTP server?)

From here, you will need to run `saveenv` in order to have the change persist across reboots.

Finally you can reboot your Odroid-C4 and after a couple of seconds it should automatically
boot into your system image.

### Option 2 - Booting from storage

TODO
