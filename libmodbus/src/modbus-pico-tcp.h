/*
 * Copyright © Gerhard Schiller 2024 - 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This file is based on the libmodbus-file "modbus-tcp.h"
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef MODBUS_PICO_TCP_H
#define MODBUS_PICO_TCP_H

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include <stdbool.h>

// The maximum number of clients that can connect to a server
#define MAX_PEERS   4
// Timeout for an idle client in seconds.
// #define CLIENT_TIMEOUT 			(10 * 60) // 10 mins
#define CLIENT_TIMEOUT 			(60) // 1 min

#define MODBUS_TCP_DEFAULT_PORT 502
#define MODBUS_TCP_SLAVE        0xFF
/* Modbus_Application_Protocol_V1_1b.pdf Chapter 4 Section 1 Page 5
 * TCP MODBUS ADU = 253 bytes + MBAP (7 bytes) = 260 bytes
 */
#define MODBUS_TCP_MAX_ADU_LENGTH 260

typedef struct _tcp_connection tcp_connection;

// SERVER
// A structure to inform core 0 that a client has modified values.
typedef struct _modbus_message_t {
    uint8_t     func;
    uint16_t    addr;
    uint16_t    count;
}modbus_message_t;

// Array to hold active connections
extern struct _tcp_connection *peers[];

// CLIENT
// Initialize modbus
modbus_t *tcp_client_init();

// CLIENT:
// Initialize the TCP client
int tcp_new_client(char *server_ip, int port);


// SERVER:
// Initialize the TCP server
// struct _modbus *tcp_server_init(int port);
modbus_t *tcp_server_init(int port);

// SERVER
/* Checks whether there is an existing connection under client_id.
 * If so, checks whether the client has sent a request or
 * whether an error has occurred (e.g., the client has disconnected).
 *
 * Return values:           *
 * >0 Nuber of bytes received sofar
 * 0 No request
 * <0 An error has occurred. The returned value is a LWIP-error
 */
int modbus_client_status(int client_id);

// SERVER AND CLIENT -> aka "peer"
// Return 1 if server_id denotes an existing link and
// a connection has been established.
// Otherwise return 0
int modbus_is_connected(int peer_id);

// SERVER
// Check whether the request has modified any data in one of
// the Modbus tables. In other words, check if it was a write
// (or read/write)operation rather than a read-only one.
// If so, push a message for core 0 into the FIFO.
void modbus_notify_if_write(modbus_t *ctx,
                            const uint8_t *req,
                            modbus_message_t *msg);

// SERVER
// Check wether a client has modified any data in one of the
// Modbus tables
modbus_message_t *modbus_write_notify(void);

// SERVER AND CLIENT -> aka "peer"
#define modbus_set_connectionID(ctx, peerID) modbus_set_socket(ctx, peerID)
#define modbus_get_connectionID(ctx) modbus_get_socket(ctx)

void modbus_tcp_mapping_lock(modbus_t *ctx);
void modbus_tcp_mapping_unlock(modbus_t *ctx);
int modbus_tcp_get_error(void);
bool modbus_get_debug(modbus_t *ctx);

// This function is only used in test cases (pico-unit-test-server)
void modbus_tcp_nodelay(modbus_t *ctx, bool enable);

// debugging only
char *err_txt();

#endif /* MODBUS_PICO_TCP_H */
