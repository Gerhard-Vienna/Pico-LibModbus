/*
 * Copyright © Gerhard Schiller 2024 - 2025, <gerhard.schiller@pm.me>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This file is based on the libmodbus-file "modbus-tcp-private.h"
 *
 * The original copyright notice is below.
 */
/*
 * Copyright © Stéphane Raimbault <stephane.raimbault@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef MODBUS_PICO_TCP_PRIVATE_H
#define MODBUS_PICO_TCP_PRIVATE_H


#define _MODBUS_TCP_HEADER_LENGTH     7
#define _MODBUS_TCP_PRESET_REQ_LENGTH 12
#define _MODBUS_TCP_PRESET_RSP_LENGTH 8

#define _MODBUS_TCP_CHECKSUM_LENGTH 0

#define _WAIT_LOOP_INTERVAL_MS 	1

/* The transaction ID must be placed on first position
 * to have a quick access
*/
typedef struct _tcp_connection {
    // The transaction identifier is used to
    // associate the future response with the request.
    // This identifier is unique on each TCP connection.
    uint16_t            t_id;
    int                 instance;
    char                ip[16]; // IP address
    int                 port;   // TCP port
    struct tcp_pcb     *pcb;	// LWIP connection
    uint8_t             buffer_send[MODBUS_TCP_MAX_ADU_LENGTH];
    uint8_t             buffer_recv[MODBUS_TCP_MAX_ADU_LENGTH];
    int                 send_len;
    int                 recv_len;
    bool                connected;
    int					error;	// LWIP-errors: ERR_*
    // Last activity time (to track timeout for idle clients)
    uint32_t 			last_activity; // time in msec!
} tcp_connection;


/*************************************************************
 * The LWIP callback functions
 *************************************************************/
// SERVER:
// Callback for incoming connections ("accepted")
// Handle the incoming TCP connection from a client
static err_t accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err);

// CLIENT:
// Callback for outgoing connections ("connected")
// Handle the outgoing TCP connection to a server
err_t connect_callback(void *arg, struct tcp_pcb *tpcb, err_t err);

// SERVER AND CLIENT -> aka "peer"
// Callback when data is received from peer
static err_t receive_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);

// SERVER AND CLIENT -> aka "peer"
// Callback when data has been sent successfully
static err_t sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len);

// SERVER
// Callback for polling
// check if a client has been idle for more than CLIENT_TIMEOUT msec
static err_t disconnect_idle_clients(void *arg, struct tcp_pcb *pcb);

// SERVER AND CLIENT -> aka "peer"
// Callback for error handling
static void error_callback(void *arg, err_t err);


/*************************************************************
 * Internal to modbus_pico_tcp.c
 *************************************************************/

// CLIENT:
// Connect to server
static int _tcp_client_connect(int serverID);

// SERVER AND CLIENT -> aka "peer"
// Send data to peer, mimics the send() system-call on linux
// errno used: EBADF, ECONNRESET
// errno from linux send() UNUSED: EPIPE
int _send(int ctx_s, void *buf, size_t len);

// SERVER AND CLIENT -> aka "peer"
static err_t _free_connection(tcp_connection *peer);

/*************************************************************
 * Functions from the backend, wich are used in modbus_pico_tcp.c
 *************************************************************/
// SERVER
// Check if a client has modified values.
bool _modbus_tcp_message(modbus_t *ctx,
                        const uint8_t *req,
                        modbus_message_t *msg);
void _modbus_tcp_close(modbus_t *ctx);
static int _modbus_tcp_flush(modbus_t *ctx);

#endif /* MODBUS_PICO_TCP_PRIVATE_H */



