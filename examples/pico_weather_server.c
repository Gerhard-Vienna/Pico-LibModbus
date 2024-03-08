/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
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

#define NB_INPUT_REGISTERS      9
#define NB_HOLDING_REGISTERS    1
#define NB_COILS                1
#define NB_DISCRETE_INPUTS      0

void runMbServer(void)
{
    modbus_message_t mb_msg;
    int rc;

    ctx = modbus_new_tcp("127.0.0.1", 502);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return;
    }
    modbus_set_debug(ctx, FALSE);

    mb_mapping = modbus_mapping_new(
        NB_COILS, NB_DISCRETE_INPUTS, NB_HOLDING_REGISTERS, NB_INPUT_REGISTERS);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }
    multicore_fifo_push_blocking(true);

    rc = modbus_tcp_listen(ctx, 2);
    if(rc == -1){
        fprintf(stderr, "Listen failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    modbus_tcp_accept(ctx, NULL);
    for (;;) {
        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
        int rc;

        rc = modbus_receive(ctx, query);
        if (rc > 0) {
            /* rc is the query size */
            // TODO check return value from modbus_reply()

            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            modbus_reply(ctx, query, rc, mb_mapping);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

            if(modbus_tcp_message(ctx, query, &mb_msg)){
                multicore_fifo_push_blocking((int32_t)&mb_msg);
            }
        }
        if (rc == -1 || !modbus_tcp_is_connected(ctx)) {
            modbus_tcp_accept(ctx, NULL);
        }
    }

    /* should never be reached */
    printf("Quit the loop: %s\n", modbus_strerror(errno));
    modbus_mapping_free(mb_mapping);
    modbus_close(ctx);
    modbus_free(ctx);
}

#define c2f(c) (((c) * 1.8) + 32)

void main(void) {
    int32_t humidity_raw, pressure_raw, temperature_raw;
    float   humidity, pressure, temperature;
    float   abs_humidity,  red_pressure, dew_point;
    int32_t height = 153;   // Europe, Vienna, Aspern :-)
    char    scale = 'C';    // C for Celsius, F for Farnheit

    modbus_message_t *mb_msg;

    stdio_init_all();

    printf("Modbus Weather Station V0.1\n");
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return;
    }

    // useful information for picotool
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));
    bi_decl(bi_program_description("weather server example for the Raspberry Pi Pico"));

    cyw43_arch_enable_sta_mode();
    printf("Connecting to WiFi... ");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("\b\b\b\b, FAILED TO CONNECT.\n");
        return;
    } else {
        printf("\b\b\b\b, connected.\n");
    }
    printf("IP Address: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));

    multicore_launch_core1(runMbServer);
    if(multicore_fifo_pop_blocking())
        printf("MB-Server ready on core 1\n");

    initializeBme280();
    for(;;){
        /*
         * Check if the client has sent some data (Holding Registers or Coils)
         */
        if(multicore_fifo_rvalid()){
            mb_msg = ( modbus_message_t *) multicore_fifo_pop_blocking();

            if(modbus_get_debug(ctx))
                printf("Core0 notified: code:%d, addr:%d, count:%d\n",
                       mb_msg->code, mb_msg->addr, mb_msg->count);

                switch (mb_msg->code) {
                    case MODBUS_FC_WRITE_SINGLE_COIL:
                    case MODBUS_FC_WRITE_MULTIPLE_COILS:
                        if(modbus_get_debug(ctx)) {
                            printf("%d COIL(S) modified:\n", mb_msg->count);
                            for(int i = 0; i <  mb_msg->count; i++){
                                printf("\t0x%02X at 0x%02X: ",
                                       mb_mapping->tab_bits[mb_msg->addr + i],
                                       mb_msg->addr + i);
                           }
                        }
                        if(mb_msg->addr == 0){
                            scale = mb_mapping->tab_bits[0] ? 'F' : 'C';
                            printf("Temperature scale set to: '%c'\n\n", scale);
                        }
                        break;

                    case MODBUS_FC_WRITE_SINGLE_REGISTER:
                    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                    case MODBUS_FC_WRITE_AND_READ_REGISTERS:
                        if(modbus_get_debug(ctx)) {
                            printf("%d REGISTER(S) modified:\n", mb_msg->count);
                            for(int i = 0; i <  mb_msg->count; i++){
                                printf("\t%d at 0x%02X\n",
                                       mb_mapping->tab_registers[mb_msg->addr + i],
                                       mb_msg->addr + i);
                            }
                        }
                        if(mb_msg->addr == 0){
                            height = mb_mapping->tab_registers[0] ;
                            printf("Station height set to: %d m\n\n", height);
                        }
                        break;

                    default:
                        if(modbus_get_debug(ctx))
                            printf("Unknown write-code %d\n", mb_msg->code);
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

