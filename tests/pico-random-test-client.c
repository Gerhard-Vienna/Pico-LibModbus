/*
 * Copyright © Gerhard Schiller 2024 - 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "unit-test-client.c"
 * to test a modbus server running on a RP2040.
 *
 * Use tests/pico-unit-test-server as the server to test this client.
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "wifi.h"
#include <modbus.h>

/* The goal of this program is to check all major functions of
   libmodbus:
   - write_coil
   - read_bits
   - write_coils
   - write_register
   - read_registers
   - write_registers
   - read_registers

   All these functions are called with random values on a address
   range defined by the following defines.
*/
#define LOOP          1
#define SERVER_ID     17
#define ADDRESS_START 0
#define ADDRESS_END   99

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

    ip4addr_aton(CLIENT_IP, &ip);
    ip4addr_aton(NETMASK, &mask);
    ip4addr_aton(GATEWAY, &gw);

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
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


/* At each loop, the program works in the range ADDRESS_START to
 * ADDRESS_END then ADDRESS_START + 1 to ADDRESS_END and so on.
 */
void runMbClient(void)
{
    modbus_t *ctx;
    int serverID = -1;

    int rc;
    int nb_fail;
    int addr;
    int nb;
    uint8_t *tab_rq_bits;
    uint8_t *tab_rp_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;
    uint16_t *tab_rp_registers;

    int loopOK = 0;
    int loopFail = 0;

    // Initialize modbus
    ctx = tcp_client_init();
    modbus_set_debug(ctx, TRUE);
    // modbus_set_response_timeout(ctx, 3, 0);
    // modbus_set_byte_timeout(ctx, 3, 0);

    // It is the client's responsibility to re-establish an interrupted
    // connection.
    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK |                                MODBUS_ERROR_RECOVERY_PROTOCOL);
    // modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK);


    /* Allocate and initialize the different memory spaces */
    nb = ADDRESS_END - ADDRESS_START;

    tab_rq_bits = (uint8_t *) malloc(nb * sizeof(uint8_t));
    memset(tab_rq_bits, 0, nb * sizeof(uint8_t));

    tab_rp_bits = (uint8_t *) malloc(nb * sizeof(uint8_t));
    memset(tab_rp_bits, 0, nb * sizeof(uint8_t));

    tab_rq_registers = (uint16_t *) malloc(nb * sizeof(uint16_t));
    memset(tab_rq_registers, 0, nb * sizeof(uint16_t));

    tab_rp_registers = (uint16_t *) malloc(nb * sizeof(uint16_t));
    memset(tab_rp_registers, 0, nb * sizeof(uint16_t));

    tab_rw_rq_registers = (uint16_t *) malloc(nb * sizeof(uint16_t));
    memset(tab_rw_rq_registers, 0, nb * sizeof(uint16_t));

    // Initialize the clients connection to the server
    serverID = tcp_new_client(SERVER_WS_IP, 1502);
    // serverID = tcp_new_client(SERVER_PICO_IP, 1502);
    modbus_set_connectionID(ctx, serverID);


    /* If the client is set to re-establish an interrupted connection
     * with “modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK);”,
     * this is not really necessary.
     * However it is good practice to first check whether the server is
     * available.
     *
     * Additionally, it is possible to eliminate any remnants of a failed
     * connection prior to initiating Modbus transactions: "modbus_flush()"
     */

#define WAIT_fOR_SERVER_ONLINE
#ifdef WAIT_fOR_SERVER_ONLINE
    while(1){
        if(modbus_connect(ctx) < 0){
            printf("*** Server, Main connect() error: %s\n", strerror(errno));
        }
        if(modbus_is_connected(serverID)){
            break;
        }
        else{
            printf("*** Server, Main: will try again to connect in 5 sec\n");
            sleep_ms(5000);
        }
    }
    modbus_flush(ctx);
