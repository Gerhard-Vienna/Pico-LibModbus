/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "random-test-client.c"
 * to test a modbus client running on a RP2040.
 *
 * Use libmodbus/tests/random-test-server as the server to test this client.
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

// #include "lwip/pbuf.h"
// #include "lwip/tcp.h"

#include "wifi.h"
#include "modbus.h"

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

#define ADDRESS_START 0
#define ADDRESS_END   99

/* At each loop, the program works in the range ADDRESS_START to
 * ADDRESS_END then ADDRESS_START + 1 to ADDRESS_END and so on.
 */
void main(void)
{
    modbus_t *ctx;
    int rc;
    int nb_fail;
    int nb_loop;
    int addr;
    int nb;
    uint8_t *tab_rq_bits;
    uint8_t *tab_rp_bits;
    uint16_t *tab_rq_registers;
    uint16_t *tab_rw_rq_registers;
    uint16_t *tab_rp_registers;

    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return;
    }

    cyw43_arch_enable_sta_mode();

    printf("pico-random-test-client\n\n");

    printf("Connecting to WiFi... ");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return;
    } else {
        printf("connected.\n");
    }
    printf("IP Address: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));

    ctx = modbus_new_tcp(SERVER_IP, 1502);
    modbus_set_debug(ctx, FALSE);

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

    nb_loop = 0;
    while (true) {
        if(!modbus_tcp_is_connected(ctx)){
            sleep_ms(3000);
            printf("trying to (re-)connect\n");
            modbus_connect(ctx);
            continue;
        }

        printf("Loop %d\n", nb_loop++);
        nb_fail = 0;
        for (addr = ADDRESS_START; addr < ADDRESS_END; addr++) {
            printf("\ttesting address %d\n", addr);
            int i;

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
                break;
            }
            else {
                rc = modbus_read_bits(ctx, addr, 1, tab_rp_bits);
                if (rc != 1 || tab_rq_bits[0] != tab_rp_bits[0]) {
                    printf("ERROR modbus_read_bits single (%d)\n", rc);
                    printf("address = %d\n", addr);
                    nb_fail++;
                    break;
                }
            }

            /* MULTIPLE BITS */
            rc = modbus_write_bits(ctx, addr, nb, tab_rq_bits);
            if (rc != nb) {
                printf("ERROR modbus_write_bits (%d)\n", rc);
                printf("Address = %d, nb = %d\n", addr, nb);
                nb_fail++;
                break;
            } else {
                rc = modbus_read_bits(ctx, addr, nb, tab_rp_bits);
                if (rc != nb) {
                    printf("ERROR modbus_read_bits\n");
                    printf("Address = %d, nb = %d\n", addr, nb);
                    nb_fail++;
                    break;
                }
                else {
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
                break;
            }
            else {
                rc = modbus_read_registers(ctx, addr, 1, tab_rp_registers);
                if (rc != 1) {
                    printf("ERROR modbus_read_registers single (%d)\n", rc);
                    printf("Address = %d\n", addr);
                    nb_fail++;
                    break;
                }
                else {
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
                break;
            }
            else {
                rc = modbus_read_registers(ctx, addr, nb, tab_rp_registers);
                if (rc != nb) {
                    printf("ERROR modbus_read_registers (%d)\n", rc);
                    printf("Address = %d, nb = %d\n", addr, nb);
                    nb_fail++;
                    break;
                }
                else {
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
                break;
            }
            else {
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
                    break;
                }
                else {
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
        }

        printf("Test: ");
        if (nb_fail)
            printf("%d FAILS\n\n", nb_fail);
        else
            printf("SUCCESS\n\n");
    } // End of "while (true)"

    // NOT REACHED (just to show what to do if your client quits...
    printf("Quit the loop\n");
    /* Free the memory */
    free(tab_rq_bits);
    free(tab_rp_bits);
    free(tab_rq_registers);
    free(tab_rp_registers);
    free(tab_rw_rq_registers);

    /* Close the connection */
    modbus_close(ctx);
    modbus_free(ctx);

    return;
}
