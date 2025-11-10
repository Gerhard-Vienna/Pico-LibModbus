/*
 * Copyright © Gerhard Schiller 2024 - 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "bandwidth-client.c"
 * to test a modbus client running on a RP2040.
 *
 * Use libmodbus/tests/bandwidth-server-one as the server to test this client.
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
#include <modbus.h>

#define G_MSEC_PER_SEC 1000

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

static uint32_t gettime_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint32_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


/* Tests based on PI-MBUS-300 documentation */
void runMbClient(void)
{
    uint8_t *tab_bit;
    uint16_t *tab_reg;
    modbus_t *ctx;
    int serverID = -1;
    int i;
    int nb_points;
    double elapsed;
    uint32_t start;
    uint32_t end;
    uint32_t bytes;
    uint32_t rate;
    int rc;
    int n_loop = 100;
    int loop = 0;

   // Initialize modbus
    ctx = tcp_client_init();
    modbus_set_debug(ctx, FALSE);
    modbus_set_response_timeout(ctx, 3, 0);
    modbus_set_byte_timeout(ctx, 3, 0);

    // It is the client's responsibility to re-establish an interrupted
    // connection.
    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK |                                MODBUS_ERROR_RECOVERY_PROTOCOL);
    // modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK);


    /* Allocate and initialize the memory to store the status */
    tab_bit = (uint8_t *) malloc(MODBUS_MAX_READ_BITS * sizeof(uint8_t));
    memset(tab_bit, 0, MODBUS_MAX_READ_BITS * sizeof(uint8_t));

    /* Allocate and initialize the memory to store the registers */
    tab_reg = (uint16_t *) malloc(MODBUS_MAX_READ_REGISTERS * sizeof(uint16_t));
    memset(tab_reg, 0, MODBUS_MAX_READ_REGISTERS * sizeof(uint16_t));

    // Initialize the clients connection to the server
    serverID = tcp_new_client(SERVER_WS_IP, 1502);
    // serverID = tcp_new_client(SERVER_PICO_IP, 1502);
    modbus_set_connectionID(ctx, serverID);


/* If the client is set to re-establish an interrupted connection
 * with “modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK);”,
 * this is not really necessary.
 * However:
 *     a) it is good practice to first check whether the server is
 *        available, AND
 *     b) establishing a connection while the test is running would
 *        distort the result!
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

    for(;;){
        loop++;

        printf("Loop %d: READ BITS\n", loop);

        nb_points = MODBUS_MAX_READ_BITS;
        start = gettime_ms();
        for (i = 0; i < n_loop; i++) {
            rc = modbus_read_bits(ctx, 0, nb_points, tab_bit);
            if (rc == -1) {
                fprintf(stderr, "%s\n", modbus_strerror(errno));
                sleep_ms(5 * 1000);
                continue;
            }
        }
        end = gettime_ms();
        elapsed = end - start;

        rate = (n_loop * nb_points) * G_MSEC_PER_SEC / (end - start);
        printf("Transfer rate in points/seconds:\n");
        printf("* %d points/s\n", rate);
        printf("\n");

        bytes = n_loop * (nb_points / 8) + ((nb_points % 8) ? 1 : 0);
        rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
        printf("Values:\n");
        printf("* %d x %d values\n", n_loop, nb_points);
        printf("* %.3f ms for %d bytes\n", elapsed, bytes);
        printf("* %d KiB/s\n", rate);
        printf("\n");

        /* TCP: Query and response header and values */
        bytes = 12 + 9 + (nb_points / 8) + ((nb_points % 8) ? 1 : 0);
        printf("Values and TCP Modbus overhead:\n");
        printf("* %d x %d bytes\n", n_loop, bytes);
        bytes = n_loop * bytes;
        rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
        printf("* %.3f ms for %d bytes\n", elapsed, bytes);
        printf("* %d KiB/s\n", rate);
        printf("\n\n");

        printf("Loop %d: READ REGISTERS\n", loop);

        nb_points = MODBUS_MAX_READ_REGISTERS;
        start = gettime_ms();
        for (i = 0; i < n_loop; i++) {
            rc = modbus_read_registers(ctx, 0, nb_points, tab_reg);
            if (rc == -1) {
                fprintf(stderr, "%s\n", modbus_strerror(errno));
                sleep_ms(5 * 1000);
                continue;
            }
        }
        end = gettime_ms();
        elapsed = end - start;

        rate = (n_loop * nb_points) * G_MSEC_PER_SEC / (end - start);
        printf("Transfer rate in points/seconds:\n");
        printf("* %d registers/s\n", rate);
        printf("\n");

        bytes = n_loop * nb_points * sizeof(uint16_t);
        rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
        printf("Values:\n");
        printf("* %d x %d values\n", n_loop, nb_points);
        printf("* %.3f ms for %d bytes\n", elapsed, bytes);
        printf("* %d KiB/s\n", rate);
        printf("\n");

        /* TCP:Query and response header and values */
        bytes = 12 + 9 + (nb_points * sizeof(uint16_t));
        printf("Values and TCP Modbus overhead:\n");
        printf("* %d x %d bytes\n", n_loop, bytes);
        bytes = n_loop * bytes;
        rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
        printf("* %.3f ms for %d bytes\n", elapsed, bytes);
        printf("* %d KiB/s\n", rate);
        printf("\n\n");

        printf("Loop %d: WRITE AND READ REGISTERS\n", loop);

        nb_points = MODBUS_MAX_WR_WRITE_REGISTERS;
        start = gettime_ms();
        for (i = 0; i < n_loop; i++) {
            rc = modbus_write_and_read_registers(
                ctx, 0, nb_points, tab_reg, 0, nb_points, tab_reg);
            if (rc == -1) {
                fprintf(stderr, "%s\n", modbus_strerror(errno));
                sleep_ms(5 * 1000);
                continue;
            }
        }
        end = gettime_ms();
        elapsed = end - start;

        rate = (n_loop * nb_points) * G_MSEC_PER_SEC / (end - start);
        printf("Transfer rate in points/seconds:\n");
        printf("* %d registers/s\n", rate);
        printf("\n");

        bytes = n_loop * nb_points * sizeof(uint16_t);
        rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
        printf("Values:\n");
        printf("* %d x %d values\n", n_loop, nb_points);
        printf("* %.3f ms for %d bytes\n", elapsed, bytes);
        printf("* %d KiB/s\n", rate);
        printf("\n");

        /* TCP:Query and response header and values */
        bytes = 12 + 9 + (nb_points * sizeof(uint16_t));
        printf("Values and TCP Modbus overhead:\n");
        printf("* %d x %d bytes\n", n_loop, bytes);
        bytes = n_loop * bytes;
        rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
        printf("* %.3f ms for %d bytes\n", elapsed, bytes);
        printf("* %d KiB/s\n", rate);
        printf("\n");

        sleep_ms(5 * 1000);
    }
}

void main()
{
    stdio_init_all();
    printf("pico-bandwith-client\n\n");

    // Initialize the network with a static IP
    init_network();

    // Initialize Wi-Fi
    init_wifi();

    runMbClient();
    for(;;){
    }
}

