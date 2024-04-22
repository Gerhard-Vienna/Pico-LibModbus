# A Modbus Library for the Pico-W
**Pico-LibModbus** is an extension of **LibModbus** by Stéphane Raimbault.
It lets you run LibModbus on a Raspberry Pico-W, with communication using the Modbus-TCP protocol via Wi-Fi.
You can set up both a server (formerly known as "slave") and a client (formerly known as "master").

Extensions and modifications to LibModbus:
The added code is in the file `modbus-pico-tcp.c`. Only minor changes were needed to the other LibModbus source files. These are marked with `#ifdef PICO_W`. This should make it easy to adapt to new versions of libmodbus.

# How To Use This Software:
Copy the following files to your project directory.

```
libmodbus/src/modbus.c
libmodbus/src/modbus-data.c
libmodbus/src/modbus.h
libmodbus/src/modbus-private.h
libmodbus/src/modbus-version.h
libmodbus/src/modbus-pico-tcp.c
libmodbus/src/modbus-pico-tcp.h
libmodbus/src/modbus-pico-tcp-private.h
```
Copy `wifi.h.example` as `wifi.h` to your project directory and enter the relevant data (SSID, password and workstation IP) in this file.
Do not forget to create your `lwipopts.h` file or copy `lwipopts_mbus_common.h` as `lwipopts.h` to your project directory.

Use `examples/CMakeLists.txt` as guideline for your `CMakeLists.txt`.
Make sure you add your project directory to the `target_include_directories`  directive so that `lwipopts.h` will be found. Also add `PICO_W` to the `target_compile_definitions`.

**Naming of the modbus-registres in LibModbus:**
LibModbus holds all modbus register as members in a structure typedef'd as `modbus_mapping_t`.

```
Modbus spezification:             LibModbus `modbus_mapping_t` member
--------------------              -----------------------------------
Input register                    tab_input_registers
Holding register                  tab_registers
Coils                             tab_bits
Discrete inputs                   tab_input_bits
```

# Examples
There are two examples of Pico-Modbus servers in the directory "examples". You'll find the MODBUS data model for each server in the source files.
Note that the examples use the standard modbus-port of 501. Therefore, clients on a workstation must be run with "sudo".

**The pico-server-example**
is a really simple server that doesn't need any extra components. It shows the time, the CPU temperature, and a counter (0-15) via Modbus TCP.

The server configuration can be changed by the client. You can set the time, start the RTC and set the debug mode. The "test-client-cli" programme can be used for this purpose.

**The pico-weather-server**
is a more realistic example. It requires a BME280 sensor as additional hardware. Temperature, humidity, and air pressure as well as data derived from these can be read out using Modbus-TCP. The client can change the temperature scale from Celsius to Fahrenheit and set the debug mode.

As a client, you can either use "test-client-cli" (which is a bit tedious) or any other test client (which is the recommended option if available). With "test-client-cli" only  integer values can be read  in a meaningful way.

**How to build the examples:**
Note: this also builds the test firmware, see below.

Refer to chapter 8 of 'Getting started with Raspberry Pi Pico' for instructions.

```
1. Create a directory for your project sitting alongside the pico-sdk directory.
2. Download "pico-modbus" to this directory.
3. Copy `wifi.h.example` to `wifi.h`
   Enter the relevant data (SSID, password and workstation IP) in this file.
4. Then copy the pico_sdk_import.cmake file from the external folder in your pico-sdk installation to your project folder.
5. Now the usual steps:
$ mkdir build
$ cd build
$ cmake ..
$ make
```
If you want to use Telegraf-InfluxDB-Grafana to visualize the data, you will find the Telegraf config-files in the `examples` subdirectory.
Search for the lines:
`token = "YOUR TOKEN"`
`organization = "YOUR ORGANIZATION"`
`controller = "tcp://MODBUS_SERVER_IP:502"`
fill in your data and rename the files from `*.conf.example` to `*.conf`

# Tests
LibModbus contains a range of tests, located in "pico-libModbus/libmodbus/tests".
The workstation programs have been adapted to bind the socket to all available interfaces, not just the local host via the loopback device. Also any tests that are not relevant have been deactivated. All changes are marked here with `#ifdef PICO_W_TESTS`.

An additional tool `test-client-cli` is available for use with the workstation.
This tool enables the user to send requests from the command line to a Pico Modbus server.

** How to build the test-software for the workstation **
cd into `libmodbus` subdirectoty.
run:
$ ./configure
$ make
If you just want to run the tests and don't need LibModbus on the workstation for any other purpose, you don't need to run the installation command (`sudo make install`).
You can just start the test software straight away from the `libmodbus/tests` subdirectoty.

** The test-firmware for the Pico-W **
was built during building the examples. If you skipped it, you must do it now.

** How to use the tests: **
For each test program (except `bandwidth-server-many-up`) to be used with the workstation, there is a corresponding counterpart for the Pico-W.

Assuming, the IP address of the Pico is 10.0.0.100:

```
Load into the Pico-W            Run on the Workstation
from directory "build/tests":   from directory "libmodbus/tests":
-----------------------------   ---------------------------------
pico-random-test-server         random-test-client 10.0.0.100
pico-random-test-client         random-test-server

pico-unit-test-server           unit-test-client tcp 10.0.0.100
pico-unit-test-client           unit-test-server tcp

pico-bandwidth-server           bandwidth-client tcp 10.0.0.100
pico-bandwidth-client           bandwidth-server-one tcp
```

# Technical Details:
Pico-LibModbus uses lwIP in NO_SYS mode with callbacks as TCP/IP stack. (/savannah.nongnu.org/projects/lwip)

Modbus servers (tests and examples) run on Core1 so that Core0 is available for the actual tasks of the device. Communication between the cores, Modbus server and device function, is shown and explained in the pico-server example.

Modbus clients run entirely on Core0, as it makes little sense to outsource the Modbus functionality to the second core.

** Ports: **
The standard Modbus port is 501. Under Linux, a port in the range 1-1023 is a privileged port. By default, privileged ports cannot be bound to non-root processes.
To avoid this problem, the tests use the (non-privileged) port 1501, while the examples use the standard port 501. Therefore, the client (on the workstation) requires root privileges (sudo ...).
