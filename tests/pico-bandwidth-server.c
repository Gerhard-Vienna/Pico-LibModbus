/*
 * Copyright © Gerhard Schiller 2024 - 2025, <gerhard.schiller@pm.me>
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

#include "wifi.h"
#include "modbus.h"
#include "unit-test.h"

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

void runMbServer(void)
{
    modbus_t *ctx = NULL;
    modbus_mapping_t *mb_mapping = NULL;
    uint8_t *query;
    int rc;
    int clientID;

    // Start the TCP server
    ctx = tcp_server_init(1502);

    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return;
    }

    query = malloc(MODBUS_TCP_MAX_ADU_LENGTH);

    modbus_set_debug(ctx, FALSE);
    modbus_set_response_timeout(ctx, 3, 0);
    modbus_set_byte_timeout(ctx, 3, 0);

    mb_mapping =
        modbus_mapping_new(MODBUS_MAX_READ_BITS, 0, MODBUS_MAX_READ_REGISTERS, 0);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return;
    }

    while(1){
        for (clientID = 0; clientID < MAX_PEERS; clientID++) {
            if(!modbus_is_connected(clientID)){
                // Either no connection with this ID or the
                // connection is down...
                continue;
            }

            // This order of calls is essential. Modbus_set_socket()
            // must be called before any othe routines that use ctx.
            modbus_set_connectionID(ctx, clientID);

            rc = modbus_client_status(clientID);
            if(rc == 0){
                // No request in  queue
                continue;
            }

            // If we have reached this point (rc > 0), it means the client has
            // sent a request, with rc being the number of bytes in the request.
            // Termination of the connection by the client (rc < 0) is handled by
            // modbus_receive().

            printf("Client %d sent a request (%d bytes)\n",
                   modbus_get_connectionID(ctx), rc);

            // Get the request
            if((rc = modbus_receive(ctx, query)) < 0){
                printf("Client %d Error: %s\n",
                       clientID, strerror(errno));
                continue;
            }

            /* rc is the query size */
            rc = modbus_reply(ctx, query, rc, mb_mapping);
        }
    }
}

int main()
{
    stdio_init_all();
    printf("pico-bandwith-server\n\n");

    // Initialize the network with a static IP
    init_network();

    // Initialize Wi-Fi
    init_wifi();

    multicore_launch_core1(runMbServer);
    for(;;){
    }
}
