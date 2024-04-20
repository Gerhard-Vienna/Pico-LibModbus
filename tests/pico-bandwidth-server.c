/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "bandwidth-server-one.c"
 * to test a modbus server running on a RP2040.
 *
 * Use libmodbus/tests/bandwidth-client as the client to query this server.
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

modbus_t *ctx = NULL;
modbus_mapping_t *mb_mapping = NULL;

void runMbServer(void)
{
    int rc;

    mb_mapping =
        modbus_mapping_new(MODBUS_MAX_READ_BITS, 0, MODBUS_MAX_READ_REGISTERS, 0);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    // The IP is meaningless as we have just one network interface for listening
    ctx = modbus_new_tcp("127.0.0.1", 1502);
    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return;
    }
    modbus_set_debug(ctx, false);

    rc = modbus_tcp_listen(ctx, 2);
    if(rc == -1){
        fprintf(stderr, "Listen failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

    modbus_tcp_accept(ctx, NULL);
    for (;;) {
        rc = modbus_receive(ctx, query);
        if (rc > 0) {
            modbus_reply(ctx, query, rc, mb_mapping);
        } else if (rc == -1 || !modbus_tcp_is_connected(ctx)) {
            modbus_tcp_accept(ctx, NULL);
        }
    }

    // NOT REACHED (just to show what to do if your server quits...
    printf("Quit the loop: %s\n", modbus_strerror(errno));
    modbus_mapping_free(mb_mapping);
    modbus_close(ctx);
    modbus_free(ctx);
}

void main(void)
{
    modbus_message_t *mb_msg;

    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return;
    }

    printf("pico-bandwidth-server\n\n");

    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return;
    } else {
        printf("Connected.\n");
    }
    printf("IP Address: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));

    multicore_launch_core1(runMbServer);

    for(;;){
        sleep_ms(1);
    }

    cyw43_arch_deinit();
    return;
}
