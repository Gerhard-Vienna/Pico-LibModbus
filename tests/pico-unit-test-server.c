/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "unit-test-server.c"
 * to run an a RP2040.
 *
 * Use libmodbus/tests/unit-test-client as the client to query this server.
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
#include "unit-test.h"

void runMbServer(void)
{
    int s = -1;
    modbus_t *ctx;
    modbus_mapping_t *mb_mapping;
    int rc;
    int i;
    int use_backend;
    uint8_t *query;
    int header_length;


    // The IP is meaningless as we have just one network interface for listening
    ctx = modbus_new_tcp("127.0.0.1", 1502);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return;
    }

    query = malloc(MODBUS_TCP_MAX_ADU_LENGTH);
    header_length = modbus_get_header_length(ctx);

    modbus_set_debug(ctx, FALSE);

    mb_mapping = modbus_mapping_new_start_address(UT_BITS_ADDRESS,
                                                  UT_BITS_NB,
                                                  UT_INPUT_BITS_ADDRESS,
                                                  UT_INPUT_BITS_NB,
                                                  UT_REGISTERS_ADDRESS,
                                                  UT_REGISTERS_NB_MAX,
                                                  UT_INPUT_REGISTERS_ADDRESS,
                                                  UT_INPUT_REGISTERS_NB);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    /* Examples from PI_MODBUS_300.pdf.
       Only the read-only input values are assigned. */

    /* Initialize input values that's can be only done server side. */
    modbus_set_bits_from_bytes(
        mb_mapping->tab_input_bits, 0, UT_INPUT_BITS_NB, UT_INPUT_BITS_TAB);

    /* Initialize values of INPUT REGISTERS */
    for (i = 0; i < UT_INPUT_REGISTERS_NB; i++) {
        mb_mapping->tab_input_registers[i] = UT_INPUT_REGISTERS_TAB[i];
    }

    rc = modbus_tcp_listen(ctx, 2);
    if(rc == -1){
        fprintf(stderr, "Listen failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    modbus_tcp_accept(ctx, NULL);

    for (;;) {
        do {
            rc = modbus_receive(ctx, query);
            /* Filtered queries return 0 */
        } while (rc == 0);

        if(rc != -1){
            /* Special server behavior to test client */
            if (query[header_length] == 0x03) {
                /* Read holding registers */

                if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 3) ==
                    UT_REGISTERS_NB_SPECIAL) {
                    printf("Set an incorrect number of values\n");
                    MODBUS_SET_INT16_TO_INT8(
                        query, header_length + 3, UT_REGISTERS_NB_SPECIAL - 1);
                } else if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 1) ==
                        UT_REGISTERS_ADDRESS_SPECIAL) {
                    printf("Reply to this special register address by an exception\n");
                    modbus_reply_exception(ctx, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
                    continue;
                } else if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 1) ==
                        UT_REGISTERS_ADDRESS_INVALID_TID_OR_SLAVE) {
                    const int RAW_REQ_LENGTH = 5;
                    uint8_t raw_req[] = {0xFF,
                                        0x03,
                                        0x02,
                                        0x00,
                                        0x00};

                    printf("Reply with an invalid TID or slave\n");
                    modbus_send_raw_request(ctx, raw_req, RAW_REQ_LENGTH * sizeof(uint8_t));
                    continue;
                } else if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 1) ==
                        UT_REGISTERS_ADDRESS_SLEEP_500_MS) {
                    printf("Sleep 0.5 s before replying\n");
                    busy_wait_ms(500);

                }
            }
        }

        rc = modbus_reply(ctx, query, rc, mb_mapping);
        if (rc == -1 || !modbus_tcp_is_connected(ctx)) {
            modbus_tcp_accept(ctx, NULL);
        }
    }

    // NOT REACHED (just to show what to do if your server quits...
    printf("Quit the loop: %s\n", modbus_strerror(errno));
    modbus_mapping_free(mb_mapping);
    free(query);
}

int main()
{
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 50000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
    }
    printf("IP Address: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));

    multicore_launch_core1(runMbServer);

    for(;;){
    }
}
