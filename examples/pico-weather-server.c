/*
 * Copyright © Gerhard Schiller 2024 - 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This programm implements a modbus server on a Raspberry Pi Picow.
 * The server offers some weather data from a bme280:
 * humidity/temperature/pressure sensor.
 *
 * Derived values - pressure at sealevel, absolute humidity and dewpoint
 * are also available via modbus.
 *
 * The client can change temperature values from °C to °F and
 * set the station heigth (im meters).
 *
 * If you plan to visulizethe data with TIG (Telegraf - InfluxDb - Grafana)
 * a config file for Telegraf is provided: pico_weather_server.conf.example
 * copy it to pico_weather_server.conf and modify it according to your local settings;
 * search for: "token", "organization" and "controller".
 * run: sudo telegraf --config pico_weather_server.conf
 *
 */

/* MODBUS data modell for the weather server:
 *
 * Input register (libmodbus mapping: mb_mapping->tab_input_registers)
 *  0       integer, temperature in 1/10 degrees
 *  1       integer, humidity in 1/10 percent
 *  2       integer, preasure in 1/10 hPa
 *    the next values could also use scaled integers,
 *    but I wanted to show the use of floats :-)
 *  3...4   float, abs. humidity in g/m3
 *  5...6   float, dewpoint
 *  7...8   float, reduced preasure
 *
 * Holding register (libmodbus mapping: mb_mapping->tab_registers)
 *  0       Heigth of this station, meters above sealevel.
 *
 * Coils:(modbus mapping: mb_mapping->tab_bits)
 *  0      Write a "1" into it to set temperature to °F, 0 for °C
 *
 * Discrete inputs (libmodbus mapping: mb_mapping->tab_input_bits)
 *  UNUSED
 *
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "bme280.h"
#include "wifi.h"
#include "modbus.h"

modbus_t *ctx;
modbus_mapping_t *mb_mapping;

#define NB_INPUT_REGISTERS  9
#define NB_REGISTERS        1
#define NB_BITS             1
#define NB_INPUT_BITS       0


// Wi-Fi initialization
void init_wifi() {
    // Connect to Wi-Fi
    printf("Connecting to WiFi... ");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("\b\b\b\b, FAILED TO CONNECT.\n");
        return;
    } else {
        printf("\b\b\b\b, connected.\n");
    }
}

// Initialize network with static IP configuration
void init_network() {
    ip4_addr_t ip, gw, mask;

    ip4addr_aton(SERVER_PICO_IP, &ip);
    ip4addr_aton(NETMASK, &mask);
    ip4addr_aton(GATEWAY, &gw);

    if (cyw43_arch_init()) {
        printf("Network failed to initialise\n");
        return;
    }
    cyw43_arch_enable_sta_mode();

    netif_set_addr(netif_default, &(ip), &(mask), &(gw));
    netif_set_up(netif_default);
    printf("Using static IP, set:\n");
    printf("\tIP Address: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_default)));
    printf("\tNet mask: %s\n",
           ip4addr_ntoa(netif_ip4_netmask(netif_default)));
    printf("\tDefault gateway: %s\n",
           ip4addr_ntoa(netif_ip4_gw(netif_default)));
}

void init_MbServer(void)
{

    // Allocate memory for the 4 Modbus tabels.
    mb_mapping =
    modbus_mapping_new(NB_BITS, NB_INPUT_BITS,
                       NB_REGISTERS, NB_INPUT_REGISTERS);
    if (mb_mapping == NULL) {
        printf("Failed to allocate the mapping: %s\n",
               modbus_strerror(errno));
        return;
    }

    // Start the server and create the libmodbus context.
    ctx = tcp_server_init(502);
    modbus_set_debug(ctx, FALSE);

    if (ctx == NULL) {
        printf("Unable to allocate libmodbus context\n");
        return;
    }
}


void runMbServer(void)
{
    modbus_message_t mb_msg;

    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
    int clientID;
    int request_len;
    int rc;

    for (;;) {
        for (clientID = 0; clientID < MAX_PEERS; clientID++) {
            if(!modbus_is_connected(clientID)){
                // Either no connection with this ID or the
                // connection is down...
                continue;
            }

            // IMPORTANT: Do not omit this line.
            // It is essential that modbus_set_connectionID() is called
            // before any other routines that use ctx becouse it sets the
            // ctx-context to the desired client.
            modbus_set_connectionID(ctx, clientID);

            // Get the clients status
            rc = modbus_client_status(clientID);
            if(rc == 0){
                // No request in  queue
                continue;
            }

            // If we have reached this point (rc > 0), it means the client has
            // sent a request, with rc being the number of bytes in the request.
            // Termination of the connection by the client (rc < 0) is handled by
            // modbus_receive().

            // Get the request
            if((request_len = modbus_receive(ctx, request)) < 0){
                printf("runMbServer, client %d: %s\n",
                       clientID, strerror(errno));
                continue;
            }

            // Send the reply
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            rc = modbus_reply(ctx, request, request_len, mb_mapping);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            if(rc < 0){
                printf("runMbServer, client %d: %s\n",
                       clientID, strerror(errno));
                continue;
            }

            // Check whether the request has modified any data in one of
            // the Modbus tables. In other words, check if it was a write
            // (or read/write) operation rather than a read-only one.
            // If so, push a message for core 0 into the FIFO.
            modbus_notify_if_write(ctx, request, &mb_msg);
        }
    }
}

#define c2f(c) (((c) * 1.8) + 32)

void main(void) {
    int32_t humidity_raw, pressure_raw, temperature_raw;
    float   humidity, pressure, temperature;
    float   abs_humidity,  red_pressure, dew_point;
    int32_t height = 153;   // Europe, Vienna, Aspern :-)
    char    scale = 'C';    // C for Celsius, F for Farnheit

    modbus_message_t *p_mb_msg;

    // useful information for picotool
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));
    bi_decl(bi_program_description("weather server example for the Raspberry Pi Pico V1.1"));

    stdio_init_all();
    printf("Modbus Weather Station V1.1\n");

    // Initialize the network
    init_network();

    // Initialize Wi-Fi
    init_wifi();

    // Initialize the ModBus-Server
    init_MbServer();

    // Run the ModBus-Server
    multicore_launch_core1(runMbServer);

    mb_mapping->tab_bits[0] = 0;    // °C
    mb_mapping->tab_registers[0] = height;

    initializeBme280();
    for(;;){
        /*
         * Check if the client has sent some data (Holding Registers or Coils)
         */
        if(p_mb_msg = modbus_write_notify()){
            if(modbus_get_debug(ctx))
                printf("MbServer notified: function:%d, address:%d, count:%d\n",
                       p_mb_msg->func, p_mb_msg->addr, p_mb_msg->count);

                switch (p_mb_msg->func) {
                    case MODBUS_FC_WRITE_SINGLE_COIL:
                    case MODBUS_FC_WRITE_MULTIPLE_COILS:
                        if(modbus_get_debug(ctx)) {
                            printf("%d COIL(S) modified:\n", p_mb_msg->count);
                            modbus_tcp_mapping_lock(ctx);
                            for(int i = 0; i <  p_mb_msg->count; i++){
                                printf("\t0x%02X at 0x%02X: ",
                                       mb_mapping->tab_bits[p_mb_msg->addr + i],
                                       p_mb_msg->addr + i);
                           }
                           modbus_tcp_mapping_unlock(ctx);
                        }
                        if(p_mb_msg->addr == 0){
                            modbus_tcp_mapping_lock(ctx);
                            scale = mb_mapping->tab_bits[0] ? 'F' : 'C';
                            modbus_tcp_mapping_unlock(ctx);
                            printf("Temperature scale set to: '%c'\n\n", scale);
                        }
                        break;

                    case MODBUS_FC_WRITE_SINGLE_REGISTER:
                    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                    case MODBUS_FC_WRITE_AND_READ_REGISTERS:
                        if(modbus_get_debug(ctx)) {
                            printf("%d REGISTER(S) modified:\n", p_mb_msg->count);
                            modbus_tcp_mapping_lock(ctx);
                            for(int i = 0; i <  p_mb_msg->count; i++){
                                printf("\t%d at 0x%02X\n",
                                       mb_mapping->tab_registers[p_mb_msg->addr + i],
                                       p_mb_msg->addr + i);
                            }
                            modbus_tcp_mapping_unlock(ctx);
                        }
                        if(p_mb_msg->addr == 0){
                            modbus_tcp_mapping_lock(ctx);
                            height = mb_mapping->tab_registers[0] ;
                            modbus_tcp_mapping_unlock(ctx);
                            printf("Station height set to: %d m\n\n", height);
                        }
                        break;

                    default:
                        if(modbus_get_debug(ctx))
                            printf("Unknown write-function %d\n", p_mb_msg->func);
                }
        }

        /*
         * Do the actual work
         */
        write_register(0xF4, 0x26); // Force measurement
        bme280_read_raw(&humidity_raw, &pressure_raw, &temperature_raw);

        // These are the raw numbers from the chip, so we need to run through the
        // compensations to get human understandable numbers
        pressure = compensate_pressure(pressure_raw)   / 100.0;
        temperature = compensate_temp(temperature_raw) / 100.0;
        humidity = compensate_humidity(humidity_raw)   / 1024.0;

        if(modbus_get_debug(ctx)) {
            if(scale == 'C')
                printf("Temp. = %.2f C\n", temperature);
            else if (scale == 'F')
                printf("Temp. = %.2f F\n", c2f(temperature));

            printf("Humidity = %.2f%%\n", humidity);
            printf("Abs. Humidity = %.2f g/m³\n",
                absoluteHumidity(temperature, humidity));

            if(scale == 'C')
                printf("Dewpoint = %.2f C\n",
                dewpoint(temperature, humidity));
            else if (scale == 'F')
                printf("Dewpoint = %.2f F\n",
                    c2f(dewpoint(temperature, humidity)));

            printf("Pressure = %.2f hPa\n", pressure);
            printf("Pressure red. = %.2f hPa\n",
                reducedPressure(pressure, 153));
            printf("\n");
        }

        modbus_tcp_mapping_lock(ctx);
        if(scale == 'C')
            mb_mapping->tab_input_registers[0] = (int)((temperature * 10.0) + 0.5);
        else if (scale == 'F'){
            float t = c2f(temperature);
            mb_mapping->tab_input_registers[0] = (int)((t * 10.0) + 0.5);
        }
        mb_mapping->tab_input_registers[1] = (int)((humidity * 10.0) + 0.5);
        mb_mapping->tab_input_registers[2] = (int)((pressure * 10.0) + 0.5);

        uint16_t fConv[2];
        modbus_set_float_abcd(absoluteHumidity(temperature, humidity), fConv);
        mb_mapping->tab_input_registers[3] = fConv[0];
        mb_mapping->tab_input_registers[4] = fConv[1];

        if(scale == 'C')
            modbus_set_float_abcd(dewpoint(temperature, humidity), fConv);
        else if (scale == 'F'){
            float tp = dewpoint(temperature, humidity);
            tp = c2f(tp);
            modbus_set_float_abcd(tp, fConv);
        }
        mb_mapping->tab_input_registers[5] = fConv[0];
        mb_mapping->tab_input_registers[6] = fConv[1];

        modbus_set_float_abcd(reducedPressure(pressure, height), fConv);
        mb_mapping->tab_input_registers[7] = fConv[0];
        mb_mapping->tab_input_registers[8] = fConv[1];
        modbus_tcp_mapping_unlock(ctx);

        sleep_ms(5000); // Should be 60 sec according to recommendations....
    }

    return;
}

