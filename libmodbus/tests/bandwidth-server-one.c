/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "bandwidth-server-one.c"
 * to test a modbus server running on a RP2040.
 *
 * Use tests/pico-bandwidth-client as the client to query this server.
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
#include <unistd.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <modbus.h>

#if defined(_WIN32)
#define close closesocket
#endif

enum {
    TCP,
    RTU
};

int main(int argc, char *argv[])
{
    int s = -1;
    modbus_t *ctx = NULL;
    modbus_mapping_t *mb_mapping = NULL;
    int rc;
    int use_backend;
#ifdef PICO_W_TESTS
    char *ip_or_device;
#endif

    /* TCP */
    if (argc > 1) {
        if (strcmp(argv[1], "tcp") == 0) {
            use_backend = TCP;
#ifdef PICO_W_TESTS
            if (argc > 2) {
                ip_or_device = argv[2];
            } else {
                ip_or_device = "127.0.0.1";
            }
#endif
        } else if (strcmp(argv[1], "rtu") == 0) {
            use_backend = RTU;
        } else {
            printf("Usage:\n  %s [tcp|rtu] - Modbus client to measure data bandwidth\n\n",
                   argv[0]);
            exit(1);
        }
    } else {
        /* By default */
        use_backend = TCP;
#ifdef PICO_W_TESTS
        ip_or_device = "127.0.0.1";
#endif
    }

    if (use_backend == TCP) {
#ifndef PICO_W_TESTS
        ctx = modbus_new_tcp("127.0.0.1", 1502);

#else
        printf("Starting server at %s:%d\n", ip_or_device, 1502);
        ctx = modbus_new_tcp(ip_or_device, 1502);
        if (ctx == NULL) {
            fprintf(stderr, "Unable to allocate libmodbus context\n");
            return -1;
        }
#endif
        s = modbus_tcp_listen(ctx, 1);
        modbus_tcp_accept(ctx, &s);
    } else {
        ctx = modbus_new_rtu("/dev/ttyUSB0", 115200, 'N', 8, 1);
        modbus_set_slave(ctx, 1);
        modbus_connect(ctx);
    }

#ifdef PICO_W_TESTS
    modbus_set_response_timeout(ctx, 1, 0);
#endif

    mb_mapping =
        modbus_mapping_new(MODBUS_MAX_READ_BITS, 0, MODBUS_MAX_READ_REGISTERS, 0);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    for (;;) {
        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

        rc = modbus_receive(ctx, query);
        if (rc > 0) {
            modbus_reply(ctx, query, rc, mb_mapping);
        } else if (rc == -1) {
            /* Connection closed by the client or error */
            break;
        }
    }

    printf("Quit the loop: %s\n", modbus_strerror(errno));

    modbus_mapping_free(mb_mapping);
    if (s != -1) {
        close(s);
    }
    /* For RTU, skipped by TCP (no TCP connect) */
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}
