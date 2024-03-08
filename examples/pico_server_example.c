/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This programm implements a modbus server on a Raspberry Pi Picow.
 * The server offers some variable data - timestamp, cpu temperatue
 * and a "heartbeat".
 * It also shows the code needed to react to changes of server state
 * initiated by the client - start the rtc and turn debugging
 * output on/off.
 *
 * If you plan to visulizethe data with TIG (Telegraf - InfluxDb - Grafana)
 * a config file for Telegraf is provided: pico_server_example.conf.example
 * copy it to pico_server_example.conf and modify it according to your local settings;
 * search for: "token", "organization" and "controller"
 * run: sudo telegraf --config pico_server_example.conf
 *
 */

/* MODBUS data modell for the example server:
 *
 * Input register (libmodbus mapping: mb_mapping->tab_input_registers)
 *  0       an integer that is incremented every 10 seconds,
 *              returns to 0 after 16 steps
 *  1...2   CPU-Temperature, float
 *  3       CPU-Temperature, int 1/10 of degree
 *  4       RTC, year
 *  5       RTC, month
 *  6       RTC, day
 *  7       RTC, week day
 *  8       RTC, hour
 *  9       RTC, min
 *  10      RTC, second
 *
 * Holding register (libmodbus mapping: mb_mapping->tab_registers)
 *  0       Intitial value of RTC, year
 *  1       Intitial value of RTC, month
 *  2       Intitial value of RTC, day
 *  3       Intitial value of RTC, week day
 *  4       Intitial value of RTC, hour
 *  5       Intitial value of RTC, min
 *  6       Intitial value of RTC, second
 *
 * Discrete inputs (libmodbus mapping: mb_mapping->tab_input_bits)
 *  0      "1": RTC inicialized
 *  1      "1": debugging output enabled
 *
 * Coils:(modbus mapping: mb_mapping->tab_bits)
 *  0      Write a "1" into it to set RTC from the initial values
 *  1      Write a "1" to enable debugging output, "0" to disable
 *  2      Write a "0" to get onboard temerature in degree Celsius,
 *         "1" for degree Farenheit.
 *
 */

#include <errno.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/util/datetime.h"
#include "hardware/adc.h"
#include "hardware/rtc.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "wifi.h"

#include "modbus.h"

modbus_t *ctx;
modbus_mapping_t *mb_mapping;


#define NB_INPUT_REGISTERS      11
#define NB_HOLDING_REGISTERS    7
#define NB_DISCRETE_INPUTS      2
#define NB_COILS                3


/* Choose 'C' for Celsius or 'F' for Fahrenheit. */
#define TEMPERATURE_UNITS 'C'

/* References for this implementation:
 * raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
 * pico-examples/adc/adc_console/adc_console.c */
void read_onboard_temperature(void)
{
    /* 12-bit conversion,
     * I use an external 3.0 V refernce, so max value == ADC_VREF == 3.0 V
     * if the internal reference is used,  max value == ADC_VREF == 3.3 V
     */
    //     const float conversionFactor = 3.3f / (1 << 12);
    const float conversionFactor = 3.0f / (1 << 12);

    float adc = (float)adc_read() * conversionFactor;
    float temp = 27.0f - (adc - 0.706f) / 0.001721f;

    modbus_tcp_mapping_lock(ctx);
    if(mb_mapping->tab_bits[2] == 1){ //Farenheit
        temp = temp * 9 / 5 + 32;
    }

    uint16_t fConv[2];
    modbus_set_float_abcd(temp, fConv);
    mb_mapping->tab_input_registers[1] = fConv[0];
    mb_mapping->tab_input_registers[2] = fConv[1];

    mb_mapping->tab_input_registers[3] = (int)((temp * 10.0) + 0.5);
    modbus_tcp_mapping_unlock(ctx);
}

void setRTC(void)
{
    datetime_t t = {
        .year  = mb_mapping->tab_registers[0],
        .month = mb_mapping->tab_registers[1],
        .day   = mb_mapping->tab_registers[2],
        .dotw  = mb_mapping->tab_registers[3],
        .hour  = mb_mapping->tab_registers[4],
        .min   = mb_mapping->tab_registers[5],
        .sec   = mb_mapping->tab_registers[6]
    };

    if(modbus_get_debug(ctx))
        printf("Set RTC from holding registers 6:0 ");
    if(rtc_set_datetime(&t)){
        if(modbus_get_debug(ctx))
            printf("OK\n");
        mb_mapping->tab_input_bits[0] = 1;
    }
    else{
        if(modbus_get_debug(ctx))
            printf("FAILED\n");
        mb_mapping->tab_input_bits[0] = 0;
    }
}

void updateRTCtoInputregs(void)
{
    datetime_t t;
    rtc_get_datetime(&t);

    modbus_tcp_mapping_lock(ctx);
    mb_mapping->tab_input_registers[4] = t.year;
    mb_mapping->tab_input_registers[5] = t.month;
    mb_mapping->tab_input_registers[6] = t.day;
    mb_mapping->tab_input_registers[7] = t.dotw;
    mb_mapping->tab_input_registers[8] = t.hour;
    mb_mapping->tab_input_registers[9] = t.min;
    mb_mapping->tab_input_registers[10] = t.sec;
    modbus_tcp_mapping_unlock(ctx);
}

void setDebugOutput(int state)
{
    printf("modbus_set_debug(%s)\n", state ? "True" : "False");
    modbus_set_debug(ctx, state);
}

int64_t ledOff(alarm_id_t id, void *user_data)
{
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    return 0;
}

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

int main()
{
    modbus_message_t *mb_msg;

    stdio_init_all();
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    printf("Starting Onboard temperature\n");

    rtc_init();
    printf("Starting Real Time Clock\n");

    cyw43_arch_enable_sta_mode();
    printf("Connecting to WiFi... ");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("\b\b\b\b, FAILED TO CONNECT.\n");
        return 1;
    } else {
        printf("\b\b\b\b, connected.\n");
    }
    printf("IP Address: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));

    multicore_launch_core1(runMbServer);
    if(multicore_fifo_pop_blocking())
        printf("MB-Server ready on core 1\n");

    setDebugOutput(true);
    int cnt = 0;
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
                            if(mb_msg->addr + i == 0 && mb_mapping->tab_bits[0] == 1){
                                setRTC();
                            }

                            else if(mb_msg->addr + i == 1){
                                setDebugOutput(mb_mapping->tab_bits[1]);
                            }

                            /* modification of coil 2 (temperatur in C or F)
                            * is handled within read_onboard_temperature()
                            */
                            else
                                printf("not handled here.\n");
                        }
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
                    break;

                default:
                    if(modbus_get_debug(ctx))
                        printf("Unknown write-code %d\n", mb_msg->code);
            }
        }

/*
 * Do the actual work
 */
        // increment Input register 0 (approx.) every 10 seconds
        if(cnt == 100){
            modbus_tcp_mapping_lock(ctx);
            if(mb_mapping->tab_input_registers[0] == 15){
                mb_mapping->tab_input_registers[0] = 0;
            }
            else{
                mb_mapping->tab_input_registers[0]++;
            }
            cnt = 0;
            modbus_tcp_mapping_unlock(ctx);
        }
        cnt++;

        // get and store CPU-temerature (Input register 3:0)
        read_onboard_temperature();

        // update date/time (Input register 10:4)
        if(mb_mapping->tab_input_bits[0] == 1){
            updateRTCtoInputregs();
        }

        // Do some other work instead of waisting time...
        sleep_ms(100);
    }

    // should never reach this!
    cyw43_arch_deinit();
    return 0;
}
