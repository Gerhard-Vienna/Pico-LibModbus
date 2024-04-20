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

#include <modbus.h>

#ifndef PICO_W_TESTS
int main(void)
#else
int main(int argc, char *argv[])
#endif
{
    int s = -1;
    modbus_t *ctx;
    modbus_mapping_t *mb_mapping;
#ifdef PICO_W_TESTS
    char *ip_or_device;
#endif


#ifndef PICO_W_TESTS
    ctx = modbus_new_tcp("127.0.0.1", 1502);
    /* modbus_set_debug(ctx, TRUE); */
#else
    if (argc > 1) {
        ip_or_device = argv[1];
        printf("Starting server at %s:%d\n", ip_or_device, 1502);
        ctx = modbus_new_tcp(ip_or_device, 1502);
        if (ctx == NULL) {
            fprintf(stderr, "Unable to allocate libmodbus context\n");
            return -1;
        }
    }
    else {
        printf("Usage:\n  %s IP\n", argv[0]);
        printf("  Eg. %s 10.0.0.1\n\n", argv[0]);
        exit(1);
    }
#endif

#ifdef PICO_W_TESTS
    modbus_set_response_timeout(ctx, 1, 0);
#endif

    mb_mapping = modbus_mapping_new(500, 500, 500, 500);
    if (mb_mapping == NULL) {
        fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    s = modbus_tcp_listen(ctx, 1);
    modbus_tcp_accept(ctx, &s);

    for (;;) {
        uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
        int rc;

        rc = modbus_receive(ctx, query);
        if (rc > 0) {
            /* rc is the query size */
            modbus_reply(ctx, query, rc, mb_mapping);
        } else if (rc == -1) {
            /* Connection closed by the client or error */
#ifndef PICO_W_TESTS
            break;
#else
            if (s != -1) {
                close(s);
            }
            s = modbus_tcp_listen(ctx, 1);
            modbus_tcp_accept(ctx, &s);
#endif
        }
    }

    printf("Quit the loop: %s\n", modbus_strerror(errno));

    if (s != -1) {
        close(s);
    }
    modbus_mapping_free(mb_mapping);
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}
