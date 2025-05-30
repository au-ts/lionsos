# Firewall

We have implemented an example firewall system on-top of LionsOS. This
firewall consists of network virtualisers that multiplex packets based
on packet type and forward them to filtering components. Additionally,
we have two ARP components, the ARP responder and ARP requester, and
routing components. There is also a webserver that allows the user to
configure routes and filtering rules at runtime.

We have two sDDF network subsystems in the Firewall, one connected to
the "external" network, and the other connected to the "internal"
network. Please see the subdirectories for more detailed explanations
of the constituent components.

## Diagram
The following is an architecture diagram of the Firewall system, with
the communication channels depicted in arrows.

![](images/Firewall.svg)


## Dependencies

```sh
pip3 install sdfgen==0.25.0
```

If you get `error: externally-managed-environment`
when installing via pip, instead run:
```sh
pip3 install --break-system-packages sdfgen==0.25.0
```

## Building

This project currently only supports one hardware platform, the
Compulab IOT-GATE-IMX8PLUS.

You must set at least the following environment variables:

```sh
export MICROKIT_SDK=<path/to/sdk>
export MICROKIT_BOARD=imx8mp_evk
export FW_IOTGATE_IDX=[1345]
```

The index selects which of the four available iotgate boards you are
building for.

This project is by default built in debug mode. Set the following
environment variable to change that:

```sh
export MICROKIT_CONFIG=<benchmark/debug/release>
```

Then run:

```sh
make
```

## Running

At trustworthy systems we use [machine
queue](https://wiki.trustworthy.systems/doku.php?id=sysadmin_hints:mq:machine_queue)
to run our systems. Machine queue uploads the provided image to the
requested board and opens a console displaying serial output from the
system. A typical machine queue command would be:

```sh
mq.sh run -c "emptystring" -l mq.log -s iotgate1 -f build/loader.img
```

where the provided arguments are: 

* -a          Keep the machine alive after image is done
* -l FILE     Optional location to write all the console output to
* -s TEXT     Specifies which machine this job is for
* -f FILE [+] Files to use as the job image

The default build system output image file name is `loader.img` which
can be found inside the `build` directory.

There are 4 iotgates available to run the firewall, numbered 1, 3, 4
and 5 (2 is missing). They are  named `iotgate[1345]`. While
running an image on an iotgate you will have exclusive access to it
via a machine queue lock. Exiting from machine queue run will
automatically release the lock, so please ensure to do this when you
are done. If machine queue does not exit correctly, it may not
properly release the lock which will stop others from using the
board. It this happens, run:

```sh
mq.sh sem -signal iotgate1
```

which will release any lock you may still have.

Since the firewall has so many components producing output, it is
particularly useful to provide a machine queue log file where the
console output is written to after exiting from the run command.  When
output becomes jumbled, it can severely effect the readability. If
this becomes a problem for you, you can turn firewall debug output on
and off by setting the `FIREWALL_DEBUG_OUTPUT` macro in the [firewall
config file](/include/lions/firewall/config.h).

You can optionally disable coloured output from the serial subsystem
which is automatically implemented by the serial virtualisers, as if
the colour codes it produces are interrupted it can cause severely
corrupted console output and make readability significantly
worse. This can be done by adding the argument `enable_color = False`
when creating the serial subsystem:

```py
serial_system = Sddf.Serial(sdf, serial_node, common_pds[-2], common_pds[-1], enable_color = False)
```

in the [firewall metaprogram](/examples/firewall/meta.py).
