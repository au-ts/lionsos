# Revision history for LionsOS

## Release 0.2.0

* Move to Microkit 1.4.0.
    * Previously a development version of Microkit was used, now we use
      an official release for everything.
* Update to sDDF 0.5.0.
* Update to libvmm (15b9ee6).
* Transition to a modular 'Makefile snippets' build system structure to
  simplify the composition of components and libraries.
* Add new 'web server' example system.
    * It is currently being used to serve beta.sel4.systems, you can find more
      information [here](https://lionsos.org/docs/examples/webserver/).
* Use the 'libmicrokitco' co-routine library for handling synchronous interfaces
  with asynchronous events (for example MicroPython).
* Refactor MicroPython support for LionsOS so that it is usable outside of just
  the reference example system (Kitty).
* Add asynchronous file system module to MicroPython support.
* Various bug fixes to improve stability of the Network File System (NFS) client.
* Fixes to the file-system protocol.
* Fixes in the MicroPython network layer that interfaces with the sDDF networking
  sub-system.
* Minor cleanup and fixes to the Kitty example.
* Large artefacts such as Linux kernel images that were part of examples were removed
  from the repository as well as the commit history. This means that the commit history
  has been rewritten. This was necessary in order to have the repository size not be
  in the dozens/hundreds of megabytes.
    * Examples now fetch the images as part of the build system.
