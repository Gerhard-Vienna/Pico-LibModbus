
# A Modbus Library for the Pico-W

**Pico-LibModbus** is an extension of **LibModbus** by Stéphane Raimbault.  
It enables LibModbus to be run on a Raspberry Pico-W, with communication taking place via Wi-Fi using the Modbus-TCP protocol.
Both a server (formerly known as a 'slave') and a client (formerly known as a 'master') can be set up.  

**What is new in Version 1.1**  
- Both servers and clients support multiple connections: a server can connect multiple clients, and a client can connect to multiple servers.  
- A server can disconnect from a client that has not sent any requests for a certain period of time.
This is useful in the event of connection interruptions, for example.  
-Pico-LibModbus V1.1 is based on the latest available version of LibModbus. That is version 3.1.11.  
- Improved reconnection by the client after a network or server failure.  

Redesigning Pico-LibModbus was necessary in order to enable multiple connections. As a result: **Version 1.1 is not compatible with version 1.0.**  

Adjustments must be made to the application's initialisation and handling of requests (indications) and responses (confirmations).  
Please look at `pico-hello-server.c` in the `examples` subdirectory for details of the required calls and the correct sequence. This file contains extensive comments.  

#### Added and modified Code:
**The PicoW-specific code is mainly located in the file "modbus-pico-tcp.c"**.  
Only minor changes were needed to the other LibModbus source files. These are marked with `#ifdef PICO_W`.   

#### Extensions to LibModbus:  
There is no direct solution in LibModbus for two problems that occur in a real application, namely:   

**Transmitting data via Modbus will never be a real server's main task.**  
 Therefore, on a Pico, it makes sense to run ModBus communication on the second core. This leaves the first core available for the server's primary function.  
However, if both cores are accessing the Modbus registers, simultaneous access must be prevented.  
There is a pair of functions *modbus_tcp_mapping_lock()* and *modbus_tcp_mapping_unlock() *that ensure this. 
Use these functions to enclose all write and read operations in the ModBus tables.  

**The client must respond when the server modifies the data in its registers.**  
The *modbus_notify_if_write()* function can be used in the ModBus task (Core 1) to check whether the last transaction wrote data. If so, this information is passed to the application (Core 0) via an 'inter-core FIFO'..  
If entries in the FIFO are created, the application (Core 0) must periodically check for messages with *modbus_write_notify()* to retrieve the entries and, if applicable, act based on the transferred information.  

#How To Use This Software:
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
Copy `wifi.h.example` as `wifi.h` to your project directory and enter SSID and password. Modify the IP and LAN-settings as required by your project.  
Do not forget to create your `lwipopts.h` file or copy `lwipopts_mbus_common.h` as `lwipopts.h` to your project directory.

Use `examples/CMakeLists.txt` as guideline for your `CMakeLists.txt`.
Make sure you add your project directory to the `target_include_directories`  directive so that `lwipopts.h` will be found. Also add `PICO_W` to the `target_compile_definitions`.

**Configure Pico-LibModbus**  
The maximum number of clients that can connect to a server is defined by the constant “MAX_PEERS” in the file `modbus-pico-tcp.h`. The default value is 4.  

The time period after which a server will disconnect from an idle client is defined by the constant “CLIENT_TIMEOUT” in the `LibModbus modbus-pico-tcp.h` file. The default value is 10 minutes.  
Note that this value must be given in seconds.

**Naming of the ModBus-Registres in LibModbus**  
LibModbus holds all modbus register as members in a structure typedef'd as `modbus_mapping_t`.  

| Modbus spezification | LibModbus `modbus_mapping_t` member |
|--|--|
| Input register | tab_input_register |
| Holding register | tab_registers |
| Coils | tab_bits  |
| Discrete inputs | tab_input_bits |

# Examples 
There are examples of Pico-Modbus servers and clients in the directory "examples". You'll find the MODBUS data model for each server in the source files.
Note that the examples use the standard modbus-port of 501. Therefore, programs on a workstation must be run with "sudo".

**The pico-hello-server**  
This example illustrates the basic structure of a server. It demonstrates the necessary ModBus function calls and their correct sequence. It also includes detailed comments on each function.  

Use the Linux program `test-client-cli` from the `libmodbus/tests` subdirectory.
Call: 
```
cd libmodbus/tests
sudo ./test-client-cli “IP address of the server” 1502
```
Then, use:  
To control the LED:  
“Modbus code”: 5 (Write Single Coil), ‘Address’: 0 and “Data”: 1 or 0 to switch on or off.  

To control the debug output:  
“Modbus code”: 5 (Write Single Coil), ‘Address’: 1and “Data”: 1 or 0 to switch on or off.  

To query the switch-on duration of the LED:   
“Modbus code”: 4 (Read Input Registers), “Start Address”: 0 and “Quantity”: 1.

**The pico-hello-client**  
This example illustrates the basic structure of a client. It demonstrates the necessary ModBus function calls and their correct sequence. It also includes detailed comments on each function.  

As a server use the Linux program `hello-server` from the `libmodbus/tests` subdirectory.
Call: 
```
cd libmodbus/tests
sudo ./hello-server
```
Every three seconds, the client sends an integer in the range 1...9 to the server, instructing it to write to the holding register at address 0.
The server sets the input registers at address 0 to the square of the received value.  
The client then requests this value from the server by issuing the command 'read input registers at address 0 with quantity 1'.

