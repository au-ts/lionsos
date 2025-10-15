# seL4 HDMI Driver

This repo contains an seL4 HDMI driver that is compatible with NXP devices that use the Display Controller Subsystem (DCSS) e.g imx8m. To display moving images, double buffering has been implemented. This means that whilst the current frame is being displayed, the next frame is being written to. 

----------------------------------------------------------------------------------------

*******For moving images, a visible redraw of the screen is seen when switching between frames. This is most noticeable when the entire screen has changed (see example rotating_bars). For a list of attempted fixes to resolve this issue, please see "Attempted solutions for the HDMI syncing issue" at the end of this README.******* 

----------------------------------------------------------------------------------------
# Building the boot loader and firmware

**To build U-Boot, the dependencies and the project use the this [Docker environment](https://github.com/sel4devkit/sel4devkit-maaxboard-microkit-docker-dev-env)**

To use this driver the correct HDMI firmware needs to be flashed onto the Maaxboard. U-Boot can be built using from this [repository](https://github.com/sel4devkit/sel4devkit-maaxboard-bootloader-u-boot)

To ensure that the UBoot driver setup doesn't interfere with this driver, the following modifications need to be made.

Change the setting to "n" for ```CONFIG_DM_VIDEO``` in uboot-imx/configs/maaxboard_defconfig

```CONFIG_DM_VIDEO=n```

Change the status to disabled for HDMI in uboot-imx/arch/arm/dts/maaxboard.dts

```
&hdmi {
	compatible = "fsl,imx8mq-hdmi";
	status = "disabled";
	...
	}

```

# Building dependencies

To build picolibc:

```
cd dep/picolibc
./build-picolibc.sh
```
To get microkit:

```
cd dep/microkit
make get
make all
```

# Building the project

This API contains the following examples:

* static_image - Displays 4 colour bars on the screen.
* resolution_change - Displays a square of the same size in pixels one after another at three different resolutions.
* rotating_bars - Displays 4 colour bars rotating across the screen.
* moving_square - A small square that moves around the screen, changing direction each time it hits the side of the screen.

**See the README in the examples folder for more detail.**

To build the project use ./build.sh with an example as the first argument.

```./build.sh static_image ```

This will create the loader.img file that will need to be loaded into the maaxboard. The name of this can be changed in the top level Makefile.

For more information setting up an environment for creating seL4 applications see https://github.com/seL4devkit.

# Using the API

Microkit is used to create the seL4 image for this project. For more information on seL4 and Microkit see the [Microkit Manual](https://github.com/seL4/microkit/blob/main/docs/manual.md)

This API makes use of two Protection Domains (PD). 

* **dcss** - This PD is responsible for the setup of the driver and the configuration of the Display Controller Subsystem (DCSS) and the HDMI TX controller.
* **client** - This PD is responsible for the API and writing to the frame buffer. 

### Initialising the client PD

Only one example can be built at a time because each example handles the microkit setup by implementing ```init()``` and ```notified()``` 

```init()``` is responsible for making the call to initialise the API and to select if the current image will display a static or moving image. See ```static_image()``` and ```moving_image()``` defined in src/api/api.c.

These two functions take in a function pointer as an argument. This function pointers type signature takes no arguments and returns a ```display_config``` struct. In the examples this function is implemented as ```init_example()```. 

```
struct display_config init_example() {
}
```

The ```display_config``` struct needs to be initialised so that the configuration settings can be sent to the dcss PD and so that the function to write to the frame buffer is defined. 

```
struct display_config {
	struct hdmi_data hd;
	void (*write_fb)();
};
```
### Configuration 

The ```hdmi_data``` struct is used to store the configuration settings. The first members hold Video Information Code (VIC) values. These values can be manually typed in (see rotating_bars example) or the ```vic_table``` defined in src/hdmi/vic_table.c can be used (see examples static_image, resolution change and moving_square).

The following settings also need to be set in ```hdmi_data```

* **rgb_format** - The ordering of the Red, Blue, Green and Alpha channels. See ```RGB_FORMAT``` in include/hdmi/hdmi_data.h.
* **alpha_enable** - Whether the alpha channel is present. See the example static_image.
* **mode** - Whether or not the image is static or moving.
* **ms_delay** - How long each frame lasts for. For moving images, this is the time between the frame. For static images this is how long the image is displayed.

The length of time that moving images are displayed by is set by the ```MAX_FRAME_COUNT``` in src/api/api.c.

### Writing to the buffer 

The function to write the frame buffer is set to the member ```write_fb``` in the ```display_config``` struct. In the examples this is implemented as ```write_frame_buffer()```.

A pointer to the active or cached frame buffer can be retrieved using the following two functions where N is the size of the pointer in bytes ```get_active_frame_buffer_uintN()``` ```get_cache_frame_buffer_uintN()```. 8, 32 and 64 bit pointers are currently implemented. See src/api/framebuffer.c.

The width and the height are defined by ```hdmi_data.h_active``` and ```hdmi_data.v_active```. A loop can be set up to read left to right and top to bottom to access and modify the data inside the frame buffer. 

For moving images, global variables are used to keep track of frame data (see example moving_square and rotating_bars). It is set up this way so that the dcss PD can notify the client PD when the frame buffer is ready to be modified and so that the client PD can notify the dcss PD when the frame buffer has finished being written to.

In src/api.c MAX_FRAME_COUNT can be set to determine the length of time the moving image is shown. 

### Empty client

An empty example has been provided in empty_client/empty_client.c. To use the driver uncomment ```static_image()``` or ```moving_image()``` to choose the type of image you wish to see. Then implement ```init_static_example()``` by initialising all fields of the ```hdmi_data``` struct. Then modify ```write_static_frame_buffer()``` or ```write_moving_frame_buffer()``` with your desired image.

Modify empty_client/Makefile to add extra files or build configurations specific to the example.

# Attempted solutions for the HDMI syncing issue

The solutions can be found on the test_branch and can be built like any other example. (See the build.sh script or examples folder)

The current approach is to use the context loader to switch the buffer set in the DPR. The current issue can most easily be seen when running the db_test example that changes the entire screen from blue to red. The vertical blanking time is the time period that the screen retrace is out of the active display region. The reason for this issue is likely because the context switch happens outside of the vertical blanking period. There is a chance that the DTG timing settings have not been set up properly and this effect is happening.

### Using the context loader to switch the address of the frame buffer using single buffered registers

The context loader can use either double buffered or single buffered registers. Currently it's using double buffered registers which means that the next state is being loaded into the shadow registers during active display time. This is likely the cause for the current problem. The alternative is to use single buffered registers, which is where it loads the next state during the vertical blanking time. When using single buffered registers, the same effect is seen with intermittent periods of the whole screen going black. This route is worth exploring further and to make sure that the DTG settings are correct.

### Using the Display Timing Generator to layer to channels with a configurable alpha channel

Two channels can be layered on top of each other with an alpha value set for the first channel. This means that channel 1 can be displayed with no alpha, revealing the channel two behind it, without having to change the source buffer address. The idea is to change this alpha for the channel to switch visibility between the two channels. When attempting this approach, the screen redraw is still visible.

### Single buffered updates during vertical blanking time

An interrupt can be received each time the screen redraw enters a specific x/y position. Current attempts to get interrupts to be received have not worked. If this was possible then for smaller screen changes the image could be redrawn during the vertical blanking time.

### Using interrupts to change the address of the frame during vertical blanking time

If interrupts could be switched on correctly, then the address of the frame buffer could be switched during vertical blanking time. This is in theory what the context loader should be doing with single buffered registers. If this did not work, then this would indicate that there are issues with how the DTG has been set up


# To do

* Use make get and make all for picolibc
* Remove build.sh and use make get and make "example_name"
* Fix HDMI syncing issue with double buffering
* Add logging system to replace printf.
* Integrate DMA library.
* Investigate the configuration issue when using https://github.com/sel4-cap/maaxboard-uboot without modification.
* Add additional branch to https://github.com/sel4-cap/maaxboard-uboot so that no modifications need to be made to run the project.
