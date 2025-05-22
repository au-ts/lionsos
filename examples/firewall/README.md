# Firewall

We have implemented an example firewall system on-top of LionsOS. This firewall consists of network virtualisers
that multiplex packets based on packet type and forward them to filtering components. Additionally, we have
two ARP components, the ARP responder and ARP requester, and routing components. There is also a webserver
that allows the user to configure routes and filtering rules at runtime.

We have two sDDF network subsytems in the Firewall, one connected to the "external" network, and the
other connected to the "internal" network. Please see the subdirectories for more detailed explanations of the
constituent components.

## Diagram
The following is an architecture diagram of the Firewall system, with the communication channels depicted in arrows.

![](images/Firewall.svg)


## Dependencies

### Microkit SDF Gen
This project currently uses an experimental version of the `microkit_sdf_gen` tool. The development branch can be found:
https://github.com/au-ts/microkit_sdf_gen/tree/courtney/firewall. Please clone this repository. To build:

```sh
git submodule --update init
python3 -m venv venv
./venv/bin/pip install .
```

In the shell you wish to build this project, run the following command:
```sh
# Only replace whats between < >
export PYTHON<path/to/microkit_sdf_gen>/venv/bin/python
```

## Building

This project currently only supports one hardware platform, the Compulab IOT-GATE-IMX8PLUS.

You must set at least the following environment variables:

```sh
export MICROKIT_SDK=<path/to/sdk>
export MICROKIT_BOARD=imx8mp
```

This project is by default built in debug mode. Set the following environment variable to change that:

```sh
export MICROKIT_CONFIG=<benchmark/debug/release>
```

Then run:

```sh
make
```

Upload the image to the board either over serial or tftp. The load address should be: `0x50000000`.