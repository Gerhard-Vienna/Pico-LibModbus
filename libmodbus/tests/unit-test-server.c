/*
 * Copyright © Gerhard Schiller 2024, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This file has been adapted from the libmodbus-file "unit-test-server.c"
 * to test a modbus server running on a RP2040.
 *
 * Use tests/pico-unit-test-client as the client to query this server.
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <modbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// clang-format off
#ifdef _WIN32
# include <winsock2.h>
#else
# include <sys/socket.h>
#endif

#ifdef PICO_W_TESTS
#include <netinet/tcp.h>
#endif

/* For MinGW */
#ifndef MSG_NOSIGNAL
# define MSG_NOSIGNAL 0
#endif
// clang-format on

#include "unit-test.h"

enum {
    TCP,
    TCP_PI,
    RTU
};

int main(int argc, char *argv[])
{
    int s = -1;
    modbus_t *ctx;
    modbus_mapping_t *mb_mapping;
    int rc;
    int i;
    int use_backend;
    uint8_t *query;
    int header_length;
    char *ip_or_device;

    if (argc > 1) {
        if (strcmp(argv[1], "tcp") == 0) {
            use_backend = TCP;
        } else if (strcmp(argv[1], "tcppi") == 0) {
            use_backend = TCP_PI;
        } else if (strcmp(argv[1], "rtu") == 0) {
            use_backend = RTU;
        } else {
            printf("Modbus server for unit testing.\n");
            printf("Usage:\n  %s [tcp|tcppi|rtu] [<ip or device>]\n", argv[0]);
            printf("Eg. tcp 127.0.0.1 or rtu /dev/ttyUSB0\n\n");
            return -1;
        }
    } else {
        /* By default */
        use_backend = TCP;
    }

    if (argc > 2) {
        ip_or_device = argv[2];
    } else {
        switch (use_backend) {
        case TCP:
#ifndef PICO_W_TESTS
            ip_or_device = "127.0.0.1";
#else
            // this directs modbus_tcp_listen() to listen on INADDR_ANY
            ip_or_device = "0.0.0.0";
#endif
            break;
        case TCP_PI:
            ip_or_device = "::1";
            break;
        case RTU:
            ip_or_device = "/dev/ttyUSB0";
            break;
        default:
            break;
        }
    }

    if (use_backend == TCP) {
        ctx = modbus_new_tcp(ip_or_device, 1502);
        query = malloc(MODBUS_TCP_MAX_ADU_LENGTH);
    } else if (use_backend == TCP_PI) {
        ctx = modbus_new_tcp_pi(ip_or_device, "1502");
        query = malloc(MODBUS_TCP_MAX_ADU_LENGTH);
    } else {
        ctx = modbus_new_rtu(ip_or_device, 115200, 'N', 8, 1);
        modbus_set_slave(ctx, SERVER_ID);
        query = malloc(MODBUS_RTU_MAX_ADU_LENGTH);
    }

    header_length = modbus_get_header_length(ctx);

    // modbus_set_debug(ctx, TRUE);

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
        return -1;
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

    if (use_backend == TCP) {
        s = modbus_tcp_listen(ctx, 1);
        modbus_tcp_accept(ctx, &s);
    } else if (use_backend == TCP_PI) {
        s = modbus_tcp_pi_listen(ctx, 1);
        modbus_tcp_pi_accept(ctx, &s);
    } else {
        rc = modbus_connect(ctx);
        if (rc == -1) {
            fprintf(stderr, "Unable to connect %s\n", modbus_strerror(errno));
            modbus_free(ctx);
            return -1;
        }
    }

    for (;;) {
        do {
            rc = modbus_receive(ctx, query);
            /* Filtered queries return 0 */
        } while (rc == 0);

        /* The connection is not closed on errors which require on reply such as
           bad CRC in RTU. */
        if (rc == -1 && errno != EMBBADCRC) {
            /* Quit */
            break;
        }

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
                uint8_t raw_req[] = {(use_backend == RTU) ? INVALID_SERVER_ID : 0xFF,
                                     0x03,
                                     0x02,
                                     0x00,
                                     0x00};

                printf("Reply with an invalid TID or slave\n");
                modbus_send_raw_request(ctx, raw_req, RAW_REQ_LENGTH * sizeof(uint8_t));
                continue;
            } else if (address == UT_REGISTERS_ADDRESS_SLEEP_500_MS) {
                printf("Sleep 0.5 s before replying\n");
                usleep(500000);
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

#ifdef PICO_W_TESTS
                // Disable the aggregation of small TCP packets.
                int flag = 1;
                setsockopt(w_s, SOL_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

                /* Copy TID */
                // The client runs in an endless loop.
                // Therefore the tid will go beyond 0X00FF.
                // We have to copy both bytes to the response!
                req[0] = query[0];
#endif
                /* Copy TID */
                req[1] = query[1];
                for (i = 0; i < req_length; i++) {
                    printf("(%.2X)", req[i]);
#ifdef PICO_W_TESTS
                    // The 5 ms pause is not sufficient to wait for thee ACK.
                    // Round-trip time pc->pico-pc with single byte packets
                    // can be up to  15 ms.
                    usleep(30 * 1000);
#else
                    usleep(5000);
#endif
                    rc = send(w_s, (const char *) (req + i), 1, MSG_NOSIGNAL);
                    if (rc == -1) {
                        break;
                    }
                }
#ifdef PICO_W_TESTS
                flag = 0;
                setsockopt(w_s, SOL_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
#endif
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

        rc = modbus_reply(ctx, query, rc, mb_mapping);
        if (rc == -1) {
            break;
        }
    }

    printf("Quit the loop: %s\n", modbus_strerror(errno));

    if (use_backend == TCP) {
        if (s != -1) {
            close(s);
        }
    }
    modbus_mapping_free(mb_mapping);
    free(query);
    /* For RTU */
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}
