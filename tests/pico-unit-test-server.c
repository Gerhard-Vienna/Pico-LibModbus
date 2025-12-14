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
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

#include "wifi.h"
#include "modbus.h"
#include "unit-test.h"

// Not normally called directly from an application
int _send(int ctx_s, void *buf, size_t len);


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
    modbus_t *ctx;
    modbus_mapping_t *mb_mapping;
    uint8_t *query;
    int header_length;
    int rc;
    int clientID;

   // Start the TCP server
    ctx = tcp_server_init(1502);

    if (ctx == NULL) {
        fprintf(stderr, "Unable to allocate libmodbus context\n");
        return;
    }

    query = malloc(MODBUS_TCP_MAX_ADU_LENGTH);

    header_length = modbus_get_header_length(ctx);

//G##    modbus_set_debug(ctx, FALSE);
    modbus_set_debug(ctx, TRUE);

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
        return ;
    }

    /* Examples from PI_MODBUS_300.pdf.
       Only the read-only input values are assigned. */

    /* Initialize input values that's can be only done server side. */
    modbus_set_bits_from_bytes(
        mb_mapping->tab_input_bits, 0, UT_INPUT_BITS_NB, UT_INPUT_BITS_TAB);

    /* Initialize values of INPUT REGISTERS */
    for (int i = 0; i < UT_INPUT_REGISTERS_NB; i++) {
        mb_mapping->tab_input_registers[i] = UT_INPUT_REGISTERS_TAB[i];
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

            // Prepare reply
            uint8_t function = query[header_length];
            uint16_t address = MODBUS_GET_INT16_FROM_INT8(query, header_length + 1);

            /** Special server behavior to test client **/
            if (function == MODBUS_FC_READ_HOLDING_REGISTERS) {
                if (MODBUS_GET_INT16_FROM_INT8(query, header_length + 3) ==
                    UT_REGISTERS_NB_SPECIAL) {
                    printf("Set an incorrect number of values\n");
                    MODBUS_SET_INT16_TO_INT8(
                        query, header_length + 3, UT_REGISTERS_NB_SPECIAL - 1);
                } else if (address == UT_REGISTERS_ADDRESS_SPECIAL) {
                    printf("Reply to this special register address by an exception\n");
                    modbus_reply_exception(ctx, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
                    continue;
                } else if (address == UT_REGISTERS_ADDRESS_INVALID_TID_OR_SLAVE) {
                    const int RAW_REQ_LENGTH = 5;

                    uint8_t raw_req[] = {
                        0xFF,
                        0x03,
                        0x02,
                        0x00,
                        0x00};
                        printf("Reply with an invalid TID or slave\n");
                    modbus_send_raw_request(ctx, raw_req, RAW_REQ_LENGTH * sizeof(uint8_t));
                    continue;
                } else if (address == UT_REGISTERS_ADDRESS_SLEEP_500_MS) {
                    printf("Sleep 0.5 s before replying\n");
                    // usleep(500000);
                    sleep_ms(500);
                } else if (address == UT_REGISTERS_ADDRESS_BYTE_SLEEP_5_MS) {
                    /* Test low level only available in TCP mode */
                    /* Catch the reply and send reply byte a byte */
                    uint8_t req[] = "\x00\x1C\x00\x00\x00\x05\xFF\x03\x02\x00\x00";
                    int req_length = 11;
                    int w_s = modbus_get_socket(ctx);
                    if (w_s == -1) {
                        fprintf(stderr, "Unable to get a valid socket in special test\n");
                        continue;
                    }
                    // Disable the aggregation of small TCP packets.
                    modbus_tcp_nodelay(ctx, TRUE);

                    /* Copy TID */
                    req[0] = query[0];
                    req[1] = query[1];
                    for (int i = 0; i < req_length; i++) {
                        printf("(%.2X)", req[i]);
                        sleep_ms(30);
                        rc = _send(w_s, (void *) (req + i), 1);
                        if (rc == -1) {
                            break;
                        }
                    }
                    // Reenable the aggregation of small TCP packets.
                    modbus_tcp_nodelay(ctx, FALSE);
                    continue;
                }
            } else if (function == MODBUS_FC_WRITE_SINGLE_COIL) {
                if (address == UT_BITS_ADDRESS_INVALID_REQUEST_LENGTH) {
                    // The valid length is lengths of header + checkum + FC + address + value
                    // (max 12)
                    rc = 34;
                    printf(
                        "Special modbus_write_bit detected. Inject a wrong length value (%d) "
                        "in modbus_reply\n",
                        rc);
                }
            } else if (function == MODBUS_FC_WRITE_SINGLE_REGISTER) {
                if (address == UT_REGISTERS_ADDRESS_SPECIAL) {
                    rc = 45;
                    printf("Special modbus_write_register detected. Inject a wrong length "
                        "value (%d) in modbus_reply\n",
                        rc);
                }
            }
            // Send reply (if not already done above...)
            rc = modbus_reply(ctx, query, rc, mb_mapping);
        }
    }
    return;
}

int main()
{
    stdio_init_all();
    printf("pico-unit-test-server\n\n");

    // Initialize the network with a static IP
    init_network();

    // Initialize Wi-Fi
    init_wifi();

    multicore_launch_core1(runMbServer);
    for(;;){
    }
}