#endif

    while (1) {
       for (addr = ADDRESS_START; addr < ADDRESS_END; addr++) {
            int i;
            nb_fail = 0;

            /* Random numbers (short) */
            for (i = 0; i < nb; i++) {
                tab_rq_registers[i] = (uint16_t) (65535.0 * rand() / (RAND_MAX + 1.0));
                tab_rw_rq_registers[i] = ~tab_rq_registers[i];
                tab_rq_bits[i] = tab_rq_registers[i] % 2;
            }
            nb = ADDRESS_END - addr;

            /* WRITE BIT */
            rc = modbus_write_bit(ctx, addr, tab_rq_bits[0]);
            if (rc != 1) {
                printf("ERROR modbus_write_bit (%d)\n", rc);
                printf("Address = %d, value = %d\n", addr, tab_rq_bits[0]);
                nb_fail++;
            } else {
                rc = modbus_read_bits(ctx, addr, 1, tab_rp_bits);
                if (rc != 1 || tab_rq_bits[0] != tab_rp_bits[0]) {
                    printf("ERROR modbus_read_bits single (%d)\n", rc);
                    printf("address = %d\n", addr);
                    nb_fail++;
                }
            }

            /* MULTIPLE BITS */
            rc = modbus_write_bits(ctx, addr, nb, tab_rq_bits);
            if (rc != nb) {
                printf("ERROR modbus_write_bits (%d)\n", rc);
                printf("Address = %d, nb = %d\n", addr, nb);
                nb_fail++;
            } else {
                rc = modbus_read_bits(ctx, addr, nb, tab_rp_bits);
                if (rc != nb) {
                    printf("ERROR modbus_read_bits\n");
                    printf("Address = %d, nb = %d\n", addr, nb);
                    nb_fail++;
                } else {
                    for (i = 0; i < nb; i++) {
                        if (tab_rp_bits[i] != tab_rq_bits[i]) {
                            printf("ERROR modbus_read_bits\n");
                            printf("Address = %d, value %d (0x%X) != %d (0x%X)\n",
                                   addr,
                                   tab_rq_bits[i],
                                   tab_rq_bits[i],
                                   tab_rp_bits[i],
                                   tab_rp_bits[i]);
                            nb_fail++;
                        }
                    }
                }
            }

            /* SINGLE REGISTER */
            rc = modbus_write_register(ctx, addr, tab_rq_registers[0]);
            if (rc != 1) {
                printf("ERROR modbus_write_register (%d)\n", rc);
                printf("Address = %d, value = %d (0x%X)\n",
                       addr,
                       tab_rq_registers[0],
                       tab_rq_registers[0]);
                nb_fail++;
            } else {
                rc = modbus_read_registers(ctx, addr, 1, tab_rp_registers);
                if (rc != 1) {
                    printf("ERROR modbus_read_registers single (%d)\n", rc);
                    printf("Address = %d\n", addr);
                    nb_fail++;
                } else {
                    if (tab_rq_registers[0] != tab_rp_registers[0]) {
                        printf("ERROR modbus_read_registers single\n");
                        printf("Address = %d, value = %d (0x%X) != %d (0x%X)\n",
                               addr,
                               tab_rq_registers[0],
                               tab_rq_registers[0],
                               tab_rp_registers[0],
                               tab_rp_registers[0]);
                        nb_fail++;
                    }
                }
            }

            /* MULTIPLE REGISTERS */
            rc = modbus_write_registers(ctx, addr, nb, tab_rq_registers);
            if (rc != nb) {
                printf("ERROR modbus_write_registers (%d)\n", rc);
                printf("Address = %d, nb = %d\n", addr, nb);
                nb_fail++;
            } else {
                rc = modbus_read_registers(ctx, addr, nb, tab_rp_registers);
                if (rc != nb) {
                    printf("ERROR modbus_read_registers (%d)\n", rc);
                    printf("Address = %d, nb = %d\n", addr, nb);
                    nb_fail++;
                } else {
                    for (i = 0; i < nb; i++) {
                        if (tab_rq_registers[i] != tab_rp_registers[i]) {
                            printf("ERROR modbus_read_registers\n");
                            printf("Address = %d, value %d (0x%X) != %d (0x%X)\n",
                                   addr,
                                   tab_rq_registers[i],
                                   tab_rq_registers[i],
                                   tab_rp_registers[i],
                                   tab_rp_registers[i]);
                            nb_fail++;
                        }
                    }
                }
            }
            /* R/W MULTIPLE REGISTERS */
            rc = modbus_write_and_read_registers(
                ctx, addr, nb, tab_rw_rq_registers, addr, nb, tab_rp_registers);
            if (rc != nb) {
                printf("ERROR modbus_read_and_write_registers (%d)\n", rc);
                printf("Address = %d, nb = %d\n", addr, nb);
                nb_fail++;
            } else {
                for (i = 0; i < nb; i++) {
                    if (tab_rp_registers[i] != tab_rw_rq_registers[i]) {
                        printf("ERROR modbus_read_and_write_registers READ\n");
                        printf("Address = %d, value %d (0x%X) != %d (0x%X)\n",
                               addr,
                               tab_rp_registers[i],
                               tab_rw_rq_registers[i],
                               tab_rp_registers[i],
                               tab_rw_rq_registers[i]);
                        nb_fail++;
                   }
                }

                rc = modbus_read_registers(ctx, addr, nb, tab_rp_registers);
                if (rc != nb) {
                    printf("ERROR modbus_read_registers (%d)\n", rc);
                    printf("Address = %d, nb = %d\n", addr, nb);
                    nb_fail++;
                } else {
                    for (i = 0; i < nb; i++) {
                        if (tab_rw_rq_registers[i] != tab_rp_registers[i]) {
                            printf("ERROR modbus_read_and_write_registers WRITE\n");
                            printf("Address = %d, value %d (0x%X) != %d (0x%X)\n",
                                   addr,
                                   tab_rw_rq_registers[i],
                                   tab_rw_rq_registers[i],
                                   tab_rp_registers[i],
                                   tab_rp_registers[i]);
                            nb_fail++;
                        }
                    }
                }
            }
            if (nb_fail){
                loopFail++;
                printf("\nLoop %d: %d tests FAILED\n", loopOK + loopFail, nb_fail);
            }
            else{
                loopOK++;
                printf("\nLoop %d: all TESTS PASSED WITH SUCCESS.\n",
                       loopOK + loopFail);
            }
            printf("Sofar %d loop(s) passed all tests, tests failed in %d loop(s).\n", loopOK, loopFail);

            sleep_ms(1000);
        }
   }
}

void main()
{
    stdio_init_all();
    printf("pico-random-test-client\n\n");

    // Initialize the network with a static IP
    init_network();

    // Initialize Wi-Fi
    init_wifi();

    runMbClient();
    for(;;){
    }
}