**The pico-server-example**  
is a really simple server that doesn't need any extra components. It shows the time, the CPU temperature, and a counter (0-15) via Modbus TCP. 

The server configuration can be changed by the client. You can set the time, start the RTC and set the debug mode. The "test-client-cli" programme can be used for this purpose.

**The pico-weather-server**  
is a more realistic example. It requires a BME280 sensor as additional hardware. Temperature, humidity, and air pressure as well as data derived from these can be read out using Modbus-TCP. The client can change the temperature scale from Celsius to Fahrenheit and set the debug mode.

As a client, you can either use "test-client-cli" (which is a bit tedious) or any other test client (which is the recommended option if available). With "test-client-cli" only  integer values can be read  in a meaningful way.

**How to build the examples**  
Note: this also builds the test firmware, see below.

Refer to chapter 8 of 'Getting started with Raspberry Pi Pico' for instructions.  

1 If you have already downloaded   Pico-LibModbus,  cd into the directory `Pico-LibModbus'.
If not, download ist now:
```
git clone https://github.com/Gerhard-Vienna/Pico-LibModbus.git
cd LibModbus
```
2 Copy `wifi.h.example` to `wifi.h`  
   Enter the relevant data (SSID, password, LAN-settings  and  IPs) in this file.  
3 Then copy the pico_sdk_import.cmake file from the external folder in your pico-sdk installation to your project folder.  
4 Now the usual steps:  
```
mkdir build  
cd build  
cmake ..  
make  
```
**TIG**  
If you want to use Telegraf-InfluxDB-Grafana to visualize the data from `pico-server-example` or , `pico-weather-server` you will find the Telegraf config-files in the `examples` subdirectory.  
Search for the lines:  
`token = "YOUR TOKEN"`  
`organization = "YOUR ORGANIZATION"`  
`controller = "tcp://MODBUS_SERVER_IP:502"`  
fill in your data and rename the files from `*.conf.example` to `*.conf`

Please note that the byte order for floats in the configuration examples has changed. This is due to a bug fix in libmodbus.

# Tests
LibModbus contains a range of tests, located in "pico-libModbus/libmodbus/tests".  
The workstation programs have been adapted to bind the socket to all available interfaces, not just the local host via the loopback device. Also any tests that are not relevant have been deactivated. All changes are marked here with `#ifdef PICO_W_TESTS`.  
Note that the tests use port 1501 instead of the standard Modbus port 501. Therefore, the "sudo" command is not required.  

An additional tool `test-client-cli` is available for use with the workstation.   
This tool enables the user to send requests from the command line to a Pico Modbus server.  

** How to build the test-software for the workstation **  
cd into `libmodbus` subdirectoty.  
run:  
 ./configure  
 make  
If you just want to run the tests and don't need LibModbus on the workstation for any other purpose, you don't need to run the installation command (`sudo make install`).  
You can just start the test software straight away from the `libmodbus/tests` subdirectoty.  

**The test-firmware for the Pico-W**  
was built during building the examples. If you skipped it, you must do it now.

**How to use the tests**  
For each test program (except `bandwidth-server-many-up`) to be used with the workstation, there is a corresponding counterpart for the Pico-W.  
Of course, you can also run the server and the client on separate Pico-W devices.  

| Load into the Pico-W <br>from directory "build/tests" | Load into the second Pico-W <br>from directory "build/tests" | Run on the Workstation <br>from directory "libmodbus/tests"|Notes | 
|--|--|--|--|
| pico-unit-test-server | | unit-test-client tcp SERVER-IP | 1 | 
| pico-unit-test-client| | unit-test-server tcp | 3 | 
| pico-unit-test-client| pico-unit-test-server | | 3 | 
| pico-bandwidth-server| | bandwidth-client tcp SERVER-IP | 1 | ~~~~
| pico-bandwidth-client  | | bandwidth-server-one tcp | 3 | 
| pico-bandwidth-client  |  pico-bandwidth-server| | 3 | 
| pico-random-test-server | | random-test-client SERVER-IP | 2 | 
| pico-random-test-client | |  random-test-server | 3 | 
| pico-random-test-client | pico-random-test-server  |  | 3 |
 
Notes:  
(1) Multiple clients can also be started on the workstation.  
(2) Multiple clients can also be started on the workstation. However, expect a number of tests to return errors, as the clients mutually overwrite the registers.  
(3) Ensure that the IP address of the server is correct in the client's code .  
Look for the lines:
```
// serverID = tcp_new_client(SERVER_WS_IP, 1502);
serverID = tcp_new_client(SERVER_PICO_IP, 1502);
```
and comment / uncomment as required!

# Technical Details
Pico-LibModbus uses lwIP in NO_SYS mode with callbacks as TCP/IP stack. (/savannah.nongnu.org/projects/lwip)

Modbus servers (tests and examples) run on Core1 so that Core0 is available for the actual tasks of the device. Communication between the cores, Modbus server and device function, is shown and explained in the pico-hello-server example.

Modbus clients run entirely on Core0, as it makes little sense to outsource the Modbus functionality to the second core.

**Ports**  
The standard Modbus port is 501. Under Linux, a port in the range 1-1023 is a privileged port. By default, privileged ports cannot be bound to non-root processes.  
To avoid this problem, the tests use the (non-privileged) port 1501, while the examples use the standard port 501. Therefore, the client (on the workstation) requires root privileges (sudo ...).
