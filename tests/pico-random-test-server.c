/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "random-test-server.c"
 * to test a modbus server running on a RP2040.
 *
 * Additional functionality to test the communication between the cores
 * on modification of a coil or register by the client.
 *
 * Use libmodbus/tests/random-test-client as the client to query this server.
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

// #include "lwip/pbuf.h"
// #include "lwip/tcp.h"

#include "wifi.h"
#include "modbus.h"

#define NB_BITS             500
#define NB_INPUT_BITS       500
#define NB_REGISTERS        500
#define NB_INPUT_REGISTERS  500

modbus_t *ctx;
modbus_mapping_t *mb_mapping;

void runMbServer(void)
{
    modbus_message_t mb_msg;
    int rc;

    // The IP is meaningless as we have just one network interface for listening
    ctx = modbus_new_tcp("127.0.0.1", 1502);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return;
    }
    modbus_set_debug(ctx, FALSE);

    mb_mapping = modbus_mapping_new(
        NB_BITS, NB_INPUT_BITS, NB_REGISTERS, NB_INPUT_REGISTERS);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }
    for(int i = 0; i < NB_INPUT_BITS; i++)
        mb_mapping->tab_input_bits[i] = i % 2;
    for(int i = 0; i < NB_INPUT_REGISTERS; i++)
        mb_mapping->tab_input_registers[i] = i + 100;

    rc = modbus_tcp_listen(ctx, 2);
    if(rc == -1){
        fprintf(stderr, "Listen failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    modbus_tcp_accept(ctx, NULL);
    int qc = 0;
    for (;;) {
        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
        int rc;

        printf("Query %d\n", qc++);
        rc = modbus_receive(ctx, query);
        if (rc > 0) {
            /* rc is the query size */
            // TODO check return value from modbus_reply()
            modbus_reply(ctx, query, rc, mb_mapping);

            if(modbus_tcp_message(ctx, query, &mb_msg)){
                multicore_fifo_push_blocking((int32_t)&mb_msg);
            }
//             else{
//                 not a write request, so no need to notify core 0
//             }
        }
        if (rc == -1 || !modbus_tcp_is_connected(ctx)) {
            modbus_tcp_accept(ctx, NULL);
        }
    }

    // NOT REACHED (just to show what to do if your server quits...
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

    printf("pico-random-test-server\n\n");

    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
    }
    printf("IP Address: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));

    multicore_launch_core1(runMbServer);

    for(;;){
        if(multicore_fifo_rvalid()){
            mb_msg = ( modbus_message_t *) multicore_fifo_pop_blocking();

            switch (mb_msg->code) {
               case MODBUS_FC_WRITE_SINGLE_COIL:
                    printf("SINGLE_COIL modified: 0x%02X at 0x%02X\n",
                           mb_mapping->tab_bits[mb_msg->addr],mb_msg->addr);
                    break;

                case MODBUS_FC_WRITE_SINGLE_REGISTER:
                    printf("SINGLE_REGISTER modified: %d at 0x%02X\n",
                           mb_mapping->tab_registers[mb_msg->addr],mb_msg->addr);
                    break;

                case MODBUS_FC_WRITE_MULTIPLE_COILS:
                    printf("MULTIPLE_COILS modified: ");
                    for(int i = 0; i <   mb_msg->count; i++){
                        printf("0x%02X at 0x%02X, ",
                           mb_mapping->tab_bits[mb_msg->addr + i],
                           mb_msg->addr + i);
                    }
                    printf("\n");
                    break;

                case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
                    printf("MULTIPLE_REGISTERS modified: ");
                    for(int i = 0; i <   mb_msg->count; i++){
                        printf("%d at 0x%02X, ",
                               mb_mapping->tab_registers[mb_msg->addr + i],
                               mb_msg->addr + i);
                    }
                    printf("\n");
                    break;

                case MODBUS_FC_WRITE_AND_READ_REGISTERS:
                    printf("MULTIPLE_REGISTERS modified: ");
                    for(int i = 0; i <   mb_msg->count; i++){
                        printf("%d at 0x%02X, ",
                               mb_mapping->tab_registers[mb_msg->addr + i],
                               mb_msg->addr + i);
                    }
                    printf("\n");
                    break;

                default:
                    printf("Unknown write-code %d\n", mb_msg->code);
            }
        }

        // At this point the server should do the real work instead of just sleeping...
        sleep_ms(1);
    }
    cyw43_arch_deinit();
    return 0;
}
