/*
 * Copyright © Gerhard Schiller 2024 - 2025, <gerhard.schiller@pm.me>
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
 *  0       Value for setting the RTC, year
 *  1       Value for setting the RTC, month
 *  2       Value for setting the RTC, day
 *  3       Value for setting the RTC, week day
 *  4       Value for setting the RTC, hour
 *  5       Value for setting the RTC, min
 *  6       Value for setting the RTC, second
 *
 * Discrete inputs (libmodbus mapping: mb_mapping->tab_input_bits)
 *  0      "1": RTC is initialised
 *  1      "1": debugging output enabled
 *
 * Coils:(modbus mapping: mb_mapping->tab_bits)
 *  0      Write a "1" into it to set RTC from the holding register
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

// #include "lwip/pbuf.h"
// #include "lwip/tcp.h"

#include "wifi.h"
#include "modbus.h"

modbus_t *ctx;
modbus_mapping_t *mb_mapping;


#define NB_INPUT_REGISTERS  11
#define NB_REGISTERS        7
#define NB_INPUT_BITS       2
#define NB_BITS             3


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

    if (ctx == NULL) {
        printf("Unable to allocate libmodbus context\n");
        return;
    }

#if PICO_TCP_DEBUG
    setDebugOutput(TRUE);
#else
    setDebugOutput(FALSE);
#endif
}

// This is the task that runs on Core 1.
// It provides the complete functionality of a client.
void runMbServer(void)
{
    modbus_message_t mb_msg;
    uint8_t request[MODBUS_TCP_MAX_ADU_LENGTH];
    int clientID;
    int request_len;
    int rc;

    while(1){
        // Iterate over all possible connections to check whether
        // a clients state has changed.
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
            if(rc <= 0){
                // == 0: not request pending, < 0: ERROR
                continue;
            }

            // If we have reached this point (rc > 0), it means the client has
            // sent a request, with rc being the number of bytes in the request.
            // Termination of the connection by the client (rc < 0) is handled by
            // modbus_receive().


            // Get the request
            if((request_len = modbus_receive(ctx, request)) < 0){
                printf("runMbServer, client %d, error: %s\n",
                       clientID, strerror(errno));
                continue;
            }

            // Send the reply, flash the LED while transmitting
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            rc = modbus_reply(ctx, request, request_len, mb_mapping);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            if(rc < 0){
                printf("runMbServer, client %d, error: %s\n",
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

void main(void)
{
    modbus_message_t *p_mb_msg;

    stdio_init_all();
    printf("pico-server-example\n\n");

    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    printf("Starting Onboard temperature\n");

    rtc_init();
    printf("Starting Real Time Clock\n");

    // Initialize the network
    init_network();

    // Initialize Wi-Fi
    init_wifi();

    // Initialize the ModBus-Server
    init_MbServer();

    // Run the ModBus-Server
    multicore_launch_core1(runMbServer);

    int cnt = 0;
    for(;;){
        // Check wether a client has modified any data in one of the
        // Modbus tables (p_mb_msg != NULL)
        if(p_mb_msg = modbus_write_notify()){
            printf("MbServer notified: function:%d, address:%d, count:%d\n",
                   p_mb_msg->func, p_mb_msg->addr, p_mb_msg->count);

            switch (p_mb_msg->func) {
                case MODBUS_FC_WRITE_SINGLE_COIL:
                case MODBUS_FC_WRITE_MULTIPLE_COILS:
                    if(modbus_get_debug(ctx)) {
                        printf("%d COIL(S) modified:\n", p_mb_msg->count);
                        for(int i = 0; i <  p_mb_msg->count; i++){
                            printf("\t0x%02X at 0x%02X: ",
                            mb_mapping->tab_bits[p_mb_msg->addr + i],
                            p_mb_msg->addr + i);
                            if(p_mb_msg->addr + i == 0 && mb_mapping->tab_bits[0] == 1){
                                setRTC();
                            }

                            else if(p_mb_msg->addr + i == 1){
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
                        printf("%d REGISTER(S) modified:\n", p_mb_msg->count);
                        for(int i = 0; i <  p_mb_msg->count; i++){
                            printf("\t%d at 0x%02X\n",
                                mb_mapping->tab_registers[p_mb_msg->addr + i],
                                p_mb_msg->addr + i);
                        }
                    }
                    break;

                default:
                    // Should never happen
                    if(modbus_get_debug(ctx))
                        printf("Unknown write-func %d\n", p_mb_msg->func);
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
}
