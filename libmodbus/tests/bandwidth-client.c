/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "bandwidth-client.c"
 * to test a modbus server running on a RP2040.
 *
 * Use tests/pico-bandwidth-server as the server to test this client.
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#ifndef _MSC_VER
#include <sys/time.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <modbus.h>

#define G_MSEC_PER_SEC 1000

static uint32_t gettime_ms(void)
{
    struct timeval tv;
#if !defined(_MSC_VER)
    gettimeofday(&tv, NULL);
    return (uint32_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
#else
    return GetTickCount();
#endif
}

enum {
    TCP,
    RTU
};

/* Tests based on PI-MBUS-300 documentation */
int main(int argc, char *argv[])
{
    uint8_t *tab_bit;
    uint16_t *tab_reg;
    modbus_t *ctx;
    int i;
    int nb_points;
    double elapsed;
    uint32_t start;
    uint32_t end;
    uint32_t bytes;
#ifndef PICO_W
    uint32_t rate;
#else
    float rate;
#endif
    int rc;
    int n_loop;
    int use_backend;
#ifdef PICO_W
    char *ip_or_device;
#endif

    if (argc > 1) {
        if (strcmp(argv[1], "tcp") == 0) {
            use_backend = TCP;
#ifndef PICO_W
            n_loop = 100000;
#else
            if (argc > 2) {
                ip_or_device = argv[2];
            } else {
                ip_or_device = "127.0.0.1";
            }
            n_loop = 10;
#endif
        } else if (strcmp(argv[1], "rtu") == 0) {
            use_backend = RTU;
            n_loop = 100;
        } else {
#ifndef PICO_W
            printf("Usage:\n  %s [tcp|rtu] - Modbus client to measure data bandwidth\n\n",
                   argv[0]);
#else
            printf("Usage:\n  %s [tcp IP|rtu]\n", argv[0]);
            printf("  Eg. %s tcp 10.0.0.1\n", argv[0]);
            printf("  or %s rtu\n", argv[0]);
#endif
            exit(1);
        }
    } else {
        /* By default */
        use_backend = TCP;
#ifndef PICO_W
        n_loop = 100000;
#else
        ip_or_device = "127.0.0.1";
        n_loop = 10;
#endif
    }

    if (use_backend == TCP) {
#ifndef PICO_W
        ctx = modbus_new_tcp("127.0.0.1", 1502);
#else
        ctx = modbus_new_tcp(ip_or_device, 1502);
#endif
    } else {
        ctx = modbus_new_rtu("/dev/ttyUSB1", 115200, 'N', 8, 1);
        modbus_set_slave(ctx, 1);
    }

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

#ifdef PICO_W
    modbus_set_response_timeout(ctx, 1, 0);
#endif

    /* Allocate and initialize the memory to store the status */
    tab_bit = (uint8_t *) malloc(MODBUS_MAX_READ_BITS * sizeof(uint8_t));
    memset(tab_bit, 0, MODBUS_MAX_READ_BITS * sizeof(uint8_t));

    /* Allocate and initialize the memory to store the registers */
    tab_reg = (uint16_t *) malloc(MODBUS_MAX_READ_REGISTERS * sizeof(uint16_t));
    memset(tab_reg, 0, MODBUS_MAX_READ_REGISTERS * sizeof(uint16_t));

    printf("READ BITS\n\n");

    nb_points = MODBUS_MAX_READ_BITS;
    start = gettime_ms();
    for (i = 0; i < n_loop; i++) {
        rc = modbus_read_bits(ctx, 0, nb_points, tab_bit);
        if (rc == -1) {
            fprintf(stderr, "%s\n", modbus_strerror(errno));
            return -1;
        }
    }
    end = gettime_ms();
    elapsed = end - start;

    rate = (n_loop * nb_points) * G_MSEC_PER_SEC / (end - start);
    printf("Transfer rate in points/seconds:\n");
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.0f p/s\n", rate);
#endif
    printf("\n");

    bytes = n_loop * (nb_points / 8) + ((nb_points % 8) ? 1 : 0);
#ifndef PICO_W
    rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#else
    rate = (float)bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#endif
    printf("Values:\n");
    printf("* %d x %d values\n", n_loop, nb_points);
    printf("* %.3f ms for %d bytes\n", elapsed, bytes);

#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.2f KiB/s\n", rate);
#endif
    printf("\n");

    /* TCP: Query and response header and values */
    bytes = 12 + 9 + (nb_points / 8) + ((nb_points % 8) ? 1 : 0);
    printf("Values and TCP Modbus overhead:\n");
    printf("* %d x %d bytes\n", n_loop, bytes);
    bytes = n_loop * bytes;
#ifndef PICO_W
    rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#else
    rate = (float)bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#endif
    printf("* %.3f ms for %d bytes\n", elapsed, bytes);
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.2f KiB/s\n", rate);
#endif
    printf("\n\n");

    printf("READ REGISTERS\n\n");

    nb_points = MODBUS_MAX_READ_REGISTERS;
    start = gettime_ms();
    for (i = 0; i < n_loop; i++) {
        rc = modbus_read_registers(ctx, 0, nb_points, tab_reg);
        if (rc == -1) {
            fprintf(stderr, "%s\n", modbus_strerror(errno));
            return -1;
        }
    }
    end = gettime_ms();
    elapsed = end - start;

    rate = (n_loop * nb_points) * G_MSEC_PER_SEC / (end - start);
    printf("Transfer rate in points/seconds:\n");
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.0f p/s\n", rate);
#endif
    printf("\n");

    bytes = n_loop * nb_points * sizeof(uint16_t);
#ifndef PICO_W
    rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#else
    rate = (float)bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#endif
    printf("Values:\n");
    printf("* %d x %d values\n", n_loop, nb_points);
    printf("* %.3f ms for %d bytes\n", elapsed, bytes);
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.2f KiB/s\n", rate);
#endif
    printf("\n");

    /* TCP:Query and response header and values */
    bytes = 12 + 9 + (nb_points * sizeof(uint16_t));
    printf("Values and TCP Modbus overhead:\n");
    printf("* %d x %d bytes\n", n_loop, bytes);
    bytes = n_loop * bytes;
#ifndef PICO_W
    rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#else
    rate = (float)bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#endif
    printf("* %.3f ms for %d bytes\n", elapsed, bytes);
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.2f KiB/s\n", rate);
#endif
    printf("\n\n");

    printf("WRITE AND READ REGISTERS\n\n");

    nb_points = MODBUS_MAX_WR_WRITE_REGISTERS;
    start = gettime_ms();
    for (i = 0; i < n_loop; i++) {
        rc = modbus_write_and_read_registers(
            ctx, 0, nb_points, tab_reg, 0, nb_points, tab_reg);
        if (rc == -1) {
            fprintf(stderr, "%s\n", modbus_strerror(errno));
            return -1;
        }
    }
    end = gettime_ms();
    elapsed = end - start;

    rate = (n_loop * nb_points) * G_MSEC_PER_SEC / (end - start);
    printf("Transfer rate in points/seconds:\n");
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.0f p/s\n", rate);
#endif
    printf("\n");

    bytes = n_loop * nb_points * sizeof(uint16_t);
#ifndef PICO_W
    rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#else
    rate = (float)bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#endif
    printf("Values:\n");
    printf("* %d x %d values\n", n_loop, nb_points);
    printf("* %.3f ms for %d bytes\n", elapsed, bytes);
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.2f KiB/s\n", rate);
#endif
    printf("\n");

    /* TCP:Query and response header and values */
    bytes = 12 + 9 + (nb_points * sizeof(uint16_t));
    printf("Values and TCP Modbus overhead:\n");
    printf("* %d x %d bytes\n", n_loop, bytes);
    bytes = n_loop * bytes;
#ifndef PICO_W
    rate = bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#else
    rate = (float)bytes / 1024 * G_MSEC_PER_SEC / (end - start);
#endif
    printf("* %.3f ms for %d bytes\n", elapsed, bytes);
#ifndef PICO_W
    printf("* %d KiB/s\n", rate);
#else
    printf("* %.2f KiB/s\n", rate);
#endif
    printf("\n");

    /* Free the memory */
    free(tab_bit);
    free(tab_reg);

    /* Close the connection */
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}
