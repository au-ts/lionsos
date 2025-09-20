# Examples

These 4 examples have been created to demonstrate the drivers capabilities.


## Static image

In this example a static image consisting of 4 equally spaced bars of red, blue, green and white are displayed on the screen. The display configuration settings in hdmi_data are set using the predefined array vic_table. The following hdmi_data members are set:

* **rgba_format** - This is set to RGBA, which defines the ordering of each 32 bit memory region of the frame buffer. In this case it will be split into 4 8 bit addresses for each colour in the order: Red, Green, Blue and Alpha.
* **alpha_enable** - This is set to ALPHA_ON which means that the alpha channel is present. In this example for each colour bar the value of the alpha channel is incremented every 3 pixels.
* **mode** - This is set to STATIC_IMAGE so that one image is displayed.
* **ms_delay** This is set to 30000 milliseconds.

In this example the frame buffer is accessed through an 8 bit pointer by using get_active_frame_buffer_uint8(). This allows for each colour to be written separately. This does however have a performance cost compared to larger pointers writing 16, 32 or 64 bits at a time.

## Resolution change

This example demonstrates the ability of changing resolutions during runtime by display a square sized 300 X 300 at 3 different resolutions. This results in the square shrinking as the screen resolution size increases. The display configuration settings in hdmi_data are set using the predefined array vic_table. The following hdmi_data members are set:

* **rgba_format** - This is set to RBGA, which defines the ordering of each 32 bit memory region of the frame buffer. In this case it will be split into 4 8 bit addresses for each colour in the order: - Red, Blue, Green and Alpha.
* **alpha_enable** - This is set to ALPHA_OFF which means that the alpha (in this case the last 8 bits of each 32 bit memory region) will not be processed.
* **mode** - This is set to STATIC_IMAGE so that one image is displayed.
* **ms_delay** This is set to 10000 milliseconds. This is how long each square will be displayed for.

In this example the frame buffer is accessed through a 32 bit pointer by using get_active_frame_buffer_uint32(). This means that each pixel is written individually. For examples where one colour is used there is no need to write each colour separately like in the static image example. Writing 64 bit pixels is even more effecient, 32 bits are used as an example.

## Moving square

In this example a small square moves in one direction diagonally, changing its direction each time it hits the side of the screen. This example demonstrates how to use global variables and the write_frame_buffer() function to create a moving image. The driver currently redraws the entire buffer each time, this results in a noticeablejitter for each frame change.

The display configuration settings in hdmi_data are set using the predefined array vic_table. The following hdmi_data members are set:

* **rgba_format** - This is set to RGBA, which defines the ordering of each 32 bit memory region of the frame buffer. In this case it will be split into 4 8 bit addresses for each colour in the order: Red, Green, Blue and Alpha.
* **alpha_enable** - This is set to ALPHA_OFF which means that the alpha (in this case the last 8 bits of each 32 bit memory region) will not be processed.
* **mode** - This is set to MOVING_IMAGE so that moving images can be drawn by using double buffering. The write_frame_buffer() function is called on each frame change. In this example global variables are used to keep track of the squares position - if it hits one of the sides then it changes direction.
* **ms_delay** This is set to NO_DELAY for the fastest possible change between each frame.

In this example the frame buffer is accessed through a 32 bit pointer by using get_active_frame_buffer_uint32(). This means that each pixel is written individually. For examples where one colour is used there is no need to write each colour separately like in the static image example. Writing 64 bit pixels is even more effecient, 32 bits are used as an example.


## Rotating bars

In this example 4 equally spaced bars of red, blue, green and white are rotated around the screen. This example demonstrates how to use global variables and the write_frame_buffer() function to create a moving image. The driver currently redraws the entire buffer each time, this results in a subtle jitter when the square moves.

The display configuration settings are set manually in this example. The following hdmi_data members are set:

* **rgba_format** - This is set to RGBA, which defines the ordering of each 32 bit memory region of the frame buffer. In this case it will be split into 4 8 bit addresses for each colour in the order: Red, Green, Blue and Alpha.
* **alpha_enable** - This is set to ALPHA_OFF which means that the alpha (in this case the last 8 bits of each 32 bit memory region) will not be processed.
* **mode** - This is set to MOVING_IMAGE so that moving images can be drawn by using double buffering. The write_frame_buffer() function is called on each frame change. In this example global variables are used to offset where the beginning of the sequence starts.
* **ms_delay** This is set to NO_DELAY for the fastest possible change between each frame.

In this example the frame buffer is accessed through an 64 bit pointer by using get_active_frame_buffer_uint64(). This means that two pixels are written simultaneously for a performance boost.
